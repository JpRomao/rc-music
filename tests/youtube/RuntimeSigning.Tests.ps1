#Requires -Version 7.0
# Standalone checks for tools/youtube-resolver/RcMusicRuntimeSigning.psm1 (ADR-0020).
# Uses only synthetic files and throwaway keys under out/product.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Import-Module (Join-Path $repository 'tools/youtube-resolver/RcMusicRuntimeSigning.psm1') -Force
$testRoot = Join-Path $repository ('out/product/runtime-signing-' + [guid]::NewGuid().ToString('N'))
$state = @{ Passed = 0; Failed = 0 }
function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Assert-True([bool] $Value, [string] $Message) { if (-not $Value) { throw $Message } }
function Assert-Throws([scriptblock] $Body, [string] $Pattern) {
    try { & $Body | Out-Null } catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw "Wrong error: $($_.Exception.Message)" }
        return
    }
    throw 'Expected an error'
}
function New-Tree([string] $Root, [string[]] $Skip = @()) {
    $files = @('extractor/yt-dlp.exe', 'javascript/node.exe', 'extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js',
        'extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js', 'extractor/_internal/python314.dll', 'notices/LICENSE.txt')
    foreach ($relative in $files | Where-Object { $Skip -notcontains $_ }) {
        $path = Join-Path $Root $relative
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($path)) | Out-Null
        [IO.File]::WriteAllText($path, "synthetic $relative")
    }
}
function Get-Verifier([string] $PublicKeyHex) {
    $bytes = [Convert]::FromHexString($PublicKeyHex)
    $parameters = [Security.Cryptography.ECParameters]::new()
    $parameters.Curve = [Security.Cryptography.ECCurve+NamedCurves]::nistP256
    $point = [Security.Cryptography.ECPoint]::new()
    $point.X = $bytes[0..31]
    $point.Y = $bytes[32..63]
    $parameters.Q = $point
    return [Security.Cryptography.ECDsa]::Create($parameters)
}
function Test-Signature([string] $PublicKeyHex, [byte[]] $Data, [byte[]] $Signature) {
    $verifier = Get-Verifier $PublicKeyHex
    try {
        return $verifier.VerifyData($Data, $Signature, [Security.Cryptography.HashAlgorithmName]::SHA256,
            [Security.Cryptography.DSASignatureFormat]::IeeeP1363FixedFieldConcatenation)
    } finally { $verifier.Dispose() }
}

