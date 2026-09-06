#Requires -Version 7.0
# Compatibility entrypoint only; keep implementation and provenance in the product tree.
Import-Module (Join-Path $PSScriptRoot '../../../../../packaging/windows/RcMusicRuntimePlan.psm1') -Force -ErrorAction Stop
Export-ModuleMember -Function New-RcMusicRuntimePlan, Copy-RcMusicRuntimePlan, Get-RcMusicSourceSnapshot, Get-RcMusicDesktopBuildConfiguration
