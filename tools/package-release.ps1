#requires -Version 7.4
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9A-Fa-f]{40}$')]
    [string]$CertificateThumbprint,
    [Parameter(Mandatory)]
    [ValidatePattern('^\d+\.\d+\.\d+\.\d+$')]
    [string]$CollectionVersion,
    [string]$OutputDirectory,
    [ValidateRange(1, 64)]
    [int]$Jobs = 6
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$root = Split-Path $PSScriptRoot
$versions = 10, 20, 22, 23, 30
$architectures = 'x86', 'x64'
$timestamp = 'http://timestamp.sectigo.com/?td=sha256'
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $root "out\release-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
}
$release = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
if (Test-Path -LiteralPath $release) { throw "Release output already exists: $release" }

$certificate = Get-Item -LiteralPath "Cert:\CurrentUser\My\$CertificateThumbprint"
if (-not $certificate.HasPrivateKey -or $certificate.NotAfter -le (Get-Date) -or
    $certificate.NotBefore -gt (Get-Date) -or $certificate.Subject -eq $certificate.Issuer) {
    throw 'Select a currently valid, CA-issued publisher certificate with a private key.'
}
$nsis = Join-Path ${env:ProgramFiles(x86)} 'NSIS\makensis.exe'
if (-not (Test-Path -LiteralPath $nsis -PathType Leaf)) { throw 'Install NSIS 3.08 or later.' }
$sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
$signTool = Get-ChildItem -LiteralPath $sdk -Directory | Where-Object Name -Match '^\d+\.\d+\.\d+\.\d+$' |
    Sort-Object { [version]$_.Name } -Descending | ForEach-Object { Join-Path $_.FullName 'x64\signtool.exe' } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $signTool) { throw 'SignTool is required from the Windows SDK.' }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -version '[18.0,19.0)' -requires `
    Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.ATL -property installationPath
if (-not $vs) { throw 'Visual Studio 2026 with x86/x64 C++ and ATL is required.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
$payload = Join-Path $release 'payload'
$buildOutput = Join-Path $root 'out\shipping'
$shared = Join-Path $payload 'shared'
$licenses = Join-Path $shared 'Licenses'
$null = New-Item -ItemType Directory -Path $release, $payload, $shared, $licenses

function Sign-Files([string[]]$Files, [string]$Description) {
    & $signTool sign /fd SHA256 /sha1 $CertificateThumbprint /d $Description /tr $timestamp /td SHA256 @Files
    foreach ($file in $Files) {
        & $signTool verify /pa /all /tw $file
        $signature = Get-AuthenticodeSignature -LiteralPath $file
        if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -ne $CertificateThumbprint -or
            -not $signature.TimeStamperCertificate) {
            throw "No valid timestamped publisher signature: $file"
        }
    }
}

function Assert-Edition([string]$File, [int]$Edition, [string]$Architecture) {
    $stream = [IO.File]::OpenRead($File)
    try {
        $reader = [IO.BinaryReader]::new($stream)
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Not a Windows executable: $File" }
        $stream.Position = 0x3c
        $stream.Position = $reader.ReadUInt32()
        $machine = if ($Architecture -eq 'x86') { 0x14c } else { 0x8664 }
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne $machine) {
            throw "Not a $Architecture build: $File"
        }
    } finally { $stream.Dispose() }
    $version = '{0}.{1}' -f [math]::Floor($Edition / 10), ($Edition % 10)
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($File)
    if ($info.FileVersion -ne "$version.0.0" -or $info.ProductVersion -ne "$version.0.0" -or
        $info.FileDescription -ne "ImageWalker $version") {
        throw "Unexpected version resource in $File"
    }
}

Push-Location $root
try {
    foreach ($arch in $architectures) {
        $vcArch = if ($arch -eq 'x64') { 'x64' } else { 'x64_x86' }
        $build = "build\shipping-$arch"
        $output = Join-Path $buildOutput $arch
        # cmd.exe must keep vcvarsall and CMake in the same process.
        & $env:ComSpec /d /c ("call `"$vcvars`" $vcArch >nul && cmake --preset $arch-release -B $build " +
            "-DIW_USE_SIBLING_DEPS=OFF `"-DIW_OUTPUT_DIR:PATH=$output`"")
        if (Select-String -LiteralPath "$build\CMakeCache.txt" -Pattern '^FETCHCONTENT_SOURCE_DIR_.*:PATH=.+$' -Quiet) {
            throw "Remove the dependency source override from the $arch shipping cache."
        }
        & $env:ComSpec /d /c "call `"$vcvars`" $vcArch >nul && cmake --build $build --parallel $Jobs"
        ctest --test-dir $build -L headless --output-on-failure --no-tests=error

        $destination = Join-Path $payload $arch
        $null = New-Item -ItemType Directory -Path $destination
        foreach ($edition in $versions) {
            $name = "imagewalker$edition.exe"
            $binary = Join-Path $output $name
            Assert-Edition $binary $edition $arch
            Copy-Item -LiteralPath $binary -Destination $destination
        }
    }

    $deps = Join-Path $root 'build\shipping-x64\_deps'
    $licenseFiles = [ordered]@{
        'ImageWalker-MIT.txt' = 'LICENSE'
        'WTL-MS-PL.txt' = 'include\wtl\MS-PL.txt'
        'ColorButton.txt' = 'include\iw\ColorButton.h'
        'RSA-MD5.txt' = 'src23\UtilMd5.cpp'
        'libdesktop-MPL-2.0.txt' = "$deps\libdesktop-src\LICENSE"
        'libfiles-MPL-2.0.txt' = "$deps\libfiles-src\LICENSE"
        'LGPL-2.1.txt' = "$deps\libfiles-src\licences\LGPL-2.1.txt"
        'Hunspell-MPL-1.1.txt' = "$deps\hunspell-src\COPYING.MPL"
        'Hunspell-notice.txt' = "$deps\hunspell-src\license.hunspell"
        'MySpell.txt' = "$deps\hunspell-src\license.myspell"
        'GIFLIB.txt' = "$deps\giflib-src\COPYING"
        'libjpeg-turbo.txt' = "$deps\libjpeg_turbo-src\LICENSE.md"
        'IJG.txt' = "$deps\libjpeg_turbo-src\README.ijg"
        'libpng.txt' = "$deps\libpng-src\LICENSE"
        'libtiff.txt' = "$deps\libtiff-src\LICENSE.md"
        'libwebp.txt' = "$deps\libwebp-src\COPYING"
        'libwebp-patents.txt' = "$deps\libwebp-src\PATENTS"
        'RapidJSON.txt' = "$deps\rapidjson-src\license.txt"
        'zlib-ng.txt' = "$deps\zlib_ng-src\LICENSE.md"
    }
    foreach ($name in $licenseFiles.Keys) {
        Copy-Item -LiteralPath $licenseFiles[$name] -Destination (Join-Path $licenses $name)
    }
    $licenseFiles.Keys | ForEach-Object { 'Delete "$INSTDIR\Licenses\{0}"' -f $_ } |
        Set-Content -LiteralPath (Join-Path $payload 'ImageWalkerLicenses.nsh') -Encoding ascii
    Copy-Item -LiteralPath 'Setup\Readme.txt', 'exe\imagewalker30-tools.json' -Destination $shared
    $dictionary = Join-Path $shared 'dic'
    $null = New-Item -ItemType Directory -Path $dictionary
    Copy-Item -LiteralPath 'exe\dic\en_GB.aff', 'exe\dic\en_GB.dic', 'exe\dic\en_US.aff', 'exe\dic\en_US.dic' `
        -Destination $dictionary

    $exes = foreach ($arch in $architectures) {
        foreach ($edition in $versions) { Join-Path $payload "$arch\imagewalker$edition.exe" }
    }
    Sign-Files $exes 'ImageWalker'
    & $nsis /WX /INPUTCHARSET UTF8 "/DPAYLOAD_DIR=$payload" "/DRELEASE_DIR=$release" `
        "/DCOLLECTION_VERSION=$CollectionVersion" "/DSIGN_TOOL=$signTool" `
        "/DCERTIFICATE=$CertificateThumbprint" "/DTIMESTAMP_URL=$timestamp" 'Setup\ImageWalker.nsi'
    $installer = Join-Path $release 'imagewalker-setup.exe'
    Sign-Files @($installer) 'ImageWalker Collection Setup'

    $archive = [IO.Compression.ZipFile]::Open((Join-Path $release 'imagewalker-portable.zip'), 'Create')
    try {
        foreach ($arch in $architectures) {
            foreach ($edition in $versions) {
                $name = "imagewalker$edition.exe"
                [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                    $archive, (Join-Path $payload "$arch\$name"), "$arch/$name",
                    [IO.Compression.CompressionLevel]::SmallestSize)
            }
            foreach ($file in Get-ChildItem -LiteralPath $shared -Recurse -File) {
                if ($file.Name -eq 'Readme.txt') { continue }
                $name = [IO.Path]::GetRelativePath($shared, $file.FullName).Replace('\', '/')
                [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                    $archive, $file.FullName, "$arch/$name", [IO.Compression.CompressionLevel]::SmallestSize)
            }
        }
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, (Join-Path $shared 'Readme.txt'), 'Readme.txt', [IO.Compression.CompressionLevel]::SmallestSize)
    } finally { $archive.Dispose() }

    $files = [ordered]@{}
    $downloads = 'imagewalker-setup.exe', 'imagewalker-portable.zip'
    foreach ($name in $downloads) {
        $file = Get-Item -LiteralPath (Join-Path $release $name)
        if ($file.Length -gt 32000000) { throw "$name exceeds App Engine's 32 MB static-file limit." }
        $sha256 = (Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant()
        $files[$name] = @{ bytes = $file.Length; sha256 = $sha256 }
        "$sha256  $name"
    }
    @{ version = $CollectionVersion; files = $files } | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath (Join-Path $release 'imagewalker-release.json') -Encoding utf8
    Write-Host "Release packages: $release"
} finally {
    Pop-Location
}
