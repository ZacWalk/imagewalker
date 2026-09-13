$script:DDHome = $PSScriptRoot
$script:DDVersion = '0.2.0'

# Authoritative runtime inventory. Copy-DDRuntime, Invoke-DD, tools/prepare.ps1,
# tools/package.ps1 and bootstrap.ps1 all derive their file lists from these.
function Get-DDRuntimeModules {
    return @('requirements.ps1', 'presets.ps1', 'execution.ps1', 'dependencies.ps1', 'commands.ps1', 'project-commands.ps1')
}

function Get-DDRuntimeFiles {
    return @('core.ps1') + (Get-DDRuntimeModules) + @('dependencies.cmake', 'catalog.json', 'mcp/server.ps1', 'mcp/tools.ps1', 'mcp/invoke.ps1')
}

function Get-DDReleaseFiles {
    return @('dd.ps1', 'profile.ps1') + ((Get-DDRuntimeFiles) | ForEach-Object { ".dd/$_" }) +
        @('.dd/templates/common/dd.psd1', '.dd/templates/common/cmake/dd-dependencies.json')
}

function Stop-DD([string]$Message, [int]$Code = 2) {
    $exception = [InvalidOperationException]::new($Message)
    $exception.Data['exitCode'] = $Code
    throw $exception
}

function Read-DDOptions([string[]]$Arguments) {
    $options = @{ json = $false; 'dry-run' = $false; 'non-interactive' = $false; yes = $false }
    $positionals = [Collections.Generic.List[string]]::new()
    $forwarded = [Collections.Generic.List[string]]::new()
    $valueOptions = @('type', 'name', 'project', 'ref', 'git', 'url', 'sha256', 'method', 'jobs', 'target', 'timeout', 'version', 'app', 'label')
    $flagOptions = @('json', 'dry-run', 'non-interactive', 'yes', 'available', 'verbose', 'no-summary', 'mcp', 'allow-execution', 'vscode')
    for ($index = 0; $index -lt $Arguments.Count; $index++) {
        $argument = $Arguments[$index]
        if ($argument -eq '--') {
            for ($tail = $index + 1; $tail -lt $Arguments.Count; $tail++) { $forwarded.Add($Arguments[$tail]) }
            break
        }
        if ($argument.StartsWith('--')) {
            $parts = $argument.Substring(2) -split '=', 2
            $key = $parts[0]
            if ($options.ContainsKey("seen:$key")) { Stop-DD "Duplicate option --$key." }
            $options["seen:$key"] = $true
            $customCommand = $positionals.Count -gt 0 -and $positionals[0] -notin @(Get-DDBuiltinCommands)
            if ($key -in $flagOptions) {
                if ($parts.Count -ne 1) { Stop-DD "--$key is a switch, not a value option." }
                $options[$key] = $true
            }
            elseif ($key -in $valueOptions -or ($customCommand -and $key -cmatch '^[a-z][a-z0-9-]*$')) {
                if ($parts.Count -eq 2) { $options[$key] = $parts[1] }
                else {
                    $index++
                    if ($index -ge $Arguments.Count -or $Arguments[$index].StartsWith('--')) { Stop-DD "--$key requires a value; use --$key=VALUE for values starting with --." }
                    $options[$key] = $Arguments[$index]
                }
            }
            else { Stop-DD "Unknown option: $argument" }
        }
        else { $positionals.Add($argument) }
    }
    $options.words = $positionals.ToArray()
    $options.forwarded = $forwarded.ToArray()
    if ($env:CI -or [Console]::IsInputRedirected -or $options.json) { $options['non-interactive'] = $true }
    return $options
}

function Assert-DDOptions($Options, [string[]]$Allowed, [int]$MaximumWords = 1) {
    foreach ($key in $Options.Keys) {
        if ($key.StartsWith('seen:') -and $key.Substring(5) -notin (@('json', 'project', 'non-interactive') + $Allowed)) {
            Stop-DD "Option --$($key.Substring(5)) does not apply to this command."
        }
    }
    if ($Options.words.Count -gt $MaximumWords) { Stop-DD 'Too many arguments. Run dd help.' }
    if ($Options.forwarded.Count -and $Options.words[0] -notin @('run','launch')) { Stop-DD 'Only run and launch accept arguments after --.' }
}

function Get-DDPlatform {
    $architecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture
    if ($architecture -ne 'X64') { Stop-DD "Only native x64 hosts are supported; this host reports $architecture. Run dd from an x64 PowerShell, or track arm64 support upstream." 3 }
    if ($IsWindows) { return 'x64-windows' }
    if ($IsLinux) { return 'x64-linux' }
    Stop-DD 'Only Windows and Linux are supported.' 3
}

function Get-DDPath([string]$Root, [string]$Relative) {
    if ([IO.Path]::IsPathRooted($Relative)) { Stop-DD "Expected a relative path: $Relative" }
    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd([IO.Path]::DirectorySeparatorChar)
    $full = [IO.Path]::GetFullPath((Join-Path $rootPath $Relative))
    $comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    if (-not $full.StartsWith($rootPath + [IO.Path]::DirectorySeparatorChar, $comparison)) { Stop-DD "Path escapes project: $Relative" }
    $current = $full
    while ($current -and $current -ne $rootPath) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -Force -LiteralPath $current).Attributes -band [IO.FileAttributes]::ReparsePoint) { Stop-DD "Linked paths are not supported: $current" }
        }
        $current = Split-Path $current
    }
    return $full
}

# Nearest ancestor holding a manifest, or $Start when there is no project above it.
function Resolve-DDProjectRoot([string]$Start) {
    $current = [IO.Path]::GetFullPath($Start)
    $boundary = $null
    $comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    if ($env:DD_MCP_WORKSPACE_ROOT) {
        $boundary = [IO.Path]::TrimEndingDirectorySeparator([IO.Path]::GetFullPath($env:DD_MCP_WORKSPACE_ROOT))
        if (-not $current.Equals($boundary, $comparison)) { $null = Get-DDPath $boundary ([IO.Path]::GetRelativePath($boundary, $current)) }
    }
    while ($current) {
        if (Test-Path -LiteralPath (Join-Path $current 'dd.psd1')) { return $current }
        if (Test-Path -LiteralPath (Join-Path $current 'dd.toml')) { Stop-DD 'Legacy dd.toml detected. Convert it to a dd.psd1 data hashtable; renaming the file is not sufficient.' }
        if ($boundary -and $current.Equals($boundary, $comparison)) { break }
        $parent = Split-Path $current
        if ($parent -eq $current) { break }
        $current = $parent
    }
    return [IO.Path]::GetFullPath($Start)
}

