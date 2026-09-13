. (Join-Path $PSScriptRoot '../requirements.ps1')
. (Join-Path $PSScriptRoot '../project-commands.ps1')

function Get-DDMcpTools {
    $project = @{ type = 'string'; default = '.'; description = 'Existing directory inside the configured workspace root.' }
    $identifier = @{ type = 'string'; pattern = '^[A-Za-z][A-Za-z0-9_-]*$' }
    $command = @{ type = 'string'; pattern = '^[a-z][a-z0-9-]*$' }
    $text = @{ type = 'string'; minLength = 1 }
    $apply = @{ type = 'boolean'; default = $false }
    $applications = @{ type = 'array'; minItems = 1; items = $identifier }
    $arguments = @{ type = 'array'; items = @{ type = 'string' }; default = @() }
    function New-McpTool([string]$Name, [string]$Description, $Properties, [string[]]$Required = @(), [bool]$ReadOnly = $false, [bool]$Destructive = $false) {
        @{ name = $Name; description = $Description; inputSchema = @{ type = 'object'; properties = $Properties; required = $Required; additionalProperties = $false }; annotations = @{ readOnlyHint = $ReadOnly; destructiveHint = $Destructive; openWorldHint = -not $ReadOnly } }
    }
    New-McpTool 'dd_help' 'Get dd commands, version and JSON contract without executing project code.' @{} @() $true
    New-McpTool 'dd_commands' 'Discover declared scripts and typed metadata without executing project code or requiring a compiler.' @{ project = $project } @() $true
    New-McpTool 'dd_targets' 'List application IDs, paths, test labels, native support and default selection without building.' @{ project = $project } @() $true
    New-McpTool 'dd_doctor' 'Inspect native build prerequisites and the manifest without installing software.' @{ project = $project } @() $true
    New-McpTool 'dd_toolchain_plan' 'Inspect missing prerequisites and the installation plan. MCP never elevates or installs compilers.' @{ project = $project } @() $true
    New-McpTool 'dd_init' 'Scaffold a GUI, CLI or library project in an empty folder. Defaults to preview; GUI requires Windows.' @{ project = $project; type = @{ type = 'string'; enum = @('gui','cli','library') }; name = $identifier; apply = $apply } @('type','name')
    New-McpTool 'dd_dependencies' 'Inspect or mutate Git/archive pins without fetching, building or staging. Mutations default to preview.' @{
        project = $project; operation = @{ type = 'string'; enum = @('list','available','install','update') }; name = $command
        ref = $text; git = $text; url = $text; sha256 = @{ type = 'string'; pattern = '^[a-fA-F0-9]{64}$' }
        method = @{ type = 'string'; enum = @('fetchcontent','externalproject','application') }; apply = $apply
    } @('operation')
    New-McpTool 'dd_build' 'Build through project CMake presets. Requires -AllowExecution for a trusted workspace.' @{
        project = $project; configuration = @{ type = 'string'; enum = @('debug','release','both'); default = 'both' }; apps = $applications
        jobs = @{ type = 'integer'; minimum = 1; maximum = 1024 }
    }
    New-McpTool 'dd_test' 'Build both configurations and run selected CTest tests and applicable GUI smoke tests. Requires -AllowExecution.' @{ project = $project; apps = $applications; label = $text; name = $text }
    New-McpTool 'dd_run' 'Build Release and run a target with exact arguments and a bounded timeout. Requires -AllowExecution.' @{
        project = $project; target = $identifier; args = $arguments; timeout = @{ type = 'integer'; minimum = 1; maximum = 9999; default = 120 }
    } @() $false $true
    New-McpTool 'dd_launch' 'Build Release and start a persistent app with file-backed logs. Returns PID; app survives request and server exit. Requires -AllowExecution.' @{ project = $project; target = $identifier; args = $arguments } @() $false $true
    New-McpTool 'dd_command' 'Run only a declared project script. Requires -AllowExecution even for previews. Writes require dryRun=false and apply=true. Discover via dd_commands first.' @{
        project = $project; name = $command; parameters = @{ type = 'object'; additionalProperties = @{ type = @('string','number','boolean') }; default = @{} }
        dryRun = @{ type = 'boolean'; default = $true }; apply = $apply
    } @('name') $false $true
}

function Assert-DDMcpRoot([string]$Root) {
    $ancestor = $Root
    while ($ancestor) {
        if ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'MCP root cannot contain a linked directory.' }
        $ancestor = Split-Path $ancestor
    }
}

function Get-DDMcpProject([string]$Root, [string]$Project, [bool]$Manifest) {
    Assert-DDMcpRoot $Root
    $path = [IO.Path]::TrimEndingDirectorySeparator([IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($Project)) { $Project } else { Join-Path $Root $Project })))
    $comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    if (-not $path.Equals($Root, $comparison)) {
        try { $null = Get-DDPath $Root ([IO.Path]::GetRelativePath($Root, $path)) }
        catch { throw 'Project path is outside the configured workspace or contains a linked path.' }
    }
    if (-not (Test-Path -LiteralPath $path -PathType Container)) { throw 'Project directory does not exist.' }
    if ($Manifest -and -not (Test-Path -LiteralPath (Get-DDPath $path 'dd.psd1') -PathType Leaf)) { throw 'Project requires its own dd.psd1 manifest.' }
    return $path
}

