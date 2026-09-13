#requires -Version 7.4
param([Parameter(Mandatory)][string]$Root, [switch]$AllowExecution)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
. (Join-Path $PSScriptRoot '../core.ps1')
. (Join-Path $PSScriptRoot 'tools.ps1')
$script:McpRoot = [IO.Path]::TrimEndingDirectorySeparator([IO.Path]::GetFullPath($Root))
if (-not (Test-Path -LiteralPath $script:McpRoot -PathType Container)) { throw 'MCP root must be an existing directory.' }
Assert-DDMcpRoot $script:McpRoot
$script:McpInvoker = Join-Path $PSScriptRoot 'invoke.ps1'
$script:McpTools = @(Get-DDMcpTools)
$script:McpRequests = [Collections.Generic.List[object]]::new()
$script:McpActive = $null
$script:McpState = 'new'
$script:McpVersion = '2025-06-18'
$encoding = [Text.UTF8Encoding]::new($false, $true)
$reader = [IO.StreamReader]::new([Console]::OpenStandardInput(), $encoding)
$writer = [IO.StreamWriter]::new([Console]::OpenStandardOutput(), $encoding)
$writer.AutoFlush = $true

function Write-McpMessage($Message) { $writer.WriteLine(($Message | ConvertTo-Json -Depth 60 -Compress)) }
function Write-McpError($Id, [int]$Code, [string]$Message) { Write-McpMessage @{ jsonrpc = '2.0'; id = $Id; error = @{ code = $Code; message = $Message } } }
function Write-McpResult($Id, $Result) { Write-McpMessage @{ jsonrpc = '2.0'; id = $Id; result = $Result } }
function Write-McpToolError($Id, [string]$Message) { Write-McpResult $Id @{ content = @(@{ type = 'text'; text = $Message }); isError = $true } }
function Get-McpIdKey($Id) { return ConvertTo-Json -InputObject $Id -Compress }
function Stop-McpChild {
    if ($script:McpActive) {
        $process = $script:McpActive.process
        try { if (-not $process.HasExited) { $process.Kill($true); $null = $process.WaitForExit(5000) } }
        finally { $process.Dispose(); $script:McpActive = $null }
    }
}