function Find-DDProject([string]$Start) {
    $root = Resolve-DDProjectRoot $Start
    if (Test-Path -LiteralPath (Join-Path $root 'dd.psd1')) { return $root }
    Stop-DD 'No dd.psd1 found. Create an empty folder and run dd init, or use --project.' 3
}

function Read-DDManifest([string]$Root, [switch]$ForBuild) {
    $path = Get-DDPath $Root 'dd.psd1'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        if (Test-Path -LiteralPath (Join-Path $Root 'dd.toml')) { Stop-DD 'Legacy dd.toml detected. Convert it to a dd.psd1 data hashtable; renaming the file is not sufficient.' }
        Stop-DD 'No dd.psd1 manifest found.' 3
    }
    # A single command reads the manifest several times; parse and validate once per content.
    if ($null -eq $script:DDManifestCache) { $script:DDManifestCache = @{} }
    $text = [IO.File]::ReadAllText($path)
    $cached = $script:DDManifestCache[$path]
    if ($cached -and $cached.text -ceq $text) {
        if ($ForBuild) { Assert-DDBuildHost $cached.model }
        return ,$cached.model
    }
    $model = Read-DDManifestModel $Root $path
    $script:DDManifestCache[$path] = @{ text = $text; model = $model }
    if ($ForBuild) { Assert-DDBuildHost $model }
    return ,$model
}

function Read-DDManifestModel([string]$Root, [string]$Path) {
    try { $model = Import-PowerShellDataFile -LiteralPath $Path -ErrorAction Stop }
    catch { Stop-DD "Invalid dd.psd1 data file: $($_.Exception.Message)" }
    Assert-DDFields $model @('schema', 'project', 'build', 'targets', 'commands', 'dependencies', 'requirements') 'the manifest'
    if ($model.ContainsKey('dependencies')) {
        Assert-DDFields $model.dependencies @('owner') 'dependencies'
        if ($model.dependencies.owner -notin @('dd', 'application')) { Stop-DD 'dependencies.owner must be dd or application.' }
    }
    if ($model.ContainsKey('requirements')) { Assert-DDRequirementData $model.requirements }
    if ($model['schema'] -ne 1) { Stop-DD 'Unsupported manifest schema. Expected schema = 1.' }
    Assert-DDPresent $model @('project', 'build', 'targets') 'the manifest'
    $project = $model['project']
    Assert-DDFields $project @('name', 'type', 'default-target') 'project'
    Assert-DDPresent $project @('name', 'type') 'project'
    if (-not $project -or $project['name'] -notmatch '^[A-Za-z][A-Za-z0-9_-]*$' -or $project['type'] -notin @('gui', 'cli', 'library')) { Stop-DD 'Invalid [project] name or type.' }
    $build = $model['build']
    Assert-DDFields $build @('x64-windows', 'x64-linux') 'build'
    foreach ($hostName in $build.Keys) {
        Assert-DDFields $build[$hostName] @('debug', 'release', 'ide') "build.$hostName"
        Assert-DDPresent $build[$hostName] @('debug', 'release') "build.$hostName"
        foreach ($configuration in @('debug', 'release')) {
            $entry = $build[$hostName][$configuration]
            if ($entry -is [hashtable]) {
                Assert-DDFields $entry @('configure','build','test') "build.$hostName.$configuration"
                foreach ($phase in @('configure','build','test')) { if ($entry[$phase] -isnot [string] -or -not $entry[$phase]) { Stop-DD 'Preset mapping needs configure, build and test names.' } }
            } elseif ($entry -isnot [string] -or -not $entry) { Stop-DD 'Preset names must be nonempty strings or phase mappings.' }
        }
        if ($build[$hostName].ContainsKey('ide') -and ($build[$hostName].ide -isnot [string] -or -not $build[$hostName].ide)) { Stop-DD 'IDE preset must be a configure preset name.' }
    }
    if ($build.Count -eq 0) { Stop-DD 'At least one native build preset table is required.' }
    $ids = @()
    if ($model['targets'] -isnot [array] -or $model['targets'].Count -eq 0) { Stop-DD 'targets must be a nonempty array of hashtables.' }
    foreach ($target in $model['targets']) {
        Assert-DDFields $target @('id', 'kind', 'cmake-target', 'debug-path', 'release-path', 'platforms', 'test-label') 'a target'
        Assert-DDPresent $target @('id', 'kind', 'cmake-target', 'debug-path', 'release-path') 'a target'
        if ($target.ContainsKey('test-label') -and ($target['test-label'] -isnot [string] -or -not $target['test-label'])) { Stop-DD 'test-label must be a nonempty string.' }
        if ($target['id'] -notmatch '^[A-Za-z][A-Za-z0-9_-]*$' -or $target['id'] -in $ids) { Stop-DD 'Target IDs must be valid and unique.' }
        $ids += $target['id']
        if ($target['kind'] -notin @('gui', 'cli', 'library') -or -not $target['cmake-target']) { Stop-DD 'Targets require kind and cmake-target.' }
        if ($target.ContainsKey('platforms')) { Assert-DDPlatforms $target.platforms }
        if ($target.kind -eq 'gui' -and $target.platforms -contains 'x64-linux') { Stop-DD 'GUI targets cannot declare Linux support yet.' }
        foreach ($config in @('debug', 'release')) {
            if ($target["$config-path"] -isnot [string] -or -not $target["$config-path"]) { Stop-DD 'Target paths must be nonempty strings.' }
            foreach ($platform in @('x64-windows', 'x64-linux')) {
                $null = Get-DDPath $Root (Expand-DDTargetPath $target["$config-path"] $platform)
            }
        }
    }
    if ($project.ContainsKey('default-target') -and ($project['default-target'] -isnot [string] -or $project['default-target'] -notin $ids)) { Stop-DD 'project.default-target must name a declared target.' }
    if ($model.ContainsKey('commands')) { Assert-DDProjectCommands $Root $model.commands }
    return $model
}