function Convert-DDMcpCall($Parameters, [string]$Root, [bool]$AllowExecution) {
    $tool = @($script:McpTools | Where-Object { $_.name -ceq $Parameters.name })
    if ($tool.Count -ne 1) { $exception = [ArgumentException]::new('Unknown tool.'); $exception.Data['mcpCode'] = -32602; throw $exception }
    $values = if ($Parameters.Contains('arguments')) { $Parameters.arguments } else { @{} }
    $schema = $tool[0].inputSchema
    if (-not (Test-Json -Json ($values | ConvertTo-Json -Depth 40 -Compress) -Schema ($schema | ConvertTo-Json -Depth 40 -Compress) -ErrorAction SilentlyContinue)) {
        $exception = [ArgumentException]::new('Tool arguments do not match inputSchema.'); $exception.Data['mcpCode'] = -32602; throw $exception
    }
    foreach ($key in $schema.properties.Keys) {
        if (-not $values.Contains($key) -and $schema.properties[$key].ContainsKey('default')) { $values[$key] = $schema.properties[$key].default }
    }
    if ($Parameters.name -in @('dd_build','dd_test','dd_run','dd_launch','dd_command') -and -not $AllowExecution) { throw 'Project code execution is disabled. Restart with -AllowExecution only for a trusted workspace.' }
    $needsManifest = $Parameters.name -in @('dd_commands','dd_targets','dd_build','dd_test','dd_run','dd_launch','dd_command') -or ($Parameters.name -eq 'dd_dependencies' -and $values.operation -ne 'available')
    $path = Get-DDMcpProject $Root $(if ($values.Contains('project')) { $values.project } else { '.' }) $needsManifest
    $arguments = [Collections.Generic.List[string]]::new()
    switch -CaseSensitive ($Parameters.name) {
        'dd_help' { $arguments.Add('help') }
        'dd_commands' { $arguments.Add('commands') }
        'dd_targets' { $arguments.Add('targets') }
        'dd_doctor' { $arguments.Add('doctor') }
        'dd_toolchain_plan' { $arguments.Add('toolchain'); $arguments.Add('--dry-run') }
        'dd_init' {
            $arguments.Add('init'); $arguments.Add("--type=$($values.type)"); $arguments.Add("--name=$($values.name)")
            if (-not $values.apply) { $arguments.Add('--dry-run') }
        }
        'dd_dependencies' {
            $arguments.Add('dep'); $arguments.Add($(if ($values.operation -eq 'available') { 'list' } else { $values.operation }))
            if ($values.operation -eq 'available') { $arguments.Add('--available') }
            if ($values.name) { $arguments.Add($values.name) }
            foreach ($key in @('ref','git','url','sha256','method')) { if ($values.Contains($key)) { $arguments.Add("--${key}=$($values[$key])") } }
            if (-not $values.apply -and $values.operation -in @('install','update')) { $arguments.Add('--dry-run') }
        }
        'dd_build' {
            $arguments.Add('build'); $arguments.Add($values.configuration)
            if ($values.apps) { $arguments.Add('--app=' + ($values.apps -join ',')) }
            if ($values.jobs) { $arguments.Add("--jobs=$($values.jobs)") }
        }
        'dd_test' {
            $arguments.Add('test')
            if ($values.apps) { $arguments.Add('--app=' + ($values.apps -join ',')) }
            foreach ($key in @('label','name')) { if ($values.Contains($key)) { $arguments.Add("--${key}=$($values[$key])") } }
        }
        { $_ -in @('dd_run','dd_launch') } {
            $arguments.Add($Parameters.name.Substring(3))
            if ($values.target) { $arguments.Add($values.target) }
            if ($values.Contains('timeout')) { $arguments.Add("--timeout=$($values.timeout)") }
            $arguments.Add('--'); foreach ($argument in $values.args) { $arguments.Add($argument) }
        }
        'dd_command' {
            if ($values.dryRun -and $values.apply) { throw 'Choose dryRun=true or apply=true, not both.' }
            if ($values.name -in @(Get-DDBuiltinCommands)) { throw 'Built-in commands cannot be invoked through dd_command.' }
            $manifest = Read-DDManifest $path
            $entries = @(Get-DDCommandMetadata $path $manifest | Where-Object { $_.name -ceq $values.name })
            if ($entries.Count -ne 1) { throw 'Unknown project command. Use dd_commands to inspect declarations.' }
            $entry = $entries[0]
            if (-not $values.dryRun -and $entry.effects -eq 'write' -and -not $values.apply) { throw 'Write command execution requires apply=true.' }
            $arguments.Add($values.name)
            foreach ($key in $values.parameters.Keys) {
                if (-not $entry.parameters.ContainsKey($key)) { throw "Unknown command parameter: $key" }
                $value = Convert-DDParameterValue $values.parameters[$key] $entry.parameters[$key] $key
                $rendered = if ($value -is [bool]) { $value.ToString().ToLowerInvariant() } else { [string]$value }
                $arguments.Add("--${key}=$rendered")
            }
            if ($values.dryRun) { $arguments.Add('--dry-run') }
            if ($values.apply) { $arguments.Add('--yes') }
        }
    }
    return @{ project = $path; arguments = $arguments.ToArray() }
}