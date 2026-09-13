function Assert-DDDependencyName([string]$Name) {
    if ($Name -cnotmatch '^[a-z][a-z0-9-]*$' -or $Name -match '^(con|prn|aux|nul|com[0-9]|lpt[0-9])$') { Stop-DD 'Dependency names use lowercase letters, digits and hyphens; reserved device names are forbidden.' }
}

function Assert-DDGitUrl([string]$Url) {
    $uri = $null
    if (-not [Uri]::TryCreate($Url, [UriKind]::Absolute, [ref]$uri) -or $uri.Scheme -notin @('https', 'ssh') -or -not $uri.Host) { Stop-DD 'Use an absolute HTTPS or ssh:// repository URL.' }
    if ($uri.Query -or $uri.Fragment -or ($uri.Scheme -eq 'https' -and $uri.UserInfo) -or $uri.UserInfo.Contains(':') -or $Url -match '[\s;"$\\\x00-\x1f]') { Stop-DD 'Repository URLs must not contain credentials, query strings, fragments or CMake list/control characters.' }
}

function Get-DDDependencyOwner([string]$Root) {
    if (-not (Test-Path -LiteralPath (Join-Path $Root 'dd.psd1'))) { return 'dd' }
    $manifest = Read-DDManifest $Root
    if ($manifest.dependencies) { return $manifest.dependencies.owner }
    return 'dd'
}

function Read-DDDependencyManifest([string]$Root) {
    $path = Get-DDPath $Root 'cmake/dd-dependencies.json'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { Stop-DD 'Missing cmake/dd-dependencies.json. Adopt the CMake dependency integration before using dd dep.' 5 }
    try { $manifest = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json -AsHashtable -ErrorAction Stop }
    catch { Stop-DD "Invalid dependency JSON: $($_.Exception.Message)" 5 }
    if ($manifest -isnot [Collections.IDictionary] -or $manifest.schema -ne 1 -or $manifest.dependencies -isnot [Collections.IDictionary]) { Stop-DD 'Expected dependency schema 1 with a dependencies object.' 5 }
    foreach ($key in $manifest.Keys) { if ($key -notin @('schema', 'dependencies')) { Stop-DD "Unknown dependency manifest field: $key" 5 } }
    foreach ($name in $manifest.dependencies.Keys) {
        Assert-DDDependencyName $name
        $dependency = $manifest.dependencies[$name]
        if ($dependency -isnot [Collections.IDictionary]) { Stop-DD "Invalid dependency: $name" 5 }
        foreach ($key in $dependency.Keys) { if ($key -notin @('url', 'commit', 'sha256', 'method')) { Stop-DD "Unknown field on dependency ${name}: $key" 5 } }
        if ($dependency.url -isnot [string]) { Stop-DD "Dependency $name requires a URL." 5 }
        Assert-DDGitUrl $dependency.url
        if ($dependency.Contains('sha256')) {
            if ($dependency.Contains('commit') -or $dependency.sha256 -cnotmatch '^[0-9a-f]{64}$' -or -not $dependency.url.StartsWith('https://')) { Stop-DD 'Archives require HTTPS and a full SHA-256 instead of a Git commit.' 5 }
        } elseif ($dependency.commit -isnot [string] -or $dependency.commit -cnotmatch '^[0-9a-f]{40}$') { Stop-DD "Dependency $name requires a full lowercase Git commit, not a floating ref." 5 }
        if ($dependency.method -notin @('fetchcontent', 'externalproject', 'application')) { Stop-DD "Dependency $name requires method fetchcontent, externalproject or application." 5 }
    }
    return $manifest
}

function Get-DDDependencies([string]$Root) {
    if ((Get-DDDependencyOwner $Root) -eq 'application') { return }
    if (-not (Test-Path -LiteralPath (Join-Path $Root 'cmake/dd-dependencies.json'))) {
        return
    }
    $manifest = Read-DDDependencyManifest $Root
    foreach ($name in $manifest.dependencies.Keys) {
        $dependency = $manifest.dependencies[$name]
        @{ name = $name; url = $dependency.url; recordedCommit = $dependency.commit; sha256 = $dependency.sha256; method = $dependency.method; status = 'declared'; path = 'cmake/dd-dependencies.json' }
    }
}