function Receive-McpMessage([string]$Line) {
    try { $request = ConvertFrom-Json -InputObject $Line -AsHashtable -Depth 64 -ErrorAction Stop }
    catch { Write-McpError $null -32700 'Parse error'; return }
    if ($request -isnot [Collections.IDictionary] -or $request.jsonrpc -cne '2.0' -or $request.method -isnot [string]) { Write-McpError $null -32600 'Invalid request'; return }
    $hasId = $request.Contains('id')
    if ($hasId -and $request.id -isnot [string] -and $request.id -isnot [int] -and $request.id -isnot [long]) { Write-McpError $null -32600 'Request ID must be a string or integer'; return }
    if ($request.Contains('params') -and $request.params -isnot [Collections.IDictionary]) {
        if ($hasId) { Write-McpError $request.id -32602 'Parameters must be an object' }
        return
    }
    if (-not $hasId) {
        if ($request.method -ceq 'notifications/initialized' -and $script:McpState -eq 'initializing') { $script:McpState = 'ready' }
        if ($request.method -ceq 'notifications/cancelled' -and $request.params -and $request.params.Contains('requestId')) {
            $key = Get-McpIdKey $request.params.requestId
            if ($script:McpActive -and $script:McpActive.key -ceq $key) { Stop-McpChild }
            for ($index = $script:McpRequests.Count - 1; $index -ge 0; $index--) {
                if ($script:McpRequests[$index].key -ceq $key) { $script:McpRequests.RemoveAt($index) }
            }
        }
        return
    }
    switch -CaseSensitive ($request.method) {
        'initialize' {
            if ($script:McpState -ne 'new') { Write-McpError $request.id -32600 'Already initialized'; return }
            if ($request.params.protocolVersion -isnot [string] -or $request.params.capabilities -isnot [Collections.IDictionary] -or $request.params.clientInfo.name -isnot [string] -or $request.params.clientInfo.version -isnot [string]) { Write-McpError $request.id -32602 'Invalid initialization parameters'; return }
            if ($request.params.protocolVersion -in @('2025-06-18','2025-03-26','2024-11-05')) { $script:McpVersion = $request.params.protocolVersion }
            $script:McpState = 'initializing'
            Write-McpResult $request.id @{ protocolVersion = $script:McpVersion; capabilities = @{ tools = @{ listChanged = $false } }; serverInfo = @{ name = 'dd-build-system'; version = $script:DDVersion } }
        }
        'ping' { Write-McpResult $request.id @{} }
        default {
            if ($script:McpState -ne 'ready') { Write-McpError $request.id -32600 'Initialize the connection first'; return }
            switch -CaseSensitive ($request.method) {
                'tools/list' {
                    if ($request.params.cursor) { Write-McpError $request.id -32602 'Unknown cursor'; return }
                    Write-McpResult $request.id @{ tools = $script:McpTools }
                }
                'tools/call' {
                    if ($request.params.name -isnot [string] -or ($request.params.Contains('arguments') -and $request.params.arguments -isnot [Collections.IDictionary])) { Write-McpError $request.id -32602 'Invalid tool call'; return }
                    if ($request.params.Contains('_meta') -and ($request.params._meta -isnot [Collections.IDictionary] -or ($request.params._meta.Contains('progressToken') -and $request.params._meta.progressToken -isnot [string] -and $request.params._meta.progressToken -isnot [int] -and $request.params._meta.progressToken -isnot [long]))) { Write-McpError $request.id -32602 'Invalid progress metadata'; return }
                    $key = Get-McpIdKey $request.id
                    if (($script:McpActive -and $script:McpActive.key -ceq $key) -or @($script:McpRequests | Where-Object { $_.key -ceq $key }).Count) { Write-McpError $request.id -32600 'Duplicate active request ID'; return }
                    if ($script:McpRequests.Count -ge 32) { Write-McpError $request.id -32000 'Request queue is full'; return }
                    $script:McpRequests.Add(@{ key = $key; request = $request })
                }
                default { Write-McpError $request.id -32601 'Method not found' }
            }
        }
    }
}