function Get-DDBuiltinCommands {
    return @('help', 'init', 'toolchain', 'doctor', 'dep', 'build', 'test', 'run', 'launch', 'clean', 'ide', 'fmt', 'env', 'self-update', 'adopt', 'mcp', 'commands', 'targets')
}

function Assert-DDPlatforms($Platforms) {
    if ($Platforms -isnot [array] -or $Platforms.Count -eq 0) { Stop-DD 'platforms must be a nonempty array.' }
    foreach ($platform in $Platforms) { if ($platform -notin @('x64-windows', 'x64-linux')) { Stop-DD "Unsupported platform: $platform" } }
}

function Get-DDTargetPlatforms($Target) {
    if ($Target.ContainsKey('platforms')) { return $Target.platforms }
    if ($Target.kind -eq 'gui') { return @('x64-windows') }
    return @('x64-windows', 'x64-linux')
}

# Static library naming differs by toolchain, so {libprefix} and {lib} carry the
# gcc "libfoo.a" versus MSVC "foo.lib" split the way {exe} carries ".exe".
function Expand-DDTargetPath($Template, [string]$Platform) {
    $windows = $Platform -eq 'x64-windows'
    return [string]$Template -replace '\{platform\}', $Platform `
        -replace '\{exe\}', $(if ($windows) { '.exe' } else { '' }) `
        -replace '\{libprefix\}', $(if ($windows) { '' } else { 'lib' }) `
        -replace '\{lib\}', $(if ($windows) { '.lib' } else { '.a' })
}

function Assert-DDBuildHost($Manifest) {
    $platform = Get-DDPlatform
    if (-not $Manifest.build[$platform]) { Stop-DD "Missing [build.$platform] presets." }
    if (-not @($Manifest.targets | Where-Object { $platform -in @(Get-DDTargetPlatforms $_) }).Count) { Stop-DD "No runnable targets support $platform. GUI apps require Windows." }
}

function Convert-DDParameterValue($Value, $Definition, [string]$Name, [switch]$FromCli) {
    switch ($Definition.type) {
        'string' { if ($Value -isnot [string]) { Stop-DD "Parameter $Name must be a string." } }
        'integer' {
            if ($FromCli) {
                $number = 0L
                if ($Value -notmatch '^-?[0-9]+$' -or -not [long]::TryParse($Value, [ref]$number)) { Stop-DD "Parameter $Name must be an integer." }
                $Value = $number
            }
            elseif ($Value -isnot [int] -and $Value -isnot [long]) { Stop-DD "Parameter $Name must be an integer." }
        }
        'boolean' {
            if ($FromCli) {
                if ($Value -notin @('true', 'false')) { Stop-DD "Parameter $Name must be true or false." }
                $Value = $Value -ieq 'true'
            }
            elseif ($Value -isnot [bool]) { Stop-DD "Parameter $Name must be a boolean." }
        }
        default { Stop-DD "Unsupported parameter type for $Name." }
    }
    if ($Definition.ContainsKey('choices') -and $Value -cnotin $Definition.choices) { Stop-DD "Parameter $Name must be one of: $($Definition.choices -join ', ')." }
    return $Value
}

function Assert-DDProjectCommands([string]$Root, $Commands) {
    if ($Commands -isnot [hashtable]) { Stop-DD 'commands must be a data hashtable.' }
    foreach ($name in $Commands.Keys) {
        if ($name -cnotmatch '^[a-z][a-z0-9-]*$' -or $name -in @(Get-DDBuiltinCommands)) { Stop-DD "Invalid or reserved project command: $name" }
        $definition = $Commands[$name]
        Assert-DDFields $definition @('description', 'script', 'parameters', 'timeout-secs', 'supports-dry-run', 'effects', 'platforms') "command $name"
        Assert-DDPresent $definition @('description', 'script', 'effects') "command $name" "command $name"
        Assert-DDPresent $definition @('description', 'script', 'effects') "command $name"
        if ($definition.description -isnot [string] -or [string]::IsNullOrWhiteSpace($definition.description)) { Stop-DD "Command $name requires a description." }
        if ($definition.script -isnot [string] -or [IO.Path]::GetExtension($definition.script) -ne '.ps1') { Stop-DD "Command $name requires a project-relative .ps1 script." }
        $null = Get-DDPath $Root $definition.script
        if ($definition.effects -notin @('read', 'write')) { Stop-DD "Command $name requires effects = read or write." }
        if ($definition.ContainsKey('supports-dry-run') -and $definition['supports-dry-run'] -isnot [bool]) { Stop-DD 'supports-dry-run must be a boolean.' }
        if ($definition.ContainsKey('timeout-secs') -and (($definition['timeout-secs'] -isnot [int] -and $definition['timeout-secs'] -isnot [long]) -or $definition['timeout-secs'] -lt 1 -or $definition['timeout-secs'] -gt 9999)) { Stop-DD 'timeout-secs must be an integer from 1 to 9999.' }
        if ($definition.ContainsKey('platforms')) { Assert-DDPlatforms $definition.platforms }
        if ($definition.ContainsKey('parameters') -and $definition.parameters -isnot [hashtable]) { Stop-DD "Command $name parameters must be a hashtable." }
        foreach ($parameter in @($definition.parameters.Keys)) {
            if ($null -eq $parameter) { continue }
            if ($parameter -cnotmatch '^[a-z][a-z0-9-]*$' -or $parameter -in @('project', 'json', 'non-interactive', 'dry-run', 'yes', 'available', 'verbose', 'no-summary', 'mcp', 'allow-execution', 'vscode', 'words', 'forwarded')) { Stop-DD "Invalid or reserved parameter: $parameter" }
            $spec = $definition.parameters[$parameter]
            Assert-DDFields $spec @('type', 'description', 'choices', 'default', 'required') "parameter $parameter"
            Assert-DDPresent $spec @('type') "parameter $parameter" "parameter $parameter"
            Assert-DDPresent $spec @('type') "parameter $parameter"
            if ($spec.type -notin @('string', 'integer', 'boolean')) { Stop-DD "Unsupported parameter type: $parameter" }
            if ($spec.ContainsKey('description') -and $spec.description -isnot [string]) { Stop-DD 'Parameter description must be a string.' }
            if ($spec.ContainsKey('required') -and $spec.required -isnot [bool]) { Stop-DD 'Parameter required must be a boolean.' }
            if ($spec.ContainsKey('choices')) {
                if ($spec.choices -isnot [array] -or $spec.choices.Count -eq 0) { Stop-DD 'Parameter choices must be a nonempty array.' }
                foreach ($choice in $spec.choices) { $null = Convert-DDParameterValue $choice @{ type = $spec.type } $parameter }
            }
            if ($spec.ContainsKey('default')) { $null = Convert-DDParameterValue $spec.default $spec $parameter }
        }
    }
}

function Assert-DDFields($Table, [string[]]$Fields, [string]$Context = 'this table') {
    if ($null -eq $Table -or $Table -isnot [hashtable]) { Stop-DD "Expected a data hashtable for $Context." }
    foreach ($key in $Table.Keys) { if ($key -notin $Fields) { Stop-DD "Unsupported schema 1 field: $key" } }
}

function Assert-DDPresent($Table, [string[]]$Fields, [string]$Context) {
    $missing = @($Fields | Where-Object { -not $Table.ContainsKey($_) })
    if ($missing.Count) { Stop-DD "Missing required schema 1 field$(if ($missing.Count -gt 1) { 's' }) in ${Context}: $($missing -join ', ')" }
}

function Write-DDProgress([string]$Message) { [Console]::Error.WriteLine("dd: $Message") }

function Resolve-DDTool([string]$Tool) {
    # Rooted paths bypass Get-Command, whose -Name parameter treats [] as a wildcard.
    if ([IO.Path]::IsPathRooted($Tool)) {
        if (-not (Test-Path -LiteralPath $Tool -PathType Leaf)) { Stop-DD "Executable not found: $Tool" 3 }
        return $Tool
    }
    $command = Get-Command $Tool -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $command) { Stop-DD "$Tool is missing. Run dd toolchain or install it with your OS package manager." 3 }
    return $command.Source
}

function Assert-DDProcessResult($Result, [string]$Tool, [switch]$IncludeOutput) {
    if ($Result.exitCode -eq 0) { return }
    $detail = if ($IncludeOutput) {
        $text = ($Result.stdout + $Result.stderr)
        ' ' + $text.Substring(0, [Math]::Min(3000, $text.Length))
    } else { "`n" + $Result.stderr.Trim() }
    Stop-DD "$Tool failed ($($Result.exitCode)). Log: $($Result.log)$detail" 1
}

