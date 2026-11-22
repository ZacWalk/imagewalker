#Requires -Version 7.4
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Exe,
    [switch]$KeepCaptures
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing.Common
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$workspace = Join-Path $PSScriptRoot "..\build\iw30-mcp-smoke-$([guid]::NewGuid().ToString('N'))"
$null = New-Item -ItemType Directory -Path $workspace
$workspace = (Resolve-Path -LiteralPath $workspace).Path
$fixture = Join-Path $workspace 'generated fixture'
$outside = Join-Path $workspace 'not a fixture'
$run = Join-Path $workspace 'app'
$nested = Join-Path $fixture 'Nested Photos'
$null = New-Item -ItemType Directory -Path $fixture, $outside, $run, $nested
$exeCopy = Join-Path $run 'imagewalker30.exe'
$sentinel = Join-Path $run 'imagewalker30.ini'
$deadline = [TimeSpan]::FromSeconds(15)

function Assert($Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
}

function New-TestPhoto([string]$Path, [System.Drawing.Color]$Color) {
    $bitmap = [System.Drawing.Bitmap]::new(280, 180)
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.Clear($Color)
            $graphics.FillRectangle([System.Drawing.Brushes]::White, 70, 50, 140, 80)
            $graphics.FillRectangle([System.Drawing.Brushes]::Black, 107, 67, 66, 46)
        } finally { $graphics.Dispose() }
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $bitmap.Dispose() }
}

function Start-Client([string[]]$Arguments) {
    $start = [System.Diagnostics.ProcessStartInfo]::new($exeCopy)
    $start.UseShellExecute = $false
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardInputEncoding = [System.Text.UTF8Encoding]::new($false)
    $start.StandardOutputEncoding = [System.Text.UTF8Encoding]::new($false)
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $process = [System.Diagnostics.Process]::Start($start)
    @{ Process = $process; ErrorStream = $process.StandardError.ReadToEndAsync(); Id = 0 }
}

function Close-Client($Client) {
    if (!$Client) { return }
    try {
        if (!$Client.Process.HasExited) {
            Stop-Process -Id $Client.Process.Id
            $null = $Client.Process.WaitForExit([int]$deadline.TotalMilliseconds)
        }
    } finally { $Client.Process.Dispose() }
}

function Assert-StartupRefused([string[]]$Arguments) {
    $client = Start-Client $Arguments
    try {
        $client.Process.StandardInput.Close()
        $output = $client.Process.StandardOutput.ReadToEndAsync()
        Assert ($client.Process.WaitForExit([int]$deadline.TotalMilliseconds)) "Invalid startup hung: $Arguments"
        Assert ($client.Process.ExitCode -eq 2) "Invalid startup was not refused: $Arguments"
        Assert ($output.GetAwaiter().GetResult().Length -eq 0) 'Invalid startup wrote to stdout.'
        Assert ($client.ErrorStream.GetAwaiter().GetResult() -match 'ImageWalker MCP:') 'No stderr diagnostic.'
    } finally { Close-Client $client }
}

function Send-Request($Client, [string]$Method, $Params) {
    $id = ++$Client.Id
    $request = @{ jsonrpc = '2.0'; id = $id; method = $Method; params = $Params }
    $Client.Process.StandardInput.WriteLine(($request | ConvertTo-Json -Depth 20 -Compress))
    $Client.Process.StandardInput.Flush()
    try {
        $line = $Client.Process.StandardOutput.ReadLineAsync().WaitAsync($deadline).GetAwaiter().GetResult()
    } catch { throw "No stdio response to $Method (request $id): $_" }
    if ($null -eq $line) {
        throw "Unexpected EOF for $Method. stderr: $($Client.ErrorStream.WaitAsync($deadline).GetAwaiter().GetResult())"
    }
    $reply = ConvertFrom-Json -InputObject $line -AsHashtable
    Assert ($reply['jsonrpc'] -eq '2.0' -and $reply['id'] -eq $id) "Not a matching JSON-RPC response: $line"
    $reply
}

