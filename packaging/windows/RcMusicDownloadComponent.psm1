#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Build-time inspection only. The pinned receipt is trusted input, not publisher
# authentication. Historical provenance paths must never be opened or executed.
$script:networkLeaves = [ordered]@{
    Bootstrap = 'RCMusicDownloadWorker.exe'; Payload = 'rcmusic-download-network.dll'
    Core = 'Qt6Core.dll'; Network = 'Qt6Network.dll'; Msvcp140 = 'msvcp140.dll'; Msvcp140_1 = 'msvcp140_1.dll'
    Vcruntime140 = 'vcruntime140.dll'; Vcruntime140_1 = 'vcruntime140_1.dll'; Plugin = 'qrcmusicschanneloffline.dll'
}
$script:youtubeRoles = [ordered]@{
    Extractor = 'extractor/yt-dlp.exe'; Node = 'javascript/node.exe'
    EjsCore = 'extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js'
    EjsLibrary = 'extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js'
}
$script:notices = @('notices/python-LICENSE.txt', 'notices/node-LICENSE.txt', 'notices/yt-dlp-LICENSE.txt',
    'notices/ejs-LICENSE.txt', 'notices/pyinstaller-COPYING.txt')

function Test-ComponentReserved([string] $Name) {
    $stem = $Name.Split('.')[0].TrimEnd(' ')
    return $stem -imatch '^(CON|PRN|AUX|NUL|CONIN\$|CONOUT\$|COM[1-9¹²³]|LPT[1-9¹²³])\z'
}