function Invoke-DDProcess([string]$Tool, [string[]]$ToolArgs, [string]$Directory, [int]$Timeout = 600, [switch]$AllowFailure, [switch]$Log, [string]$InputJson, [string]$Progress) {
    if ($Progress) { Write-DDProgress $Progress }
    $source = Resolve-DDTool $Tool
    $start = [Diagnostics.ProcessStartInfo]::new($source)
    $start.WorkingDirectory = $Directory
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.RedirectStandardInput = $true
    # Redirected streams otherwise inherit the Windows console code page, which cannot
    # represent most non-ASCII text. Pin them so the JSON bridge is byte-exact.
    $start.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $start.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
    $start.StandardInputEncoding = [Text.UTF8Encoding]::new($false)
    if ([IO.Path]::GetFileName($source) -eq 'cmd.exe') { $start.Arguments = $ToolArgs -join ' ' }
    else { foreach ($argument in $ToolArgs) { $start.ArgumentList.Add($argument) } }
    $start.Environment['GIT_TERMINAL_PROMPT'] = '0'
    $start.Environment['GCM_INTERACTIVE'] = 'Never'
    $start.Environment['GIT_SSH_COMMAND'] = 'ssh -oBatchMode=yes'
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        $null = $process.Start()
        # Long phases stream to a log and to stderr, so -Log is implied: the stream needs a sink.
        if ($Progress -and -not $PSBoundParameters.ContainsKey('InputJson')) {
            $result = Invoke-DDStreamingProcess $process $Directory $Timeout $Progress
            if (-not $AllowFailure) { Assert-DDProcessResult $result $Tool -IncludeOutput }
            return $result
        }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if ($PSBoundParameters.ContainsKey('InputJson')) {
            try {
                $write = $process.StandardInput.WriteLineAsync($InputJson)
                if (-not $write.Wait($Timeout * 1000)) { Stop-DD "$Tool did not accept its request within $Timeout seconds." 1 }
            }
            catch {
                if (-not $process.HasExited) { $process.Kill($true); $process.WaitForExit() }
                throw
            }
        }
        $process.StandardInput.Close()
        $timedOut = -not $process.WaitForExit($Timeout * 1000)
        if ($timedOut) { $process.Kill($true); $process.WaitForExit() }
        $output = $stdout.GetAwaiter().GetResult()
        $errorOutput = $stderr.GetAwaiter().GetResult()
        $logPath = $null
        if ($Log) {
            $logDir = Join-Path ([IO.Path]::GetTempPath()) 'dd-logs'
            [IO.Directory]::CreateDirectory($logDir) | Out-Null
            $logPath = Join-Path $logDir ([guid]::NewGuid().ToString('N') + '.log')
            [IO.File]::WriteAllText($logPath, $output + $errorOutput)
            if ($null -ne $script:DDLogs) { $script:DDLogs.Add($logPath) }
        }
        $result = @{ exitCode = $process.ExitCode; stdout = $output; stderr = $errorOutput; log = $logPath; timedOut = $timedOut }
        if ($timedOut) { Stop-DD "$Tool timed out after $Timeout seconds. Log: $logPath" 1 }
        if (-not $AllowFailure) { Assert-DDProcessResult $result $Tool }
        return $result
    }
    finally { $process.Dispose() }
}

function Invoke-DDGit([string]$Root, [string[]]$GitArgs, [switch]$AllowFailure) {
    Invoke-DDProcess git (@('-c', 'protocol.ext.allow=never', '-c', 'protocol.file.allow=never', '-c', 'core.hooksPath=/dev/null') + $GitArgs) $Root -AllowFailure:$AllowFailure
}

function Backup-DDFile([string]$Path, [int]$Keep = 3) {
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    [IO.File]::Copy($Path, "$Path.dd-backup-$stamp")
    $existing = @(Get-ChildItem -LiteralPath (Split-Path $Path) -Force -File -Filter "$([IO.Path]::GetFileName($Path)).dd-backup-*" -ErrorAction SilentlyContinue | Sort-Object Name -Descending)
    foreach ($stale in @($existing | Select-Object -Skip $Keep)) { Remove-Item -LiteralPath $stale.FullName -Force -ErrorAction SilentlyContinue }
}

