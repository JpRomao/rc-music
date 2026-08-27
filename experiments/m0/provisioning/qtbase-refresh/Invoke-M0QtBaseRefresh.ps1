#Requires -Version 7.0

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:ContractFileName = 'qtbase-refresh-contract.v2.json'
$script:SidecarFileName = 'qtbase-refresh-contract.v2.json.sha256'
$script:OrchestratorFileName = 'M0.QtBaseRefresh.psm1'
$script:ExpectedRecipe = @(
    @('orchestrator', 'experiments/m0/provisioning/qtbase-refresh/M0.QtBaseRefresh.psm1'),
    @('runner', 'experiments/m0/provisioning/qtbase-refresh/M0.QtBaseRefreshNative.psm1'),
    @('entrypoint', 'experiments/m0/provisioning/qtbase-refresh/Invoke-M0QtBaseRefresh.ps1')
)

function Get-M0BootstrapSha256 {
    param([Parameter(Mandatory)] [IO.Stream] $Stream)
    $position = $Stream.Position
    try {
        $Stream.Position = 0
        $algorithm = [Security.Cryptography.SHA256]::Create()
        try { return [Convert]::ToHexString($algorithm.ComputeHash($Stream)).ToLowerInvariant() }
        finally { $algorithm.Dispose() }
    }
    finally { $Stream.Position = $position }
}

function Assert-M0BootstrapNoDuplicateProperties {
    param(
        [Parameter(Mandatory)] [Text.Json.JsonElement] $Element,
        [Parameter(Mandatory)] [string] $Context
    )

    if ($Element.ValueKind -eq [Text.Json.JsonValueKind]::Object) {
        $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($property in $Element.EnumerateObject()) {
            if (-not $seen.Add($property.Name)) {
                throw "$Context contains duplicate JSON property '$($property.Name)'."
            }
            Assert-M0BootstrapNoDuplicateProperties $property.Value "$Context.$($property.Name)"
        }
    }
    elseif ($Element.ValueKind -eq [Text.Json.JsonValueKind]::Array) {
        $index = 0
        foreach ($item in $Element.EnumerateArray()) {
            Assert-M0BootstrapNoDuplicateProperties $item "$Context[$index]"
            $index++
        }
    }
}

function Read-M0BootstrapUtf8 {
    param(
        [Parameter(Mandatory)] [byte[]] $Bytes,
        [Parameter(Mandatory)] [string] $Context
    )

    if ($Bytes.Length -eq 0 -or
        ($Bytes.Length -ge 3 -and $Bytes[0] -eq 0xef -and $Bytes[1] -eq 0xbb -and $Bytes[2] -eq 0xbf)) {
        throw "$Context must be non-empty UTF-8 without BOM."
    }
    try { return [Text.UTF8Encoding]::new($false, $true).GetString($Bytes) }
    catch { throw [InvalidDataException]::new("$Context must be strict UTF-8.", $_.Exception) }
}

function Assert-M0BootstrapContainedPath {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [Parameter(Mandatory)] [string] $Context
    )

    $root = [IO.Path]::GetFullPath($BoundaryRoot).TrimEnd([IO.Path]::DirectorySeparatorChar)
    $full = [IO.Path]::GetFullPath($LiteralPath)
    $prefix = $root + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Context escaped the repository root."
    }
    $current = $full
    while ($true) {
        $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "$Context contains a reparse point."
        }
        if ([string]::Equals($item.FullName, $root, [StringComparison]::OrdinalIgnoreCase)) { break }
        $current = [IO.Path]::GetDirectoryName($item.FullName)
        if ([string]::IsNullOrEmpty($current) -or $current.Length -lt $root.Length) {
            throw "$Context physical ancestry escaped the repository root."
        }
    }
    return $full
}

