#Requires -Version 7.0

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$contractRoot = Split-Path -Parent $PSScriptRoot
$modulePath = Join-Path $contractRoot 'M0.QtShellCompositeNative.psm1'
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('m0-qt-shell-composite-native-tests-' + [Guid]::NewGuid().ToString('N'))
$state = [ordered]@{ Passed = 0; Failed = 0 }
$module = $null

function Test-Case {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [scriptblock] $Body
    )

    try {
        & $Body
        $state.Passed++
        Write-Host "PASS: $Name"
    }
    catch {
        $state.Failed++
        Write-Host "FAIL: $Name`n$($_.Exception.ToString())" -ForegroundColor Red
    }
}

function Assert-True {
    param(
        [Parameter(Mandatory)] [bool] $Condition,
        [Parameter(Mandatory)] [string] $Message
    )

    if (-not $Condition) { throw $Message }
}

function Assert-Equal {
    param(
        $Actual,
        $Expected,
        [string] $Message = 'Values differ.'
    )

    if ($Actual -is [array] -or $Expected -is [array]) {
        $actualValues = @($Actual)
        $expectedValues = @($Expected)
        if ($actualValues.Count -ne $expectedValues.Count) {
            throw "$Message Expected $($expectedValues.Count) values, got $($actualValues.Count)."
        }
        for ($index = 0; $index -lt $expectedValues.Count; $index++) {
            if ($actualValues[$index] -cne $expectedValues[$index]) {
                throw "$Message Difference at index ${index}: '$($actualValues[$index])' != '$($expectedValues[$index])'."
            }
        }
        return
    }

    if ($Actual -cne $Expected) { throw "$Message '$Actual' != '$Expected'." }
}

function Assert-Rejected {
    param(
        [Parameter(Mandatory)] [scriptblock] $Body,
        [Parameter(Mandatory)] [string] $Pattern
    )

    try { & $Body }
    catch {
        if ($_.Exception.ToString() -notmatch $Pattern) {
            throw "Expected rejection matching '$Pattern', got: $($_.Exception.ToString())"
        }
        return
    }

    throw "Expected rejection matching '$Pattern', but the operation succeeded."
}

function Write-TestBytes {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [byte[]] $Bytes
    )

    [IO.Directory]::CreateDirectory((Split-Path -Parent $LiteralPath)) | Out-Null
    [IO.File]::WriteAllBytes($LiteralPath, $Bytes)
}