function Import-DDCompilerEnvironment($Variables) {
    foreach ($name in $Variables.Keys) { [Environment]::SetEnvironmentVariable($name, [string]$Variables[$name], 'Process') }
    foreach ($tool in @('cl.exe', 'link.exe', 'rc.exe')) { if (-not (Get-Command $tool -CommandType Application -ErrorAction SilentlyContinue)) { return $false } }
    return $true
}

function Initialize-DDCompiler($Requirements) {
    if (-not $IsWindows) { return }
    if (-not $Requirements) { $Requirements = Get-DDRequirements $null }
    $key = $Requirements.msvc | ConvertTo-Json -Compress
    if ($script:DDCompilerReady -eq $key) { return }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path $vswhere)) { Stop-DD 'Visual Studio discovery is missing. Run dd toolchain.' 3 }
    $found = Invoke-DDProcess $vswhere (@('-latest', '-products', '*', '-version', "[$($Requirements.msvc.minimum),)", '-requires') + $Requirements.msvc.components + @('-property', 'installationPath')) ([IO.Path]::GetTempPath())
    if (-not $found.stdout.Trim()) { Stop-DD "No Visual Studio $($Requirements.msvc.minimum)+ installation satisfies components: $($Requirements.msvc.components -join ', '). Run dd toolchain --dry-run." 3 }
    $vcvars = Join-Path $found.stdout.Trim() 'VC/Auxiliary/Build/vcvars64.bat'
    if (-not (Test-Path -LiteralPath $vcvars -PathType Leaf)) { Stop-DD "Visual Studio is installed but $vcvars is missing. Repair the C++ workload, then rerun dd doctor." 3 }
    if ($env:__VSCMD_PREINIT_PATH) { $env:PATH = $env:__VSCMD_PREINIT_PATH }
    foreach ($variable in @('VSCMD_VER', 'VSCMD_ARG_TGT_ARCH', 'VSCMD_ARG_HOST_ARCH')) { [Environment]::SetEnvironmentVariable($variable, $null, 'Process') }
    $environment = Invoke-DDProcess $env:ComSpec @('/d', '/s', '/c', "`"`"$vcvars`" >nul && set`"") ([IO.Path]::GetTempPath())
    $variables = [ordered]@{}
    foreach ($line in ($environment.stdout -split '\r?\n')) {
        if ($line -match '^([^=]+)=(.*)$') { $variables[$Matches[1]] = $Matches[2] }
    }
    if (-not (Import-DDCompilerEnvironment $variables)) { Stop-DD 'MSVC setup did not provide cl.exe, link.exe and rc.exe.' 4 }
    $script:DDCompilerReady = $key
}

function Get-DDDoctor([string]$Root, [switch]$ToolsOnly) {
    $report = Get-DDRequirementReport $Root
    if (-not $ToolsOnly -and (Test-Path (Join-Path $Root 'dd.psd1'))) { Assert-DDDependencies $Root }
    $dependencies = if (-not $ToolsOnly -and (Get-Command Get-DDDependencies -ErrorAction SilentlyContinue)) { @(Get-DDDependencies $Root) } else { @() }
    if ((Test-Path (Join-Path $Root 'dd.psd1')) -or (Test-Path (Join-Path $Root 'dd.toml'))) { $null = Read-DDManifest $Root -ForBuild }
    $report.dependencies = @($dependencies)
    $report.dependencyOwner = Get-DDDependencyOwner $Root
    $report.inventoryKnown = $report.dependencyOwner -eq 'dd'
    return $report
}

function Copy-DDRuntime([string]$Destination) {
    [IO.Directory]::CreateDirectory($Destination) | Out-Null
    foreach ($relative in (Get-DDRuntimeFiles)) {
        $source = Join-Path $script:DDHome $relative
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { Stop-DD "Runtime incomplete: missing .dd/$relative. Restore the source checkout or reinstall dd." 3 }
        $target = Join-Path $Destination $relative
        [IO.Directory]::CreateDirectory((Split-Path $target)) | Out-Null
        Copy-Item -LiteralPath $source -Destination $target
    }
    Copy-Item -LiteralPath (Join-Path $script:DDHome 'templates') -Destination (Join-Path $Destination 'templates') -Recurse
}

