function Invoke-DDToolchain([string]$Root, $Options) {
    Assert-DDOptions $Options @('yes', 'dry-run')
    $report = Get-DDDoctor $Root -ToolsOnly
    if ($report.ready) { return $report }
    $packages = $report.packages
    $report.packages = $packages
    $report.status = 'planned'
    if ($Options['dry-run']) { return $report }
    if (-not $Options.yes) { Stop-DD "Missing prerequisites: $($report.issues -join '; '). Review dd toolchain --dry-run, then use --yes in a suitably privileged terminal." 3 }
    if ($IsWindows) {
        $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
        if (-not ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { Stop-DD 'Run dd toolchain --yes yourself in an elevated PowerShell. dd will not open an elevation prompt.' 3 }
        foreach ($package in $packages) {
            $arguments = @('install', '--id', $package, '--exact', '--accept-package-agreements', '--accept-source-agreements', '--disable-interactivity')
            if ($package -eq 'Microsoft.VisualStudio.BuildTools') {
                $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
                if (Test-Path $vswhere) {
                    $existing = (Invoke-DDProcess $vswhere @('-latest','-products','*','-property','installationPath') $Root).stdout.Trim()
                    if ($existing) {
                        $installer = Join-Path (Split-Path $vswhere) 'setup.exe'
                        $modify = @('modify','--installPath',$existing,'--passive','--norestart')
                        foreach ($component in $report.requirements.msvc.components) { $modify += @('--add',$component) }
                        $null = Invoke-DDProcess $installer $modify $Root 3600 -Log -Progress 'Modify Visual Studio components'
                        continue
                    }
                }
                $override = '--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
                foreach ($component in $report.requirements.msvc.components) { $override += " --add $component" }
                $arguments += @('--override', $override)
            }
            $install = Invoke-DDProcess winget $arguments $Root 3600 -AllowFailure
            if ($install.exitCode -ne 0 -and $install.exitCode -ne [int]0x8A15002B -and $install.exitCode -ne [int]0x8A150061) { Stop-DD "winget returned $($install.exitCode). Check its log and any pending reboot." 3 }
        }
        $env:PATH = [Environment]::GetEnvironmentVariable('PATH', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('PATH', 'User')
        $script:DDCompilerReady = $null
    }
    else {
        $os = Get-Content /etc/os-release -Raw
        if ($os -notmatch '(?m)^ID="?ubuntu"?$') { Stop-DD 'Automatic Linux provisioning currently supports Ubuntu only. Install GCC, CMake 3.24+ and Ninja using your distribution package manager.' 3 }
        if ((Invoke-DDProcess id @('-u') $Root).stdout.Trim() -ne '0') { Stop-DD "Run these commands yourself: sudo apt-get update; sudo apt-get install $($packages -join ' '). Then rerun dd doctor." 3 }
        $null = Invoke-DDProcess apt-get @('update') $Root 1800
        $null = Invoke-DDProcess apt-get (@('install', '-y') + $packages) $Root 3600
    }
    Clear-DDRequirementReportCache
    $report = Get-DDDoctor $Root -ToolsOnly
    if (-not $report.ready) { Stop-DD "Installation did not satisfy requirements: $($report.issues -join '; ')" 3 }
    return $report
}

function Invoke-DDClean([string]$Root, $Options) {
    Assert-DDOptions $Options @('dry-run', 'yes') 2
    $Root = Find-DDProject $Root
    $manifest = Read-DDManifest $Root
    $choice = if ($Options.words.Count -eq 2) { $Options.words[1] } else { 'both' }
    if ($choice -notin @('debug', 'release', 'both')) { Stop-DD 'clean accepts debug, release or both; artifact deletion is not supported.' }
    $configs = if ($choice -eq 'both') { @('debug', 'release') } else { @($choice) }
    $catalog = Get-DDPresetCatalog $Root
    $paths = foreach ($config in $configs) {
        $state = Read-DDPresetState $Root $config
        if ($state.multiConfig -and $choice -ne 'both') { Stop-DD 'This is a shared multi-config tree; use clean both to authorize removal of every configuration.' }
        if ($choice -ne 'both') {
            foreach ($otherConfig in @('debug','release','ide')) {
                if ($otherConfig -eq $config) { continue }
                $mapping = if ($otherConfig -eq 'ide') { @{ configure = $manifest.build[(Get-DDPlatform)].ide } } else { Get-DDPresetMapping $manifest $otherConfig }
                if (-not $mapping.configure) { continue }
                $other = Get-DDPresetDirectory $Root (Resolve-DDPreset $catalog 'configurePresets' $mapping.configure)
                if ($other -eq $state.directory) { Stop-DD 'Build directory is shared with another configuration; use clean both.' }
            }
        }
        $relative = [IO.Path]::GetRelativePath($Root, $state.directory)
        $path = Get-DDPath $Root $relative
        if (Test-Path $path) {
            $tracked = Invoke-DDGit $Root @('ls-files', '--', $relative)
            if ($tracked.stdout) { Stop-DD "Refusing to delete tracked files in $relative." }
            $cache = Join-Path $path 'CMakeCache.txt'
            if (-not (Test-Path $cache)) { Stop-DD "Not a recognized CMake build directory: $relative" }
            $homeEntry = Select-String -LiteralPath $cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$' | Select-Object -First 1
            if (-not $homeEntry -or [IO.Path]::GetFullPath($homeEntry.Matches[0].Groups[1].Value) -ne $Root) { Stop-DD 'Build cache belongs to a different source tree; refusing cleanup.' }
            if (@(Get-ChildItem $path -Force -Recurse | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { Stop-DD 'Refusing to clean a tree containing linked paths.' }
            Assert-DDCachedSources $Root $path
            $path
        }
    }
    $paths = @($paths | Select-Object -Unique)
    if (-not $Options['dry-run'] -and $paths -and -not $Options.yes) { Stop-DD 'Review clean --dry-run and pass --yes to authorize deletion of generated build trees.' }
    if (-not $Options['dry-run']) { foreach ($path in $paths) { Remove-Item -LiteralPath $path -Recurse -Force } }
    return @{ paths = @($paths); status = $(if ($Options['dry-run']) { 'planned' } else { 'cleaned' }) }
}

function Invoke-DDGuiSmoke([string]$Binary, [string]$Root) {
    if (-not $IsWindows) { Stop-DD 'GUI smoke tests require Windows.' }
    $started = [DateTime]::UtcNow
    $process = Start-Process -FilePath $Binary -WorkingDirectory $Root -PassThru
    $result = @{ status = 'failed'; binary = $Binary; title = '' }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $wait = [Threading.ManualResetEvent]::new($false)
    try {
        while ($timer.Elapsed.TotalSeconds -lt 20) {
            $process.Refresh()
            if ($process.HasExited) { Stop-DD "GUI exited during startup ($($process.ExitCode))." 1 }
            if ($process.MainWindowHandle -ne 0) { break }
            $null = $wait.WaitOne(100)
        }
        if ($process.MainWindowHandle -eq 0) { Stop-DD 'GUI did not create a window within 20 seconds.' 1 }
        $null = $wait.WaitOne(800)
        $process.Refresh()
        if ($process.HasExited) { Stop-DD 'GUI exited before settling.' 1 }
        $result.title = $process.MainWindowTitle
        $result.status = 'passed'
    }
    finally {
        if (-not $process.HasExited) {
            $null = $process.CloseMainWindow()
            if (-not $process.WaitForExit(4000)) { $process.Kill($true); $process.WaitForExit() }
        }
        $reports = @(Get-ChildItem -LiteralPath $Root,(Split-Path $Binary) -File -Filter '*crash*' -ErrorAction SilentlyContinue | Where-Object LastWriteTimeUtc -ge $started)
        $process.Dispose()
        $wait.Dispose()
        if ($reports.Count) { Stop-DD "GUI produced a crash report: $($reports.FullName -join ', ')" 1 }
    }
    return $result
}

function Invoke-DDExtendedCommand([string]$Command, [string]$Root, $Options) {
    switch ($Command) {
        'dep' { return Invoke-DDDependencies $Root $Options }
        'toolchain' { return Invoke-DDToolchain $Root $Options }
        'clean' { return Invoke-DDClean $Root $Options }
        'ide' {
            Assert-DDOptions $Options @('yes', 'mcp', 'dry-run')
            # MCP registration is editor integration, not solution generation, so it runs
            # before the Windows-only Visual Studio path.
            if ($Options.mcp) {
                $server = Join-Path $script:DDHome 'mcp/server.ps1'
                if (-not (Test-Path $server)) { Stop-DD 'PowerShell MCP runtime is incomplete. Restore the source checkout or reinstall dd.' 3 }
                $Root = Find-DDProject $Root
                $driver = Get-DDPath $Root 'dd.ps1'
                if (-not (Test-Path $driver)) { Stop-DD 'No dd.ps1 driver at the project root. Run dd init or adopt dd first.' }
                $configuration = @{ servers = @{ dd = @{ type = 'stdio'; command = 'pwsh'; args = @('-NoProfile', '-NonInteractive', '-File', $driver, 'mcp') } } }
                $path = Get-DDPath $Root '.vscode/mcp.json'
                if ($Options['dry-run']) { return @{ path = $path; configuration = $configuration; status = 'planned' } }
                if (Test-Path $path) { Stop-DD 'Existing .vscode/mcp.json is preserved. Review dd ide --mcp --dry-run and merge it manually.' }
                [IO.Directory]::CreateDirectory((Split-Path $path)) | Out-Null
                [IO.File]::WriteAllText($path, ($configuration | ConvertTo-Json -Depth 8))
                return @{ registered = $path; status = 'registered' }
            }
            if (-not $IsWindows) { Stop-DD 'Visual Studio IDE generation is Windows-only.' }
            $Root = Find-DDProject $Root
            $manifest = Read-DDManifest $Root -ForBuild
            Assert-DDDependencies $Root
            Assert-DDRequirements $Root
            $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
            $installation = (Invoke-DDProcess $vswhere @('-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-format', 'json') $Root).stdout | ConvertFrom-Json
            $preset = $manifest.build['x64-windows'].ide
            if (-not $preset) { Stop-DD 'Declare build.x64-windows.ide as an existing Visual Studio configure preset.' }
            $state = Invoke-DDConfigure $Root @{ configure = $preset } 'ide'
            if ($state.generator -notmatch '^Visual Studio') { Stop-DD 'IDE preset must use a Visual Studio generator.' }
            $directory = $state.directory
            $solution = Get-ChildItem $directory -File | Where-Object Extension -in @('.sln', '.slnx') | Select-Object -First 1
            if (-not $solution) { Stop-DD 'CMake did not generate a solution.' 1 }
            if ($Options.yes) {
                $ide = Join-Path $installation[0].installationPath 'Common7/IDE/devenv.exe'
                if (-not (Test-Path $ide)) { Stop-DD "Solution generated at $($solution.FullName), but only Build Tools is installed. Install Visual Studio to open it." 3 }
                Start-Process -FilePath $ide -ArgumentList ('"' + $solution.FullName + '"') | Out-Null
            }
            return @{ solution = $solution.FullName; opened = [bool]$Options.yes }
        }
        'fmt' {
            Assert-DDOptions $Options @('dry-run')
            $Root = Find-DDProject $Root
            $paths = foreach ($folder in @('include', 'src', 'tests')) {
                $directory = Get-DDPath $Root $folder
                if (Test-Path $directory) {
                    foreach ($file in Get-ChildItem $directory -Recurse -File) {
                        if ($file.Extension -in @('.c', '.cpp', '.h', '.hpp')) { Get-DDPath $Root ([IO.Path]::GetRelativePath($Root, $file.FullName)) }
                    }
                }
            }
            if (-not $Options['dry-run']) {
                foreach ($path in $paths) { $null = Invoke-DDProcess clang-format @('-i', '--', $path) $Root }
            }
            return @{ paths = @($paths); status = $(if ($Options['dry-run']) { 'planned' } else { 'formatted' }) }
        }
        'self-update' {
            Assert-DDOptions $Options @('yes', 'dry-run', 'version')
            $bootstrap = Join-Path (Split-Path $script:DDHome) 'bootstrap.ps1'
            if (-not (Test-Path $bootstrap)) { Stop-DD 'This is a project-pinned driver. Update it through a reviewed change; use the installed bootstrap for user-level updates.' }
            $version = if ($Options.version) { $Options.version } else { 'latest' }
            if ($version -ne 'latest' -and $version -notmatch '^v\d+\.\d+\.\d+$') { Stop-DD 'Version must be latest or vMAJOR.MINOR.PATCH.' }
            $plan = @{ repository = 'https://github.com/ZacWalk/dd'; version = $version; mode = 'side-by-side user release'; profile = 'unchanged'; status = 'planned' }
            if (-not $Options['dry-run']) {
                if (-not $Options.yes) { Stop-DD 'Review self-update --dry-run, then use --yes to download a verified side-by-side release. Profile registration remains explicit.' }
                $result = Invoke-DDProcess pwsh @('-NoProfile', '-File', $bootstrap, '-Version', $version, '-NoProfile') $Root -Log
                $plan.status = 'installed'
                $plan.details = $result.stdout
            }
            return $plan
        }
        'adopt' {
            Assert-DDOptions $Options @('dry-run')
            if (-not $Options['dry-run']) { Stop-DD 'adopt is inspection-only; use --dry-run.' }
            $files = @(Get-ChildItem $Root -File -Force | Select-Object -ExpandProperty Name)
            $warnings = [Collections.Generic.List[string]]::new()
            $dependencies = @()
            try { $dependencies = @(Get-DDDependencies $Root) } catch { $warnings.Add($_.Exception.Message) }
            if ('dd.ps1' -in $files) { $warnings.Add('Existing driver: review its command semantics before replacing it.') }
            if ('CMakeLists.txt' -in $files) {
                $cmake = Get-Content (Join-Path $Root 'CMakeLists.txt') -Raw
                foreach ($dependency in $dependencies) { if ($cmake.Contains($dependency.url)) { $warnings.Add("Check duplicate acquisition of $($dependency.name): declaration and separate CMake URL present.") } }
            }
            return @{ root = $Root; files = $files; dependencies = $dependencies; warnings = $warnings.ToArray(); changed = $false }
        }
        'env' {
            Assert-DDOptions $Options @()
            Initialize-DDCompiler (Get-DDRequirements $Root)
            return @{ environment = @{ PATH = $env:PATH; INCLUDE = $env:INCLUDE; LIB = $env:LIB; LIBPATH = $env:LIBPATH }; note = 'Environment applies to this process only. Use the profile function dd env to import these values into PowerShell.' }
        }
        'mcp' {
            Assert-DDOptions $Options @('allow-execution')
            $server = Join-Path $script:DDHome 'mcp/server.ps1'
            if (-not (Test-Path $server)) { Stop-DD 'PowerShell MCP runtime is incomplete. Restore the source checkout or reinstall dd.' 3 }
            if ($Options.json) { Stop-DD 'dd mcp is a stdio server: stdout carries JSON-RPC messages, so --json does not apply.' }
            # Anchor the workspace boundary on the project root when there is one, but still
            # serve machine-level inspection outside a project.
            $Root = Resolve-DDProjectRoot $Root
            # Server mode owns stdout for protocol messages, so it bypasses the result
            # envelope entirely and exits before Invoke-DD can render anything.
            try { & $server -Root $Root -AllowExecution:([bool]$Options['allow-execution']) }
            catch {
                [Console]::Error.WriteLine("dd mcp: $($_.Exception.Message)")
                exit 1
            }
            exit 0
        }
        default { Stop-DD "Unknown or not yet supported command: $Command. Run dd help." }
    }
}