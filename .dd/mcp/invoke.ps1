#requires -Version 7.4
$ErrorActionPreference = 'Stop'
[string[]]$arguments = [Console]::In.ReadToEnd() | ConvertFrom-Json
& (Join-Path $PSScriptRoot '../../dd.ps1') @arguments
exit $LASTEXITCODE