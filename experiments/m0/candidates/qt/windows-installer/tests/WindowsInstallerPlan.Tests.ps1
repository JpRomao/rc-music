#Requires -Version 7.0
# Compatibility runner; product tests opt in to the historical schema explicitly.
& (Join-Path $PSScriptRoot '../../../../../../packaging/windows/tests/WindowsInstallerPlan.Tests.ps1') -IncludeLegacy @args
exit $LASTEXITCODE
