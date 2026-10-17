function Assert-DDRequirementData($Requirements) {
    Assert-DDFields $Requirements @('tools','msvc') 'requirements'
    if ($Requirements.tools) {
        if ($Requirements.tools -isnot [hashtable]) { Stop-DD 'requirements.tools must be a hashtable.' }
        foreach ($name in $Requirements.tools.Keys) {
            if ($name -notin @('cmake','ctest','ninja','git','gcc','python','gdb')) { Stop-DD "Unsupported prerequisite provider: $name. Project probes belong in trusted project commands." }
            $entry = $Requirements.tools[$name]
            Assert-DDFields $entry @('minimum','optional','platforms') "requirements.tools.$name"
            if ($entry.minimum) { $version = $null; if (-not [version]::TryParse([string]$entry.minimum, [ref]$version)) { Stop-DD "Invalid minimum version: $name" } }
            if ($entry.ContainsKey('optional') -and $entry.optional -isnot [bool]) { Stop-DD 'optional must be boolean.' }
            if ($entry.platforms) { Assert-DDPlatforms $entry.platforms }
        }
    }
    if ($Requirements.msvc) {
        Assert-DDFields $Requirements.msvc @('minimum','components') 'requirements.msvc'
        if ($Requirements.msvc.minimum) { $version = $null; if (-not [version]::TryParse([string]$Requirements.msvc.minimum, [ref]$version)) { Stop-DD 'Invalid Visual Studio minimum version.' } }
        if ($Requirements.msvc.ContainsKey('components')) {
            if ($Requirements.msvc.components -isnot [array]) { Stop-DD 'MSVC components must be an array.' }
            foreach ($component in $Requirements.msvc.components) { if ($component -notmatch '^Microsoft\.VisualStudio\.[A-Za-z0-9_.]+$') { Stop-DD 'Invalid Visual Studio component ID.' } }
        }
    }
}

function Get-DDRequirements([string]$Root) {
    $tools = @{
        cmake = @{ minimum = '3.24.0'; optional = $false }
        ctest = @{ minimum = '3.24.0'; optional = $false }
        ninja = @{ minimum = '1.10.0'; optional = $false }
        git = @{ minimum = '2.20.0'; optional = $false }
    }
    if (-not $IsWindows) { $tools.gcc = @{ minimum = '10.0.0'; optional = $false } }
    $msvc = @{ minimum = '17.0'; components = @('Microsoft.VisualStudio.Component.VC.Tools.x86.x64') }
    if ($Root -and (Test-Path -LiteralPath (Join-Path $Root 'dd.psd1'))) {
        $manifest = Read-DDManifest $Root
        if ($manifest.requirements) {
            foreach ($name in @($manifest.requirements.tools.Keys)) {
                if (-not $name) { continue }
                $entry = $manifest.requirements.tools[$name]
                if ($entry.platforms -and (Get-DDPlatform) -notin $entry.platforms) { continue }
                if ($name -eq 'gcc' -and $IsWindows) { continue }
                $minimum = if ($entry.minimum) { $entry.minimum } else { '0.0.0' }
                $optional = [bool]$entry.optional
                if ($tools.ContainsKey($name)) {
                    if ([version]$tools[$name].minimum -gt [version]$minimum) { $minimum = $tools[$name].minimum }
                    $optional = $false
                }
                $tools[$name] = @{ minimum = $minimum; optional = $optional }
            }
            if ($manifest.requirements.msvc) {
                if ($manifest.requirements.msvc.minimum -and [version]$manifest.requirements.msvc.minimum -gt [version]$msvc.minimum) { $msvc.minimum = $manifest.requirements.msvc.minimum }
                $msvc.components = @($msvc.components + $manifest.requirements.msvc.components | Where-Object { $_ } | Select-Object -Unique)
            }
        }
    }
    return @{ tools = $tools; msvc = $msvc }
}

function Get-DDRequirementReport([string]$Root) {
    $requirements = Get-DDRequirements $Root
    $key = "$Root|$($requirements | ConvertTo-Json -Depth 6 -Compress)"
    if ($null -eq $script:DDReportCache) { $script:DDReportCache = @{} }
    if ($script:DDReportCache.ContainsKey($key)) { return $script:DDReportCache[$key] }
    $issues = @(); $warnings = @(); $tools = @(); $packages = @()
    $packageMap = if ($IsWindows) { @{ cmake = 'Kitware.CMake'; ctest = 'Kitware.CMake'; ninja = 'Ninja-build.Ninja'; git = 'Git.Git'; python = 'Python.Python.3.13' } } else { @{ cmake = 'cmake'; ctest = 'cmake'; ninja = 'ninja-build'; git = 'git'; gcc = 'build-essential'; python = 'python3'; gdb = 'gdb' } }
    if ($IsWindows) {
        try { Initialize-DDCompiler $requirements } catch { $issues += $_.Exception.Message; $packages += 'Microsoft.VisualStudio.BuildTools' }
    }
    foreach ($name in ($requirements.tools.Keys | Sort-Object)) {
        $spec = $requirements.tools[$name]
        $executable = switch ($name) { 'gcc' { 'g++' } 'python' { if ($IsWindows) { 'python' } else { 'python3' } } default { $name } }
        $command = Get-Command $executable -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        $version = $null; $problem = $null
        if (-not $command) { $problem = "Missing $name (minimum $($spec.minimum))." }
        else {
            try {
                $arguments = if ($name -eq 'gcc') { @('-dumpfullversion') } else { @('--version') }
                $probe = Invoke-DDProcess $command.Source $arguments $Root 15 -AllowFailure
                $text = $probe.stdout + $probe.stderr
                if ($probe.exitCode -ne 0 -or $text -notmatch '(\d+\.\d+(?:\.\d+)?)') { $problem = "Cannot determine $name version." }
                else {
                    $version = $Matches[1]
                    if ([version]$version -lt [version]$spec.minimum) { $problem = "$name $version is below required $($spec.minimum)." }
                }
            } catch { $problem = "Cannot inspect ${name}: $($_.Exception.Message)" }
        }
        $tools += @{ name = $name; path = $command.Source; version = $version; minimum = $spec.minimum; optional = $spec.optional; ready = -not $problem }
        if ($problem) {
            if ($spec.optional) { $warnings += $problem } else { $issues += $problem; if ($packageMap[$name]) { $packages += $packageMap[$name] } }
        }
    }
    $report = @{ platform = Get-DDPlatform; tools = $tools; requirements = $requirements; packages = @($packages | Select-Object -Unique); issues = $issues; warnings = $warnings; ready = $issues.Count -eq 0 }
    $script:DDReportCache[$key] = $report
    return $report
}

function Clear-DDRequirementReportCache { $script:DDReportCache = @{}; $script:DDCheckedRequirements = $null }

function Assert-DDRequirements([string]$Root) {
    if ($script:DDCheckedRequirements -eq $Root) { return }
    $report = Get-DDRequirementReport $Root
    if (-not $report.ready) { Stop-DD ($report.issues -join "`n") 3 }
    $script:DDCheckedRequirements = $Root
}