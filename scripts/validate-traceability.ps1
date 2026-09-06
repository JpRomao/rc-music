#Requires -Version 7.0
[CmdletBinding()]
param(
    [string] $RepositoryRoot = (Join-Path $PSScriptRoot '..'),
    [switch] $Json
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'Traceability.psm1') -Force
try {
    $report = Get-RcMusicTraceabilityReport -RepositoryRoot $RepositoryRoot
    if ($Json) { $report | ConvertTo-Json -Depth 12 }
    else {
        $counts = @($report.statusCounts.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join '; '
        Write-Host "CHK-TRACE: $($report.total) requisitos com vínculos válidos. $counts."
        Write-Host 'Vínculos válidos não comprovam execução atual dos testes nem aprovação de release.'
    }
} catch {
    Write-Error "CHK-TRACE: $($_.Exception.Message)" -ErrorAction Continue
    exit 1
}
