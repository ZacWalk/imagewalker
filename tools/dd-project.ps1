#requires -Version 7.4
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

function Invoke-IWTool([string]$Tool, [string[]]$ToolArguments) {
    & $Tool @ToolArguments 2>&1 | ForEach-Object { [Console]::Error.WriteLine($_.ToString()) }
    if ($LASTEXITCODE) {
        $failure = [InvalidOperationException]::new("$Tool failed with exit code $LASTEXITCODE.")
        $failure.Data['exitCode'] = $LASTEXITCODE
        throw $failure
    }
}

function Import-IWEnvironment {
    $output = & $script:PowerShell -NoProfile -File $script:Driver env --json
    if ($LASTEXITCODE) { throw 'Could not initialize the MSVC environment.' }
    $environment = ($output | ConvertFrom-Json).data.environment
    foreach ($entry in $environment.psobject.Properties) {
        [Environment]::SetEnvironmentVariable($entry.Name, $entry.Value, 'Process')
    }
}

try {
    $request = [Console]::In.ReadToEnd() | ConvertFrom-Json -AsHashtable
    if ($request.schema -ne 1 -or $request.command -notin @('test-app', 'check-help')) {
        throw 'Expected an ImageWalker project command request with schema 1.'
    }
    $root = Split-Path $PSScriptRoot
    if ([IO.Path]::GetFullPath($request.projectRoot) -ne $root) { throw 'Project root does not match the script.' }
    Set-Location -LiteralPath $root
    $script:Driver = Join-Path $root 'dd.ps1'
    $script:PowerShell = Join-Path $PSHOME 'pwsh.exe'
    $manifest = Import-PowerShellDataFile (Join-Path $root 'dd.psd1')
    $data = [ordered]@{ command = $request.command; dryRun = [bool]$request.dryRun }
    $files = @()

    if ($request.command -eq 'test-app') {
        $targets = @($manifest.targets | Where-Object id -eq $request.parameters.app)
        if ($targets.Count -ne 1) { throw 'Expected one declared application target.' }
        $target = $targets[0]
        $data.app = $target.id
    }

    switch ($request.command) {
        'test-app' {
            $data.configurations = @('release', 'debug')
            if (-not $request.dryRun) {
                Invoke-IWTool $script:PowerShell @('-NoProfile', '-File', $script:Driver, 'test', '--app', $target.id, '--json')
            }
        }
        'check-help' {
            $data.target = 'iw_check_help'
            $files = @('exe/ImageWalker.chm')
            if (-not $request.dryRun) {
                Import-IWEnvironment
                $preset = $manifest.build['x64-windows'].release
                Invoke-IWTool 'cmake' @('--preset', $preset)
                Invoke-IWTool 'cmake' @('--build', '--preset', $preset, '--target', 'iw_check_help')
            }
        }
    }
    [Console]::Out.WriteLine((@{ schema = 1; data = $data; files = $files } | ConvertTo-Json -Depth 10 -Compress))
}
catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    $code = if ($_.Exception.Data.Contains('exitCode')) { [int]$_.Exception.Data['exitCode'] } else { 1 }
    exit $code
}