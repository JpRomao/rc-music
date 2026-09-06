#Requires -Version 7.0
# Compatibility entrypoint: only historical callers select the frozen schema v1 baseline.
Import-Module (Join-Path $PSScriptRoot '../../../../../packaging/windows/RcMusicInstallerPlan.psm1') -Force -ErrorAction Stop

function New-RcMusicInstallerPlan {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string] $RuntimeDirectory,
        [Parameter(Mandatory)][string] $OutputDirectory, [string] $FormatMatrix)
    RcMusicInstallerPlan\New-RcMusicInstallerPlan -RuntimeDirectory $RuntimeDirectory -OutputDirectory $OutputDirectory `
        -FormatMatrix $FormatMatrix -LegacyBaseline
}

Export-ModuleMember -Function New-RcMusicInstallerPlan, Get-RcMusicInstallerRecipeHashes, Get-RcMusicInstallerIdentity, ConvertTo-RcMusicNsisLiteral