function New-DDProject($Options, [string]$Root) {
    $platform = Get-DDPlatform
    $type = $Options.type
    if (-not $type) {
        if ($Options['non-interactive'] -or $Options['dry-run']) { Stop-DD 'init requires --type gui|cli|library in non-interactive or dry-run mode.' }
        $prompt = if ($IsWindows) { 'App type (gui/cli/library)' } else { 'App type (cli/library; GUI awaits platform-h Linux support)' }
        $type = Read-Host $prompt
    }
    if ($type -notin @('gui', 'cli', 'library')) { Stop-DD 'App type must be gui, cli or library.' }
    if ($type -eq 'gui' -and -not $IsWindows) { Stop-DD 'GUI scaffolding is Windows-only until platform-h supports Linux.' }
    $name = if ($Options.name) { $Options.name } else { Split-Path $Root -Leaf }
    if ($name -notmatch '^[A-Za-z][A-Za-z0-9_-]*$') { Stop-DD 'Use --name with letters, numbers, hyphens or underscores, starting with a letter.' }
    if (@(Get-ChildItem -LiteralPath $Root -Force | Where-Object Name -ne '.git').Count) { Stop-DD 'init requires an empty folder. Use dd adopt --dry-run to inspect an existing app.' }
    $gitRoot = Invoke-DDGit $Root @('rev-parse', '--show-toplevel') -AllowFailure
    if ($gitRoot.exitCode -eq 0 -and [IO.Path]::GetFullPath($gitRoot.stdout.Trim()) -ne $Root) { Stop-DD 'Cannot initialize inside another Git working tree.' }
    if ($gitRoot.exitCode -eq 0) {
        $tracked = Invoke-DDGit $Root @('ls-files')
        $head = Invoke-DDGit $Root @('rev-parse', '--verify', 'HEAD') -AllowFailure
        if ($tracked.stdout -or $head.exitCode -eq 0) { Stop-DD 'Existing Git repository is not empty.' }
    }
    $sources = if ($type -eq 'library') { @('include/applib.h', 'src/applib.c', 'src/main.c', 'tests/lib_tests.c') } else { @('src/main.cpp') }
    $plan = @{ name = $name; type = $type; root = $Root; platform = $platform; dependencies = @(); files = @('dd.ps1', '.dd/', 'dd.psd1', 'cmake/dd-dependencies.json', 'CMakeLists.txt', 'CMakePresets.json') + $sources + @('tests/smoke.cpp', '.gitignore', 'README.md', 'AGENTS.md', '.vscode/', '.github/workflows/ci.yml') }
    if ($type -eq 'gui') { $plan.dependencies = @('platform-h') }
    if ($Options['dry-run']) { $plan.status = 'planned'; return $plan }
    if (-not (Test-Path (Join-Path $script:DDHome 'templates/common/dd.psd1'))) { Stop-DD 'Runtime incomplete: missing dd.psd1 template. Restore the source checkout or reinstall dd.' 3 }
    if ($gitRoot.exitCode -ne 0) { $null = Invoke-DDGit $Root @('init') }
    Copy-Item -LiteralPath (Join-Path (Split-Path $script:DDHome) 'dd.ps1') -Destination $Root
    Copy-DDRuntime (Join-Path $Root '.dd')
    $ciRunners = if ($type -eq 'gui') { '["windows-latest"]' } else { '["windows-latest", "ubuntu-24.04"]' }
    $summary = switch ($type) {
        'gui' { 'C++20 GUI application' }
        'library' { 'C static library with an example CLI' }
        default { 'C++20 CLI application' }
    }
    foreach ($item in Get-ChildItem (Join-Path $script:DDHome 'templates/common') -File -Recurse -Force) {
        $relative = [IO.Path]::GetRelativePath((Join-Path $script:DDHome 'templates/common'), $item.FullName)
        $destination = Get-DDPath $Root $relative
        [IO.Directory]::CreateDirectory((Split-Path $destination)) | Out-Null
        [IO.File]::WriteAllText($destination, ([IO.File]::ReadAllText($item.FullName).Replace('@NAME@', $name).Replace('@TYPE@', $type).Replace('@SUMMARY@', $summary).Replace('@CI_RUNNERS@', $ciRunners)))
    }
    $launchPath = Join-Path $Root '.vscode/launch.json'
    $launch = Get-Content -LiteralPath $launchPath -Raw | ConvertFrom-Json
    $nativeDebugger = if ($IsWindows) { 'cppvsdbg' } else { 'cppdbg' }
    $launch.configurations = @($launch.configurations | Where-Object { $type -ne 'gui' -or $_.type -eq 'cppvsdbg' } | Sort-Object { $_.type -ne $nativeDebugger })
    [IO.File]::WriteAllText($launchPath, ($launch | ConvertTo-Json -Depth 10))
    if ($type -eq 'library') {
        # A library scaffold ships the library, its public header, an example CLI and a
        # unit test, so the two-target shape is visible from the first build. Its manifest
        # replaces the single-target common template rather than patching it.
        $libraryRoot = Join-Path $script:DDHome 'templates/library/files'
        foreach ($item in Get-ChildItem $libraryRoot -File -Recurse -Force) {
            $destination = Get-DDPath $Root ([IO.Path]::GetRelativePath($libraryRoot, $item.FullName))
            [IO.Directory]::CreateDirectory((Split-Path $destination)) | Out-Null
            [IO.File]::WriteAllText($destination, [IO.File]::ReadAllText($item.FullName).Replace('@NAME@', $name))
        }
        $libraryManifest = Join-Path $script:DDHome 'templates/library/dd.psd1'
        [IO.File]::WriteAllText((Join-Path $Root 'dd.psd1'), [IO.File]::ReadAllText($libraryManifest).Replace('@NAME@', $name))
    }
    else {
        $source = Join-Path $script:DDHome "templates/$type/main.cpp"
        [IO.Directory]::CreateDirectory((Join-Path $Root 'src')) | Out-Null
        [IO.File]::WriteAllText((Join-Path $Root 'src/main.cpp'), [IO.File]::ReadAllText($source).Replace('@NAME@', $name))
    }
    $appCmake = [IO.File]::ReadAllText((Join-Path $script:DDHome "templates/$type/app.cmake"))
    $cmake = Join-Path $Root 'CMakeLists.txt'
    [IO.File]::WriteAllText($cmake, [IO.File]::ReadAllText($cmake).Replace('@APP_CMAKE@', $appCmake))
    if ($type -eq 'gui') { $null = Install-DDDependency $Root 'platform-h' $null $null $false }
    $plan.status = 'created'
    return $plan
}

function Invoke-DDBuild([string]$Root, $Manifest, [string]$Config, $Options, [string[]]$Targets = @()) {
    Assert-DDRequirements $Root
    $platform = Get-DDPlatform
    if ($Options.target -and $Options.target -ne $platform) { Stop-DD 'Cross-compilation is not supported.' }
    $mapping = Get-DDPresetMapping $Manifest $Config
    $state = Invoke-DDConfigure $Root $mapping $Config
    $buildArgs = @('--build', '--preset', $mapping.build, '--parallel', $(if ($Options.jobs) { $Options.jobs } else { [Environment]::ProcessorCount.ToString() }))
    if ($Targets.Count) { $buildArgs += @('--target') + $Targets }
    if ($state.generator -match '^Ninja') { $buildArgs += @('--', '-k', '0') }
    $build = Invoke-DDProcess cmake $buildArgs $Root -Log -AllowFailure -Progress "$Config build"
    $diagnostics = Get-DDDiagnostics ($build.stdout + $build.stderr)
    if ($build.exitCode) { Stop-DD "Build failed ($Config). Log: $($build.log). Diagnostics: $($diagnostics | ConvertTo-Json -Depth 5 -Compress)" 1 }
    return @{ configuration = $Config; log = $build.log; status = 'built'; diagnostics = $diagnostics }
}