function Get-TestSha256File {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    return (Get-FileHash -LiteralPath $LiteralPath -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-TestSha256Text {
    param([Parameter(Mandatory)] [string] $Text)

    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($Text)
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
}

Test-Case 'native Qt shell composite implementation exists' {
    if (-not [IO.File]::Exists($modulePath)) {
        throw [IO.FileNotFoundException]::new("Native composite implementation is missing: $modulePath")
    }
}

if (-not [IO.File]::Exists($modulePath)) {
    if ([IO.Directory]::Exists($testRoot)) { [IO.Directory]::Delete($testRoot, $true) }
    Write-Host "Tests: $($state.Passed) passed, $($state.Failed) failed."
    exit 1
}

if (-not $IsWindows) {
    throw [PlatformNotSupportedException]::new(
        'QtShellCompositeNative.Tests.ps1 requires Windows and real NTFS link, reparse-point, and ADS semantics.')
}

if (-not ('M0QtShellCompositeNativeTests.FileIdentity' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace M0QtShellCompositeNativeTests {
    [StructLayout(LayoutKind.Sequential)]
    public struct BY_HANDLE_FILE_INFORMATION {
        public uint FileAttributes;
        public System.Runtime.InteropServices.ComTypes.FILETIME CreationTime;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastAccessTime;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastWriteTime;
        public uint VolumeSerialNumber;
        public uint FileSizeHigh;
        public uint FileSizeLow;
        public uint NumberOfLinks;
        public uint FileIndexHigh;
        public uint FileIndexLow;
    }

    public static class FileIdentity {
        private const uint GENERIC_READ = 0x80000000;
        private const uint FILE_SHARE_READ = 0x00000001;
        private const uint FILE_SHARE_WRITE = 0x00000002;
        private const uint FILE_SHARE_DELETE = 0x00000004;
        private const uint OPEN_EXISTING = 3;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(string path, uint access, uint share,
            IntPtr securityAttributes, uint creationDisposition, uint flagsAndAttributes, IntPtr templateFile);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetFileInformationByHandle(
            SafeFileHandle handle,
            out BY_HANDLE_FILE_INFORMATION information);

        public static SafeFileHandle OpenReadShareAll(string path) {
            var handle = CreateFileW(path, GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static string GetIdentity(SafeFileHandle handle) {
            BY_HANDLE_FILE_INFORMATION information;
            if (!GetFileInformationByHandle(handle, out information)) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            return information.VolumeSerialNumber.ToString("X8") + ":" +
                information.FileIndexHigh.ToString("X8") + information.FileIndexLow.ToString("X8");
        }

        public static uint GetLinkCount(SafeFileHandle handle) {
            BY_HANDLE_FILE_INFORMATION information;
            if (!GetFileInformationByHandle(handle, out information)) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            return information.NumberOfLinks;
        }
    }
}
'@
}
$script:NativeIdentityType = 'M0QtShellCompositeNativeTests.FileIdentity' -as [type]

function Get-TestPhysicalIdentity {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    $handle = $script:NativeIdentityType::OpenReadShareAll([IO.Path]::GetFullPath($LiteralPath))
    try {
        return [pscustomobject][ordered]@{
            identity = $script:NativeIdentityType::GetIdentity($handle)
            link_count = [long]$script:NativeIdentityType::GetLinkCount($handle)
        }
    }
    finally { $handle.Dispose() }
}

function Get-TestSourceSnapshot {
    param([Parameter(Mandatory)] [Collections.IDictionary] $SourceRoots)

    $rows = [Collections.Generic.List[object]]::new()
    foreach ($ownerId in @('qtbase-refresh-v2', 'qtdeclarative', 'qtsvg', 'd3d')) {
        $sourceRoot = [string]$SourceRoots[$ownerId]
        foreach ($file in @(Get-ChildItem -LiteralPath $sourceRoot -File -Force -Recurse | Sort-Object FullName)) {
            $physical = Get-TestPhysicalIdentity $file.FullName
            $rows.Add([pscustomobject][ordered]@{
                    owner_id = $ownerId
                    relative_path = [IO.Path]::GetRelativePath($sourceRoot, $file.FullName).Replace('\', '/')
                    size_bytes = [long]$file.Length
                    sha256 = Get-TestSha256File $file.FullName
                    attributes = [long]$file.Attributes
                    last_write_utc_ticks = [long]$file.LastWriteTimeUtc.Ticks
                    identity = $physical.identity
                    link_count = [long]$physical.link_count
                })
        }
    }

    return ($rows | ConvertTo-Json -Depth 4 -Compress)
}

function Assert-TestSourcesUnchanged {
    param(
        [Parameter(Mandatory)] [Collections.IDictionary] $SourceRoots,
        [Parameter(Mandatory)] [string] $ExpectedSnapshot,
        [Parameter(Mandatory)] [string] $CaseId
    )

    Assert-Equal (Get-TestSourceSnapshot $SourceRoots) $ExpectedSnapshot `
        "Rejected '$CaseId' composition changed an upstream source."
}

function Assert-NoFailedPublication {
    param(
        [Parameter(Mandatory)] [pscustomobject] $Fixture,
        [Parameter(Mandatory)] [string] $CaseId
    )

    Assert-True (-not [IO.Directory]::Exists($Fixture.OutputRoot)) `
        "Rejected '$CaseId' composition left a public output root."
    Assert-True (-not [IO.Directory]::Exists($Fixture.ScratchRoot)) `
        "Rejected '$CaseId' composition left its private scratch root."
}

function New-CompositeFixture {
    param([Parameter(Mandatory)] [string] $Name)

    $root = Join-Path $testRoot $Name
    $sourceParent = Join-Path $root 'upstream'
    $sourceRoots = [ordered]@{
        'qtbase-refresh-v2' = Join-Path $sourceParent 'qtbase-refresh-v2'
        'qtdeclarative' = Join-Path $sourceParent 'qtdeclarative'
        'qtsvg' = Join-Path $sourceParent 'qtsvg'
        'd3d' = Join-Path $sourceParent 'd3d'
    }
    foreach ($sourceRoot in $sourceRoots.Values) {
        [IO.Directory]::CreateDirectory([string]$sourceRoot) | Out-Null
    }
    $outputParent = Join-Path $root 'public'
    $scratchParent = Join-Path $root 'private'
    [IO.Directory]::CreateDirectory($outputParent) | Out-Null
    [IO.Directory]::CreateDirectory($scratchParent) | Out-Null

    $specifications = @(
        [pscustomobject]@{
            owner_id = 'd3d'
            source_relative_path = 'bin/d3dcompiler_47.dll'
            relative_path = 'bin/d3dcompiler_47.dll'
            content = 'd3d-pinned'
        },
        [pscustomobject]@{
            owner_id = 'qtbase-refresh-v2'
            source_relative_path = 'bin/Qt6Core.dll'
            relative_path = 'bin/Qt6Core.dll'
            content = 'qtbase-refreshed'
        },
        [pscustomobject]@{
            owner_id = 'qtdeclarative'
            source_relative_path = 'bin/Qt6Qml.dll'
            relative_path = 'bin/Qt6Qml.dll'
            content = 'qtdeclarative-official'
        },
        [pscustomobject]@{
            owner_id = 'qtsvg'
            source_relative_path = 'bin/Qt6Svg.dll'
            relative_path = 'bin/Qt6Svg.dll'
            content = 'qtsvg-official'
        }
    )

    $ownership = [Collections.Generic.List[object]]::new()
    foreach ($specification in $specifications) {
        $sourcePath = Join-Path ([string]$sourceRoots[$specification.owner_id]) $specification.source_relative_path
        Write-TestBytes $sourcePath ([Text.UTF8Encoding]::new($false).GetBytes([string]$specification.content))
        $item = Get-Item -LiteralPath $sourcePath -Force
        $ownership.Add([pscustomobject][ordered]@{
                owner_id = [string]$specification.owner_id
                source_relative_path = [string]$specification.source_relative_path
                relative_path = [string]$specification.relative_path
                size_bytes = [long]$item.Length
                sha256 = Get-TestSha256File $sourcePath
            })
    }

    return [pscustomobject][ordered]@{
        Root = $root
        SourceRoots = $sourceRoots
        Ownership = @($ownership)
        OutputRoot = Join-Path $outputParent 'published'
        ScratchRoot = Join-Path $scratchParent 'scratch'
    }
}

$productionModuleSource = [IO.File]::ReadAllText($modulePath)
$prepublicationInsertionPoint = '    # TEST-FIXTURE-INSERTION-POINT: COMPOSITE_PREPUBLICATION'
if ($productionModuleSource.IndexOf($prepublicationInsertionPoint, [StringComparison]::Ordinal) -lt 0 -or
    $productionModuleSource.IndexOf($prepublicationInsertionPoint, [StringComparison]::Ordinal) -ne
    $productionModuleSource.LastIndexOf($prepublicationInsertionPoint, [StringComparison]::Ordinal)) {
    throw "Native composite prepublication fixture insertion point is missing or ambiguous: $prepublicationInsertionPoint"
}
$prepublicationFixtureCode = @'
    $testPrepublicationCallback = Get-Variable -Name CompositeNativePrepublicationCallback -Scope Script -ValueOnly -ErrorAction SilentlyContinue
    if ($null -ne $testPrepublicationCallback) {
        & $testPrepublicationCallback $Context
    }
'@
$instrumentedModuleSource = $productionModuleSource.Replace(
    $prepublicationInsertionPoint,
    $prepublicationFixtureCode)
$instrumentedModulePath = Join-Path $testRoot 'module/M0.QtShellCompositeNative.TestFixture.psm1'
Write-TestBytes $instrumentedModulePath ([Text.UTF8Encoding]::new($false).GetBytes($instrumentedModuleSource))
$module = Import-Module -Name $instrumentedModulePath -Force -PassThru

function Set-TestPrepublicationCallback {
    param([Parameter(Mandatory)] [scriptblock] $Callback)

    & $script:module {
        param($Value)
        Set-Variable -Name CompositeNativePrepublicationCallback -Scope Script -Value $Value
    } $Callback
}

function Clear-TestPrepublicationCallback {
    & $script:module {
        Remove-Variable -Name CompositeNativePrepublicationCallback -Scope Script -ErrorAction SilentlyContinue
    }
}

function Invoke-TestComposite {
    param(
        [Parameter(Mandatory)] [pscustomobject] $Fixture,
        [object[]] $Ownership = $Fixture.Ownership
    )

    return & $script:module {
        param($SourceRoots, $OutputRoot, $ScratchRoot, $OwnershipRows)
        Invoke-M0QtShellCompositeNative `
            -SourceRoots $SourceRoots `
            -OutputRoot $OutputRoot `
            -ScratchRoot $ScratchRoot `
            -Ownership $OwnershipRows
    } $Fixture.SourceRoots $Fixture.OutputRoot $Fixture.ScratchRoot $Ownership
}

try {
    Test-Case 'native composition copies every owned leaf into distinct single-link storage' {
        $fixture = New-CompositeFixture 'green-single-link'
        $sourceSnapshot = Get-TestSourceSnapshot $fixture.SourceRoots

        $result = Invoke-TestComposite $fixture

        Assert-Equal @($result.PSObject.Properties.Name) `
            @('exit_code', 'sdk_root', 'inventory_relative_path', 'entries', 'published_root_identity') `
            'Native composite result shape drifted.'
        Assert-Equal $result.exit_code ([long]0) 'Native composite did not report exit zero.'
        Assert-Equal $result.sdk_root 'sdk' 'Native composite SDK path drifted.'
        Assert-Equal $result.inventory_relative_path 'evidence/composite-inventory.tsv' `
            'Native composite inventory path drifted.'
        Assert-Equal @($result.entries).Count $fixture.Ownership.Count `
            'Native composite result omitted owned leaves.'
        $publishedIdentity = & $module {
            param($Path)
            [string](Get-M0CompositeCurrentIdentity $Path $true 'published output identity fixture')
        } $fixture.OutputRoot
        Assert-Equal $result.published_root_identity $publishedIdentity `
            'Native handoff did not return the authenticated published-root identity.'

        $inventoryRows = [Collections.Generic.List[string]]::new()
        for ($index = 0; $index -lt $fixture.Ownership.Count; $index++) {
            $expected = $fixture.Ownership[$index]
            $actual = @($result.entries)[$index]
            Assert-Equal @($actual.PSObject.Properties.Name) `
                @('owner_id', 'source_relative_path', 'relative_path', 'size_bytes', 'sha256') `
                "Composite entry $index shape drifted."
            foreach ($property in @('owner_id', 'source_relative_path', 'relative_path', 'size_bytes', 'sha256')) {
                Assert-Equal $actual.$property $expected.$property "Composite entry $index property '$property' drifted."
            }

            $sourcePath = Join-Path ([string]$fixture.SourceRoots[$expected.owner_id]) $expected.source_relative_path
            $outputPath = Join-Path (Join-Path $fixture.OutputRoot 'sdk') $expected.relative_path
            Assert-True ([IO.File]::Exists($outputPath)) "Owned output leaf is missing: $($expected.relative_path)"
            Assert-Equal (Get-TestSha256File $outputPath) $expected.sha256 `
                "Owned output leaf bytes drifted: $($expected.relative_path)"
            $sourcePhysical = Get-TestPhysicalIdentity $sourcePath
            $outputPhysical = Get-TestPhysicalIdentity $outputPath
            Assert-Equal $sourcePhysical.link_count ([long]1) "Source leaf is not single-link: $sourcePath"
            Assert-Equal $outputPhysical.link_count ([long]1) "Output leaf is not single-link: $outputPath"
            Assert-True ($sourcePhysical.identity -cne $outputPhysical.identity) `
                "Output leaf aliases upstream storage: $($expected.relative_path)"

            $inventoryRows.Add(
                "$($expected.relative_path)`t$($expected.owner_id)`t$($expected.source_relative_path)`t" +
                "$($expected.size_bytes)`t$($expected.sha256)`n")
        }

        $expectedInventory = $inventoryRows -join ''
        $inventoryPath = Join-Path $fixture.OutputRoot $result.inventory_relative_path
        Assert-True ([IO.File]::Exists($inventoryPath)) 'Composite inventory was not published.'
        Assert-Equal ([IO.File]::ReadAllText($inventoryPath)) $expectedInventory `
            'Composite inventory is not canonical.'
        Assert-Equal (Get-TestSha256File $inventoryPath) (Get-TestSha256Text $expectedInventory) `
            'Composite inventory bytes do not match its canonical text.'
        Assert-True (-not [IO.Directory]::Exists($fixture.ScratchRoot)) `
            'Successful composition left its private scratch root behind.'
        Assert-TestSourcesUnchanged $fixture.SourceRoots $sourceSnapshot 'green-single-link'
    }

    Test-Case 'source hardlinks are rejected without changing upstream or publishing output' {
        $fixture = New-CompositeFixture 'source-hardlink'
        $sourceLeaf = Join-Path ([string]$fixture.SourceRoots['qtbase-refresh-v2']) 'bin/Qt6Core.dll'
        $externalAlias = Join-Path $fixture.Root 'upstream-alias.bin'
        New-Item -ItemType HardLink -Path $externalAlias -Target $sourceLeaf | Out-Null
        $sourceSnapshot = Get-TestSourceSnapshot $fixture.SourceRoots

        Assert-Rejected { Invoke-TestComposite $fixture } 'hardlink|hard link|link count|single-link'

        Assert-TestSourcesUnchanged $fixture.SourceRoots $sourceSnapshot 'source-hardlink'
        Assert-NoFailedPublication $fixture 'source-hardlink'
        Assert-True ([IO.File]::Exists($externalAlias)) 'Rejected source hardlink validation removed an upstream alias.'
    }

    Test-Case 'source reparse points are rejected without traversing or changing their targets' {
        $fixture = New-CompositeFixture 'source-reparse'
        $externalRoot = Join-Path $fixture.Root 'external-reparse-target'
        $externalLeaf = Join-Path $externalRoot 'sentinel.bin'
        Write-TestBytes $externalLeaf ([byte[]](0x41, 0x42, 0x43))
        $expectedExternalHash = Get-TestSha256File $externalLeaf
        $junction = Join-Path ([string]$fixture.SourceRoots['qtdeclarative']) 'linked-outside'
        New-Item -ItemType Junction -Path $junction -Target $externalRoot | Out-Null

        Assert-Rejected { Invoke-TestComposite $fixture } 'reparse|junction|symbolic link|link'

        Assert-True ((Get-Item -LiteralPath $junction -Force).Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)) `
            'Rejected source reparse validation removed the upstream junction.'
        Assert-Equal (Get-TestSha256File $externalLeaf) $expectedExternalHash `
            'Rejected source reparse validation changed the external target.'
        Assert-NoFailedPublication $fixture 'source-reparse'
        [IO.Directory]::Delete($junction)
    }

    Test-Case 'source alternate data streams are rejected and preserved on failure' {
        $fixture = New-CompositeFixture 'source-ads'
        $sourceLeaf = Join-Path ([string]$fixture.SourceRoots['qtsvg']) 'bin/Qt6Svg.dll'
        $streamPath = $sourceLeaf + ':m0-native-probe'
        [IO.File]::WriteAllText($streamPath, 'source-stream-probe', [Text.UTF8Encoding]::new($false))
        $expectedMainHash = Get-TestSha256File $sourceLeaf
        $expectedStream = [IO.File]::ReadAllText($streamPath, [Text.UTF8Encoding]::new($false, $true))

        Assert-Rejected { Invoke-TestComposite $fixture } 'alternate data stream|ADS|named stream|stream'

        Assert-Equal (Get-TestSha256File $sourceLeaf) $expectedMainHash `
            'Rejected source ADS validation changed the unnamed stream.'
        Assert-Equal ([IO.File]::ReadAllText($streamPath, [Text.UTF8Encoding]::new($false, $true))) $expectedStream `
            'Rejected source ADS validation changed the named stream.'
        Assert-NoFailedPublication $fixture 'source-ads'
    }

    Test-Case 'case-insensitive destination collisions fail before copying or publication' {
        $fixture = New-CompositeFixture 'case-collision'
        $sourceSnapshot = Get-TestSourceSnapshot $fixture.SourceRoots
        $collisionOwnership = @($fixture.Ownership | ForEach-Object {
                [pscustomobject][ordered]@{
                    owner_id = $_.owner_id
                    source_relative_path = $_.source_relative_path
                    relative_path = if ($_.owner_id -ceq 'd3d') { 'BIN/qt6core.DLL' } else { $_.relative_path }
                    size_bytes = $_.size_bytes
                    sha256 = $_.sha256
                }
            })
        $prepublicationReached = [pscustomobject]@{ Value = $false }
        $callback = {
            param($Context)
            $prepublicationReached.Value = $true
        }.GetNewClosure()
        Set-TestPrepublicationCallback $callback
        try {
            Assert-Rejected {
                Invoke-TestComposite -Fixture $fixture -Ownership $collisionOwnership
            } 'case-insensitive|collision|duplicate|same destination'
        }
        finally { Clear-TestPrepublicationCallback }

        Assert-True (-not $prepublicationReached.Value) `
            'Case-insensitive collision reached the copy/prepublication boundary.'
        Assert-TestSourcesUnchanged $fixture.SourceRoots $sourceSnapshot 'case-collision'
        Assert-NoFailedPublication $fixture 'case-collision'
    }

    Test-Case 'closed ownership validation scales by path depth rather than pair count' {
        $bulkRows = [Collections.Generic.List[object]]::new()
        foreach ($ownerId in @('qtbase-refresh-v2', 'qtdeclarative', 'qtsvg', 'd3d')) {
            for ($index = 0; $index -lt 400; $index++) {
                $relative = "bulk/$ownerId/leaf-$($index.ToString('D4')).bin"
                $bulkRows.Add([pscustomobject][ordered]@{
                        owner_id = $ownerId
                        source_relative_path = $relative
                        relative_path = $relative
                        size_bytes = [long]$index
                        sha256 = 'a' * 64
                    })
            }
        }

        $measurement = & $script:module {
            param($Rows)
            $stopwatch = [Diagnostics.Stopwatch]::StartNew()
            $accepted = @(Assert-M0CompositeClosedOwnership $Rows)
            $stopwatch.Stop()
            [pscustomobject][ordered]@{
                count = [long]$accepted.Count
                elapsed_milliseconds = [long]$stopwatch.ElapsedMilliseconds
            }
        } $bulkRows.ToArray()

        Assert-Equal $measurement.count ([long]$bulkRows.Count) `
            'Bulk ownership validation changed cardinality.'
        Assert-True ($measurement.elapsed_milliseconds -lt 4000) `
            "Bulk ownership validation remained pairwise: $($measurement.elapsed_milliseconds) ms."
    }

    Test-Case 'cleanup refuses an ordinary child tree substituted after authentication' {
        $fixture = New-CompositeFixture 'cleanup-child-replacement'
        $sourceSnapshot = Get-TestSourceSnapshot $fixture.SourceRoots
        $victimRoot = Join-Path $fixture.Root 'external-cleanup-victim'
        $victimLeaf = Join-Path $victimRoot 'sentinel.bin'
        Write-TestBytes $victimLeaf ([byte[]](0x61, 0x62, 0x63))
        $victimHash = Get-TestSha256File $victimLeaf
        $displacedBin = Join-Path $fixture.Root 'authenticated-candidate-bin'

        $callback = {
            param($Context)
            $candidateBin = Join-Path ([string]$Context.candidate_sdk_root) 'bin'
            [IO.Directory]::Move($candidateBin, $displacedBin)
            [IO.Directory]::Move($victimRoot, $candidateBin)
        }.GetNewClosure()
        Set-TestPrepublicationCallback $callback
        try {
            Assert-Rejected { Invoke-TestComposite $fixture } `
                'cleanup|identity|owned|replacement|refus'
        }
        finally { Clear-TestPrepublicationCallback }

        $substitutedVictimLeaf = Join-Path $fixture.ScratchRoot 'sdk/bin/sentinel.bin'
        Assert-True ([IO.File]::Exists($substitutedVictimLeaf)) `
            'Cleanup deleted an ordinary tree substituted into private scratch.'
        Assert-Equal (Get-TestSha256File $substitutedVictimLeaf) $victimHash `
            'Cleanup changed the substituted ordinary-tree sentinel.'
        Assert-True ([IO.Directory]::Exists($displacedBin)) `
            'Cleanup deleted the displaced authenticated candidate tree.'
        Assert-True (-not [IO.Directory]::Exists($fixture.OutputRoot)) `
            'Rejected child replacement published an output.'
        Assert-TestSourcesUnchanged $fixture.SourceRoots $sourceSnapshot 'cleanup-child-replacement'
    }

    Test-Case 'retained scratch identity prevents root replacement before cleanup' {
        $fixture = New-CompositeFixture 'cleanup-root-replacement'
        $sourceSnapshot = Get-TestSourceSnapshot $fixture.SourceRoots
        $victimRoot = Join-Path $fixture.Root 'external-root-victim'
        $victimLeaf = Join-Path $victimRoot 'sentinel.bin'
        Write-TestBytes $victimLeaf ([byte[]](0x71, 0x72, 0x73))
        $victimHash = Get-TestSha256File $victimLeaf
        $displacedScratch = Join-Path $fixture.Root 'displaced-authenticated-scratch'

        $callback = {
            param($Context)
            $scratch = Split-Path -Parent ([string]$Context.candidate_sdk_root)
            [IO.Directory]::Move($scratch, $displacedScratch)
            [IO.Directory]::Move($victimRoot, $scratch)
        }.GetNewClosure()
        Set-TestPrepublicationCallback $callback
        try {
            Assert-Rejected { Invoke-TestComposite $fixture } `
                'sharing|used by another process|access|anchor|identity|replacement'
        }
        finally { Clear-TestPrepublicationCallback }

        Assert-True ([IO.File]::Exists($victimLeaf)) `
            'Cleanup removed an external victim during scratch-root replacement.'
        Assert-Equal (Get-TestSha256File $victimLeaf) $victimHash `
            'Cleanup changed the external root-replacement sentinel.'
        Assert-True (-not [IO.Directory]::Exists($displacedScratch)) `
            'Scratch root was movable despite its retained identity handle.'
        Assert-NoFailedPublication $fixture 'cleanup-root-replacement'
        Assert-TestSourcesUnchanged $fixture.SourceRoots $sourceSnapshot 'cleanup-root-replacement'
    }

    Test-Case 'retained publication parent rejects rename and junction replacement' {
        foreach ($replacementKind in @('ordinary', 'junction')) {
            $fixture = New-CompositeFixture ('publication-parent-' + $replacementKind)
            $sourceSnapshot = Get-TestSourceSnapshot $fixture.SourceRoots
            $outputParent = Split-Path -Parent $fixture.OutputRoot
            $authenticatedParent = Join-Path $fixture.Root 'displaced-public-parent'
            $externalRoot = Join-Path $fixture.Root 'external-publication-target'
            $externalLeaf = Join-Path $externalRoot 'sentinel.bin'
            Write-TestBytes $externalLeaf ([byte[]](0x21, 0x22, 0x23))
            $externalHash = Get-TestSha256File $externalLeaf

            $callback = {
                param($Context)
                [IO.Directory]::Move($outputParent, $authenticatedParent)
                if ($replacementKind -ceq 'ordinary') {
                    [IO.Directory]::Move($externalRoot, $outputParent)
                }
                else {
                    New-Item -ItemType Junction -Path $outputParent -Target $externalRoot | Out-Null
                }
            }.GetNewClosure()
            Set-TestPrepublicationCallback $callback
            try {
                Assert-Rejected { Invoke-TestComposite $fixture } `
                    'sharing|used by another process|access|anchor|parent|replacement'
            }
            finally { Clear-TestPrepublicationCallback }

            Assert-True ([IO.Directory]::Exists($outputParent)) `
                "Publication parent '$replacementKind' replacement removed the authenticated parent."
            Assert-True (-not ((Get-Item -LiteralPath $outputParent -Force).Attributes.HasFlag(
                        [IO.FileAttributes]::ReparsePoint))) `
                "Publication parent '$replacementKind' replacement installed a junction."
            Assert-True (-not [IO.Directory]::Exists($authenticatedParent)) `
                "Publication parent '$replacementKind' replacement displaced the authenticated parent."
            Assert-True ([IO.File]::Exists($externalLeaf)) `
                "Publication parent '$replacementKind' replacement moved or deleted the external target."
            Assert-Equal (Get-TestSha256File $externalLeaf) $externalHash `
                "Publication parent '$replacementKind' replacement changed the external sentinel."
            Assert-NoFailedPublication $fixture ('publication-parent-' + $replacementKind)
            Assert-TestSourcesUnchanged $fixture.SourceRoots $sourceSnapshot `
                ('publication-parent-' + $replacementKind)
        }
    }

    Test-Case 'output hardlinks reparse points and alternate streams fail closed before publication' {
        foreach ($kind in @('hardlink', 'reparse', 'ads')) {
            $fixture = New-CompositeFixture ('output-' + $kind)
            $sourceSnapshot = Get-TestSourceSnapshot $fixture.SourceRoots
            $externalRoot = Join-Path $fixture.Root 'external-output-probe'
            [IO.Directory]::CreateDirectory($externalRoot) | Out-Null
            Write-TestBytes (Join-Path $externalRoot 'sentinel.bin') ([byte[]](0x51, 0x52, 0x53))
            $expectedExternalHash = Get-TestSha256File (Join-Path $externalRoot 'sentinel.bin')
            $externalAlias = Join-Path $fixture.Root 'candidate-hardlink-alias.bin'

            $callback = {
                param($Context)
                $contextProperties = @($Context.PSObject.Properties.Name)
                if ($contextProperties.Count -ne 1 -or $contextProperties[0] -cne 'candidate_sdk_root') {
                    throw 'Prepublication context shape drifted.'
                }
                $candidateSdkRoot = [string]$Context.candidate_sdk_root
                switch ($kind) {
                    'hardlink' {
                        New-Item -ItemType HardLink `
                            -Path $externalAlias `
                            -Target (Join-Path $candidateSdkRoot 'bin/Qt6Core.dll') | Out-Null
                    }
                    'reparse' {
                        New-Item -ItemType Junction `
                            -Path (Join-Path $candidateSdkRoot 'linked-outside') `
                            -Target $externalRoot | Out-Null
                    }
                    'ads' {
                        [IO.File]::WriteAllText(
                            (Join-Path $candidateSdkRoot 'bin/Qt6Svg.dll') + ':m0-native-probe',
                            'output-stream-probe',
                            [Text.UTF8Encoding]::new($false))
                    }
                }
            }.GetNewClosure()
            Set-TestPrepublicationCallback $callback
            try {
                $pattern = switch ($kind) {
                    'hardlink' { 'hardlink|hard link|link count|single-link' }
                    'reparse' { 'reparse|junction|symbolic link|link' }
                    'ads' { 'alternate data stream|ADS|named stream|stream' }
                }
                Assert-Rejected { Invoke-TestComposite $fixture } $pattern
            }
            finally { Clear-TestPrepublicationCallback }

            Assert-TestSourcesUnchanged $fixture.SourceRoots $sourceSnapshot ('output-' + $kind)
            Assert-NoFailedPublication $fixture ('output-' + $kind)
            Assert-Equal (Get-TestSha256File (Join-Path $externalRoot 'sentinel.bin')) $expectedExternalHash `
                "Rejected output '$kind' validation changed an external probe target."
        }
    }
}
finally {
    Clear-TestPrepublicationCallback
    if ($null -ne $module) {
        Remove-Module -ModuleInfo $module -Force -ErrorAction SilentlyContinue
    }
    if ([IO.Directory]::Exists($testRoot)) { [IO.Directory]::Delete($testRoot, $true) }
}

Write-Host "Tests: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -ne 0) { exit 1 }