function Start-McpRequest($Pending) {
    $request = $Pending.request
    try { $call = Convert-DDMcpCall $request.params $script:McpRoot ([bool]$AllowExecution) }
    catch {
        if ($_.Exception.Data.Contains('mcpCode')) { Write-McpError $request.id $_.Exception.Data['mcpCode'] $_.Exception.Message }
        else { Write-McpToolError $request.id $_.Exception.Message }
        return
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = [Diagnostics.ProcessStartInfo]::new((Join-Path $PSHOME $(if ($IsWindows) { 'pwsh.exe' } else { 'pwsh' })))
    $process.StartInfo.WorkingDirectory = $call.project
    $process.StartInfo.Environment['DD_MCP_WORKSPACE_ROOT'] = $script:McpRoot
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.RedirectStandardInput = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    $process.StartInfo.StandardInputEncoding = [Text.UTF8Encoding]::new($false)
    $process.StartInfo.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $process.StartInfo.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
    foreach ($argument in @('-NoProfile','-NonInteractive','-File',$script:McpInvoker)) { $process.StartInfo.ArgumentList.Add($argument) }
    try {
        $null = $process.Start()
        $vector = @('--project',$call.project,'--json','--non-interactive') + $call.arguments
        $process.StandardInput.WriteLine((ConvertTo-Json -InputObject $vector -Compress))
        $process.StandardInput.Close()
    }
    catch { $process.Dispose(); Write-McpToolError $request.id $_.Exception.Message; return }
    $streams = foreach ($stream in @($process.StandardOutput, $process.StandardError)) {
        $buffer = [char[]]::new(4096)
        @{ reader = $stream; buffer = $buffer; text = [Text.StringBuilder]::new(); task = $stream.ReadAsync($buffer, 0, $buffer.Length); done = $false }
    }
    $script:McpActive = @{ key = $Pending.key; request = $request; process = $process; streams = @($streams); timer = [Diagnostics.Stopwatch]::StartNew(); progress = 0 }
}

function Update-McpRequest {
    $active = $script:McpActive
    if (-not $active) { return }
    try {
        for ($index = 0; $index -lt 2; $index++) {
            $stream = $active.streams[$index]
            if ($stream.done -or -not $stream.task.IsCompleted) { continue }
            $count = $stream.task.GetAwaiter().GetResult()
            if ($count -eq 0) { $stream.done = $true; continue }
            $text = [string]::new($stream.buffer, 0, $count)
            $null = $stream.text.Append($text)
            if ($index -eq 0 -and $stream.text.Length -gt 8MB) { throw 'CLI response exceeds 8 MiB.' }
            if ($index -eq 1) {
                if ($stream.text.Length -gt 65536) { $null = $stream.text.Remove(0, $stream.text.Length - 65536) }
                $token = $active.request.params._meta.progressToken
                if ($null -ne $token) {
                    $active.progress++
                    Write-McpMessage @{ jsonrpc = '2.0'; method = 'notifications/progress'; params = @{ progressToken = $token; progress = $active.progress; message = $text.Substring(0, [Math]::Min(2000, $text.Length)) } }
                }
            }
            $stream.task = $stream.reader.ReadAsync($stream.buffer, 0, $stream.buffer.Length)
        }
        if ($active.timer.Elapsed.TotalSeconds -ge 1800) { throw 'CLI request timed out after 1800 seconds.' }
        if ($active.process.HasExited -and $active.streams[0].done -and $active.streams[1].done) {
            try { $result = $active.streams[0].text.ToString() | ConvertFrom-Json -AsHashtable -Depth 60 -ErrorAction Stop }
            catch { throw "dd did not return JSON. $($active.streams[1].text)" }
            if ($result.schema -ne 1 -or $result.ok -isnot [bool] -or $result.exitCode -ne $active.process.ExitCode) { throw 'dd returned an invalid result envelope.' }
            $response = @{ content = @(@{ type = 'text'; text = ($result | ConvertTo-Json -Depth 60 -Compress) }); isError = -not $result.ok }
            if ($script:McpVersion -eq '2025-06-18') { $response.structuredContent = $result }
            Write-McpResult $active.request.id $response
            Stop-McpChild
        }
    } catch { Write-McpToolError $active.request.id $_.Exception.Message; Stop-McpChild }
}

$buffer = [char[]]::new(4096)
$pendingText = ''
$read = $reader.ReadAsync($buffer, 0, $buffer.Length)
try {
    while ($true) {
        if ($read.IsCompleted) {
            $count = $read.GetAwaiter().GetResult()
            if ($count -eq 0) { break }
            $pendingText += [string]::new($buffer, 0, $count)
            while (($newline = $pendingText.IndexOf("`n")) -ge 0) {
                $line = $pendingText.Substring(0, $newline).TrimEnd("`r")
                $pendingText = $pendingText.Substring($newline + 1)
                if ($line.Length -gt 1MB) { throw 'MCP message exceeds 1 MiB.' }
                Receive-McpMessage $line
            }
            if ($pendingText.Length -gt 1MB) { throw 'MCP message exceeds 1 MiB.' }
            $read = $reader.ReadAsync($buffer, 0, $buffer.Length)
        }
        Update-McpRequest
        if (-not $script:McpActive -and $script:McpRequests.Count) {
            $pending = $script:McpRequests[0]; $script:McpRequests.RemoveAt(0)
            Start-McpRequest $pending
        }
        $tasks = @($read)
        if ($script:McpActive) { $tasks += @($script:McpActive.streams | Where-Object { -not $_.done } | ForEach-Object task) }
        if (-not $script:McpRequests.Count -or $script:McpActive) { $null = [Threading.Tasks.Task]::WaitAny([Threading.Tasks.Task[]]$tasks, 50) }
    }
} catch { [Console]::Error.WriteLine($_.Exception.Message); exit 1 }
finally { Stop-McpChild; $writer.Dispose(); $reader.Dispose() }
exit 0