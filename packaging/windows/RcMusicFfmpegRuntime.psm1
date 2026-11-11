#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'RcMusicRuntimePlan.psm1')

function Get-RcMusicFfmpegRuntime {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $ManifestPath,
        [Parameter(Mandatory)][string] $DllDirectory,
        [Parameter(Mandatory)][string] $PluginPath,
        [Parameter(Mandatory)][string] $Destination
    )

    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json -AsHashtable
    $names = @('avcodec-61.dll', 'avformat-61.dll', 'avutil-59.dll', 'swresample-5.dll', 'swscale-8.dll')
    $plugin = 'plugins/multimedia/ffmpegmediaplugin.dll'
    if ($manifest -isnot [Collections.IDictionary] -or $manifest['schema_version'] -cne 1 -or
        $manifest['product_version'] -cnotmatch '\A\d+\.\d+\.\d+-rcmusic\d+\z' -or
        $manifest['qualified_build_receipt_sha256'] -cnotmatch '\A[0-9a-f]{64}\z' -or
        $manifest['files'] -isnot [array] -or $manifest['files'].Count -ne $names.Count) {
        throw 'Invalid FFmpeg runtime manifest'
    }
    $expected = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
    foreach ($file in $manifest['files']) {
        if ($file -isnot [Collections.IDictionary] -or $file['name'] -cnotin $names -or
            -not $expected.TryAdd($file['name'], $file)) { throw 'Invalid FFmpeg runtime DLL name or duplicate' }
    }
    $expected.Add($plugin, $manifest['plugin'])
    $mappings = foreach ($relative in $names + $plugin) {
        $pin = $expected[$relative]
        if ($pin -isnot [Collections.IDictionary] -or
            ($pin['size_bytes'] -isnot [int] -and $pin['size_bytes'] -isnot [long]) -or
            $pin['size_bytes'] -le 0 -or $pin['sha256'] -cnotmatch '\A[0-9a-f]{64}\z') {
            throw "Invalid FFmpeg runtime pin: $relative"
        }
        $source = if ($relative -ceq $plugin) { $PluginPath } else { Join-Path $DllDirectory $relative }
        if (-not [IO.File]::Exists($source)) { throw "Missing FFmpeg runtime file: $relative. Prepare the qualified Qt Multimedia SDK." }
        @{ source = $source; relative_path = $relative }
    }
    $plan = New-RcMusicRuntimePlan -Files $mappings `
        -AllowedSourceRoots @($DllDirectory, [IO.Path]::GetDirectoryName($PluginPath)) -Destination $Destination
    foreach ($file in $plan.files) {
        $pin = $expected[$file.relative_path]
        if ($file.size_bytes -ne $pin['size_bytes'] -or $file.sha256 -cne $pin['sha256']) {
            throw "FFmpeg runtime hash or size mismatch: $($file.relative_path). Prepare the qualified Qt Multimedia SDK."
        }
    }
    [pscustomobject]@{
        product_version = $manifest['product_version']
        qualified_build_receipt_sha256 = $manifest['qualified_build_receipt_sha256']
        files = $plan.files
    }
}

Export-ModuleMember -Function Get-RcMusicFfmpegRuntime
