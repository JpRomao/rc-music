#Requires -Version 7.0
# Historical entrypoint; the maintained suite lives with the product planner.
& (Join-Path $PSScriptRoot '../../../../../../packaging/windows/tests/WindowsRuntimePlan.Tests.ps1') @args
exit $LASTEXITCODE