function Get-DDDiagnostics([string]$Text) {
    $items = foreach ($line in ($Text -split '\r?\n')) {
        if ($line -match '^(.*?)\(\d+(?:,\d+)?\):\s*(?:fatal )?(warning|error) ([A-Z]+\d+):') {
            [pscustomobject]@{ file = $Matches[1]; severity = $Matches[2]; code = $Matches[3] }
        }
        elseif ($line -match '^(.*?):\d+:\d+:\s*(?:fatal )?(warning|error):.*?(\[-W[^\]]+\])?$') {
            [pscustomobject]@{ file = $Matches[1]; severity = $Matches[2]; code = $(if ($Matches[3]) { $Matches[3] } else { 'uncoded' }) }
        }
    }
    return @{ total = @($items).Count; byFile = @($items | Group-Object file | Sort-Object Count -Descending | Select-Object Name,Count); byCode = @($items | Group-Object code | Sort-Object Count -Descending | Select-Object Name,Count) }
}

function Invoke-DDCommand($Options) {
    $command = if ($Options.words.Count) { $Options.words[0] } else { 'help' }
    $root = [IO.Path]::GetFullPath($(if ($Options.project) { $Options.project } else { (Get-Location).Path }))
    if (-not (Test-Path -LiteralPath $root -PathType Container)) { Stop-DD "Directory not found: $root" }
    # init deliberately stays in the current folder; every other project-aware command
    # resolves the nearest manifest so a subdirectory behaves like the project root.
    if ($command -in @('doctor', 'toolchain', 'dep', 'env', 'adopt', 'mcp')) { $root = Resolve-DDProjectRoot $root }
    if ($command -in @(Get-DDBuiltinCommands) -and $Options.jobs -and ($Options.jobs -notmatch '^[1-9][0-9]*$' -or [long]$Options.jobs -gt 1024)) { Stop-DD '--jobs must be 1..1024.' }
    switch ($command) {
        'help' {
            Assert-DDOptions $Options @() 2
            if ($Options.words.Count -eq 2) {
                $root = Find-DDProject $root
                $manifest = Read-DDManifest $root
                $name = $Options.words[1]
                $entry = @(Get-DDCommandMetadata $root $manifest | Where-Object name -eq $name)
                if ($entry.Count -ne 1) { Stop-DD "No project command named $name. Use dd help for built-ins or dd commands." }
                return $entry[0]
            }
            return @{ version = $script:DDVersion; commands = @('init --type gui|cli|library [--name NAME]', 'toolchain [--yes]', 'doctor', 'dep list|install|update', 'build [debug|release|both] [--app ID,ID]', 'test [--app ID,ID] [--label REGEX] [--name REGEX]', 'run [TARGET] [--timeout SECONDS] -- ARGS', 'launch [TARGET] -- ARGS', 'targets', 'commands', 'help NAME', 'clean [debug|release|both] [--yes]', 'ide [--yes] [--mcp]', 'fmt [--dry-run]', 'env', 'self-update [--version vX.Y.Z] [--yes]', 'adopt --dry-run', 'mcp [--allow-execution]'); options = @('--json', '--project PATH', '--non-interactive'); repository = 'https://github.com/ZacWalk/dd' }
        }
        'commands' {
            Assert-DDOptions $Options @()
            $root = Find-DDProject $root
            $manifest = Read-DDManifest $root
            return @{ commands = @(Get-DDCommandMetadata $root $manifest) }
        }
        'targets' {
            Assert-DDOptions $Options @('vscode', 'dry-run', 'yes')
            $root = Find-DDProject $root
            $manifest = Read-DDManifest $root
            if ($Options.vscode) { return Update-DDTargetLaunch $root $manifest $Options }
            if ($Options['dry-run'] -or $Options.yes) { Stop-DD '--dry-run/--yes require targets --vscode.' }
            return @{ targets = @(Get-DDTargetMetadata $manifest); defaultTarget = $manifest.project['default-target'] }
        }
        'init' { Assert-DDOptions $Options @('type', 'name', 'dry-run'); return New-DDProject $Options $root }
        'doctor' { Assert-DDOptions $Options @(); $result = Get-DDDoctor $root; if (-not $result.ready) { $script:DDExitCode = 3 }; return $result }
        { $_ -in @('build', 'test', 'run', 'launch') } {
            Assert-DDOptions $Options @('jobs', 'target', 'timeout', 'app', 'label', 'name') 2
            if (($Options.label -or $Options.name) -and $command -ne 'test') { Stop-DD '--label/--name apply only to test.' }
            if ($Options.app -and $command -notin @('build','test')) { Stop-DD '--app applies to build/test; run/launch take a positional app ID.' }
            if ($Options.timeout -and ($command -ne 'run' -or $Options.timeout -notmatch '^[1-9][0-9]{0,3}$')) { Stop-DD '--timeout applies to run and must be 1..9999 seconds.' }
            $root = Find-DDProject $root
            $manifest = Read-DDManifest $root -ForBuild
            $configs = @('release', 'debug')
            if ($command -eq 'build' -and $Options.words.Count -gt 1) {
                if ($Options.words[1] -notin @('debug', 'release', 'both')) { Stop-DD 'Expected debug, release or both.' }
                if ($Options.words[1] -ne 'both') { $configs = @($Options.words[1]) }
            }
            if ($command -eq 'test' -and $Options.words.Count -gt 1) { Stop-DD 'test runs the configured CTest suite; no target argument is supported yet.' }
            $targets = @()
            $applications = @()
            if ($Options.app) {
                $ids = @($Options.app -split ',')
                foreach ($id in $ids) {
                    $match = @($manifest.targets | Where-Object id -eq $id)
                    if ($match.Count -ne 1 -or (Get-DDPlatform) -notin @(Get-DDTargetPlatforms $match[0])) { Stop-DD "Unknown or unsupported app: $id" }
                    $applications += $match[0]
                }
                $targets = @($applications | ForEach-Object { $_['cmake-target'] })
                if ($command -eq 'test' -and @($applications | Where-Object { -not $_['test-label'] }).Count) { Stop-DD 'Selected apps must declare test-label.' }
                if ($command -eq 'test') { $targets = @() }
            }
            if ($command -in @('run','launch')) {
                $selected = @(Select-DDRunTarget $manifest $Options)
                $targets = @([string]$selected[0]['cmake-target'])
                $configs = @('release')
            }
            if (Get-Command Assert-DDDependencies -ErrorAction SilentlyContinue) { Assert-DDDependencies $root }
            Assert-DDRequirements $root
            $results = [Collections.Generic.List[object]]::new()
            $failures = [Collections.Generic.List[string]]::new()
            foreach ($config in $configs) {
                try {
                    $results.Add((Invoke-DDBuild $root $manifest $config $Options $targets))
                    if ($command -eq 'test') {
                        $results.Add((Invoke-DDTests $root $manifest $config $Options $applications))
                        $smokeTargets = if ($applications.Count) { $applications } else { $manifest.targets }
                        foreach ($gui in @($smokeTargets | Where-Object { $_['kind'] -eq 'gui' -and (Get-DDPlatform) -in @(Get-DDTargetPlatforms $_) })) {
                            if ($Options.label -or $Options.name) { continue }
                            $binary = Get-DDPath $root (Expand-DDTargetPath $gui["$config-path"] (Get-DDPlatform))
                            $results.Add((Invoke-DDGuiSmoke $binary $root))
                        }
                    }
                }
                catch {
                    $failures.Add($_.Exception.Message)
                    if ($_.Exception.Data.Contains('testFailures')) { $script:DDTestFailures += @($_.Exception.Data['testFailures']) }
                }
            }
            if ($failures.Count) {
                # Keep whatever did succeed so a half-failed run stays actionable.
                $script:DDPartial = @{ results = $results.ToArray() }
                Stop-DD ($failures -join "`n") 1
            }
            if ($command -in @('run','launch')) {
                $binary = Get-DDPath $root (Expand-DDTargetPath $selected[0]['release-path'] (Get-DDPlatform))
                if ($command -eq 'launch') { return Start-DDApplication $binary $Options.forwarded $root }
                $timeout = if ($Options.timeout -match '^[1-9][0-9]{0,3}$') { [int]$Options.timeout } elseif ($Options.timeout) { Stop-DD '--timeout must be 1..9999 seconds.' } else { 120 }
                $run = Invoke-DDProcess $binary $Options.forwarded $root $timeout -AllowFailure -Log
                $script:DDExitCode = $run.exitCode
                if ($run.exitCode -and $null -ne $script:DDErrors) { $script:DDErrors.Add("$($selected[0].id) exited with code $($run.exitCode). Log: $($run.log)") }
                return $run
            }
            return @{ results = $results.ToArray() }
        }
        default {
            if ($command -notin @(Get-DDBuiltinCommands)) {
                $root = Find-DDProject $root
                return Invoke-DDProjectCommand $root (Read-DDManifest $root) $command $Options
            }
            if (Get-Command Invoke-DDExtendedCommand -ErrorAction SilentlyContinue) { return Invoke-DDExtendedCommand $command $root $Options }
            Stop-DD "Unknown command: $command. Run dd help."
        }
    }
}