function Open-M0BootstrapRecipeLease {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [long] $ExpectedSizeBytes,
        [Parameter(Mandatory)] [string] $ExpectedSha256,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [string] $Context
    )

    if ($ExpectedSizeBytes -le 0 -or $ExpectedSha256 -cnotmatch '\A[0-9a-f]{64}\z') {
        throw "$Context identity is invalid."
    }
    $path = Assert-M0BootstrapContainedPath $LiteralPath $RepositoryRoot $Context
    $item = Get-Item -LiteralPath $path -Force -ErrorAction Stop
    if ($item -isnot [IO.FileInfo]) { throw "$Context is not a regular file." }
    foreach ($alternateStream in @(Get-Item -LiteralPath $path -Stream * -Force -ErrorAction Stop)) {
        if ($alternateStream.Stream -cne ':$DATA') { throw "$Context contains an alternate data stream." }
    }
    $stream = [IO.FileStream]::new(
        $path,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::Read,
        4096,
        [IO.FileOptions]::SequentialScan
    )
    try {
        if ($stream.Length -ne $ExpectedSizeBytes -or (Get-M0BootstrapSha256 $stream) -cne $ExpectedSha256) {
            throw "$Context bytes differ from the externally authenticated contract."
        }
        return [pscustomobject]@{ path = $path; stream = $stream }
    }
    catch {
        $stream.Dispose()
        throw
    }
}

if (-not $IsWindows) { throw 'The QtBase refresh production launcher is proven only on Windows.' }

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..')).TrimEnd([IO.Path]::DirectorySeparatorChar)
$contractPath = Join-Path $PSScriptRoot $script:ContractFileName
$sidecarPath = Join-Path $PSScriptRoot $script:SidecarFileName
$orchestratorPath = Join-Path $PSScriptRoot $script:OrchestratorFileName

$sidecarBytes = [IO.File]::ReadAllBytes($sidecarPath)
$sidecarText = Read-M0BootstrapUtf8 $sidecarBytes 'QtBase refresh v2 sidecar'
$sidecarPattern = '\A(?<sha256>[0-9a-f]{64}) \*qtbase-refresh-contract\.v2\.json\n\z'
$sidecarMatch = [Text.RegularExpressions.Regex]::Match(
    $sidecarText,
    $sidecarPattern,
    [Text.RegularExpressions.RegexOptions]::CultureInvariant
)
if (-not $sidecarMatch.Success) { throw 'QtBase refresh v2 sidecar has a noncanonical format.' }
$expectedContractSha256 = $sidecarMatch.Groups['sha256'].Value