function Resolve-DDDependencyCommit([string]$Root, [string]$Url, [string]$Ref) {
    if (-not $Ref -or $Ref.StartsWith('-') -or $Ref -match '[\s\x00-\x1f]') { Stop-DD 'Specify a full commit, tag or branch with --ref.' }
    if ($Ref -match '^[0-9a-fA-F]{40}$') { return $Ref.ToLowerInvariant() }
    # Catalog declarations carry no pin, so HEAD resolves the remote default branch and
    # the concrete commit is recorded in the project's own manifest.
    if ($Ref -ceq 'HEAD') {
        $head = Invoke-DDGit $Root @('ls-remote', '--exit-code', '--', $Url, 'HEAD')
        foreach ($line in ($head.stdout -split '\r?\n')) { if ($line -match '^([0-9a-f]{40})\s+HEAD$') { return $Matches[1] } }
        Stop-DD "Remote HEAD not found for $Url" 5
    }
    $patterns = if ($Ref.StartsWith('refs/')) { @($Ref, "$Ref^{}") } else { @("refs/heads/$Ref", "refs/tags/$Ref", "refs/tags/$Ref^{}") }
    $remote = Invoke-DDGit $Root (@('ls-remote', '--exit-code', '--', $Url) + $patterns)
    $refs = @{}
    foreach ($line in ($remote.stdout -split '\r?\n')) { if ($line -match '^([0-9a-f]{40})\s+(refs/\S+)$') { $refs[$Matches[2]] = $Matches[1] } }
    $branch = if ($Ref.StartsWith('refs/')) { $Ref } else { "refs/heads/$Ref" }
    $tag = if ($Ref.StartsWith('refs/')) { $Ref } else { "refs/tags/$Ref" }
    if ($branch -ne $tag -and $refs.ContainsKey($branch) -and $refs.ContainsKey($tag)) { Stop-DD 'Ambiguous branch/tag name; specify refs/heads/... or refs/tags/....' 5 }
    if ($refs.ContainsKey("$tag^{}")) { return $refs["$tag^{}"] }
    if ($refs.ContainsKey($tag)) { return $refs[$tag] }
    if ($refs.ContainsKey($branch)) { return $refs[$branch] }
    Stop-DD "Remote ref not found: $Ref" 5
}

function Write-DDDependencyManifest([string]$Root, $Manifest, [string]$Original) {
    $path = Get-DDPath $Root 'cmake/dd-dependencies.json'
    if ([IO.File]::ReadAllText($path) -cne $Original) { Stop-DD 'Dependency declarations changed during resolution; retry after reviewing the file.' 5 }
    $temporary = Join-Path (Split-Path $path) ('.dd-dependencies-' + [guid]::NewGuid().ToString('N') + '.tmp')
    [IO.File]::WriteAllText($temporary, ($Manifest | ConvertTo-Json -Depth 8) + "`n")
    [IO.File]::Move($temporary, $path, $true)
}