function Start-Session($Client) {
    $reply = Send-Request $Client 'initialize' @{
        protocolVersion = '2025-11-25'
        capabilities = @{}
        clientInfo = @{ name = 'iw30-mcp-smoke'; version = '1' }
    }
    Assert ($reply['result']['protocolVersion'] -eq '2025-11-25') 'The MCP handshake failed.'
    $Client.Process.StandardInput.WriteLine('{"jsonrpc":"2.0","method":"notifications/initialized"}')
}

function Stop-Session($Client) {
    $Client.Process.StandardInput.Close()
    Assert ($Client.Process.WaitForExit([int]$deadline.TotalMilliseconds)) 'Closing stdin did not end the session.'
    Assert ($Client.Process.ExitCode -eq 0) "MCP process exited with $($Client.Process.ExitCode)."
    Assert ($Client.Process.StandardOutput.ReadToEnd().Length -eq 0) 'Unexpected stdout after the final response.'
}

function Invoke-Tool($Client, [string]$Name, $Arguments = @{}, [switch]$Refused) {
    $reply = Send-Request $Client 'tools/call' @{ name = $Name; arguments = $Arguments }
    $failed = $reply['error'] -or ($reply['result'] -and $reply['result']['isError'])
    if ($Refused) {
        Assert $failed "Expected $Name to be refused."
        return
    }
    if ($failed) { throw "$Name failed: $($reply | ConvertTo-Json -Depth 20 -Compress)" }
    $reply['result']
}

function Invoke-State($Client, [string]$Tool = 'ui_state', $Arguments = @{}) {
    $result = Invoke-Tool $Client $Tool $Arguments
    $result['structuredContent']
}

function Wait-State($Client, [string]$What, [scriptblock]$Ready) {
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    do {
        $state = Invoke-State $Client
        if (& $Ready $state) { return $state }
        Start-Sleep -Milliseconds 40
    } while ($timer.Elapsed -lt $deadline)
    throw "Timeout waiting for $What. Last state: $($state | ConvertTo-Json -Depth 10 -Compress)"
}

function Assert-Capture($Client, [string]$Label, [switch]$PhotoVisible) {
    $image = Invoke-Tool $Client 'screenshot' @{ max_edge = 768 }
    $section = @($image['content'] | Where-Object { $_['type'] -eq 'image' })
    Assert ($section.Count -eq 1 -and $section[0]['mimeType'] -eq 'image/png') "Expected inline PNG: $Label"
    $bytes = [Convert]::FromBase64String($section[0]['data'])
    Assert ($bytes.Length -ge 32 -and $bytes.Length -le 2600000) "Unbounded or empty PNG: $Label"
    Assert ([Convert]::ToHexString($bytes, 0, 8) -ceq '89504E470D0A1A0A') "Missing PNG signature: $Label"
    $stream = [System.IO.MemoryStream]::new($bytes)
    try {
        $bitmap = [System.Drawing.Bitmap]::new($stream)
        try {
            Assert ([Math]::Max($bitmap.Width, $bitmap.Height) -le 768 -and
                    $bitmap.Width -eq $image['structuredContent']['width'] -and
                    $bitmap.Height -eq $image['structuredContent']['height']) "Incorrect PNG extent: $Label"
            if ($PhotoVisible) {
                # The opaque magenta outside the checker marks cannot come from an empty surface or title bar.
                $count = 0
                for ($y = 4; $y -lt $bitmap.Height; $y += 4) {
                    for ($x = 4; $x -lt $bitmap.Width; $x += 4) {
                        $pixel = $bitmap.GetPixel($x, $y)
                        if ($pixel.R -gt 220 -and $pixel.G -lt 65 -and $pixel.B -gt 145) { ++$count }
                    }
                }
                Assert ($count -gt 60) "The captured $Label does not show the generated photo ($count magenta samples)."
            }
            Write-Host "$Label capture: $($bitmap.Width)x$($bitmap.Height), $($bytes.Length) bytes"
        } finally { $bitmap.Dispose() }
    } finally { $stream.Dispose() }
    if ($KeepCaptures) { [System.IO.File]::WriteAllBytes((Join-Path $workspace "$Label.png"), $bytes) }
}