function Write-DDDiagnosticSummary($Diagnostics) {
    if (-not $Diagnostics -or -not $Diagnostics.total) { return }
    Write-Host "  $($Diagnostics.total) diagnostic$(if ($Diagnostics.total -ne 1) { 's' })"
    foreach ($group in @('byFile', 'byCode')) {
        $entries = @($Diagnostics.$group | Select-Object -First 5)
        if (-not $entries.Count) { continue }
        Write-Host "  $(if ($group -eq 'byFile') { 'Worst files' } else { 'Most frequent codes' }):"
        foreach ($entry in $entries) { Write-Host ('    {0,6}  {1}' -f $entry.Count, $entry.Name) }
    }
}

function Write-DDHumanResult($Data) {
    if ($null -eq $Data) { return }
    if ($Data -is [Collections.IDictionary] -and $Data.Contains('results')) {
        foreach ($result in @($Data.results)) {
            Write-Host "$($result.configuration) $($result.status)"
            if ($result.tests) { Write-Host "  $(@($result.tests).Count) test$(if (@($result.tests).Count -ne 1) { 's' }) passed" }
            Write-DDDiagnosticSummary $result.diagnostics
            if ($result.log) { Write-Host "  Log: $($result.log)" }
        }
        if ($Data.Contains('testFailures') -and @($Data.testFailures).Count) {
            Write-Host 'Failed tests:'
            foreach ($failure in @($Data.testFailures)) { Write-Host "  $($failure.name)" }
        }
        return
    }
    $Data | ConvertTo-Json -Depth 20 | Write-Host
}

function Invoke-DD([string[]]$Arguments) {
    $script:DDExitCode = 0
    $script:DDTestFailures = @()
    $script:DDPartial = $null
    $script:DDErrors = [Collections.Generic.List[string]]::new()
    $script:DDLogs = [Collections.Generic.List[string]]::new()
    $json = $Arguments -contains '--json'
    $envelope = [ordered]@{ schema = 1; version = $script:DDVersion; ok = $false; exitCode = 0; data = $null; errors = @(); logs = @() }
    try {
        foreach ($module in (Get-DDRuntimeModules)) {
            $path = Join-Path $script:DDHome $module
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { Stop-DD "Runtime incomplete: missing .dd/$module. Reinstall dd or restore the vendored runtime." 3 }
            . $path
        }
        $options = Read-DDOptions $Arguments
        $json = $options.json
        $envelope.data = Invoke-DDCommand $options
        $envelope.ok = $script:DDExitCode -eq 0
    }
    catch {
        $script:DDExitCode = if ($_.Exception.Data.Contains('exitCode')) { [int]$_.Exception.Data['exitCode'] } else { 1 }
        $script:DDErrors.Add($_.Exception.Message)
    }
    $envelope.exitCode = $script:DDExitCode
    $envelope.errors = $script:DDErrors.ToArray()
    if ($null -eq $envelope.data -and $script:DDPartial) { $envelope.data = $script:DDPartial }
    if ($script:DDTestFailures.Count) {
        if ($envelope.data -isnot [Collections.IDictionary]) { $envelope.data = @{} }
        $envelope.data['testFailures'] = $script:DDTestFailures
    }
    $envelope.logs = $script:DDLogs.ToArray()
    if ($json) { [Console]::Out.WriteLine(($envelope | ConvertTo-Json -Depth 30 -Compress)) }
    else {
        Write-DDHumanResult $envelope.data
        foreach ($message in $envelope.errors) { [Console]::Error.WriteLine($message) }
        # The rendered result already cites the logs that matter; list the rest only on failure.
        if (-not $envelope.ok) { foreach ($log in $envelope.logs) { [Console]::Error.WriteLine("Log: $log") } }
    }
    exit $script:DDExitCode
}