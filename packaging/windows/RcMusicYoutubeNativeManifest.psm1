#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# A trusted build-receipt producer is a caller precondition. This component
# creates candidates only; it does not install, register or execute a host.
$script:modulePath = [IO.Path]::GetFullPath($PSCommandPath)
$script:recipeNames = @('RcMusicYoutubeNativeManifest.psm1', 'New-RcMusicYoutubeNativeManifest.ps1')

function Test-NativeManifestReservedName([string] $Component) {
    $stem = $Component.Split('.')[0].TrimEnd(' ')
    return $stem -imatch '^(CON|PRN|AUX|NUL|CONIN\$|CONOUT\$|COM[1-9¹²³]|LPT[1-9¹²³])\z'
}

function Get-NativeManifestLocalPath([string] $Path) {
    # Mirror the native peer check's local drive/path grammar before any file
    # open. UNC, device namespaces, ADS and DOS device components are refused.
    if ([string]::IsNullOrWhiteSpace($Path) -or $Path.Length -lt 4 -or $Path.Length -gt 32767 -or
        $Path -cnotmatch '^[A-Za-z]:[\\/]') { throw 'A local absolute Windows path is required' }
    $null = [Text.UTF8Encoding]::new($false, $true).GetByteCount($Path)
    foreach ($component in $Path.Substring(3).Replace('\', '/').Split('/')) {
        if ($component.Length -eq 0 -or $component.EndsWith('.') -or $component.EndsWith(' ') -or
            $component -match '[\x00-\x1F:"<>|?*]' -or (Test-NativeManifestReservedName $component)) {
            throw 'A Windows path component is unsupported'
        }
    }
    return [IO.Path]::GetFullPath($Path)
}

function Assert-NativeManifestPlainPath([string] $Path) {
    $ancestor = $Path
    while ($ancestor) {
        try { $attributes = [IO.File]::GetAttributes($ancestor) }
        catch [IO.FileNotFoundException] { $attributes = 0 }
        catch [IO.DirectoryNotFoundException] { $attributes = 0 }
        if ($attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'Native manifest paths must not traverse reparse points'
        }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
}

function Open-NativeManifestInput([string] $Path) {
    Assert-NativeManifestPlainPath $Path
    return [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
}

function Get-NativeManifestStreamHash([IO.Stream] $Stream) {
    $Stream.Position = 0
    $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Stream)).ToLowerInvariant()
    $Stream.Position = 0
    return $hash
}

function Assert-NativeManifestObject([Text.Json.JsonElement] $Element, [string[]] $Fields) {
    if ($Element.ValueKind -ne [Text.Json.JsonValueKind]::Object) { throw 'A JSON object is required' }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($property in $Element.EnumerateObject()) {
        if (-not $seen.Add($property.Name) -or $property.Name -cnotin $Fields) {
            throw 'Unknown or duplicate build-receipt field'
        }
    }
    if ($seen.Count -ne $Fields.Count) { throw 'A build-receipt field is missing' }
}

function Get-NativeManifestString([Text.Json.JsonElement] $Element) {
    if ($Element.ValueKind -ne [Text.Json.JsonValueKind]::String) { throw 'A JSON string is required' }
    $value = $Element.GetString()
    if ($value.Length -gt 32767 -or $value.Contains([char]0)) { throw 'Unsupported build-receipt string' }
    $null = [Text.UTF8Encoding]::new($false, $true).GetByteCount($value)
    return $value
}

function Get-NativeManifestSha256([Text.Json.JsonElement] $Element) {
    $hash = Get-NativeManifestString $Element
    if ($hash -cnotmatch '^[0-9a-f]{64}\z') { throw 'A lowercase SHA256 is required' }
    return $hash
}

function Get-NativeManifestImages([Text.Json.JsonElement] $Element) {
    if ($Element.ValueKind -ne [Text.Json.JsonValueKind]::Array -or
        $Element.GetArrayLength() -lt 1 -or $Element.GetArrayLength() -gt 4) {
        throw 'A peer image list must have one to four basenames'
    }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($item in $Element.EnumerateArray()) {
        $name = Get-NativeManifestString $item
        if ($name -inotmatch '^[A-Za-z0-9_-]+\.exe\z' -or (Test-NativeManifestReservedName $name) -or
            -not $seen.Add($name)) { throw 'Unsupported or duplicate peer image basename' }
        $name
    }
}

function Read-RcMusicNativeHostBuildProfile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $ReceiptPath,
        [Parameter(Mandatory)][string] $ReceiptSha256,
        [Parameter(Mandatory)][ValidateSet('Main', 'Youtube')][string] $Family
    )
    if ($ReceiptSha256 -cnotmatch '^[0-9a-f]{64}\z') { throw 'An explicit lowercase receipt SHA256 is required' }
    $path = Get-NativeManifestLocalPath $ReceiptPath
    $stream = Open-NativeManifestInput $path
    try {
        return Read-NativeManifestBuildProfile $stream $ReceiptSha256 -Family $Family
    } finally { $stream.Dispose() }
}

