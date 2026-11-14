#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'RcMusicRuntimePlan.psm1')

function Get-RcMusicQtNetworkRuntime {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $ManifestPath,
        [Parameter(Mandatory)][string] $DllDirectory,
        [Parameter(Mandatory)][string] $Destination,
        [string] $SchannelPluginDirectory
    )

    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json -AsHashtable
    if ($manifest -isnot [Collections.IDictionary] -or $manifest['schema_version'] -cne 1 -or
        $manifest['qualified_build_receipt_sha256'] -cnotmatch '\A[0-9a-f]{64}\z' -or
        $manifest['sha256'] -cnotmatch '\A[0-9a-f]{64}\z' -or
        ($manifest['size_bytes'] -isnot [int] -and $manifest['size_bytes'] -isnot [long]) -or
        $manifest['size_bytes'] -le 0) {
        throw 'Invalid Qt Network runtime manifest'
    }
    $dll = Join-Path $DllDirectory 'Qt6Network.dll'
    if (-not [IO.File]::Exists($dll)) { throw 'Qt6Network.dll is missing. Prepare the qualified Qt SDK.' }
    $plan = New-RcMusicRuntimePlan -Files @(@{ source = $dll; relative_path = 'Qt6Network.dll' }) `
        -AllowedSourceRoots @($DllDirectory) -Destination $Destination
    $file = $plan.files[0]
    if ($file.size_bytes -ne $manifest['size_bytes'] -or $file.sha256 -cne $manifest['sha256']) {
        throw 'Qt6Network.dll hash or size mismatch. Prepare the qualified Qt SDK.'
    }
    $qualified = [ordered]@{
        source = $file.source
        relative_path = $file.relative_path
        size_bytes = $file.size_bytes
        sha256 = $file.sha256
        qualified_build_receipt_sha256 = $manifest['qualified_build_receipt_sha256']
    }
    if ($SchannelPluginDirectory) {
        $schannel = $manifest['schannel']
        if ($schannel -isnot [Collections.IDictionary] -or $schannel['sha256'] -cnotmatch '\A[0-9a-f]{64}\z' -or
            ($schannel['size_bytes'] -isnot [int] -and $schannel['size_bytes'] -isnot [long]) -or
            $schannel['size_bytes'] -le 0) {
            throw 'Invalid desktop qschannelbackend.dll pin in Qt Network runtime manifest'
        }
        $plugin = Join-Path $SchannelPluginDirectory 'qschannelbackend.dll'
        if (-not [IO.File]::Exists($plugin)) { throw 'qschannelbackend.dll is missing. Prepare the qualified Qt SDK.' }
        $pluginPlan = New-RcMusicRuntimePlan -Files @(@{ source = $plugin; relative_path = 'plugins/tls/qschannelbackend.dll' }) `
            -AllowedSourceRoots @($SchannelPluginDirectory) -Destination $Destination
        $pluginFile = $pluginPlan.files[0]
        if ($pluginFile.size_bytes -ne $schannel['size_bytes'] -or $pluginFile.sha256 -cne $schannel['sha256']) {
            throw 'qschannelbackend.dll hash or size mismatch. Prepare the qualified Qt SDK.'
        }
        $qualified.schannel = $pluginFile
    }
    [pscustomobject]$qualified
}

Export-ModuleMember -Function Get-RcMusicQtNetworkRuntime