$contractItem = Get-Item -LiteralPath $contractPath -Force -ErrorAction Stop
$contractLease = Open-M0BootstrapRecipeLease `
    -LiteralPath $contractPath `
    -ExpectedSizeBytes ([long]$contractItem.Length) `
    -ExpectedSha256 $expectedContractSha256 `
    -RepositoryRoot $repositoryRoot `
    -Context 'QtBase refresh v2 contract'
$recipeLeases = [Collections.Generic.List[object]]::new()
$orchestratorModule = $null
try {
    $contractBytes = [byte[]]::new($contractLease.stream.Length)
    $contractLease.stream.Position = 0
    $offset = 0
    while ($offset -lt $contractBytes.Length) {
        $read = $contractLease.stream.Read($contractBytes, $offset, $contractBytes.Length - $offset)
        if ($read -eq 0) { throw 'QtBase refresh v2 contract ended before its authenticated size.' }
        $offset += $read
    }
    $contractText = Read-M0BootstrapUtf8 $contractBytes 'QtBase refresh v2 contract'
    $jsonDocument = [Text.Json.JsonDocument]::Parse($contractText)
    try { Assert-M0BootstrapNoDuplicateProperties $jsonDocument.RootElement 'contract' }
    finally { $jsonDocument.Dispose() }
    $contract = $contractText | ConvertFrom-Json -Depth 100 -DateKind String

    if ($contract.schema_version -isnot [long] -or $contract.schema_version -ne 2 -or
        $contract.recipe -isnot [pscustomobject] -or $contract.recipe.runner_export -cne 'Invoke-M0QtBaseRefreshNative' -or
        $contract.recipe.files -isnot [Array] -or @($contract.recipe.files).Count -ne $script:ExpectedRecipe.Count) {
        throw 'QtBase refresh v2 contract does not contain the closed production recipe.'
    }

    for ($index = 0; $index -lt $script:ExpectedRecipe.Count; $index++) {
        $spec = @($contract.recipe.files)[$index]
        $expected = $script:ExpectedRecipe[$index]
        if ($spec -isnot [pscustomobject] -or $spec.id -isnot [string] -or $spec.id -cne $expected[0] -or
            $spec.relative_path -isnot [string] -or $spec.relative_path -cne $expected[1] -or
            $spec.size_bytes -isnot [long] -or $spec.sha256 -isnot [string]) {
            throw "QtBase refresh recipe entry $index drifted from the closed production layout."
        }
        $candidate = Join-Path $repositoryRoot $spec.relative_path
        $lease = Open-M0BootstrapRecipeLease `
            -LiteralPath $candidate `
            -ExpectedSizeBytes ([long]$spec.size_bytes) `
            -ExpectedSha256 $spec.sha256 `
            -RepositoryRoot $repositoryRoot `
            -Context "QtBase refresh recipe '$($spec.id)'"
        $recipeLeases.Add($lease)
    }

    $entrypointLease = $recipeLeases[2]
    if (-not [string]::Equals($entrypointLease.path, [IO.Path]::GetFullPath($PSCommandPath), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The authenticated entrypoint is not the launcher currently executing.'
    }
    if (-not [string]::Equals($recipeLeases[0].path, [IO.Path]::GetFullPath($orchestratorPath), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The authenticated orchestrator path drifted from the fixed launcher binding.'
    }

    $imported = @(Import-Module -Name $recipeLeases[0].path -Force -PassThru)
    if ($imported.Count -ne 1) { throw 'The authenticated orchestrator import did not yield exactly one module.' }
    $orchestratorModule = $imported[0]
    $exports = @($orchestratorModule.ExportedFunctions.Keys | Sort-Object)
    $expectedExports = @('Assert-M0QtBaseRefreshContract', 'Assert-M0QtBaseRefreshReceipt', 'Invoke-M0QtBaseRefresh')
    if ((ConvertTo-Json $exports -Compress) -cne (ConvertTo-Json $expectedExports -Compress)) {
        throw 'The authenticated orchestrator export set drifted.'
    }

    $validatedContract = & $orchestratorModule.ExportedFunctions['Assert-M0QtBaseRefreshContract'] `
        -LiteralPath $contractLease.path `
        -ExpectedSha256 $expectedContractSha256
    $runParent = Join-Path $repositoryRoot $validatedContract.isolation.run_parent_relative_path
    if (-not [IO.Directory]::Exists($runParent)) {
        throw "Pinned isolated RunRoot parent does not exist: $runParent"
    }
    $runName = [Guid]::NewGuid().ToString('N')
    $runRoot = Join-Path $runParent $runName
    $windowsFolder = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
    $toolRoot = [IO.Path]::GetPathRoot($windowsFolder)

    & $orchestratorModule.ExportedFunctions['Invoke-M0QtBaseRefresh'] `
        -ContractPath $contractLease.path `
        -ExpectedContractSha256 $expectedContractSha256 `
        -RepositoryRoot $repositoryRoot `
        -ToolRoot $toolRoot `
        -RunRoot $runRoot
}
finally {
    if ($null -ne $orchestratorModule) {
        try { Remove-Module -ModuleInfo $orchestratorModule -Force } catch { }
    }
    for ($index = $recipeLeases.Count - 1; $index -ge 0; $index--) {
        try { $recipeLeases[$index].stream.Dispose() } catch { }
    }
    $contractLease.stream.Dispose()
}
