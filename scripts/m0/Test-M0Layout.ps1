[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$modulePath = Join-Path $repositoryRoot 'experiments\m0\harness\M0.Harness.psd1'
Import-Module $modulePath -Force -ErrorAction Stop

Assert-M0Layout -RepositoryRoot $repositoryRoot
Write-Host 'OK: M0 layout is complete and isolated.'
