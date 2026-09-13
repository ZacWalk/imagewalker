function Get-DDCommandMetadata([string]$Root, $Manifest) {
    if (-not $Manifest.commands) { return }
    foreach ($name in ($Manifest.commands.Keys | Sort-Object)) {
        $definition = $Manifest.commands[$name]
        $parameters = if ($definition.parameters) { $definition.parameters } else { @{} }
        $platforms = if ($definition.ContainsKey('platforms')) { @($definition.platforms) } else { @('x64-windows', 'x64-linux') }
        @{ name = $name; description = $definition.description; script = $definition.script; parameters = $parameters;
            effects = $definition.effects; supportsDryRun = [bool]$definition['supports-dry-run']; platforms = $platforms;
            timeoutSeconds = $(if ($definition.ContainsKey('timeout-secs')) { $definition['timeout-secs'] } else { 60 });
            scriptExists = Test-Path -LiteralPath (Get-DDPath $Root $definition.script) -PathType Leaf }
    }
}

function Get-DDTargetMetadata($Manifest) {
    $platform = Get-DDPlatform
    foreach ($target in $Manifest.targets) {
        @{ id = $target.id; kind = $target.kind; cmakeTarget = $target['cmake-target'];
            debugPath = $target['debug-path']; releasePath = $target['release-path']; testLabel = $target['test-label'];
            platforms = @(Get-DDTargetPlatforms $target); isDefault = $target.id -eq $Manifest.project['default-target'];
            runnable = $target.kind -ne 'library' -and $platform -in @(Get-DDTargetPlatforms $target) -and [bool]$Manifest.build[$platform] }
    }
}

function Select-DDRunTarget($Manifest, $Options) {
    $platform = Get-DDPlatform
    $name = if ($Options.words.Count -eq 2) { $Options.words[1] } else { $Manifest.project['default-target'] }
    if (-not $name) {
        $available = @($Manifest.targets | Where-Object { $_.kind -ne 'library' -and $platform -in @(Get-DDTargetPlatforms $_) })
        # Assert-DDBuildHost only proves some target supports the host, not that any of
        # them is runnable, so a library-only project reaches here with nothing to infer.
        if ($available.Count -eq 0) { Stop-DD "This project declares no executable target to run on $platform; its targets are libraries. Build or test them instead, or add an executable target. Use dd targets." }
        if ($available.Count -eq 1) { $name = $available[0].id }
        elseif ($Options['non-interactive']) { Stop-DD "Multiple targets; specify one or set project.default-target. Available: $($available.id -join ', ')." }
        else { $name = Read-Host "Run target ($($available.id -join ', '))" }
    }
    $selected = @($Manifest.targets | Where-Object id -eq $name)
    if ($selected.Count -ne 1) { Stop-DD "Unknown target: $name. Use dd targets." }
    if ($selected[0].kind -eq 'library') { Stop-DD "Target $name is a library and has no executable to run; build or test it, or choose an executable target. Use dd targets." }
    if ($platform -notin @(Get-DDTargetPlatforms $selected[0])) { Stop-DD "Target $name does not support $platform; choose another target explicitly." }
    return $selected[0]
}

