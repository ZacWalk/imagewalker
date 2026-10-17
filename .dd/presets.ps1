function Get-DDPresetMapping($Manifest, [string]$Config) {
    $entry = $Manifest.build[(Get-DDPlatform)][$Config]
    if ($entry -is [string]) { return @{ configure = $entry; build = $entry; test = $entry } }
    return $entry
}

# Presets are shared, mutable state; every value handed out is copied so that resolving
# one preset can never write into another preset's inherited table.
function Copy-DDPresetValue($Value) {
    if ($Value -is [Collections.IDictionary]) {
        $copy = @{}
        foreach ($key in $Value.Keys) { $copy[$key] = Copy-DDPresetValue $Value[$key] }
        return $copy
    }
    if ($Value -is [object[]]) { return @($Value | ForEach-Object { Copy-DDPresetValue $_ }) }
    return $Value
}

function Get-DDPresetCatalog([string]$Root) {
    if ($null -eq $script:DDPresetCache) { $script:DDPresetCache = @{} }
    $cached = $script:DDPresetCache[$Root]
    if ($cached -and (Test-DDPresetCache $Root $cached)) { return $cached }
    $catalog = @{ configurePresets = @{}; buildPresets = @{}; testPresets = @{}; files = @{}; stamps = @{} }
    $visiting = [Collections.Generic.HashSet[string]]::new()
    function Read-PresetFile([string]$Path) {
        $Path = [IO.Path]::GetFullPath($Path)
        if ($visiting.Contains($Path)) { Stop-DD 'CMake preset include cycle.' }
        if ($catalog.files.ContainsKey($Path)) { return }
        $null = Get-DDPath $Root ([IO.Path]::GetRelativePath($Root, $Path))
        $null = $visiting.Add($Path)
        $content = [IO.File]::ReadAllText($Path)
        try { $data = $content | ConvertFrom-Json -AsHashtable -ErrorAction Stop } catch { Stop-DD "Invalid preset JSON: $Path" }
        $catalog.files[$Path] = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
        $item = Get-Item -LiteralPath $Path
        $catalog.stamps[$Path] = "$($item.LastWriteTimeUtc.Ticks):$($item.Length)"
        foreach ($include in @($data.include)) {
            if (-not $include) { continue }
            $include = $include.Replace('${sourceDir}', $Root).Replace('${fileDir}', (Split-Path $Path))
            $include = [regex]::Replace($include, '\$penv\{([^}]+)\}', { param($match) [Environment]::GetEnvironmentVariable($match.Groups[1].Value) })
            if ($include -match '\$') { Stop-DD 'Unsupported macro in preset include path.' }
            Read-PresetFile (Join-Path (Split-Path $Path) $include)
        }
        foreach ($kind in @('configurePresets','buildPresets','testPresets')) {
            foreach ($preset in @($data[$kind])) {
                if (-not $preset) { continue }
                if ($catalog[$kind].ContainsKey($preset.name)) { Stop-DD "Duplicate CMake preset: $($preset.name)" }
                $copy = Copy-DDPresetValue $preset
                if ($copy.binaryDir) { $copy.binaryDir = $copy.binaryDir.Replace('${fileDir}', (Split-Path $Path)) }
                $catalog[$kind][$copy.name] = $copy
            }
        }
        $null = $visiting.Remove($Path)
    }
    Read-PresetFile (Join-Path $Root 'CMakePresets.json')
    if (Test-Path (Join-Path $Root 'CMakeUserPresets.json')) { Read-PresetFile (Join-Path $Root 'CMakeUserPresets.json') }
    $catalog.root = $Root
    $catalog.userPresets = Test-Path (Join-Path $Root 'CMakeUserPresets.json')
    $script:DDPresetCache[$Root] = $catalog
    return $catalog
}

function Test-DDPresetCache([string]$Root, $Catalog) {
    if ($Catalog.userPresets -ne (Test-Path (Join-Path $Root 'CMakeUserPresets.json'))) { return $false }
    foreach ($path in $Catalog.stamps.Keys) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $false }
        $item = Get-Item -LiteralPath $path
        if ($Catalog.stamps[$path] -ne "$($item.LastWriteTimeUtc.Ticks):$($item.Length)") { return $false }
    }
    return $true
}

