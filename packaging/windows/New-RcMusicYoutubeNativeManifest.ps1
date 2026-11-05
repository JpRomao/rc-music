#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $BuildReceipt,
    [Parameter(Mandatory)][string] $BuildReceiptSha256,
    [Parameter(Mandatory)][string] $ExtensionId,
    [Parameter(Mandatory)][string] $HostExecutable,
    [Parameter(Mandatory)][string] $OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = [IO.Path]::GetFullPath($OutputDirectory, $repository)
Import-Module (Join-Path $PSScriptRoot 'RcMusicYoutubeNativeManifest.psm1') -Force
$plan = New-RcMusicYoutubeNativeManifestPlan -RepositoryRoot $repository -BuildReceipt $BuildReceipt `
    -BuildReceiptSha256 $BuildReceiptSha256 -ExtensionId $ExtensionId -HostExecutable $HostExecutable -OutputDirectory $output
Copy-RcMusicYoutubeNativeManifestPlan -Plan $plan