function Update-DDTargetLaunch([string]$Root, $Manifest, $Options) {
    $path = Get-DDPath $Root '.vscode/launch.json'
    $taskPath = Get-DDPath $Root '.vscode/tasks.json'
    if (-not (Test-Path -LiteralPath $taskPath)) { Stop-DD 'Keep the scaffold dd: build debug task before generating launch configurations.' }
    try {
        $tasks = Get-Content -LiteralPath $taskPath -Raw | ConvertFrom-Json -AsHashtable -ErrorAction Stop
        $original = if (Test-Path -LiteralPath $path) { [IO.File]::ReadAllText($path) } else { $null }
        $launch = if ($null -ne $original) { $original | ConvertFrom-Json -AsHashtable -ErrorAction Stop } else { @{ version = '0.2.0'; configurations = @() } }
    }
    catch { Stop-DD 'VS Code config must be parseable JSON/JSONC; no files were changed.' }
    if (@($tasks.tasks | Where-Object label -eq 'dd: build debug').Count -ne 1) { Stop-DD 'Expected exactly one dd: build debug task.' }
    if ($launch -isnot [Collections.IDictionary] -or $launch.configurations -isnot [array]) { Stop-DD 'Invalid launch configurations array.' }
    $added = @()
    foreach ($target in $Manifest.targets) {
        if ($target.kind -eq 'library') { continue }
        foreach ($platform in @(Get-DDTargetPlatforms $target)) {
            if (-not $Manifest.build[$platform]) { continue }
            $type = if ($platform -eq 'x64-windows') { 'cppvsdbg' } else { 'cppdbg' }
            $relative = (Expand-DDTargetPath $target['debug-path'] $platform).Replace('\', '/')
            $program = '${workspaceFolder}/' + $relative
            $name = "dd: $($target.id) ($platform) Debug"
            if (@($launch.configurations | Where-Object { $_.type -eq $type -and $_.program -eq $program }).Count) { continue }
            if (@($launch.configurations | Where-Object name -eq $name).Count) { Stop-DD "Existing launch '$name' differs; update it manually rather than overwriting it." }
            $entry = [ordered]@{ name = $name; type = $type; request = 'launch'; program = $program; args = @(); cwd = '${workspaceFolder}'; stopAtEntry = $false; preLaunchTask = 'dd: build debug' }
            if ($type -eq 'cppvsdbg') { $entry.console = 'integratedTerminal' }
            else { $entry.externalConsole = $false; $entry.MIMode = 'gdb'; $entry.miDebuggerPath = '/usr/bin/gdb' }
            $added += $entry
        }
    }
    $result = @{ path = '.vscode/launch.json'; added = $added; changed = $false; dryRun = [bool]$Options['dry-run'] }
    if ($Options['dry-run'] -or -not $added.Count) { return $result }
    if (-not $Options.yes) { Stop-DD 'Review targets --vscode --dry-run, then use --yes to add missing launch entries.' }
    if ((Test-Path -LiteralPath $path) -and [IO.File]::ReadAllText($path) -cne $original) { Stop-DD 'Launch config changed during inspection; retry.' }
    if ($null -ne $original) { Backup-DDFile $path }
    $launch.configurations = @($launch.configurations) + $added
    [IO.File]::WriteAllText($path, ($launch | ConvertTo-Json -Depth 30) + "`n")
    $result.changed = $true
    return $result
}

function Invoke-DDProjectCommand([string]$Root, $Manifest, [string]$Name, $Options) {
    if (-not $Manifest.commands -or -not $Manifest.commands.ContainsKey($Name)) { Stop-DD "Unknown project command: $Name. Use dd commands." }
    $definition = $Manifest.commands[$Name]
    $definitions = if ($definition.parameters) { $definition.parameters } else { @{} }
    Assert-DDOptions $Options (@('dry-run', 'yes') + @($definitions.Keys))
    $metadata = @(Get-DDCommandMetadata $Root $Manifest | Where-Object name -eq $Name)[0]
    if ((Get-DDPlatform) -notin $metadata.platforms) { Stop-DD "Command $Name is unsupported on this host." }
    $parameters = @{}
    foreach ($parameter in $definitions.Keys) {
        $spec = $definitions[$parameter]
        if ($Options.ContainsKey("seen:$parameter")) { $parameters[$parameter] = Convert-DDParameterValue $Options[$parameter] $spec $parameter -FromCli }
        elseif ($spec.ContainsKey('default')) { $parameters[$parameter] = $spec.default }
        elseif ($spec.required) { Stop-DD "Missing required parameter --$parameter." }
    }
    $dryRun = [bool]$Options['dry-run']
    if ($dryRun -and -not $metadata.supportsDryRun) { Stop-DD "Command $Name does not support a script-backed dry-run. Inspect it with dd help $Name." }
    if (-not $dryRun -and $definition.effects -eq 'write' -and -not $Options.yes) {
        if ($Options['non-interactive']) { Stop-DD "Command $Name writes project data. Use --dry-run to preview or --yes to authorize execution." }
        if ((Read-Host "Run project command '$Name' with write access? [y/N]") -ne 'y') { Stop-DD 'Project command cancelled.' }
    }
    $scriptPath = Get-DDPath $Root $definition.script
    if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) { Stop-DD "Project command script not found: $($definition.script)" 3 }
    $request = @{ schema = 1; command = $Name; projectRoot = $Root; parameters = $parameters; dryRun = $dryRun }
    $json = $request | ConvertTo-Json -Depth 12 -Compress
    if ([Text.Encoding]::UTF8.GetByteCount($json) -gt 1MB) { Stop-DD 'Project command request exceeds 1 MiB.' }
    # Pinning the parent's pipes to UTF-8 only fixes half of it: the child pwsh still
    # decodes stdin and encodes stdout using the console code page it inherited. Aligning
    # that by changing the console itself would outlive dd and corrupt the caller's shell,
    # so the child is pinned in-process instead. The path is single-quoted for the child,
    # and $false/$LASTEXITCODE are escaped so the parent does not expand them here.
    $escaped = $scriptPath.Replace("'", "''")
    $launcher = @('-NoProfile', '-NonInteractive')
    if ($IsWindows) {
        $launcher += @('-Command', "[Console]::InputEncoding=[Text.UTF8Encoding]::new(`$false);[Console]::OutputEncoding=[Text.UTF8Encoding]::new(`$false);& '$escaped';exit `$LASTEXITCODE")
    }
    else { $launcher += @('-File', $scriptPath) }
    $execution = Invoke-DDProcess (Join-Path $PSHOME $(if ($IsWindows) { 'pwsh.exe' } else { 'pwsh' })) $launcher $Root $metadata.timeoutSeconds -AllowFailure -Log -InputJson $json
    if ($execution.exitCode -ne 0) { Stop-DD "Project command $Name failed ($($execution.exitCode)). Log: $($execution.log). $($execution.stderr.Trim())" $execution.exitCode }
    if ([Text.Encoding]::UTF8.GetByteCount($execution.stdout) -gt 1MB) { Stop-DD "Project command output exceeds 1 MiB. Log: $($execution.log)" 1 }
    try { $response = $execution.stdout | ConvertFrom-Json -AsHashtable -ErrorAction Stop } catch { Stop-DD "Project command $Name must return one JSON response. Log: $($execution.log)" 1 }
    if ($response -isnot [Collections.IDictionary] -or $response.schema -ne 1 -or -not $response.Contains('data') -or $response.files -isnot [array]) { Stop-DD "Project command $Name returned an invalid schema 1 response. Log: $($execution.log)" 1 }
    foreach ($file in $response.files) {
        if ($file -isnot [string] -or [string]::IsNullOrWhiteSpace($file)) { Stop-DD 'Response files must be project-relative paths.' 1 }
        $null = Get-DDPath $Root $file
    }
    return @{ command = $Name; dryRun = $dryRun; result = $response.data; files = $response.files; log = $execution.log }
}