function Resolve-DDPreset($Catalog, [string]$Kind, [string]$Name, [string[]]$Parents = @()) {
    if ($Name -in $Parents) { Stop-DD 'CMake preset inheritance cycle.' }
    if (-not $Catalog[$Kind].ContainsKey($Name)) { Stop-DD "Missing $Kind preset: $Name" }
    $entry = $Catalog[$Kind][$Name]
    if (-not $Parents.Count -and $entry.hidden) { Stop-DD "$Name is a hidden CMake preset; name a selectable preset in dd.psd1." }
    $result = @{}
    $inherit = @($entry.inherits)
    for ($index = $inherit.Count - 1; $index -ge 0; $index--) {
        if (-not $inherit[$index]) { continue }
        $base = Resolve-DDPreset $Catalog $Kind $inherit[$index] ($Parents + $Name)
            foreach ($key in $base.Keys) {
                if ($key -in @('name','hidden','inherits')) { continue }
                if ($key -in @('environment','cacheVariables') -and $result[$key] -is [Collections.IDictionary]) {
                    foreach ($nested in $base[$key].Keys) { $result[$key][$nested] = $base[$key][$nested] }
                } else { $result[$key] = Copy-DDPresetValue $base[$key] }
            }
    }
    foreach ($key in $entry.Keys) {
        if ($key -in @('environment','cacheVariables') -and $result[$key] -is [Collections.IDictionary]) {
            $merged = @{}; foreach ($nested in $result[$key].Keys) { $merged[$nested] = $result[$key][$nested] }
            foreach ($nested in $entry[$key].Keys) { $merged[$nested] = $entry[$key][$nested] }
            $result[$key] = $merged
        } else { $result[$key] = Copy-DDPresetValue $entry[$key] }
    }
    return $result
}

# CMake refuses a preset whose condition is false; fail here with a preset name instead.
function Assert-DDPresetUsable($Catalog, $Preset, [string]$Name) {
    if (-not $Preset.Contains('condition')) { return }
    if (-not (Test-DDPresetCondition $Preset.condition $Preset $Catalog.root)) {
        Stop-DD "CMake preset $Name is disabled by its condition on $(Get-DDPlatform). Declare a preset that applies to this host in dd.psd1."
    }
}

function Expand-DDPresetMacro([string]$Text, [string]$Root, $Preset) {
    if ($null -eq $Text) { return '' }
    $value = $Text.Replace('${sourceDir}', $Root).Replace('${sourceParentDir}', (Split-Path $Root)).Replace('${sourceDirName}', (Split-Path $Root -Leaf)).Replace('${presetName}', [string]$Preset.name).Replace('${hostSystemName}', $(if ($IsWindows) { 'Windows' } else { 'Linux' })).Replace('${pathListSep}', [string][IO.Path]::PathSeparator)
    function Expand-EnvironmentValue([string]$Name, [string[]]$Stack) {
        if ($Name -in $Stack) { Stop-DD 'Preset environment macro cycle.' }
        if ($Preset.environment -and $Preset.environment.Contains($Name)) {
            $text = [string]$Preset.environment[$Name]
            $text = [regex]::Replace($text, '\$penv\{([^}]+)\}', { param($match) [string][Environment]::GetEnvironmentVariable($match.Groups[1].Value) })
            return [regex]::Replace($text, '\$env\{([^}]+)\}', { param($match) Expand-EnvironmentValue $match.Groups[1].Value ($Stack + $Name) })
        }
        return [string][Environment]::GetEnvironmentVariable($Name)
    }
    $value = [regex]::Replace($value, '\$penv\{([^}]+)\}', { param($match) [string][Environment]::GetEnvironmentVariable($match.Groups[1].Value) })
    $value = [regex]::Replace($value, '\$env\{([^}]+)\}', { param($match) Expand-EnvironmentValue $match.Groups[1].Value @() })
    return $value.Replace('${dollar}', '$')
}

function Test-DDPresetCondition($Condition, $Preset, [string]$Root, [int]$Depth = 0) {
    if ($Depth -gt 16) { Stop-DD 'CMake preset condition nests too deeply.' }
    if ($Condition -is [bool]) { return $Condition }
    if ($Condition -isnot [Collections.IDictionary] -or $Condition.type -isnot [string]) { Stop-DD 'Unsupported CMake preset condition.' }
    $expand = { param($text) Expand-DDPresetMacro ([string]$text) $Root $Preset }
    switch ($Condition.type) {
        'const' { return [bool]$Condition.value }
        'equals' { return (& $expand $Condition.lhs) -ceq (& $expand $Condition.rhs) }
        'notEquals' { return (& $expand $Condition.lhs) -cne (& $expand $Condition.rhs) }
        'inList' { return @($Condition.list | ForEach-Object { & $expand $_ }) -ccontains (& $expand $Condition.string) }
        'notInList' { return @($Condition.list | ForEach-Object { & $expand $_ }) -cnotcontains (& $expand $Condition.string) }
        'matches' { return (& $expand $Condition.string) -cmatch (& $expand $Condition.regex) }
        'notMatches' { return (& $expand $Condition.string) -cnotmatch (& $expand $Condition.regex) }
        'anyOf' { return [bool]@($Condition.conditions | Where-Object { Test-DDPresetCondition $_ $Preset $Root ($Depth + 1) }).Count }
        'allOf' { return -not @($Condition.conditions | Where-Object { -not (Test-DDPresetCondition $_ $Preset $Root ($Depth + 1)) }).Count }
        'not' { return -not (Test-DDPresetCondition $Condition.condition $Preset $Root ($Depth + 1)) }
    }
    Stop-DD "Unsupported CMake preset condition type: $($Condition.type)"
}

