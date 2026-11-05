#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('Main', 'Youtube')][string] $Channel,
    [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string] $OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = [IO.Path]::GetFullPath($OutputDirectory, $repository)
Import-Module (Join-Path $PSScriptRoot 'RcMusicYoutubeExtensionPackage.psm1') -Force
$plan = New-RcMusicChromiumExtensionPackagePlan -RepositoryRoot $repository -OutputDirectory $output -Channel $Channel
Copy-RcMusicChromiumExtensionPackagePlan -Plan $plan
