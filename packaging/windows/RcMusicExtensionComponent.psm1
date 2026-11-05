#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'RcMusicChromiumIdentity.psm1') -Force

function Get-ExtensionComponentLocalPath([string] $Value) {
    if ([string]::IsNullOrWhiteSpace($Value) -or $Value.Length -lt 4 -or $Value.Length -gt 32767 -or
        $Value -cnotmatch '\A[A-Za-z]:[\\/]') { throw 'Extension component paths must be ordinary local absolute paths.' }
    $null = [Text.UTF8Encoding]::new($false, $true).GetByteCount($Value)
    foreach ($part in $Value.Substring(3).Replace('\', '/').Split('/')) {
        if ($part.Length -eq 0 -or $part -in @('.', '..') -or $part.EndsWith('.') -or $part.EndsWith(' ') -or
            $part -match '[\x00-\x1f\x7f:"<>|?*]' -or
            $part.Split('.')[0] -imatch '\A(?:CON|PRN|AUX|NUL|CONIN\$|CONOUT\$|COM[1-9¹²³]|LPT[1-9¹²³])\z') {
            throw 'Unsafe extension component path segment.'
        }
    }
    return [IO.Path]::GetFullPath($Value)
}

function Assert-ExtensionComponentPlainPath([string] $Path) {
    $current = $Path
    while ($current) {
        try { $attributes = [IO.File]::GetAttributes($current) }
        catch [IO.FileNotFoundException] { $attributes = 0 }
        catch [IO.DirectoryNotFoundException] { $attributes = 0 }
        if ($attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Extension component paths cannot traverse reparse points.' }
        $current = [IO.Path]::GetDirectoryName($current)
    }
}

function Open-ExtensionComponentInput([string] $Path) {
    Assert-ExtensionComponentPlainPath $Path
    return [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
}

function Get-ExtensionComponentHash([IO.Stream] $Stream) {
    $Stream.Position = 0
    $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Stream)).ToLowerInvariant()
    $Stream.Position = 0
    return $hash
}

function Read-ExtensionComponentBytes([IO.Stream] $Stream) {
    if ($Stream.Length -lt 1 -or $Stream.Length -gt 65536) { throw 'Extension JSON exceeds the 64KiB bound.' }
    $bytes = [byte[]]::new([int]$Stream.Length)
    $Stream.Position = 0
    $offset = 0
    while ($offset -lt $bytes.Length) {
        $read = $Stream.Read($bytes, $offset, $bytes.Length - $offset)
        if ($read -eq 0) { throw 'Extension JSON ended unexpectedly.' }
        $offset += $read
    }
    $Stream.Position = 0
    return ,$bytes
}

function Assert-ExtensionComponentJson([Text.Json.JsonElement] $Value) {
    if ($Value.ValueKind -eq [Text.Json.JsonValueKind]::Object) {
        $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($property in $Value.EnumerateObject()) {
            if (-not $names.Add($property.Name)) { throw 'Duplicate extension receipt JSON property.' }
            Assert-ExtensionComponentJson $property.Value
        }
    } elseif ($Value.ValueKind -eq [Text.Json.JsonValueKind]::Array) {
        foreach ($item in $Value.EnumerateArray()) { Assert-ExtensionComponentJson $item }
    }
}

function Assert-ExtensionComponentObject([Text.Json.JsonElement] $Value, [string[]] $Fields) {
    if ($Value.ValueKind -ne [Text.Json.JsonValueKind]::Object) { throw 'An extension receipt object is required.' }
    $properties = @($Value.EnumerateObject())
    if ($properties.Count -ne $Fields.Count) { throw 'Invalid extension receipt fields.' }
    foreach ($property in $properties) {
        if ($Fields -cnotcontains $property.Name) { throw 'Unknown extension receipt field.' }
    }
}

function Get-ExtensionComponentString([Text.Json.JsonElement] $Value) {
    if ($Value.ValueKind -ne [Text.Json.JsonValueKind]::String) { throw 'An extension receipt string is required.' }
    $text = $Value.GetString()
    if ($text.Length -gt 32767 -or $text.Contains([char]0)) { throw 'Invalid extension receipt string.' }
    $null = [Text.UTF8Encoding]::new($false, $true).GetByteCount($text)
    return $text
}

function Get-ExtensionComponentSha([Text.Json.JsonElement] $Value) {
    $sha = Get-ExtensionComponentString $Value
    if ($sha -cnotmatch '\A[0-9a-f]{64}\z') { throw 'Extension receipt hashes must be lowercase SHA256.' }
    return $sha
}

function Get-ExtensionComponentSize([Text.Json.JsonElement] $Value) {
    [long] $size = 0
    if ($Value.ValueKind -ne [Text.Json.JsonValueKind]::Number -or
        $Value.GetRawText() -cnotmatch '\A[1-9][0-9]*\z' -or -not $Value.TryGetInt64([ref]$size)) {
        throw 'Extension receipt sizes must be positive integers.'
    }
    return $size
}

function Assert-ExtensionComponentTree([string] $Root, [string[]] $Assets) {
    Assert-ExtensionComponentPlainPath $Root
    $roots = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($path in [IO.Directory]::EnumerateFileSystemEntries($Root)) {
        $name = [IO.Path]::GetFileName($path)
        $attributes = [IO.File]::GetAttributes($path)
        if (-not $roots.Add($name) -or ($attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Ambiguous or reparse extension package entry.'
        }
        $directory = ($attributes -band [IO.FileAttributes]::Directory) -ne 0
        if (($name -ieq 'extension' -and -not $directory) -or
            ($name -ieq 'package-receipt.json' -and $directory) -or
            ($name -ine 'extension' -and $name -ine 'package-receipt.json')) {
            throw 'The extension package root contains undeclared entries.'
        }
    }
    if ($roots.Count -ne 2) { throw 'The extension package root is incomplete.' }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($path in [IO.Directory]::EnumerateFileSystemEntries((Join-Path $Root 'extension'))) {
        $name = [IO.Path]::GetFileName($path)
        $attributes = [IO.File]::GetAttributes($path)
        if (-not $seen.Add($name) -or $Assets -cnotcontains $name -or
            ($attributes -band ([IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint))) {
            throw 'The extension asset tree must be flat and exactly whitelisted.'
        }
    }
    if ($seen.Count -ne $Assets.Count) { throw 'A required extension asset is missing.' }
}

# The receipt pin binds a trusted source candidate. Historical provenance paths
# stay inert, and the manifest's public key binds identity rather than publisher
# authenticity. This planner never copies, registers or activates an extension.
function Get-RcMusicExtensionComponentPlan {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ReceiptPath,
        [Parameter(Mandatory)] [string] $ReceiptSha256,
        [Parameter(Mandatory)] [ValidateSet('Main', 'Youtube')] [string] $Channel,
        [Parameter(Mandatory)] $NativeChannel
    )
    if ($ReceiptSha256 -cnotmatch '\A[0-9a-f]{64}\z') { throw 'An explicit lowercase extension receipt SHA256 is required.' }
    $receipt = Get-ExtensionComponentLocalPath $ReceiptPath
    if ([IO.Path]::GetFileName($receipt) -cne 'package-receipt.json') { throw 'The package receipt basename must be package-receipt.json.' }
    $package = [IO.Path]::GetDirectoryName($receipt)
    $canonicalChannel = if ($Channel -ieq 'Main') { 'Main' } else { 'Youtube' }
    $lowerChannel = $canonicalChannel.ToLowerInvariant()
    $hostName = if ($canonicalChannel -ceq 'Main') { 'com.rcmusic.media' } else { 'com.rcmusic.youtube' }
    $protocol = if ($canonicalChannel -ceq 'Main') { 'direct.media.v1' } else { 'youtube.audio.v1' }
    $assets = @('manifest.json', 'native-client.mjs', 'policy.mjs', 'popup-controller.mjs', 'popup.css', 'popup.html', 'popup.mjs')
    $recipes = @('packaging/windows/New-RcMusicChromiumExtensionPackage.ps1',
        'packaging/windows/RcMusicYoutubeExtensionPackage.psm1', 'packaging/windows/RcMusicChromiumIdentity.psm1')
    if ($canonicalChannel -ceq 'Main') { $assets += @('detector.js', 'media-collector.js') }
    else { $recipes = @('packaging/windows/New-RcMusicYoutubeExtensionPackage.ps1') + $recipes }
    $streams = [Collections.Generic.List[IO.Stream]]::new()
    $document = $null
    try {
        $receiptStream = Open-ExtensionComponentInput $receipt
        $streams.Add($receiptStream)
        if ($receiptStream.Length -lt 1 -or $receiptStream.Length -gt 65536) { throw 'Extension package receipt exceeds 64KiB.' }
        if ((Get-ExtensionComponentHash $receiptStream) -cne $ReceiptSha256) { throw 'Extension receipt bytes differ from their explicit pin.' }
        $text = [Text.UTF8Encoding]::new($false, $true).GetString((Read-ExtensionComponentBytes $receiptStream))
        $options = [Text.Json.JsonDocumentOptions]::new()
        $options.MaxDepth = 16; $options.AllowTrailingCommas = $false
        $options.CommentHandling = [Text.Json.JsonCommentHandling]::Disallow
        $document = [Text.Json.JsonDocument]::Parse($text, $options)
        $root = $document.RootElement
        Assert-ExtensionComponentJson $root
        Assert-ExtensionComponentObject $root @('schema_version', 'component', 'channel', 'extension_identity', 'identity_state',
            'native_host_name', 'native_manifest', 'source_commit', 'extension_dirty', 'recipe_dirty', 'source_dirty',
            'source_inputs', 'recipe_files', 'files')
        if ($root.GetProperty('schema_version').GetRawText() -cne '1' -or
            (Get-ExtensionComponentString ($root.GetProperty('component'))) -cne ($lowerChannel + '-extension-source') -or
            (Get-ExtensionComponentString ($root.GetProperty('channel'))) -cne $canonicalChannel -or
            (Get-ExtensionComponentString ($root.GetProperty('identity_state'))) -cne 'public_key_bound' -or
            (Get-ExtensionComponentString ($root.GetProperty('native_host_name'))) -cne $hostName -or
            $root.GetProperty('native_manifest').ValueKind -ne [Text.Json.JsonValueKind]::Null -or
            (Get-ExtensionComponentString ($root.GetProperty('source_commit'))) -cnotmatch '\A[0-9a-f]{40}\z') {
            throw 'The extension receipt must describe the requested clean identity-bound source family.'
        }
        foreach ($flag in @('extension_dirty', 'recipe_dirty', 'source_dirty')) {
            if ($root.GetProperty($flag).ValueKind -ne [Text.Json.JsonValueKind]::False) { throw 'Dirty extension or recipe sources cannot enter an installer.' }
        }
        $claimed = $root.GetProperty('extension_identity')
        Assert-ExtensionComponentObject $claimed @('extension_id', 'origin', 'public_key_sha256')
        $claimedId = Get-ExtensionComponentString ($claimed.GetProperty('extension_id'))
        $claimedOrigin = Get-ExtensionComponentString ($claimed.GetProperty('origin'))
        $claimedKeyHash = Get-ExtensionComponentSha ($claimed.GetProperty('public_key_sha256'))
        if ($claimedId -cnotmatch '\A[a-p]{32}\z' -or $claimedOrigin -cne ('chrome-extension://' + $claimedId + '/')) {
            throw 'Invalid extension receipt identity.'
        }
        foreach ($field in @('protocol', 'host_name', 'origin')) {
            $present = if ($NativeChannel -is [Collections.IDictionary]) { $NativeChannel.Contains($field) }
                elseif ($NativeChannel -is [pscustomobject]) { $null -ne $NativeChannel.PSObject.Properties[$field] }
                else { $false }
            if (-not $present -or $NativeChannel.$field -isnot [string]) { throw 'The matching native runtime channel is required.' }
        }
        if ($NativeChannel.protocol -cne $protocol -or $NativeChannel.host_name -cne $hostName -or
            $NativeChannel.origin -cne $claimedOrigin) { throw 'Extension identity and native runtime channel do not match.' }

        # Validate the fixed provenance shape only. These are repository-relative
        # historical records, not paths to reopen in this exported package.
        $inputs = $root.GetProperty('source_inputs')
        $expectedInputs = @('extensions/chromium/' + $lowerChannel) + $recipes
        if ($inputs.ValueKind -ne [Text.Json.JsonValueKind]::Array -or $inputs.GetArrayLength() -ne $expectedInputs.Count) {
            throw 'Invalid extension source provenance.'
        }
        $seenInputs = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($inputPath in $inputs.EnumerateArray()) {
            $path = Get-ExtensionComponentString $inputPath
            if ($expectedInputs -cnotcontains $path -or -not $seenInputs.Add($path)) { throw 'Unexpected or duplicate extension source input.' }
        }
        $recipeRows = $root.GetProperty('recipe_files')
        if ($recipeRows.ValueKind -ne [Text.Json.JsonValueKind]::Array -or $recipeRows.GetArrayLength() -ne $recipes.Count) {
            throw 'Invalid extension recipe provenance.'
        }
        $seenRecipes = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($row in $recipeRows.EnumerateArray()) {
            Assert-ExtensionComponentObject $row @('path', 'size_bytes', 'sha256')
            $path = Get-ExtensionComponentString ($row.GetProperty('path'))
            if ($recipes -cnotcontains $path -or -not $seenRecipes.Add($path)) { throw 'Unexpected or duplicate extension recipe record.' }
            $null = Get-ExtensionComponentSize ($row.GetProperty('size_bytes'))
            $null = Get-ExtensionComponentSha ($row.GetProperty('sha256'))
        }
        $rows = $root.GetProperty('files')
        if ($rows.ValueKind -ne [Text.Json.JsonValueKind]::Array -or $rows.GetArrayLength() -ne $assets.Count) {
            throw 'The extension asset inventory must equal the fixed family whitelist.'
        }
        $byName = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($row in $rows.EnumerateArray()) {
            Assert-ExtensionComponentObject $row @('relative_path', 'size_bytes', 'sha256')
            $relative = Get-ExtensionComponentString ($row.GetProperty('relative_path'))
            if (-not $relative.StartsWith('extension/', [StringComparison]::Ordinal)) { throw 'Unexpected extension asset path.' }
            $name = $relative.Substring(10)
            if ($assets -cnotcontains $name -or $byName.ContainsKey($name)) { throw 'Unknown or duplicate extension asset.' }
            $byName.Add($name, [pscustomobject]@{
                size_bytes = Get-ExtensionComponentSize ($row.GetProperty('size_bytes'))
                sha256 = Get-ExtensionComponentSha ($row.GetProperty('sha256'))
            })
        }
        Assert-ExtensionComponentTree $package $assets
        $files = [Collections.Generic.List[object]]::new()
        $identity = $null
        foreach ($name in $assets) {
            $source = Join-Path $package ('extension/' + $name)
            $stream = Open-ExtensionComponentInput $source
            $streams.Add($stream)
            $expected = $byName[$name]
            if ($stream.Length -ne $expected.size_bytes) { throw 'Extension asset size differs from the pinned receipt.' }
            if ($name -ceq 'manifest.json' -and $stream.Length -gt 65536) { throw 'Extension manifest exceeds 64KiB.' }
            if ((Get-ExtensionComponentHash $stream) -cne $expected.sha256) { throw 'Extension asset hash differs from the pinned receipt.' }
            if ($name -ceq 'manifest.json') {
                $identity = Get-RcMusicChromiumManifestIdentity -ManifestBytes (Read-ExtensionComponentBytes $stream)
            }
            $files.Add([pscustomobject]@{
                source = $source; relative_path = 'browser-extensions/' + $lowerChannel + '/' + $name
                size_bytes = $stream.Length; sha256 = $expected.sha256
            })
        }
        if ($null -eq $identity -or $identity.extension_id -cne $claimedId -or
            $identity.origin -cne $claimedOrigin -or $identity.public_key_sha256 -cne $claimedKeyHash) {
            throw 'The copied extension manifest does not match its claimed public identity.'
        }
        Assert-ExtensionComponentTree $package $assets
        return [pscustomobject]@{
            channel = $canonicalChannel; extension_id = $identity.extension_id; origin = $identity.origin; native_host_name = $hostName
            receipt_path = $receipt; receipt_sha256 = $ReceiptSha256; package_root = $package; files = $files.ToArray()
        }
    } finally {
        if ($null -ne $document) { $document.Dispose() }
        foreach ($stream in $streams) { $stream.Dispose() }
    }
}

Export-ModuleMember -Function Get-RcMusicExtensionComponentPlan
