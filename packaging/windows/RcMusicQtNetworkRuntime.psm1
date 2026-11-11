#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'RcMusicRuntimePlan.psm1')

function Get-RcMusicQtNetworkRuntime {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $ManifestPath,
        [Parameter(Mandatory)][string] $DllDirectory,
        [Parameter(Mandatory)][string] $Destination
    )

    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json -AsHashtable
    if ($manifest -isnot [Collections.IDictionary] -or $manifest['schema_version'] -cne 1 -or
        $manifest['qualified_build_receipt_sha256'] -cnotmatch '\A[0-9a-f]{64}\z' -or
        $manifest['sha256'] -cnotmatch '\A[0-9a-f]{64}\z' -or
        ($manifest['size_bytes'] -isnot [int] -and $manifest['size_bytes'] -isnot [long]) -or
        $manifest['size_bytes'] -le 0) {
        throw 'Invalid Qt Network runtime manifest'
    }
    $name = 'Qt6Network.dll'
    $dll = Join-Path $DllDirectory $name
    if (-not [IO.File]::Exists($dll)) { throw 'Qt6Network.dll is missing. Prepare the qualified Qt SDK.' }
    $plan = New-RcMusicRuntimePlan -Files @(@{ source = $dll; relative_path = $name }) `
        -AllowedSourceRoots @($DllDirectory) -Destination $Destination
    $file = $plan.files[0]
    if ($file.size_bytes -ne $manifest['size_bytes'] -or $file.sha256 -cne $manifest['sha256']) {
        throw 'Qt6Network.dll hash or size mismatch. Prepare the qualified Qt SDK.'
    }
    [pscustomobject]@{
        source = $file.source
        relative_path = $file.relative_path
        size_bytes = $file.size_bytes
        sha256 = $file.sha256
        qualified_build_receipt_sha256 = $manifest['qualified_build_receipt_sha256']
    }
}

Export-ModuleMember -Function Get-RcMusicQtNetworkRuntime
