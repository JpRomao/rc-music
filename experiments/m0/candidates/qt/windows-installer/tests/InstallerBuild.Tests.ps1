#Requires -Version 7.0
[CmdletBinding()]
param(
    [string] $RuntimeDirectory,
    [string] $OutputDirectory,
    [string] $FormatMatrix
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
$expectedFormats = @()
$matrixPath = $null
$matrixHash = $null
if ($FormatMatrix) {
    $matrixPath = (Resolve-Path -LiteralPath $FormatMatrix).ProviderPath
    $matrixHash = (Get-FileHash -LiteralPath $matrixPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $matrix = Get-Content -LiteralPath $matrixPath -Raw | ConvertFrom-Json
    $expectedFormats = @($matrix.formats | Sort-Object -Property extension -CaseSensitive | ForEach-Object {
        [ordered]@{ extension = $_.extension; description = $_.description }
    })
}
$expectedFormatsJson = ConvertTo-Json -InputObject $expectedFormats -Compress
$expectedSignature = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData(
        [Text.Encoding]::UTF8.GetBytes($expectedFormatsJson))).ToLowerInvariant()
$result = & (Join-Path $PSScriptRoot '../New-RcMusicInstaller.ps1') -RuntimeDirectory $RuntimeDirectory -OutputDirectory $OutputDirectory -FormatMatrix $FormatMatrix
$receipt = Get-Content -LiteralPath $result.receipt -Raw | ConvertFrom-Json
if ($receipt.distribution -cne 'private-local-development-only' -or $receipt.associations_enabled -ne [bool] $FormatMatrix) {
    throw 'Installer claims public distribution or enables associations without the explicit matrix'
}
if ($receipt.format_signature -cne $expectedSignature -or
    (ConvertTo-Json -InputObject @($receipt.formats | ForEach-Object {
                [ordered]@{ extension = $_.extension; description = $_.description }
            }) -Compress) -cne $expectedFormatsJson) {
    throw 'Receipt format signature or explicit list does not match the selected matrix'
}
if ($FormatMatrix) {
    if ($receipt.format_matrix.path -cne $matrixPath -or $receipt.format_matrix.sha256 -cne $matrixHash -or
        (Get-FileHash -LiteralPath $matrixPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $matrixHash -or
        ($receipt.format_matrix.validation | ConvertTo-Json -Depth 8 -Compress) -cne ($matrix | ConvertTo-Json -Depth 8 -Compress) -or
        (ConvertTo-Json -InputObject @($receipt.format_matrix.formats) -Depth 8 -Compress) -cne
        (ConvertTo-Json -InputObject @($receipt.formats) -Depth 8 -Compress)) {
        throw 'Receipt lost or changed the exact format-matrix provenance'
    }
} elseif ($null -ne $receipt.format_matrix -or @($receipt.formats).Count -ne 0) {
    throw 'Default OFF build must not infer a matrix or formats'
}
$configPath = Join-Path ([IO.Path]::GetDirectoryName($result.receipt)) 'installer-config.nsh'
$config = Get-Content -LiteralPath $configPath -Raw
if ($receipt.config_sha256 -cne (Get-FileHash -LiteralPath $configPath -Algorithm SHA256).Hash.ToLowerInvariant() -or
    $config -cnotmatch ('(?m)^!define RCMUSIC_ASSOCIATIONS_ENABLED ' + [int] [bool] $FormatMatrix + '\r?$') -or
    $config -cnotmatch ('(?m)^!define RCMUSIC_FORMAT_SIGNATURE "' + $expectedSignature + '"\r?$')) {
    throw 'Compiled configuration does not match the receipt association flag/signature'
}
$formatBlock = [regex]::Match($config, '(?ms)^!macro RCMUSIC_FOR_EACH_FORMAT CALLBACK\r?\n(?<formats>.*?)^!macroend\r?$')
$configuredFormats = @($formatBlock.Groups['formats'].Value -split '\r?\n' |
        ForEach-Object { $_.Trim() } | Where-Object { $_ }) -join "`n"
$expectedConfiguredFormats = @($expectedFormats | ForEach-Object {
        '!insertmacro ${CALLBACK} "' + $_.extension + '" "' + $_.description + '"'
    }) -join "`n"
if (-not $formatBlock.Success -or $configuredFormats -cne $expectedConfiguredFormats) {
    throw 'Compiled configuration contains missing or inferred format callbacks'
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
Write-Host ('[PASS] Real private runtime compiled with verified hashes; associations enabled only by explicit matrix: ' + [bool] $FormatMatrix)
$result