function Get-ComponentLocalPath([string] $Path) {
    # This lexical helper is also used for inert provenance; it must not query
    # a drive or inspect an original source that may no longer be available.
    if ([string]::IsNullOrWhiteSpace($Path) -or $Path.Length -lt 4 -or $Path.Length -gt 32767 -or
        $Path -cnotmatch '^[A-Za-z]:[\\/]') { throw 'A local absolute component path is required' }
    $null = [Text.UTF8Encoding]::new($false, $true).GetByteCount($Path)
    foreach ($part in $Path.Substring(3).Replace('\', '/').Split('/')) {
        if ($part.Length -eq 0 -or $part.EndsWith('.') -or $part.EndsWith(' ') -or
            $part -match '[\x00-\x1F:"<>|?*]' -or (Test-ComponentReserved $part)) {
            throw 'Unsupported component path segment'
        }
    }
    return [IO.Path]::GetFullPath($Path)
}

function Assert-ComponentNoReparse([string] $Path) {
    $ancestor = $Path
    while ($ancestor) {
        try { $attributes = [IO.File]::GetAttributes($ancestor) }
        catch [IO.FileNotFoundException] { $attributes = 0 }
        catch [IO.DirectoryNotFoundException] { $attributes = 0 }
        if ($attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Component reparse path refused' }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
}

function Get-ComponentRelativePath([string] $Path, [int] $MaxUnits = 1100, [int] $MaxSegments = 20) {
    if ([string]::IsNullOrEmpty($Path) -or $Path.Length -gt $MaxUnits -or $Path.Contains('\')) {
        throw 'A bounded forward-slash relative component path is required'
    }
    $parts = $Path.Split('/')
    if ($parts.Count -gt $MaxSegments) { throw 'Component path depth exceeded' }
    foreach ($part in $parts) {
        if ($part.Length -eq 0 -or $part.Length -gt 255 -or $part -cin @('.', '..') -or $part.EndsWith('.') -or
            $part -cnotmatch '^[A-Za-z0-9_.-]+\z' -or (Test-ComponentReserved $part)) {
            throw 'Unsupported relative component path'
        }
    }
    return $Path
}

function Assert-ComponentObject([Text.Json.JsonElement] $Element, [string[]] $Fields) {
    if ($Element.ValueKind -ne [Text.Json.JsonValueKind]::Object) { throw 'A JSON object is required' }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($property in $Element.EnumerateObject()) {
        if (-not $seen.Add($property.Name) -or $property.Name -cnotin $Fields) {
            throw 'Duplicate or unknown component field'
        }
    }
    if ($seen.Count -ne $Fields.Count) { throw 'A required component field is missing' }
}

function Get-ComponentString([Text.Json.JsonElement] $Element) {
    if ($Element.ValueKind -ne [Text.Json.JsonValueKind]::String) { throw 'A JSON string is required' }
    $value = $Element.GetString()
    if ($value.Length -gt 32767 -or $value -match '[\x00-\x1F\x7F]') { throw 'Unsupported component string' }
    $null = [Text.UTF8Encoding]::new($false, $true).GetByteCount($value)
    return $value
}

function Get-ComponentSha([Text.Json.JsonElement] $Element) {
    $hash = Get-ComponentString $Element
    if ($hash -cnotmatch '^[0-9a-f]{64}\z') { throw 'A lowercase component SHA256 is required' }
    return $hash
}

function Get-ComponentSize([Text.Json.JsonElement] $Element, [long] $Maximum = 134217728) {
    [long]$size = 0
    if ($Element.ValueKind -ne [Text.Json.JsonValueKind]::Number -or
        $Element.GetRawText() -cnotmatch '^[1-9][0-9]*\z' -or -not $Element.TryGetInt64([ref]$size) -or $size -gt $Maximum) {
        throw 'A bounded positive component byte count is required'
    }
    return $size
}

function Get-ComponentStreamHash([IO.Stream] $Stream) {
    $Stream.Position = 0
    $result = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Stream)).ToLowerInvariant()
    $Stream.Position = 0
    return $result
}

function Open-ComponentInput([string] $Path) {
    Assert-ComponentNoReparse $Path
    return [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
}

function Assert-ComponentTree([string] $Root, [Collections.Generic.HashSet[string]] $ExpectedFiles) {
    $directories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $spellings = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    $null = $directories.Add('')
    foreach ($file in $ExpectedFiles) {
        $parts = $file.Split('/')
        for ($index = 1; $index -lt $parts.Count; $index++) {
            $directory = $parts[0..($index - 1)] -join '/'
            if ($ExpectedFiles.Contains($directory)) { throw 'Component file-directory conflict' }
            if ($spellings.ContainsKey($directory) -and $spellings[$directory] -cne $directory) {
                throw 'Component directory casing conflict'
            }
            $spellings[$directory] = $directory
            $null = $directories.Add($directory)
        }
    }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $seenDirectories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push('')
    while ($pending.Count -gt 0) {
        $relative = $pending.Pop()
        $path = if ($relative.Length -eq 0) { $Root } else { Join-Path $Root $relative }
        Assert-ComponentNoReparse $path
        foreach ($child in [IO.Directory]::EnumerateFileSystemEntries($path)) {
            $leaf = [IO.Path]::GetFileName($child)
            $childRelative = if ($relative.Length -eq 0) { $leaf } else { $relative + '/' + $leaf }
            if (-not $ExpectedFiles.Contains($childRelative) -and -not $directories.Contains($childRelative)) {
                throw 'Undeclared component file or directory'
            }
            $attributes = [IO.File]::GetAttributes($child)
            if ($attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Component tree contains a reparse point' }
            if ($ExpectedFiles.Contains($childRelative)) {
                if (($attributes -band [IO.FileAttributes]::Directory) -or -not $seen.Add($childRelative)) {
                    throw 'Component payload is not a unique regular file'
                }
            } else {
                if (-not ($attributes -band [IO.FileAttributes]::Directory) -or -not $seenDirectories.Add($childRelative)) {
                    throw 'Component directory alias or type conflict'
                }
                $pending.Push($childRelative)
            }
        }
    }
    if ($seen.Count -ne $ExpectedFiles.Count) { throw 'Component physical inventory is incomplete' }
}

function Get-RcMusicDownloadComponentPlan {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $ReceiptPath,
        [Parameter(Mandatory)][string] $ReceiptSha256
    )
    if ($ReceiptSha256 -cnotmatch '^[0-9a-f]{64}\z') { throw 'An explicit lowercase receipt SHA256 is required' }
    $receiptPath = Get-ComponentLocalPath $ReceiptPath
    $packageRoot = [IO.Path]::GetDirectoryName($receiptPath)
    $receiptLeaf = [IO.Path]::GetFileName($receiptPath)
    if ([string]::Equals($receiptLeaf, 'PlayerDownloadPackageProfile.hpp', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Component receipt and header must be separate files'
    }
    $streams = [Collections.Generic.List[IO.Stream]]::new()
    $document = $null
    try {
        $receipt = Open-ComponentInput $receiptPath
        $streams.Add($receipt)
        if ($receipt.Length -lt 1 -or $receipt.Length -gt 8388608) { throw 'Component receipt exceeds its JSON byte bound' }
        if ((Get-ComponentStreamHash $receipt) -cne $ReceiptSha256) { throw 'Component receipt bytes differ from the explicit pin' }
        $bytes = [byte[]]::new([int]$receipt.Length)
        $offset = 0
        while ($offset -lt $bytes.Length) {
            $read = $receipt.Read($bytes, $offset, $bytes.Length - $offset)
            if ($read -eq 0) { throw 'Incomplete component receipt read' }
            $offset += $read
        }
        $options = [Text.Json.JsonDocumentOptions]::new()
        $options.MaxDepth = 8
        $options.AllowTrailingCommas = $false
        $options.CommentHandling = [Text.Json.JsonCommentHandling]::Disallow
        $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes)
        $document = [Text.Json.JsonDocument]::Parse($text, $options)
        $root = $document.RootElement
        $rootFields = @('schemaVersion', 'purpose', 'state', 'files', 'header', 'recipe_files', 'youtube_receipt')
        $featureObject = [Text.Json.JsonElement]::new()
        $hasFeatures = $root.ValueKind -eq [Text.Json.JsonValueKind]::Object -and
            $root.TryGetProperty('features', [ref]$featureObject)
        if ($hasFeatures) { $rootFields += 'features' }
        Assert-ComponentObject $root $rootFields
        $schema = $root.GetProperty('schemaVersion')
        if ($schema.ValueKind -ne [Text.Json.JsonValueKind]::Number -or $schema.GetRawText() -cne '1' -or
            (Get-ComponentString ($root.GetProperty('purpose'))) -cne 'player-download-package-profile-v1' -or
            (Get-ComponentString ($root.GetProperty('state'))) -cne 'candidate') {
            throw 'Unsupported component receipt schema, purpose or state'
        }
        $features = [ordered]@{ provision_media_profile = $false; embedded_browser_ingress = $false }
        if ($hasFeatures) {
            Assert-ComponentObject $featureObject @('provision_media_profile', 'embedded_browser_ingress')
            foreach ($name in @($features.Keys)) {
                $value = $featureObject.GetProperty($name)
                if ($value.ValueKind -notin @([Text.Json.JsonValueKind]::True, [Text.Json.JsonValueKind]::False)) {
                    throw 'Component compile-time features must be Boolean'
                }
                $features[$name] = $value.GetBoolean()
            }
        }
        $header = $root.GetProperty('header')
        Assert-ComponentObject $header @('relative_path', 'bytes', 'sha256')
        $headerRelative = Get-ComponentString ($header.GetProperty('relative_path'))
        if ($headerRelative -cne 'PlayerDownloadPackageProfile.hpp') { throw 'Unexpected component header path' }
        $headerSize = Get-ComponentSize ($header.GetProperty('bytes'))
        $headerSha = Get-ComponentSha ($header.GetProperty('sha256'))
        $headerPath = Get-ComponentLocalPath (Join-Path $packageRoot $headerRelative)

        # Validate bounded provenance records as inert data. Do not inspect any
        # of these absolute historical paths or require the original build tree.
        $recipes = $root.GetProperty('recipe_files')
        if ($recipes.ValueKind -ne [Text.Json.JsonValueKind]::Array -or
            $recipes.GetArrayLength() -lt 1 -or $recipes.GetArrayLength() -gt 64) {
            throw 'Invalid component recipe provenance list'
        }
        $recipePaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($recipe in $recipes.EnumerateArray()) {
            Assert-ComponentObject $recipe @('path', 'bytes', 'sha256')
            $path = Get-ComponentLocalPath (Get-ComponentString ($recipe.GetProperty('path')))
            if (-not $recipePaths.Add($path)) { throw 'Duplicate component recipe provenance' }
            $null = Get-ComponentSize ($recipe.GetProperty('bytes'))
            $null = Get-ComponentSha ($recipe.GetProperty('sha256'))
        }
        $youtubeReceipt = $root.GetProperty('youtube_receipt')
        $hasYoutube = $youtubeReceipt.ValueKind -ne [Text.Json.JsonValueKind]::Null
        if ($hasYoutube) {
            Assert-ComponentObject $youtubeReceipt @('path', 'bytes', 'sha256')
            $null = Get-ComponentLocalPath (Get-ComponentString ($youtubeReceipt.GetProperty('path')))
            $null = Get-ComponentSize ($youtubeReceipt.GetProperty('bytes')) 8388608
            $null = Get-ComponentSha ($youtubeReceipt.GetProperty('sha256'))
        }

        $items = $root.GetProperty('files')
        if ($items.ValueKind -ne [Text.Json.JsonValueKind]::Array -or
            $items.GetArrayLength() -lt 10 -or $items.GetArrayLength() -gt 266) { throw 'Invalid component payload count' }
        $files = [Collections.Generic.List[object]]::new()
        $expected = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $null = $expected.Add($receiptLeaf)
        $null = $expected.Add($headerRelative)
        $networkSeen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        $youtubeSeen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        $noticeSeen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        [int]$mediaCount = 0
        [int]$youtubeCount = 0
        [long]$youtubeBytes = 0
        foreach ($item in $items.EnumerateArray()) {
            Assert-ComponentObject $item @('component', 'role', 'relative_path', 'bytes', 'sha256')
            $component = Get-ComponentString ($item.GetProperty('component'))
            $role = Get-ComponentString ($item.GetProperty('role'))
            $relative = Get-ComponentRelativePath (Get-ComponentString ($item.GetProperty('relative_path')))
            $size = Get-ComponentSize ($item.GetProperty('bytes'))
            $sha = Get-ComponentSha ($item.GetProperty('sha256'))
            if ($component -ceq 'network') {
                if ($role -cnotin @($script:networkLeaves.Keys) -or -not $networkSeen.Add($role) -or
                    $relative -cne ('payload/download-tools/network/' + $script:networkLeaves[$role])) {
                    throw 'Unknown, repeated or misplaced network role'
                }
            } elseif ($component -ceq 'media') {
                $mediaCount++
                if ($mediaCount -ne 1 -or $role -cne 'Media' -or
                    $relative -cne 'payload/download-tools/media/RCMusicMediaWorker.exe') { throw 'Invalid Media Foundation entry' }
            } elseif ($component -ceq 'youtube') {
                $prefix = 'payload/download-tools/youtube/'
                if (-not $hasYoutube -or -not $relative.StartsWith($prefix, [StringComparison]::Ordinal)) { throw 'Unexpected YouTube payload' }
                $youtubeCount++
                $youtubeBytes += $size
                if ($youtubeCount -gt 256 -or $youtubeBytes -gt 536870912) { throw 'YouTube component quota exceeded' }
                $leaf = Get-ComponentRelativePath ($relative.Substring($prefix.Length)) 1024 16
                if ($role -cin @($script:youtubeRoles.Keys)) {
                    if ($leaf -cne $script:youtubeRoles[$role] -or -not $youtubeSeen.Add($role)) { throw 'Repeated or misplaced YouTube role' }
                } elseif ($role -cne 'Support' -or
                    (-not $leaf.StartsWith('extractor/_internal/', [StringComparison]::Ordinal) -and
                     -not $leaf.StartsWith('notices/', [StringComparison]::Ordinal))) { throw 'Unsupported YouTube artifact role or location' }
                if ($leaf -cin $script:notices) { $null = $noticeSeen.Add($leaf) }
            } else { throw 'Unknown component kind' }
            if (-not $expected.Add($relative)) { throw 'Duplicate or case-aliased component payload path' }
            $source = Get-ComponentLocalPath (Join-Path $packageRoot $relative)
            $files.Add([pscustomobject]@{ source = $source; relative_path = $relative.Substring('payload/'.Length)
                component = $component; role = $role; size_bytes = $size; sha256 = $sha })
        }
        if ($networkSeen.Count -ne 9 -or $mediaCount -ne 1 -or
            ($hasYoutube -and ($youtubeSeen.Count -ne 4 -or $noticeSeen.Count -ne 5)) -or
            (-not $hasYoutube -and $youtubeCount -ne 0)) { throw 'Incomplete component role inventory' }
        Assert-ComponentTree $packageRoot $expected

        $checks = [Collections.Generic.List[object]]::new()
        $checks.Add([pscustomobject]@{ path = $headerPath; size = $headerSize; sha256 = $headerSha })
        foreach ($file in $files) { $checks.Add([pscustomobject]@{ path = $file.source; size = $file.size_bytes; sha256 = $file.sha256 }) }
        $held = [Collections.Generic.List[object]]::new()
        # Inspect every declared size before hashing any potentially large input.
        foreach ($check in $checks) {
            $stream = Open-ComponentInput $check.path
            $streams.Add($stream)
            if ($stream.Length -ne $check.size) { throw 'Component header or payload size changed' }
            $held.Add([pscustomobject]@{ stream = $stream; sha256 = $check.sha256 })
        }
        foreach ($check in $held) {
            if ((Get-ComponentStreamHash $check.stream) -cne $check.sha256) { throw 'Component header or payload SHA256 changed' }
        }
        # Detect extra/missing objects introduced during the bounded snapshot.
        Assert-ComponentTree $packageRoot $expected
        return [pscustomobject]@{
            package_root = $packageRoot; receipt_path = $receiptPath; receipt_sha256 = $ReceiptSha256
            header_path = $headerPath; header_sha256 = $headerSha
            features = [pscustomobject]$features
            files = @($files | Sort-Object -Property relative_path -CaseSensitive)
            metadata = [pscustomobject]@{
                schema_version = 1; selection_default = $false; receipt_sha256 = $ReceiptSha256; header_sha256 = $headerSha
                activation_marker = [pscustomobject]@{ relative_path = 'download-tools/installed-v1'; content = "RCMusicDownloadTools-v1`n" }
            }
        }
    } finally {
        if ($null -ne $document) { $document.Dispose() }
        foreach ($stream in $streams) { $stream.Dispose() }
    }
}

Export-ModuleMember -Function Get-RcMusicDownloadComponentPlan