function Get-FixtureHashes {
    Get-ChildItem -LiteralPath $fixture, $outside -File -Recurse |
        Sort-Object FullName | ForEach-Object { "$($_.FullName):$((Get-FileHash -LiteralPath $_.FullName).Hash)" }
}

$client = $null
$junction = Join-Path $fixture 'outside-junction'
$hardlink = Join-Path $fixture 'outside-hardlink.png'
try {
    Copy-Item -LiteralPath $Exe -Destination $exeCopy
    [System.IO.File]::WriteAllText($sentinel, "[ImageWalker30]`r`nLastFolder=$outside`r`nViewMode=0`r`n")
    $iniHash = (Get-FileHash -LiteralPath $sentinel).Hash
    New-TestPhoto (Join-Path $fixture 'alpha.png') ([System.Drawing.Color]::FromArgb(249, 21, 191))
    New-TestPhoto (Join-Path $fixture 'bêta.png') ([System.Drawing.Color]::Cyan)
    New-TestPhoto (Join-Path $nested 'nested.png') ([System.Drawing.Color]::Gold)
    $outsideImage = Join-Path $outside 'not-admitted.png'
    New-TestPhoto $outsideImage ([System.Drawing.Color]::DarkRed)
    $null = New-Item -ItemType File -Path (Join-Path $fixture '.imagewalker-mcp-fixture')
    $fixtureHashes = @(Get-FixtureHashes)

    Assert-StartupRefused @('--mcp')
    Assert-StartupRefused @('-mcp')
    Assert-StartupRefused @('--mcp', '--fixture-root', $outside)
    Assert-StartupRefused @('--mcp', '--fixture-root', $fixture, '--screenshot')
    $null = New-Item -ItemType Junction -Path $junction -Target $outside
    Assert-StartupRefused @('--mcp', '--fixture-root', $fixture)
    # Delete the link itself, never follow it.
    [System.IO.Directory]::Delete($junction)
    $null = New-Item -ItemType HardLink -Path $hardlink -Target $outsideImage
    Assert-StartupRefused @('--mcp', '--fixture-root', $fixture)
    [System.IO.File]::Delete($hardlink)

    $client = Start-Client @('--mcp', '--fixture-root', $fixture)
    Start-Session $client
    $plainTools = (Send-Request $client 'tools/list' @{})['result']['tools']
    $expected = @('browse', 'close_view', 'open_preview', 'select_photos', 'set_fullscreen', 'ui_state')
    $plainNames = @($plainTools | ForEach-Object { $_['name'] } | Sort-Object)
    Assert (($plainNames -join ',') -ceq ($expected -join ',')) 'Unexpected tool set without capture.'
    Invoke-Tool $client 'screenshot' -Refused
    Stop-Session $client
    Close-Client $client
    $client = $null

    $client = Start-Client @('--mcp', '--fixture-root', $fixture, '--mcp-allow-capture')
    Start-Session $client
    $captureNames = @((Send-Request $client 'tools/list' @{})['result']['tools'] | ForEach-Object { $_['name'] })
    Assert (((Compare-Object @($expected + 'screenshot') $captureNames) | Measure-Object).Count -eq 0) 'Unexpected capture-session tools.'
    $root = Wait-State $client 'root scan and thumbnails' {
        param($s) $s['folder'] -eq '.' -and $s['scan']['state'] -eq 'ready' -and $s['thumbnails']['complete'] -eq 2
    }
    Assert ($root['view'] -eq 'browse' -and $root['scan']['complete'] -eq 3 -and $root['scan']['photos'] -eq 2 -and
            $root['thumbnails']['total'] -eq 2 -and $root['items'].Count -eq 3 -and $root['capture_allowed']) `
           "Wrong fixture state: $($root | ConvertTo-Json -Depth 10 -Compress)"
    Invoke-Tool $client 'browse' @{ folder = '../not a fixture' } -Refused
    Invoke-Tool $client 'browse' @{ folder = $outside } -Refused
    Invoke-Tool $client 'select_photos' @{ names = @($outsideImage) } -Refused
    Invoke-Tool $client 'open_preview' @{ view = 'copy' } -Refused
    Invoke-Tool $client 'screenshot' @{ path = (Join-Path $outside 'refused.png') } -Refused
    Invoke-Tool $client 'screenshot' @{ max_edge = 4096 } -Refused
    $null = Invoke-State $client 'browse' @{ folder = 'Nested Photos' }
    $nestedState = Wait-State $client 'nested scan' { param($s) $s['scan']['state'] -eq 'ready' }
    Assert ($nestedState['folder'] -ceq 'Nested Photos' -and $nestedState['items'][0]['name'] -ceq 'nested.png') 'Wrong nested fixture state.'
    # A new file cannot become visible or selectable after admission, even on a second scan.
    $later = Join-Path $fixture 'later.png'
    New-TestPhoto $later ([System.Drawing.Color]::Coral)
    try {
        $null = Invoke-State $client 'browse' @{ folder = '.' }
        $rescanned = Wait-State $client 'second root scan' { param($s) $s['scan']['state'] -eq 'ready' }
        Assert ($rescanned['items'].Count -eq 3) 'A file added after admission became visible.'
        Invoke-Tool $client 'select_photos' @{ names = @('later.png') } -Refused
    } finally { [System.IO.File]::Delete($later) }

    $two = Invoke-State $client 'select_photos' @{ names = @('bêta.png', 'alpha.png') }
    Assert ($two['selected'].Count -eq 2 -and $two['selected'] -ccontains 'bêta.png' -and $two['photo'] -ceq 'alpha.png') 'Multi-select failed.'
    Invoke-Tool $client 'select_photos' @{ names = @('alpha.png', 'absent.png') } -Refused
    Assert ((Invoke-State $client)['selected'].Count -eq 2) 'An invalid request modified selection.'
    foreach ($view in @('convert', 'rename', 'edit', 'sync')) {
        $entered = Invoke-State $client 'open_preview' @{ view = $view }
        Assert ($entered['view'] -ceq $view) "Could not enter $view preview."
        $preview = Wait-State $client "$view preview" { param($s) !$s['task_busy'] }
        if ($view -in 'convert', 'rename') { Assert ($preview['review_rows'] -ge 2) "$view preview did not analyze the photos." }
        if ($view -eq 'convert') { Assert-Capture $client 'convert' }
        Assert ((Invoke-State $client 'close_view')['view'] -eq 'browse') "Could not close $view preview."
    }

    $null = Invoke-State $client 'select_photos' @{ names = @('alpha.png') }
    $null = Wait-State $client 'selected photo decode' { param($s) $s['image_state'] -eq 'ready' }
    Assert-Capture $client 'browse' -PhotoVisible
    Assert ((Invoke-State $client 'set_fullscreen' @{ enabled = $true })['view'] -eq 'fullscreen') 'Could not enter fullscreen.'
    Assert-Capture $client 'fullscreen' -PhotoVisible
    Assert ((Invoke-State $client 'close_view')['view'] -eq 'browse') 'Could not leave fullscreen.'
    Stop-Session $client
    Assert ((Get-FileHash -LiteralPath $sentinel).Hash -eq $iniHash) 'The interactive INI was changed.'
    Assert (!(Test-Path -LiteralPath (Join-Path $run 'imagewalker30.undo'))) 'Live Undo was initialized.'
    Assert (((Compare-Object $fixtureHashes @(Get-FixtureHashes)) | Measure-Object).Count -eq 0) 'Fixture or outside bytes changed.'
    Write-Host 'ImageWalker 3.0 MCP smoke passed.'
} finally {
    Close-Client $client
    if ([System.IO.Directory]::Exists($junction)) { [System.IO.Directory]::Delete($junction) }
    if ($KeepCaptures) { Write-Host "Generated captures and fixtures: $workspace" }
    else { Remove-Item -LiteralPath $workspace -Recurse -Force }
}