function Get-DDPresetDirectory([string]$Root, $Preset) {
    if (-not $Preset.binaryDir) { Stop-DD "Preset $($Preset.name) must declare or inherit binaryDir." }
    $value = Expand-DDPresetMacro $Preset.binaryDir $Root $Preset
    if ($value -match '\$\w*\{') { Stop-DD 'Unsupported binaryDir macro; dd will not guess a cleanup path.' }
    if (-not [IO.Path]::IsPathRooted($value)) { $value = Join-Path $Root $value }
    return Get-DDPath $Root ([IO.Path]::GetRelativePath($Root, [IO.Path]::GetFullPath($value)))
}

function Get-DDPresetStatePath([string]$Root, [string]$Config) {
    return Get-DDPath $Root ".dd/state/$(Get-DDPlatform)-$Config.json"
}

function Invoke-DDConfigure([string]$Root, $Mapping, [string]$Config) {
    $catalog = Get-DDPresetCatalog $Root
    $preset = Resolve-DDPreset $catalog 'configurePresets' $Mapping.configure
    Assert-DDPresetUsable $catalog $preset $Mapping.configure
    $directory = Get-DDPresetDirectory $Root $preset
    foreach ($kind in @('build','test')) {
        if (-not $Mapping[$kind]) { continue }
        $other = Resolve-DDPreset $catalog ($kind + 'Presets') $Mapping[$kind]
        if ($other.configurePreset -ne $Mapping.configure) { Stop-DD "$kind preset must reference configure preset $($Mapping.configure)." }
        Assert-DDPresetUsable $catalog $other $Mapping[$kind]
    }
    $query = Get-DDPath $Root ([IO.Path]::GetRelativePath($Root, (Join-Path $directory '.cmake/api/v1/query/client-dd')))
    [IO.Directory]::CreateDirectory($query) | Out-Null
    [IO.File]::WriteAllText((Join-Path $query 'cache-v2'), '')
    $null = Invoke-DDProcess cmake @('--preset', $Mapping.configure) $Root -Log -Progress "$Config configure"
    $reply = Get-DDPath $Root ([IO.Path]::GetRelativePath($Root, (Join-Path $directory '.cmake/api/v1/reply')))
    $index = Get-ChildItem -LiteralPath $reply -Filter 'index-*.json' | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if (-not $index) { Stop-DD 'CMake did not return requested File API metadata.' 1 }
    $data = Get-Content $index.FullName -Raw | ConvertFrom-Json -AsHashtable
    $cacheFile = $data.reply['client-dd']['cache-v2'].jsonFile
    if (-not $cacheFile) { Stop-DD 'CMake did not return cache metadata.' 1 }
    $cache = Get-Content (Get-DDPath $Root ([IO.Path]::GetRelativePath($Root, (Join-Path $reply $cacheFile)))) -Raw | ConvertFrom-Json -AsHashtable
    $values = @{}
    foreach ($entry in $cache.entries) { $values[$entry.name] = $entry.value }
    if ([IO.Path]::GetFullPath($values.CMAKE_HOME_DIRECTORY) -ne $Root -or [IO.Path]::GetFullPath($values.CMAKE_CACHEFILE_DIR) -ne $directory) { Stop-DD 'CMake resolved a different source/build directory; refusing to record it.' 1 }
    $state = @{ root = $Root; directory = $directory; generator = $values.CMAKE_GENERATOR; multiConfig = [bool]$values.CMAKE_CONFIGURATION_TYPES; mapping = $Mapping; files = $catalog.files; manifestHash = (Get-FileHash (Join-Path $Root 'dd.psd1')).Hash }
    $statePath = Get-DDPresetStatePath $Root $Config
    [IO.Directory]::CreateDirectory((Split-Path $statePath)) | Out-Null
    [IO.File]::WriteAllText($statePath, ($state | ConvertTo-Json -Depth 10))
    return $state
}

function Read-DDPresetState([string]$Root, [string]$Config) {
    $path = Get-DDPresetStatePath $Root $Config
    if (-not (Test-Path -LiteralPath $path)) { Stop-DD "No verified $Config build directory. Configure/build it before using clean; no paths were guessed." }
    $state = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json -AsHashtable
    if ($state.root -ne $Root -or $state.manifestHash -ne (Get-FileHash (Join-Path $Root 'dd.psd1')).Hash) { Stop-DD 'Build metadata is stale. Reconfigure before cleanup.' }
    $catalog = Get-DDPresetCatalog $Root
    if ($catalog.files.Count -ne $state.files.Count) { Stop-DD 'Preset metadata is stale.' }
    foreach ($file in $catalog.files.Keys) { if ($state.files[$file] -ne $catalog.files[$file]) { Stop-DD 'Preset metadata is stale. Reconfigure before cleanup.' } }
    $preset = Resolve-DDPreset $catalog 'configurePresets' $state.mapping.configure
    if ((Get-DDPresetDirectory $Root $preset) -ne $state.directory) { Stop-DD 'Preset environment changed the build directory. Reconfigure before cleanup.' }
    $null = Get-DDPath $Root ([IO.Path]::GetRelativePath($Root, $state.directory))
    return $state
}