#Requires -Version 7.0
# ADR-0020: signs the YouTube runtime inventory so the player can trust a new yt-dlp
# runtime without being rebuilt. The private key never leaves this machine's user
# account (DPAPI) and is never written into the repository or the package.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:Purpose = 'rcmusic-youtube-runtime-v1'
$script:Entropy = [Text.Encoding]::ASCII.GetBytes('rcmusic-youtube-runtime-signing-v1')
$script:UniqueRoles = [ordered]@{
    'extractor/yt-dlp.exe' = 'Extractor'
    'javascript/node.exe' = 'Node'
    'extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js' = 'EjsCore'
    'extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js' = 'EjsLibrary'
}
$script:MaxFiles = 256
$script:MaxFileBytes = 128L * 1024 * 1024
$script:MaxTotalBytes = 512L * 1024 * 1024

function Read-SigningKey([string] $KeyPath) {
    if (-not (Test-Path -LiteralPath $KeyPath -PathType Leaf)) { throw "Signing key not found: $KeyPath" }
    $protected = [IO.File]::ReadAllBytes($KeyPath)
    $pkcs8 = [Security.Cryptography.ProtectedData]::Unprotect($protected, $script:Entropy, 'CurrentUser')
    try {
        $key = [Security.Cryptography.ECDsa]::Create()
        $read = 0
        $key.ImportPkcs8PrivateKey($pkcs8, [ref] $read)
        if ($key.KeySize -ne 256) { throw 'Signing key is not P-256' }
        return $key
    } finally { [Array]::Clear($pkcs8) }
}

function ConvertTo-PublicKeyHex([Security.Cryptography.ECDsa] $Key) {
    $q = $Key.ExportParameters($false).Q
    return ([Convert]::ToHexString($q.X) + [Convert]::ToHexString($q.Y)).ToLowerInvariant()
}

function New-RcMusicRuntimeSigningKey {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $KeyPath)
    $full = [IO.Path]::GetFullPath($KeyPath)
    if (Test-Path -LiteralPath $full) { throw "Refusing to overwrite an existing signing key: $full" }
    $key = [Security.Cryptography.ECDsa]::Create([Security.Cryptography.ECCurve+NamedCurves]::nistP256)
    try {
        $pkcs8 = $key.ExportPkcs8PrivateKey()
        try {
            $protected = [Security.Cryptography.ProtectedData]::Protect($pkcs8, $script:Entropy, 'CurrentUser')
        } finally { [Array]::Clear($pkcs8) }
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($full)) | Out-Null
        [IO.File]::WriteAllBytes($full, $protected)
        return ConvertTo-PublicKeyHex $key
    } finally { $key.Dispose() }
}

function Get-RcMusicRuntimePublicKey {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $KeyPath)
    $key = Read-SigningKey $KeyPath
    try { return ConvertTo-PublicKeyHex $key } finally { $key.Dispose() }
}

function Get-RuntimeInventory([string] $Root) {
    $rootItem = Get-Item -LiteralPath $Root -Force
    if (-not $rootItem.PSIsContainer -or $rootItem.LinkType) { throw "Runtime root must be a plain directory: $Root" }
    $prefix = $rootItem.FullName.TrimEnd('\') + '\'
    $rows = [Collections.Generic.List[object]]::new()
    $total = 0L
    foreach ($item in Get-ChildItem -LiteralPath $rootItem.FullName -Recurse -Force) {
        if ($item.LinkType -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Runtime tree contains a link or reparse point: $($item.FullName)"
        }
        if ($item.PSIsContainer) { continue }
        $relative = $item.FullName.Substring($prefix.Length).Replace('\', '/')
        if ($item.Length -gt $script:MaxFileBytes) { throw "Runtime file exceeds 128 MiB: $relative" }
        $total += $item.Length
        $role = if ($script:UniqueRoles.Contains($relative)) { $script:UniqueRoles[$relative] } else { 'Support' }
        $rows.Add([ordered]@{
            role = $role
            path = $relative
            bytes = [long] $item.Length
            sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        })
    }
    if ($rows.Count -eq 0 -or $rows.Count -gt $script:MaxFiles) { throw 'Runtime tree must hold 1 to 256 files' }
    if ($total -gt $script:MaxTotalBytes) { throw 'Runtime tree exceeds 512 MiB' }
    foreach ($path in $script:UniqueRoles.Keys) {
        if (-not ($rows | Where-Object { $_.path -ceq $path })) { throw "Runtime tree is missing $path" }
    }
    return @($rows | Sort-Object -Property { $_.path } -CaseSensitive)
}

# Writes youtube-runtime.json and youtube-runtime.sig next to the runtime tree.
function New-RcMusicSignedRuntimeManifest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RuntimeRoot,
        [Parameter(Mandatory)] [string] $KeyPath,
        [Parameter(Mandatory)] [ValidateRange(1, 9007199254740991)] [long] $Sequence,
        [Parameter(Mandatory)] [ValidatePattern('^[!-~]{1,32}$')] [string] $YtDlpVersion,
        [string] $OutputDirectory,
        [switch] $Replace)
    $root = [IO.Path]::GetFullPath($RuntimeRoot)
    $output = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { [IO.Path]::GetDirectoryName($root.TrimEnd('\')) }
    $manifestPath = Join-Path $output 'youtube-runtime.json'
    $signaturePath = Join-Path $output 'youtube-runtime.sig'
    if (-not $Replace -and ((Test-Path -LiteralPath $manifestPath) -or (Test-Path -LiteralPath $signaturePath))) {
        throw 'A signed manifest already exists; pass -Replace to update it'
    }
    $manifest = [ordered]@{
        schemaVersion = 1
        purpose = $script:Purpose
        sequence = $Sequence
        ytDlpVersion = $YtDlpVersion
        artifacts = Get-RuntimeInventory $root
    }
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($manifest | ConvertTo-Json -Depth 4 -Compress))
    if ($bytes.Length -gt 1MB) { throw 'Manifest exceeds 1 MiB' }
    $key = Read-SigningKey $KeyPath
    try {
        # IEEE P1363 r||s, the format the player's CNG verifier expects.
        $signature = $key.SignData($bytes, [Security.Cryptography.HashAlgorithmName]::SHA256,
            [Security.Cryptography.DSASignatureFormat]::IeeeP1363FixedFieldConcatenation)
        if (-not $key.VerifyData($bytes, $signature, [Security.Cryptography.HashAlgorithmName]::SHA256,
                [Security.Cryptography.DSASignatureFormat]::IeeeP1363FixedFieldConcatenation)) {
            throw 'Fresh signature did not verify'
        }
        $publicKey = ConvertTo-PublicKeyHex $key
    } finally { $key.Dispose() }
    New-Item -ItemType Directory -Force -Path $output | Out-Null
    [IO.File]::WriteAllBytes($manifestPath, $bytes)
    [IO.File]::WriteAllBytes($signaturePath, $signature)
    return [pscustomobject]@{
        Manifest = $manifestPath
        Signature = $signaturePath
        PublicKey = $publicKey
        Sequence = $Sequence
        Files = $manifest.artifacts.Count
    }
}

Export-ModuleMember -Function New-RcMusicRuntimeSigningKey, Get-RcMusicRuntimePublicKey, New-RcMusicSignedRuntimeManifest