function Install-DDDependency([string]$Root, [string]$Name, [string]$Url, [string]$Ref, [bool]$DryRun, [string]$Method, [string]$Sha256) {
    Assert-DDDependencyName $Name
    $path = Get-DDPath $Root 'cmake/dd-dependencies.json'
    $manifest = Read-DDDependencyManifest $Root
    $original = [IO.File]::ReadAllText($path)
    if ($manifest.dependencies.Contains($Name)) {
        if ($Url -or $Ref -or $Method -or $Sha256) { Stop-DD 'Already declared. Use dep update to change its pin; review JSON to change its integration method.' }
        $entry = $manifest.dependencies[$Name]
        return @{ dependency = $Name; action = 'verify'; status = 'declared'; commit = $entry.commit; sha256 = $entry.sha256; method = $entry.method; changed = $false; fetch = 'CMake configure/build' }
    }
    if (-not $Url) {
        $catalog = Get-Content (Join-Path $script:DDHome 'catalog.json') -Raw | ConvertFrom-Json -AsHashtable
        if (-not $catalog.dependencies.ContainsKey($Name)) { Stop-DD 'Unknown dependency. Use dep list --available or supply --git and --ref.' }
        if ($Ref) { Stop-DD '--ref on a new dependency requires --git.' }
        $Url = $catalog.dependencies[$Name].url
        # The catalog carries no pins; take the remote default branch head now and record it.
        $Ref = 'HEAD'
    }
    if ($Sha256) {
        if ($Ref -or $Sha256 -notmatch '^[0-9a-fA-F]{64}$' -or -not $Url.StartsWith('https://')) { Stop-DD 'Archive declarations require an HTTPS URL and SHA-256, not --ref.' }
    } elseif (-not $Ref -or $Ref.StartsWith('-') -or $Ref -match '[\s\x00-\x1f]') { Stop-DD 'An explicit --ref is required with --git.' }
    Assert-DDGitUrl $Url
    if (-not $Method) { $Method = 'fetchcontent' }
    if ($Method -notin @('fetchcontent', 'externalproject','application')) { Stop-DD '--method must be fetchcontent, externalproject or application.' }
    $plan = @{ dependency = $Name; action = 'declare'; path = 'cmake/dd-dependencies.json'; url = $Url; requestedRef = $Ref; commit = $null; method = $Method; status = 'planned'; changed = $false; fetch = 'CMake configure/build' }
    if ($Sha256) { $plan.sha256 = $Sha256.ToLowerInvariant() }
    if ($DryRun) { return $plan }
    $commit = $null
    if ($Sha256) { $manifest.dependencies[$Name] = [ordered]@{ url = $Url; sha256 = $Sha256.ToLowerInvariant(); method = $Method }; $plan.sha256 = $Sha256.ToLowerInvariant() }
    else { $commit = Resolve-DDDependencyCommit $Root $Url $Ref; $manifest.dependencies[$Name] = [ordered]@{ url = $Url; commit = $commit; method = $Method } }
    Write-DDDependencyManifest $Root $manifest $original
    $plan.commit = $commit
    $plan.status = 'declared'
    $plan.changed = $true
    return $plan
}

function Update-DDDependency([string]$Root, [string]$Name, [string]$Ref, [bool]$DryRun, [string]$Url, [string]$Sha256) {
    Assert-DDDependencyName $Name
    $manifest = Read-DDDependencyManifest $Root
    $path = Get-DDPath $Root 'cmake/dd-dependencies.json'
    $original = [IO.File]::ReadAllText($path)
    if (-not $manifest.dependencies.Contains($Name)) { Stop-DD 'Dependency is not declared.' }
    $entry = $manifest.dependencies[$Name]
    if ($entry.sha256) {
        if ($Ref -or -not $Url -or $Sha256 -notmatch '^[0-9a-fA-F]{64}$' -or -not $Url.StartsWith('https://')) { Stop-DD 'Archive updates require --url and --sha256 together.' }
        Assert-DDGitUrl $Url
        $plan = @{ action = 'update'; dependency = $Name; previousSha256 = $entry.sha256; sha256 = $Sha256.ToLowerInvariant(); url = $Url; changed = $false }
        if (-not $DryRun -and ($entry.sha256 -ne $plan.sha256 -or $entry.url -ne $Url)) {
            $entry.sha256 = $plan.sha256; $entry.url = $Url; Write-DDDependencyManifest $Root $manifest $original; $plan.changed = $true
        }
        return $plan
    }
    if ($Url -or $Sha256 -or -not $Ref -or $Ref.StartsWith('-') -or $Ref -match '[\s\x00-\x1f]') { Stop-DD 'Git updates require --ref only.' }
    $plan = @{ action = 'update'; dependency = $Name; previousCommit = $entry.commit; requestedRef = $Ref; commit = $null; status = 'planned'; changed = $false; path = 'cmake/dd-dependencies.json' }
    if ($DryRun) { return $plan }
    $commit = Resolve-DDDependencyCommit $Root $entry.url $Ref
    if ($commit -ne $entry.commit) {
        $entry.commit = $commit
        Write-DDDependencyManifest $Root $manifest $original
        $plan.changed = $true
    }
    $plan.commit = $commit
    $plan.status = 'declared'
    return $plan
}

function Assert-DDDependencies([string]$Root) {
    if ((Get-DDDependencyOwner $Root) -eq 'application') { return }
    $null = Read-DDDependencyManifest $Root
}

