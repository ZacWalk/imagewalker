#requires -Version 7.4
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = Split-Path $PSScriptRoot
$driver = Join-Path $root 'dd.ps1'
$powershell = Join-Path $PSHOME 'pwsh.exe'

$presets = Get-Content -LiteralPath (Join-Path $root 'CMakePresets.json') -Raw | ConvertFrom-Json
foreach ($name in @('vs-debug', 'vs-release')) {
    $build = @($presets.buildPresets | Where-Object name -eq $name)
    $test = @($presets.testPresets | Where-Object name -eq $name)
    if ($build.Count -ne 1 -or $test.Count -ne 1 -or
        $test[0].configurePreset -ne 'vs' -or
        $test[0].configuration -ne $build[0].configuration -or
        -not $test[0].output.outputOnFailure) {
        throw "Visual Studio test preset '$name' must match its build configuration and report failures."
    }
}

function Test-Driver([string[]]$DriverArguments, [int]$ExpectedCode = 0) {
    $output = & $powershell -NoProfile -File $driver @DriverArguments --project $root --json
    if ($LASTEXITCODE -ne $ExpectedCode) { throw "Unexpected exit code $LASTEXITCODE for $DriverArguments" }
    $result = $output | ConvertFrom-Json
    if ($result.exitCode -ne $ExpectedCode -or $result.ok -ne ($ExpectedCode -eq 0)) { throw 'Invalid driver result envelope.' }
    return $result
}

$result = Test-Driver @('targets')
if ($result.data.targets.Count -ne 5 -or $result.data.defaultTarget -ne 'iw30') { throw 'Target configuration changed.' }
$result = Test-Driver @('dep', 'install')
if ($result.data.owner -ne 'application' -or $result.data.inventoryKnown -ne $false -or $result.data.changed) {
    throw 'Dependencies must remain owned by application CMake with unknown inventory.'
}
$result = Test-Driver @('help')
if (-not @($result.data.commands | Where-Object { $_ -like 'launch *' }).Count) { throw 'Persistent launch must be provided by the shared driver.' }
$result = Test-Driver @('commands')
if ($result.data.commands.Count -ne 2 -or @($result.data.commands | Where-Object { -not $_.scriptExists }).Count) {
    throw 'Project commands are missing their implementation.'
}
$result = Test-Driver @('test-app', '--app', 'iw10', '--dry-run')
if (($result.data.result.configurations -join ',') -ne 'release,debug') { throw 'Focused tests must build both configurations.' }
$result = Test-Driver @('check-help', '--dry-run')
if ($result.data.result.target -ne 'iw_check_help' -or $result.data.files[0] -ne 'exe/ImageWalker.chm') {
    throw 'Help preview is incorrect.'
}
$null = Test-Driver @('launch', 'invalid') 2
$null = Test-Driver @('launch', 'iw30', '--timeout', '1') 2
$null = Test-Driver @('test-app', '--app', 'invalid', '--dry-run') 2
$null = Test-Driver @('test-app', '--app', 'iw30') 2
$null = Test-Driver @('dep', 'install', 'zlib') 2
$null = Test-Driver @('run', '30') 2
$null = Test-Driver @('test', '30') 2
Write-Output 'Shared dd manifest and project-command contracts passed.'