function Read-NativeManifestBuildProfile([IO.Stream] $Stream, [string] $ExpectedSha256,
        [ValidateSet('Main', 'Youtube')][string] $Family = 'Youtube') {
    $expectedProtocol = if ($Family -ieq 'Main') { 'direct.media.v1' } else { 'youtube.audio.v1' }
    $expectedHostName = if ($Family -ieq 'Main') { 'com.rcmusic.media' } else { 'com.rcmusic.youtube' }
    if ($Stream.Length -lt 1 -or $Stream.Length -gt 65536) { throw 'Build receipt must be at most 64 KiB' }
    if ((Get-NativeManifestStreamHash $Stream) -cne $ExpectedSha256) { throw 'Build receipt SHA256 does not match its explicit pin' }
    $bytes = [byte[]]::new([int]$Stream.Length)
    $offset = 0
    while ($offset -lt $bytes.Length) {
        $read = $Stream.Read($bytes, $offset, $bytes.Length - $offset)
        if ($read -eq 0) { throw 'Build receipt ended before its declared size' }
        $offset += $read
    }
    $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes)
    $options = [Text.Json.JsonDocumentOptions]::new()
    $options.MaxDepth = 8
    $options.AllowTrailingCommas = $false
    $options.CommentHandling = [Text.Json.JsonCommentHandling]::Disallow
    $document = [Text.Json.JsonDocument]::Parse($text, $options)
    try {
        $root = $document.RootElement
        Assert-NativeManifestObject $root @('schema_version', 'component', 'protocol', 'host_name', 'origin', 'endpoint',
            'enabled', 'owner_images', 'host_images', 'executable', 'configuration')
        $schema = $root.GetProperty('schema_version')
        if ($schema.ValueKind -ne [Text.Json.JsonValueKind]::Number -or $schema.GetRawText() -cne '1' -or
            (Get-NativeManifestString ($root.GetProperty('component'))) -cne 'rcmusic-native-host-build-profile' -or
            (Get-NativeManifestString ($root.GetProperty('protocol'))) -cne $expectedProtocol -or
            (Get-NativeManifestString ($root.GetProperty('host_name'))) -cne $expectedHostName -or
            $root.GetProperty('enabled').ValueKind -ne [Text.Json.JsonValueKind]::True) {
            throw "An enabled $Family native-host build profile is required"
        }
        $origin = Get-NativeManifestString ($root.GetProperty('origin'))
        $endpoint = Get-NativeManifestString ($root.GetProperty('endpoint'))
        if ($origin -cnotmatch '^chrome-extension://[a-p]{32}/\z' -or $endpoint -cnotmatch '^[A-Za-z0-9_-]{1,96}\z') {
            throw 'Build profile origin or endpoint is invalid'
        }
        $ownerImages = @(Get-NativeManifestImages ($root.GetProperty('owner_images')))
        $hostImages = @(Get-NativeManifestImages ($root.GetProperty('host_images')))
        $executable = $root.GetProperty('executable')
        Assert-NativeManifestObject $executable @('path', 'sha256', 'size_bytes')
        $exePath = Get-NativeManifestLocalPath (Get-NativeManifestString ($executable.GetProperty('path')))
        $exeHash = Get-NativeManifestSha256 ($executable.GetProperty('sha256'))
        $sizeElement = $executable.GetProperty('size_bytes')
        [long]$size = 0
        if ($sizeElement.ValueKind -ne [Text.Json.JsonValueKind]::Number -or
            $sizeElement.GetRawText() -cnotmatch '^[1-9][0-9]*\z' -or -not $sizeElement.TryGetInt64([ref]$size)) {
            throw 'Build profile executable size must be a positive integer'
        }
        if ([IO.Path]::GetFileName($exePath) -inotin $hostImages) { throw 'Build profile host image does not match its executable' }
        $configuration = $root.GetProperty('configuration')
        Assert-NativeManifestObject $configuration @('path', 'sha256')
        $configPath = Get-NativeManifestLocalPath (Get-NativeManifestString ($configuration.GetProperty('path')))
        $configHash = Get-NativeManifestSha256 ($configuration.GetProperty('sha256'))
        return [pscustomobject]@{
            protocol = $expectedProtocol; host_name = $expectedHostName
            origin = $origin; endpoint = $endpoint; owner_images = $ownerImages; host_images = $hostImages
            executable = [pscustomobject]@{ path = $exePath; sha256 = $exeHash; size_bytes = $size }
            configuration = [pscustomobject]@{ path = $configPath; sha256 = $configHash }
        }
    } finally { $document.Dispose() }
}

