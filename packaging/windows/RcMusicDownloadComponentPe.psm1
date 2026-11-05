#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-ComponentPeFields($Value, [string[]] $Expected, [string] $Context) {
    if ($null -eq $Value -or $Value -is [string] -or $Value -is [Array]) {
        throw "Invalid $Context record."
    }
    $names = if ($Value -is [Collections.IDictionary]) { @($Value.Keys) } else { @($Value.PSObject.Properties.Name) }
    if ($names.Count -ne $Expected.Count) { throw "Invalid $Context fields." }
    foreach ($name in $names) {
        if ($name -isnot [string] -or $Expected -cnotcontains $name) { throw "Unknown $Context field." }
    }
}

function Get-ComponentPeRelativePath($Value) {
    if ($Value -isnot [string] -or $Value.Length -eq 0 -or $Value.Length -gt 4096) {
        throw 'Invalid inventory/report relative path.'
    }
    $relative = $Value.Replace('\', '/')
    foreach ($part in $relative.Split('/')) {
        if ($part.Length -eq 0 -or $part.Length -gt 255 -or $part -in @('.', '..') -or
            $part -cnotmatch '\A[A-Za-z0-9_.+-]+\z' -or $part.EndsWith('.') -or
            $part -match '\A(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') {
            throw 'Invalid inventory/report relative path component.'
        }
    }
    return $relative
}

function Assert-ComponentPeSource($Value) {
    if ($Value -isnot [string] -or $Value -notmatch '\A[A-Za-z]:[\\/]' -or
        $Value -match '[\x00-\x1f\x7f]') {
        throw 'Inventory source must be an explicit absolute local path.'
    }
    foreach ($part in $Value.Replace('\', '/').Substring(3).Split('/')) {
        if ($part.Length -eq 0 -or $part -in @('.', '..') -or $part -match '[<>:"|?*]' -or
            $part -match '[ .]$' -or $part -match '\A(?i:CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³])(?:\.|$)') {
            throw 'Invalid inventory source path.'
        }
    }
}

# Pure validation of an already pinned component inventory and caller-provided
# PE reports. The caller owns inspection, copying and all filesystem access.
function Get-RcMusicDownloadComponentPeClosure {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $Files,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $Reports
    )
    $network = [ordered] @{
        Bootstrap = 'RCMusicDownloadWorker.exe'
        Payload = 'rcmusic-download-network.dll'
        Core = 'Qt6Core.dll'
        Network = 'Qt6Network.dll'
        Msvcp140 = 'msvcp140.dll'
        Msvcp140_1 = 'msvcp140_1.dll'
        Vcruntime140 = 'vcruntime140.dll'
        Vcruntime140_1 = 'vcruntime140_1.dll'
        Plugin = 'qrcmusicschanneloffline.dll'
    }
    $youtube = @{
        Extractor = 'download-tools/youtube/extractor/yt-dlp.exe'
        Node = 'download-tools/youtube/javascript/node.exe'
        EjsCore = 'download-tools/youtube/extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js'
        EjsLibrary = 'download-tools/youtube/extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js'
    }
    # Exact observed imports of the reviewed toolchain. API-set contracts are
    # explicit names, not filesystem probes or prefix-based approvals.
    $windows = @{
        network = @(
            'ADVAPI32.dll', 'AUTHZ.dll', 'bcrypt.dll', 'CRYPT32.dll', 'DNSAPI.dll', 'icuuc.dll',
            'KERNEL32.dll', 'MPR.dll', 'ncrypt.dll', 'NETAPI32.dll', 'ntdll.dll', 'ole32.dll',
            'OLEAUT32.dll', 'Secur32.dll', 'SHELL32.dll', 'USER32.dll', 'USERENV.dll', 'VERSION.dll',
            'WINHTTP.dll', 'WINMM.dll', 'WS2_32.dll', 'api-ms-win-core-synch-l1-2-0.dll',
            'api-ms-win-core-winrt-error-l1-1-1.dll', 'api-ms-win-core-winrt-l1-1-0.dll',
            'api-ms-win-crt-convert-l1-1-0.dll', 'api-ms-win-crt-environment-l1-1-0.dll',
            'api-ms-win-crt-filesystem-l1-1-0.dll', 'api-ms-win-crt-heap-l1-1-0.dll',
            'api-ms-win-crt-locale-l1-1-0.dll', 'api-ms-win-crt-math-l1-1-0.dll',
            'api-ms-win-crt-runtime-l1-1-0.dll', 'api-ms-win-crt-stdio-l1-1-0.dll',
            'api-ms-win-crt-string-l1-1-0.dll', 'api-ms-win-crt-time-l1-1-0.dll',
            'api-ms-win-crt-utility-l1-1-0.dll'
        )
        media = @('KERNEL32.dll', 'ole32.dll')
        extractor = @(
            'ADVAPI32.dll', 'bcrypt.dll', 'CRYPT32.dll', 'KERNEL32.dll', 'ole32.dll', 'OLEAUT32.dll',
            'PROPSYS.dll', 'RPCRT4.dll', 'USER32.dll', 'VERSION.dll', 'WS2_32.dll',
            'api-ms-win-core-path-l1-1-0.dll', 'api-ms-win-crt-conio-l1-1-0.dll',
            'api-ms-win-crt-convert-l1-1-0.dll', 'api-ms-win-crt-environment-l1-1-0.dll',
            'api-ms-win-crt-filesystem-l1-1-0.dll', 'api-ms-win-crt-heap-l1-1-0.dll',
            'api-ms-win-crt-locale-l1-1-0.dll', 'api-ms-win-crt-math-l1-1-0.dll',
            'api-ms-win-crt-process-l1-1-0.dll', 'api-ms-win-crt-runtime-l1-1-0.dll',
            'api-ms-win-crt-stdio-l1-1-0.dll', 'api-ms-win-crt-string-l1-1-0.dll',
            'api-ms-win-crt-time-l1-1-0.dll', 'api-ms-win-crt-utility-l1-1-0.dll'
        )
        node = @('ADVAPI32.dll', 'CRYPT32.dll', 'dbghelp.dll', 'KERNEL32.dll', 'ole32.dll',
            'SHELL32.dll', 'USER32.dll', 'USERENV.dll', 'WINMM.dll', 'WS2_32.dll')
    }
    $systemNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($domain in $windows.Keys) {
        foreach ($name in $windows[$domain]) { $null = $systemNames.Add($name) }
    }
    # These two are deliberately loaded from System32 by the MF worker and are
    # not static imports. They must not become packaged private DLL shadows.
    $null = $systemNames.Add('mfplat.dll')
    $null = $systemNames.Add('mfreadwrite.dll')

    $inventory = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
    $networkRoles = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $youtubeRoles = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $mediaCount = 0
    $youtubeCount = 0
    foreach ($file in $Files) {
        Assert-ComponentPeFields $file @('source', 'relative_path', 'component', 'role', 'size_bytes', 'sha256') 'inventory'
        Assert-ComponentPeSource $file.source
        $relative = Get-ComponentPeRelativePath $file.relative_path
        if ($inventory.ContainsKey($relative)) { throw "Duplicate inventory path/collision: $relative" }
        $bytes = $file.size_bytes
        if (($bytes -isnot [byte] -and $bytes -isnot [sbyte] -and $bytes -isnot [int16] -and
                $bytes -isnot [uint16] -and $bytes -isnot [int32] -and $bytes -isnot [uint32] -and
                $bytes -isnot [int64] -and $bytes -isnot [uint64]) -or $bytes -le 0 -or $bytes -gt 134217728) {
            throw 'Inventory bytes must be a positive bounded integer.'
        }
        if ($file.sha256 -isnot [string] -or $file.sha256 -cnotmatch '\A[0-9a-f]{64}\z') {
            throw 'Inventory SHA256 pin must be explicit lowercase hexadecimal.'
        }
        if ($file.component -isnot [string] -or $file.role -isnot [string]) {
            throw 'Invalid inventory component or role.'
        }
        $domain = $null
        switch -CaseSensitive ($file.component) {
            'network' {
                if (@($network.Keys) -cnotcontains $file.role -or
                    -not [string]::Equals($relative, ('download-tools/network/' + $network[$file.role]),
                        [StringComparison]::OrdinalIgnoreCase) -or -not $networkRoles.Add($file.role)) {
                    throw 'Invalid network inventory role or fixed path.'
                }
                $domain = 'network'
            }
            'media' {
                if ($file.role -cne 'Media' -or
                    -not [string]::Equals($relative, 'download-tools/media/RCMusicMediaWorker.exe',
                        [StringComparison]::OrdinalIgnoreCase) -or ++$mediaCount -ne 1) {
                    throw 'Invalid media inventory role or fixed path.'
                }
                $domain = 'media'
            }
            'youtube' {
                ++$youtubeCount
                if ($file.role -ceq 'Support') {
                    if ($relative.StartsWith('download-tools/youtube/extractor/_internal/', [StringComparison]::OrdinalIgnoreCase)) {
                        $domain = 'extractor'
                    } elseif (-not $relative.StartsWith('download-tools/youtube/notices/', [StringComparison]::OrdinalIgnoreCase)) {
                        throw 'Invalid YouTube support inventory path.'
                    }
                } elseif (@($youtube.Keys) -ccontains $file.role) {
                    if (-not [string]::Equals($relative, $youtube[$file.role], [StringComparison]::OrdinalIgnoreCase) -or
                        -not $youtubeRoles.Add($file.role)) {
                        throw 'Invalid YouTube inventory role or fixed path.'
                    }
                    $domain = if ($file.role -ceq 'Node') { 'node' } else { 'extractor' }
                } else {
                    throw 'Unknown YouTube inventory role.'
                }
            }
            default { throw 'Unknown inventory component.' }
        }
        $extension = [IO.Path]::GetExtension($relative)
        $pe = $extension -in @('.exe', '.dll', '.pyd')
        if ($pe -and $null -eq $domain) { throw 'Inventory PE lies outside its permitted load domains.' }
        $leaf = $relative.Substring($relative.LastIndexOf('/') + 1)
        if ($pe -and ($systemNames.Contains($leaf) -or $leaf -match '\A(?i:api-ms-win-|ext-ms-win-)')) {
            throw "Bundled Windows system DLL shadow is refused: $relative"
        }
        $inventory.Add($relative, [pscustomobject] @{ relative_path = $relative; domain = $domain; pe = $pe })
    }
    if ($networkRoles.Count -ne 9 -or $mediaCount -ne 1) { throw 'The fixed network/media inventory is incomplete.' }
    if ($youtubeCount -gt 0 -and $youtubeRoles.Count -ne 4) { throw 'The fixed YouTube inventory roles are incomplete.' }
    foreach ($relative in $inventory.Keys) {
        $parent = $relative
        while ($parent.Contains('/')) {
            $parent = $parent.Substring(0, $parent.LastIndexOf('/'))
            if ($inventory.ContainsKey($parent)) { throw 'Inventory file/directory path collision.' }
        }
    }

    $byReport = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($report in $Reports) {
        Assert-ComponentPeFields $report @('relative_path', 'machine', 'imports') 'report'
        $relative = Get-ComponentPeRelativePath $report.relative_path
        if ($byReport.ContainsKey($relative)) { throw "Duplicate PE report: $relative" }
        if (-not $inventory.ContainsKey($relative) -or -not $inventory[$relative].pe) {
            throw "Extra report or non-PE report: $relative"
        }
        if ($report.machine -isnot [string] -or $report.machine -cne 'x64') { throw 'PE report machine must be x64.' }
        if ($report.imports -isnot [Array]) { throw 'PE report imports must be an array of strings.' }
        $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($name in $report.imports) {
            if ($name -isnot [string] -or $name -inotmatch '\A[A-Za-z0-9_][A-Za-z0-9_.+-]*\.dll\z') {
                throw 'Invalid PE import basename.'
            }
            $null = $names.Add($name.ToLowerInvariant())
        }
        [string[]] $imports = @($names)
        [Array]::Sort($imports, [StringComparer]::Ordinal)
        $file = $inventory[$relative]
        $byReport.Add($relative, [pscustomobject] [ordered] @{
            relative_path = $file.relative_path; domain = $file.domain; machine = 'x64'; imports = $imports
        })
    }
    foreach ($file in $inventory.Values) {
        if ($file.pe -and -not $byReport.ContainsKey($file.relative_path)) {
            throw "Missing PE report: $($file.relative_path)"
        }
    }

    $normalized = [Collections.Generic.List[object]]::new()
    $private = [Collections.Generic.List[object]]::new()
    $external = [Collections.Generic.List[object]]::new()
    [string[]] $reportPaths = @($byReport.Keys)
    [Array]::Sort($reportPaths, [StringComparer]::OrdinalIgnoreCase)
    foreach ($relative in $reportPaths) {
        $report = $byReport[$relative]
        $normalized.Add($report)
        foreach ($name in $report.imports) {
            $candidatePaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
            if ($report.domain -ceq 'network') {
                $null = $candidatePaths.Add('download-tools/network/' + $name)
            } elseif ($report.domain -ceq 'extractor') {
                $null = $candidatePaths.Add('download-tools/youtube/extractor/_internal/' + $name)
                $parent = $report.relative_path.Substring(0, $report.relative_path.LastIndexOf('/'))
                $null = $candidatePaths.Add($parent + '/' + $name)
            }
            $matches = [Collections.Generic.List[object]]::new()
            foreach ($candidate in $candidatePaths) {
                if ($inventory.ContainsKey($candidate) -and $inventory[$candidate].pe -and
                    $inventory[$candidate].domain -ceq $report.domain) {
                    $matches.Add($inventory[$candidate])
                }
            }
            if ($matches.Count -gt 1) { throw "Ambiguous private import: $name in $($report.relative_path)" }
            if ($matches.Count -eq 1) {
                $private.Add([pscustomobject] [ordered] @{
                    relative_path = $report.relative_path; domain = $report.domain; name = $name
                    resolved_path = $matches[0].relative_path
                })
            } elseif ($windows[$report.domain] -icontains $name) {
                $external.Add([pscustomobject] [ordered] @{
                    relative_path = $report.relative_path; domain = $report.domain; name = $name
                })
            } else {
                throw "Unresolved private or unreviewed import '$name' in '$($report.relative_path)'."
            }
        }
    }
    return [pscustomobject] [ordered] @{
        reports = @($normalized.ToArray())
        private_imports = @($private.ToArray())
        windows_external = @($external.ToArray())
    }
}

Export-ModuleMember -Function Get-RcMusicDownloadComponentPeClosure
