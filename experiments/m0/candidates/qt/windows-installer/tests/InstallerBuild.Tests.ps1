#Requires -Version 7.0
[CmdletBinding()]
param(
    [string] $RuntimeDirectory,
    [string] $OutputDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
if (-not $RuntimeDirectory) {
    $RuntimeDirectory = Join-Path $repository 'out/m0/rc-music-local/rc-music-windows-dev-717c88a/app'
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repository ('out/m0/rc-music-installer/' + [Guid]::NewGuid().ToString('N'))
}
$result = & (Join-Path $PSScriptRoot '../New-RcMusicInstaller.ps1') -RuntimeDirectory $RuntimeDirectory -OutputDirectory $OutputDirectory
$receipt = Get-Content -LiteralPath $result.receipt -Raw | ConvertFrom-Json
if ($receipt.distribution -cne 'private-local-development-only' -or $receipt.associations_enabled -ne $false) {
    throw 'Installer incorrectly claims public distribution or approved associations'
}
if (-not [IO.File]::Exists($result.installer) -or (Get-Item -LiteralPath $result.installer).Length -le 0) {
    throw 'No compiled installer artifact'
}
if ($receipt.installer_sha256 -cne (Get-FileHash -LiteralPath $result.installer -Algorithm SHA256).Hash.ToLowerInvariant()) {
    throw 'Installer receipt does not match its compiled bytes'
}
if ($receipt.payload_id -cnotmatch '^[0-9a-f]{64}$' -or $receipt.files.Count -lt 10) {
    throw 'Missing real runtime inventory'
}
Write-Host '[PASS] Real private runtime compiled into a per-user NSIS installer, with hashes and no inferred format registration'
$result
