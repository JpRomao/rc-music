#requires -Version 7.2
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# This is an inventory of a quiescent, locally owned Windows staging tree, not a sandbox.
# Paths, reparse flags, LinkType and streams are inspected before hashing each file. A read
# handle protects that individual hash/size read, but no identity/handle lease spans the
# whole tree or a later launch. Concurrent replacement (including ancestors) is outside
# this contract; the caller must prevent writers and must not claim TOCTOU-safe execution.
# Nothing here executes payloads, writes a manifest or decides metadata/licensing policy.

function Assert-QtPackageRelativePath([string] $Path) {
    if ([string]::IsNullOrEmpty($Path) -or $Path -match '[\\:\x00-\x1f<>"|?*]') {
        throw 'Invalid relative path in the package inventory.'
    }
    foreach ($component in $Path.Split('/')) {
        $device = $component.Split('.')[0].TrimEnd(' ')
        if ([string]::IsNullOrEmpty($component) -or $component -in @('.', '..') -or
            $component -match '[. ]$' -or
            $device -match '^(?i:CON|PRN|AUX|NUL|CONIN\$|CONOUT\$|COM[1-9¹²³]|LPT[1-9¹²³])$') {
            throw 'Invalid or reserved relative path in the package inventory.'
        }
    }
}

function Resolve-QtPackagePath([string] $Path, [ValidateSet('directory', 'file')][string] $Kind) {
    if (-not $IsWindows) { throw 'Windows package inventory requires PowerShell on Windows.' }
    if ([string]::IsNullOrWhiteSpace($Path)) { throw 'A package path is required.' }
    $candidate = $Path.Replace('/', '\')
    if ($candidate -match '^[A-Za-z]:\\') {
        $relative = $candidate.Substring(3)
    }
    else {
        # UNC, device namespaces, provider paths and drive-relative paths are not local stage paths.
        if ([IO.Path]::IsPathRooted($candidate) -or $candidate.Contains(':')) { throw 'Invalid local package path.' }
        $relative = $candidate
        $location = Get-Location
        if ($location.Provider.Name -ne 'FileSystem') { throw 'A filesystem location is required for a relative package path.' }
        $candidate = Join-Path $location.ProviderPath $candidate
    }
    if ($relative.EndsWith('\', [StringComparison]::Ordinal)) { $relative = $relative.Substring(0, $relative.Length - 1) }
    Assert-QtPackageRelativePath $relative.Replace('\', '/')
    $full = [IO.Path]::TrimEndingDirectorySeparator([IO.Path]::GetFullPath($candidate))
    $ancestor = $full
    while ($ancestor) {
        $item = Get-Item -LiteralPath $ancestor -Force -ErrorAction Stop
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'A package path has a reparse point or ancestor.'
        }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
    $leaf = Get-Item -LiteralPath $full -Force -ErrorAction Stop
    if (($Kind -eq 'directory' -and $leaf -isnot [IO.DirectoryInfo]) -or
        ($Kind -eq 'file' -and $leaf -isnot [IO.FileInfo])) {
        throw "The package path must be a regular $Kind."
    }
    $leaf.FullName
}

function Assert-QtPackageItem([IO.FileSystemInfo] $Item) {
    if (($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Package inventory contains a reparse point.'
    }
    if ($Item.LinkType -eq 'HardLink') { throw 'Package inventory contains a hardlink.' }
    # Get-Item -Stream also inspects directories in PowerShell 7.2+. Fail closed on I/O errors.
    foreach ($stream in @(Get-Item -LiteralPath $Item.FullName -Stream * -Force -ErrorAction Stop)) {
        if ($stream.Stream -cne ':$DATA') { throw 'Package inventory contains an alternate data stream.' }
    }
}

function Get-QtPackageInventory([string] $Root) {
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push($Root)
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $files = [Collections.Generic.SortedDictionary[string, object]]::new([StringComparer]::Ordinal)
    while ($pending.Count -gt 0) {
        $directory = Get-Item -LiteralPath $pending.Pop() -Force -ErrorAction Stop
        Assert-QtPackageItem $directory
        # No -Recurse: inspect and reject each link before deciding whether to descend.
        foreach ($item in @(Get-ChildItem -LiteralPath $directory.FullName -Force -ErrorAction Stop)) {
            # GetRelativePath can normalize names that Win32 would trim; validate the raw leaf first.
            Assert-QtPackageRelativePath $item.Name
            $relative = [IO.Path]::GetRelativePath($Root, $item.FullName).Replace('\', '/')
            Assert-QtPackageRelativePath $relative
            if (-not $seen.Add($relative)) { throw 'Package inventory has duplicate case-insensitive paths.' }
            Assert-QtPackageItem $item
            if ($item -is [IO.DirectoryInfo]) {
                $pending.Push($item.FullName)
                continue
            }
            if ($item -isnot [IO.FileInfo]) { throw 'Package inventory contains a nonregular file.' }
            $reader = [IO.File]::Open($item.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
            try {
                $size = $reader.Length
                $algorithm = [Security.Cryptography.SHA256]::Create()
                try { $hash = [Convert]::ToHexString($algorithm.ComputeHash($reader)).ToLowerInvariant() }
                finally { $algorithm.Dispose() }
            }
            finally { $reader.Dispose() }
            $files.Add($relative, [pscustomobject][ordered]@{
                relative_path = $relative
                size_bytes = $size
                sha256 = $hash
            })
        }
    }
    $result = [object[]]::new($files.Count)
    $files.Values.CopyTo($result, 0)
    return ,$result
}

function Assert-QtPackageJsonFields([Text.Json.JsonElement] $Value, [string[]] $Names, [string] $Context) {
    if ($Value.ValueKind -ne [Text.Json.JsonValueKind]::Object) { throw "$Context must be a JSON object." }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($property in $Value.EnumerateObject()) {
        if ($Names -cnotcontains $property.Name -or -not $seen.Add($property.Name)) {
            throw "$Context fields are closed and cannot be duplicated."
        }
    }
    if ($seen.Count -ne $Names.Count) { throw "$Context fields are incomplete." }
}

function ConvertFrom-QtPackageMetadata([Text.Json.JsonElement] $Value) {
    # Do not let ConvertFrom-Json coerce metadata timestamp strings to DateTime values.
    switch ($Value.ValueKind) {
        Object {
            $result = [Collections.Specialized.OrderedDictionary]::new([StringComparer]::Ordinal)
            foreach ($property in $Value.EnumerateObject()) {
                $result.Add($property.Name, (ConvertFrom-QtPackageMetadata $property.Value))
            }
            return $result
        }
        Array {
            $result = [Collections.Generic.List[object]]::new()
            foreach ($item in $Value.EnumerateArray()) { $result.Add((ConvertFrom-QtPackageMetadata $item)) }
            return ,$result.ToArray()
        }
        String { return $Value.GetString() }
        Number { return (ConvertFrom-Json -InputObject $Value.GetRawText() -NoEnumerate) }
        True { return $true }
        False { return $false }
        Null { return $null }
        default { throw 'Invalid JSON metadata value.' }
    }
}

function New-QtShellPackageManifest {
    <#
    .SYNOPSIS
    Inventories an unchanged local Windows payload without writing or executing anything.
    .DESCRIPTION
    Returns schema_version, the supplied metadata object and ordinal files with relative '/' paths.
    Metadata content is the builder's responsibility. Serialize the result outside PackageRoot.
    The tree must remain quiescent; this function does not retain a tree-wide identity lease.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $PackageRoot,
        [Parameter(Mandatory)][ValidateNotNull()][object] $Metadata
    )

    if ($Metadata -isnot [Collections.IDictionary] -and $Metadata -isnot [pscustomobject]) {
        throw 'Package metadata must be an object.'
    }
    $root = Resolve-QtPackagePath $PackageRoot directory
    [pscustomobject][ordered]@{
        schema_version = 1
        metadata = $Metadata
        files = Get-QtPackageInventory $root
    }
}

function Test-QtShellPackageManifest {
    <#
    .SYNOPSIS
    Returns the validated manifest or throws a terminating error; never executes the payload.
    .DESCRIPTION
    Requires an external UTF-8 JSON manifest and the exact unchanged file inventory. An optional
    ExpectedManifestSha256 anchors its bytes; without one this checks consistency, not provenance.
    Windows paths must be local, ordinary filesystem paths. Reparse points, alternate streams and
    hardlinks are rejected. Concurrent changes during verification or before launch are out of scope.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $PackageRoot,
        [Parameter(Mandatory)][string] $ManifestPath,
        [ValidatePattern('^[a-fA-F0-9]{64}$')][string] $ExpectedManifestSha256
    )

    $root = Resolve-QtPackagePath $PackageRoot directory
    $manifestFile = Resolve-QtPackagePath $ManifestPath file
    if ($manifestFile.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The manifest must be outside the payload.'
    }
    Assert-QtPackageItem (Get-Item -LiteralPath $manifestFile -Force -ErrorAction Stop)
    $bytes = [IO.File]::ReadAllBytes($manifestFile)
    if ($PSBoundParameters.ContainsKey('ExpectedManifestSha256')) {
        $actualHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))
        if (-not [StringComparer]::OrdinalIgnoreCase.Equals($actualHash, $ExpectedManifestSha256)) {
            throw 'External manifest SHA-256 does not match the expected digest.'
        }
    }
    try {
        $json = [Text.UTF8Encoding]::new($false, $true).GetString($bytes)
        if ($json.StartsWith([string][char]0xfeff, [StringComparison]::Ordinal)) { $json = $json.Substring(1) }
        $document = [Text.Json.JsonDocument]::Parse($json)
    }
    catch { throw "Invalid UTF-8 JSON manifest: $($_.Exception.Message)" }
    try {
        $top = $document.RootElement
        Assert-QtPackageJsonFields $top @('schema_version', 'metadata', 'files') 'Manifest'
        $version = $top.GetProperty('schema_version')
        $integer = 0L
        if ($version.ValueKind -ne [Text.Json.JsonValueKind]::Number -or
            -not $version.TryGetInt64([ref] $integer) -or $integer -ne 1) {
            throw 'Manifest schema_version must be integer 1.'
        }
        $metadata = $top.GetProperty('metadata')
        if ($metadata.ValueKind -ne [Text.Json.JsonValueKind]::Object) { throw 'Manifest metadata must be an object.' }
        $entries = $top.GetProperty('files')
        if ($entries.ValueKind -ne [Text.Json.JsonValueKind]::Array) { throw 'Manifest files must be an array.' }
        $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $files = [Collections.Generic.List[object]]::new()
        $previous = $null
        foreach ($entry in $entries.EnumerateArray()) {
            Assert-QtPackageJsonFields $entry @('relative_path', 'size_bytes', 'sha256') 'Manifest file'
            $pathValue = $entry.GetProperty('relative_path')
            if ($pathValue.ValueKind -ne [Text.Json.JsonValueKind]::String) { throw 'File relative_path must be a string.' }
            $path = $pathValue.GetString()
            Assert-QtPackageRelativePath $path
            if (-not $seen.Add($path)) { throw 'Manifest contains duplicate case-insensitive file paths.' }
            if ($null -ne $previous -and [StringComparer]::Ordinal.Compare($previous, $path) -ge 0) {
                throw 'Manifest files must be in ordinal path order.'
            }
            $previous = $path
            $sizeValue = $entry.GetProperty('size_bytes')
            $size = 0L
            if ($sizeValue.ValueKind -ne [Text.Json.JsonValueKind]::Number -or
                -not $sizeValue.TryGetInt64([ref] $size) -or $size -lt 0) {
                throw 'File size_bytes must be a nonnegative Int64 integer.'
            }
            $hashValue = $entry.GetProperty('sha256')
            if ($hashValue.ValueKind -ne [Text.Json.JsonValueKind]::String -or
                $hashValue.GetString() -cnotmatch '^[a-fA-F0-9]{64}$') {
                throw 'File sha256 must contain exactly 64 hexadecimal characters.'
            }
            $files.Add([pscustomobject][ordered]@{ relative_path = $path; size_bytes = $size; sha256 = $hashValue.GetString() })
        }
        $actualFiles = Get-QtPackageInventory $root
        if ($files.Count -ne $actualFiles.Count) { throw 'Package file inventory does not match the manifest.' }
        for ($index = 0; $index -lt $files.Count; $index++) {
            $expected = $files[$index]
            $actual = $actualFiles[$index]
            if (-not [StringComparer]::Ordinal.Equals($expected.relative_path, $actual.relative_path)) {
                throw 'Package file path does not match the manifest inventory.'
            }
            if ($expected.size_bytes -ne $actual.size_bytes) { throw "Package file size does not match: $($actual.relative_path)" }
            if (-not [StringComparer]::OrdinalIgnoreCase.Equals($expected.sha256, $actual.sha256)) {
                throw "Package file SHA-256 does not match: $($actual.relative_path)"
            }
        }
        [pscustomobject][ordered]@{
            schema_version = 1
            metadata = ConvertFrom-QtPackageMetadata $metadata
            files = $files.ToArray()
        }
    }
    finally { $document.Dispose() }
}

Export-ModuleMember -Function New-QtShellPackageManifest, Test-QtShellPackageManifest