function Get-NativeManifestPlan {
    param([string] $RepositoryRoot, [string] $BuildReceipt, [string] $BuildReceiptSha256, [string] $ExtensionId,
        [string] $HostExecutable, [string] $OutputDirectory, [switch] $AllowExistingOutput)
    if ($BuildReceiptSha256 -cnotmatch '^[0-9a-f]{64}\z') { throw 'An explicit lowercase receipt SHA256 is required' }
    if ($ExtensionId -cnotmatch '^[a-p]{32}\z') { throw 'An explicit 32-letter extension ID is required' }
    $root = Get-NativeManifestLocalPath $RepositoryRoot
    $receiptPath = Get-NativeManifestLocalPath $BuildReceipt
    $hostPath = Get-NativeManifestLocalPath $HostExecutable
    $output = Get-NativeManifestLocalPath $OutputDirectory
    Assert-NativeManifestPlainPath $root
    Assert-NativeManifestPlainPath $output
    if (-not [IO.Directory]::Exists($root) -or -not [string]::Equals($script:modulePath,
            (Join-Path $root 'packaging/windows/RcMusicYoutubeNativeManifest.psm1'), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The loaded Native manifest recipe must belong to its source repository'
    }
    $prefix = (Join-Path $root 'out/product') + [IO.Path]::DirectorySeparatorChar
    if (-not $output.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Native manifest output must be a fresh directory below repository out/product'
    }
    if (-not $AllowExistingOutput -and (Test-Path -LiteralPath $output)) { throw 'Native manifest output already exists' }
    $streams = [Collections.Generic.List[IO.Stream]]::new()
    try {
        $receiptStream = Open-NativeManifestInput $receiptPath
        $streams.Add($receiptStream)
        $profile = Read-NativeManifestBuildProfile $receiptStream $BuildReceiptSha256
        $origin = 'chrome-extension://' + $ExtensionId + '/'
        if ($profile.origin -cne $origin) { throw 'Extension ID does not match the pinned build profile origin' }
        if (-not [string]::Equals([IO.Path]::GetFileName($hostPath), [IO.Path]::GetFileName($profile.executable.path),
                [StringComparison]::OrdinalIgnoreCase)) { throw 'Selected host basename does not match the build profile' }
        $hostStream = Open-NativeManifestInput $hostPath
        $streams.Add($hostStream)
        if ($hostStream.Length -ne $profile.executable.size_bytes -or
            (Get-NativeManifestStreamHash $hostStream) -cne $profile.executable.sha256) {
            throw 'Selected host bytes do not match the pinned build profile'
        }
        $configurationPresent = $false
        Assert-NativeManifestPlainPath $profile.configuration.path
        if ([IO.Directory]::Exists($profile.configuration.path)) { throw 'Build configuration path is a directory' }
        if ([IO.File]::Exists($profile.configuration.path)) {
            $configurationStream = Open-NativeManifestInput $profile.configuration.path
            $streams.Add($configurationStream)
            if ((Get-NativeManifestStreamHash $configurationStream) -cne $profile.configuration.sha256) {
                throw 'Existing build configuration no longer matches its recorded hash'
            }
            $configurationPresent = $true
        }
        $recipes = @(foreach ($name in $script:recipeNames) {
            $path = Get-NativeManifestLocalPath (Join-Path $root ('packaging/windows/' + $name))
            $stream = Open-NativeManifestInput $path
            $streams.Add($stream)
            [pscustomobject]@{ path = ('packaging/windows/' + $name); size_bytes = $stream.Length
                sha256 = (Get-NativeManifestStreamHash $stream) }
        })
        return [pscustomobject]@{
            schema_version = 1
            component = 'youtube-native-manifest-candidate'
            installation_state = 'installation_pending'
            repository_root = $root
            output = $output
            extension_id = $ExtensionId
            origin = $origin
            endpoint = $profile.endpoint
            build_receipt = [pscustomobject]@{ path = $receiptPath; sha256 = $BuildReceiptSha256; size_bytes = $receiptStream.Length }
            host_executable = [pscustomobject]@{ path = $hostPath; sha256 = $profile.executable.sha256; size_bytes = $hostStream.Length }
            original_executable_path = $profile.executable.path
            owner_images = $profile.owner_images
            host_images = $profile.host_images
            configuration = $profile.configuration
            configuration_present = $configurationPresent
            recipe_files = $recipes
            native_manifest = [pscustomobject]@{
                name = 'com.rcmusic.youtube'
                description = 'RC Music YouTube audio extension host'
                path = $hostPath
                type = 'stdio'
                allowed_origins = @($origin)
            }
        }
    } finally { foreach ($stream in $streams) { $stream.Dispose() } }
}

function Assert-NativeManifestPlanMatches([object] $Plan, [object] $Current) {
    if ((ConvertTo-Json -InputObject $Plan -Depth 10 -Compress) -cne
        (ConvertTo-Json -InputObject $Current -Depth 10 -Compress)) {
        throw 'Native manifest plan changed or its input snapshot is stale'
    }
}

function Write-NativeManifestCandidateJson([string] $Path, [object] $Value) {
    $bytes = [Text.UTF8Encoding]::new($false, $true).GetBytes((ConvertTo-Json -InputObject $Value -Depth 10) + "`n")
    $stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try {
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush($true)
        return [pscustomobject]@{ sha256 = (Get-NativeManifestStreamHash $stream); size_bytes = $stream.Length }
    } finally { $stream.Dispose() }
}

function New-RcMusicYoutubeNativeManifestPlan {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $RepositoryRoot,
        [Parameter(Mandatory)][string] $BuildReceipt,
        [Parameter(Mandatory)][string] $BuildReceiptSha256,
        [Parameter(Mandatory)][string] $ExtensionId,
        [Parameter(Mandatory)][string] $HostExecutable,
        [Parameter(Mandatory)][string] $OutputDirectory
    )
    return Get-NativeManifestPlan $RepositoryRoot $BuildReceipt $BuildReceiptSha256 $ExtensionId $HostExecutable $OutputDirectory
}