try {
    $keyPath = Join-Path $testRoot 'keys/runtime.key'
    $publicKey = $null
    Test-Case 'creates a DPAPI-protected P-256 key and reports its public half' {
        $script:publicKey = New-RcMusicRuntimeSigningKey -KeyPath $keyPath
        Assert-True ($script:publicKey -cmatch '^[0-9a-f]{128}$') "Bad public key: $script:publicKey"
        Assert-True ((Get-RcMusicRuntimePublicKey -KeyPath $keyPath) -ceq $script:publicKey) 'Public key changed'
        $raw = [Security.Cryptography.ECDsa]::Create()
        $read = 0
        Assert-Throws { $raw.ImportPkcs8PrivateKey([IO.File]::ReadAllBytes($keyPath), [ref] $read) } '.'
    }
    Test-Case 'never overwrites an existing key' {
        Assert-Throws { New-RcMusicRuntimeSigningKey -KeyPath $keyPath } 'overwrite'
    }
    Test-Case 'signs the exact runtime inventory with fixed roles and sorted paths' {
        $runtime = Join-Path $testRoot 'one/download-tools/youtube'
        New-Tree $runtime
        $result = New-RcMusicSignedRuntimeManifest -RuntimeRoot $runtime -KeyPath $keyPath -Sequence 7 -YtDlpVersion '2026.11.01'
        Assert-True ($result.Manifest -ceq (Join-Path $testRoot 'one/download-tools/youtube-runtime.json')) $result.Manifest
        $bytes = [IO.File]::ReadAllBytes($result.Manifest)
        $signature = [IO.File]::ReadAllBytes($result.Signature)
        Assert-True ($signature.Length -eq 64) 'Signature must be 64 bytes'
        Assert-True (Test-Signature $script:publicKey $bytes $signature) 'Signature does not verify'
        $manifest = [Text.Encoding]::UTF8.GetString($bytes) | ConvertFrom-Json
        Assert-True ($manifest.schemaVersion -eq 1 -and $manifest.purpose -ceq 'rcmusic-youtube-runtime-v1') 'Header'
        Assert-True ($manifest.sequence -eq 7 -and $manifest.ytDlpVersion -ceq '2026.11.01') 'Sequence/version'
        Assert-True ($manifest.artifacts.Count -eq 6) 'Artifact count'
        $paths = @($manifest.artifacts | ForEach-Object path)
        Assert-True ((@($paths | Sort-Object -CaseSensitive) -join '|') -ceq ($paths -join '|')) 'Paths not sorted'
        $extractor = $manifest.artifacts | Where-Object path -ceq 'extractor/yt-dlp.exe'
        Assert-True ($extractor.role -ceq 'Extractor') 'Extractor role'
        $expected = (Get-FileHash (Join-Path $runtime 'extractor/yt-dlp.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
        Assert-True ($extractor.sha256 -ceq $expected) 'Extractor hash'
        Assert-True (($manifest.artifacts | Where-Object path -ceq 'notices/LICENSE.txt').role -ceq 'Support') 'Support role'
        $tampered = [byte[]] $bytes.Clone()
        $tampered[20] = $tampered[20] -bxor 1
        Assert-True (-not (Test-Signature $script:publicKey $tampered $signature)) 'Tampered bytes verified'
    }
    Test-Case 'refuses to replace a signed manifest unless asked' {
        $runtime = Join-Path $testRoot 'one/download-tools/youtube'
        Assert-Throws { New-RcMusicSignedRuntimeManifest -RuntimeRoot $runtime -KeyPath $keyPath -Sequence 8 -YtDlpVersion 'x' } 'Replace'
        $result = New-RcMusicSignedRuntimeManifest -RuntimeRoot $runtime -KeyPath $keyPath -Sequence 8 -YtDlpVersion 'x' -Replace
        Assert-True ($result.Sequence -eq 8) 'Replace did not update'
    }
    Test-Case 'refuses an incomplete runtime' {
        $runtime = Join-Path $testRoot 'two/youtube'
        New-Tree $runtime -Skip @('javascript/node.exe')
        Assert-Throws { New-RcMusicSignedRuntimeManifest -RuntimeRoot $runtime -KeyPath $keyPath -Sequence 1 -YtDlpVersion 'x' } 'missing'
    }
    Test-Case 'refuses links inside the runtime tree' {
        $runtime = Join-Path $testRoot 'three/youtube'
        New-Tree $runtime
        $target = Join-Path $testRoot 'outside'
        New-Item -ItemType Directory -Force -Path $target | Out-Null
        New-Item -ItemType Junction -Path (Join-Path $runtime 'extractor/linked') -Target $target | Out-Null
        Assert-Throws { New-RcMusicSignedRuntimeManifest -RuntimeRoot $runtime -KeyPath $keyPath -Sequence 1 -YtDlpVersion 'x' } 'link'
    }
    Test-Case 'refuses invalid versions and sequences before signing' {
        $runtime = Join-Path $testRoot 'one/download-tools/youtube'
        Assert-Throws { New-RcMusicSignedRuntimeManifest -RuntimeRoot $runtime -KeyPath $keyPath -Sequence 0 -YtDlpVersion 'x' -Replace } '.'
        Assert-Throws { New-RcMusicSignedRuntimeManifest -RuntimeRoot $runtime -KeyPath $keyPath -Sequence 1 -YtDlpVersion 'with space' -Replace } '.'
    }
} finally {
    if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}
Write-Host "Passed: $($state.Passed) Failed: $($state.Failed)"
if ($state.Failed -gt 0) { exit 1 }