function Assert-DDCachedSources([string]$Root, [string]$BuildDirectory) {
    $cache = Join-Path $BuildDirectory '_deps'
    if (-not (Test-Path -LiteralPath $cache)) { return }
    foreach ($source in Get-ChildItem -LiteralPath $cache -Directory -Filter '*-src') {
        $null = Get-DDPath $Root ([IO.Path]::GetRelativePath($Root, $source.FullName))
        if ($source.Name -match '-[0-9a-f]{64}-src$') {
            $null = Invoke-DDProcess cmake @("-DDD_ARCHIVE_SOURCE=$($source.FullName)", '-P', (Join-Path $script:DDHome 'dependencies.cmake')) $Root
            continue
        }
        if ($source.Name -notmatch '-([0-9a-f]{40})-src$') { Stop-DD 'Unrecognized dependency cache; inspect it before cleanup.' 5 }
        $commit = $Matches[1]
        if (-not (Test-Path -LiteralPath (Join-Path $source.FullName '.git'))) { Stop-DD 'Incomplete dependency cache; inspect it before cleanup.' 5 }
        $status = Invoke-DDGit $source.FullName @('status', '--porcelain', '--untracked-files=all')
        $head = (Invoke-DDGit $source.FullName @('rev-parse', 'HEAD')).stdout.Trim()
        $branch = Invoke-DDGit $source.FullName @('symbolic-ref', '-q', 'HEAD') -AllowFailure
        if ($status.stdout -or $head -ne $commit -or $branch.exitCode -eq 0) { Stop-DD "Preserving local dependency work: $($source.FullName)" 5 }
    }
}

function Invoke-DDDependencies([string]$Root, $Options) {
    Assert-DDOptions $Options @('git', 'url', 'sha256', 'ref', 'method', 'available', 'dry-run') 3
    $operation = if ($Options.words.Count -gt 1) { $Options.words[1] } else { 'list' }
    $name = if ($Options.words.Count -gt 2) { $Options.words[2] } else { $null }
    if ($operation -notin @('list', 'install', 'update')) { Stop-DD 'Use dep list, install or update.' }
    if ($Options.available) {
        if ($operation -ne 'list' -or $name -or $Options.git -or $Options.url -or $Options.sha256 -or $Options.ref -or $Options.method -or $Options['dry-run']) { Stop-DD '--available applies only to dep list.' }
        return Get-Content (Join-Path $script:DDHome 'catalog.json') -Raw | ConvertFrom-Json -AsHashtable
    }
    try {
        if ((Get-DDDependencyOwner $Root) -eq 'application') {
            if ($name -or $Options.git -or $Options.url -or $Options.sha256 -or $Options.ref -or $Options.method -or $operation -eq 'update') { Stop-DD 'Dependencies are managed by application CMake; dd cannot mutate their pins.' 2 }
            return @{ owner = 'application'; status = 'managed by application CMake'; inventoryKnown = $false; changed = $false }
        }
        $null = Read-DDDependencyManifest $Root
        if ($operation -eq 'list') {
            if ($name -or $Options.git -or $Options.url -or $Options.sha256 -or $Options.ref -or $Options.method -or $Options['dry-run']) { Stop-DD 'dep list takes no dependency name or mutation options.' }
            return @{ dependencies = @(Get-DDDependencies $Root) }
        }
        if ($operation -eq 'update') {
            if (-not $name -or $Options.git -or $Options.method) { Stop-DD 'Use dep update NAME --ref REF.' }
            return Update-DDDependency $Root $name $Options.ref $Options['dry-run'] $Options.url $Options.sha256
        }
        if ($Options.url -and ($Options.git -or -not $Options.sha256)) { Stop-DD '--url requires --sha256 and cannot be combined with --git.' }
        if ($Options.sha256 -and -not $Options.url) { Stop-DD '--sha256 requires --url.' }
        if ($name) { return Install-DDDependency $Root $name $(if ($Options.url) { $Options.url } else { $Options.git }) $Options.ref $Options['dry-run'] $Options.method $Options.sha256 }
        if ($Options.git -or $Options.url -or $Options.sha256 -or $Options.ref -or $Options.method) { Stop-DD 'Dependency options require a dependency name.' }
        return @{ dependencies = @(Get-DDDependencies $Root); status = 'validated'; changed = $false; fetch = 'CMake configure/build' }
    }
    catch {
        if ($_.Exception.Data['exitCode'] -eq 1) { $_.Exception.Data['exitCode'] = 5 }
        throw
    }
}