function Copy-RcMusicYoutubeNativeManifestPlan {
    [CmdletBinding()]
    param([Parameter(Mandatory)][object] $Plan)
    $current = Get-NativeManifestPlan $Plan.repository_root $Plan.build_receipt.path $Plan.build_receipt.sha256 `
        $Plan.extension_id $Plan.host_executable.path $Plan.output
    Assert-NativeManifestPlanMatches $Plan $current
    $streams = [Collections.Generic.List[IO.Stream]]::new()
    try {
        $paths = @($current.build_receipt.path, $current.host_executable.path)
        if ($current.configuration_present) { $paths += $current.configuration.path }
        $paths += @($current.recipe_files | ForEach-Object { Join-Path $current.repository_root $_.path })
        foreach ($path in $paths) { $streams.Add((Open-NativeManifestInput $path)) }
        $current = Get-NativeManifestPlan $Plan.repository_root $Plan.build_receipt.path $Plan.build_receipt.sha256 `
            $Plan.extension_id $Plan.host_executable.path $Plan.output
        Assert-NativeManifestPlanMatches $Plan $current
        $null = New-Item -ItemType Directory -Path $current.output -ErrorAction Stop
        $manifestPath = Join-Path $current.output 'com.rcmusic.youtube.json'
        $manifestFile = Write-NativeManifestCandidateJson $manifestPath $current.native_manifest
        $after = Get-NativeManifestPlan $current.repository_root $current.build_receipt.path $current.build_receipt.sha256 `
            $current.extension_id $current.host_executable.path $current.output -AllowExistingOutput
        Assert-NativeManifestPlanMatches $current $after
        $receipt = [ordered]@{
            schema_version = 1
            component = $current.component
            installation_state = 'installation_pending'
            extension_id = $current.extension_id
            origin = $current.origin
            endpoint = $current.endpoint
            build_receipt = $current.build_receipt
            host_executable = $current.host_executable
            original_executable_path = $current.original_executable_path
            owner_images = $current.owner_images
            host_images = $current.host_images
            configuration = $current.configuration
            configuration_present = $current.configuration_present
            recipe_files = $current.recipe_files
            manifest = [ordered]@{ relative_path = 'com.rcmusic.youtube.json'; sha256 = $manifestFile.sha256; size_bytes = $manifestFile.size_bytes }
        }
        $receiptPath = Join-Path $current.output 'native-manifest-receipt.json'
        $null = Write-NativeManifestCandidateJson $receiptPath $receipt
        return [pscustomobject]@{ package_directory = $current.output; manifest_path = $manifestPath; receipt_path = $receiptPath }
    } finally { foreach ($stream in $streams) { $stream.Dispose() } }
}

Export-ModuleMember -Function New-RcMusicYoutubeNativeManifestPlan, Copy-RcMusicYoutubeNativeManifestPlan, Read-RcMusicNativeHostBuildProfile
