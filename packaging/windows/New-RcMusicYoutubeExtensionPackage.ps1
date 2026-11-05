#Requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][ValidateNotNullOrEmpty()][string] $OutputDirectory)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = [IO.Path]::GetFullPath($OutputDirectory, $repository)
Import-Module (Join-Path $PSScriptRoot 'RcMusicYoutubeExtensionPackage.psm1') -Force
$plan = New-RcMusicYoutubeExtensionPackagePlan -RepositoryRoot $repository -OutputDirectory $output
Copy-RcMusicYoutubeExtensionPackagePlan -Plan $plan
