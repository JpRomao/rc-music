[CmdletBinding()]
param(
    [string] $DestinationRoot,
    [switch] $AllowTestDestination
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion -ne [Version]'7.6.5') {
    throw [PlatformNotSupportedException]::new("M0-E1 requires PowerShell 7.6.5; found $($PSVersionTable.PSVersion).")
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
Import-Module (Join-Path $repositoryRoot 'experiments\m0\harness\M0.Harness.psd1') -Force -ErrorAction Stop
$parameters = @{
    RepositoryRoot = $repositoryRoot
    AllowTestDestination = $AllowTestDestination
}
if (-not [string]::IsNullOrWhiteSpace($DestinationRoot)) {
    $parameters.DestinationRoot = $DestinationRoot
}
New-M0HarnessManifest @parameters
