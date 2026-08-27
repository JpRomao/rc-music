#requires -Version 7.2
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IsWindows) { throw 'These synthetic Windows package tests require Windows.' }

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
Import-Module (Join-Path $PSScriptRoot '../QtShellPackageManifest.psm1') -Force
$testBase = Join-Path $repositoryRoot 'out/m0/test-runs/qt-package'
$testRun = Join-Path $testBase ('manifest-' + [guid]::NewGuid().ToString('N'))
$ancestor = $testBase
while ($ancestor) {
    if (Test-Path -LiteralPath $ancestor) {
        if (((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "The synthetic test root has a reparse ancestor: $ancestor"
        }
    }
    $ancestor = [IO.Path]::GetDirectoryName($ancestor)
}
$null = New-Item -ItemType Directory -Path $testRun
$script:passed = 0
$script:failed = 0

function Assert-True([bool] $Condition, [string] $Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-Equal($Actual, $Expected, [string] $Message) {
    if ($Actual -cne $Expected) { throw "$Message (expected '$Expected', actual '$Actual')" }
}

function Assert-Throws([scriptblock] $Action, [string] $Pattern = '.') {
    $caught = $null
    try { & $Action | Out-Null }
    catch { $caught = $_ }
    Assert-True ($null -ne $caught) 'Expected a terminating error.'
    Assert-True ($caught.Exception.Message -match $Pattern) "Unexpected error: $($caught.Exception.Message)"
}

function Invoke-Test([string] $Name, [scriptblock] $Body) {
    try {
        & $Body
        $script:passed++
        Write-Host "PASS $Name"
    }
    catch {
        $script:failed++
        Write-Host "FAIL $Name`: $($_.Exception.Message)"
        Write-Host $_.ScriptStackTrace
    }
}

function New-Fixture([switch] $Minimal) {
    $caseRoot = Join-Path $testRun ([guid]::NewGuid().ToString('N'))
    $payload = Join-Path $caseRoot 'payload com espaço'
    $null = New-Item -ItemType Directory -Path $payload
    [IO.File]::WriteAllBytes((Join-Path $payload 'a.dat'), [byte[]]@(97, 98, 99))
    if (-not $Minimal) {
        $null = New-Item -ItemType Directory -Path (Join-Path $payload 'qml/ação [1]')
        [IO.File]::WriteAllBytes((Join-Path $payload 'Z.dat'), [byte[]]@())
        [IO.File]::WriteAllText((Join-Path $payload 'qml/ação [1]/main.qml'), 'synthetic, never executed')
        [IO.File]::WriteAllBytes((Join-Path $payload 'z-last.bin'), [byte[]]@(0, 255, 10))
    }
    [pscustomobject]@{
        case_root = $caseRoot
        payload = $payload
        manifest_path = Join-Path $caseRoot 'manifest.json'
        metadata = [ordered]@{
            purpose = 'synthetic local evaluation'
            nested = [ordered]@{ enabled = $false; values = @(1, 'ação', $null) }
            timestamp_text = '2026-08-27T12:34:56Z'
        }
    }
}

function Write-Manifest($Fixture, $Manifest) {
    [IO.File]::WriteAllText($Fixture.manifest_path, ($Manifest | ConvertTo-Json -Depth 100 -Compress))
}

function Save-ValidManifest($Fixture) {
    $manifest = New-QtShellPackageManifest -PackageRoot $Fixture.payload -Metadata $Fixture.metadata
    Write-Manifest $Fixture $manifest
    $manifest
}

function Assert-RejectedTree($Fixture, [string] $Pattern) {
    Assert-Throws { New-QtShellPackageManifest -PackageRoot $Fixture.payload -Metadata $Fixture.metadata } $Pattern
    Assert-Throws { Test-QtShellPackageManifest -PackageRoot $Fixture.payload -ManifestPath $Fixture.manifest_path } $Pattern
}

try {
    Invoke-Test 'inventory is ordinal, relative, exact and preserves metadata' {
        $fixture = New-Fixture
        $manifest = New-QtShellPackageManifest -PackageRoot $fixture.payload -Metadata $fixture.metadata
        Assert-Equal $manifest.schema_version 1 'schema version'
        Assert-Equal $manifest.files.Count 4 'file count'
        Assert-Equal ($manifest.files.relative_path -join '|') 'Z.dat|a.dat|qml/ação [1]/main.qml|z-last.bin' 'ordinal paths'
        Assert-Equal $manifest.files[0].size_bytes 0 'empty file size'
        Assert-Equal $manifest.files[1].size_bytes 3 'binary file size'
        Assert-Equal $manifest.files[1].sha256 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad' 'known SHA-256'
        Assert-True ([object]::ReferenceEquals($manifest.metadata, $fixture.metadata)) 'metadata was replaced'
        Assert-True (-not (Test-Path -LiteralPath $fixture.manifest_path)) 'generator wrote a manifest'
    }

    Invoke-Test 'round trip preserves opaque metadata including timestamp strings' {
        $fixture = New-Fixture
        $manifest = Save-ValidManifest $fixture
        $verified = Test-QtShellPackageManifest -PackageRoot $fixture.payload -ManifestPath $fixture.manifest_path
        Assert-Equal ($verified | ConvertTo-Json -Depth 100 -Compress) ($manifest | ConvertTo-Json -Depth 100 -Compress) 'manifest changed'
        Assert-True ($verified.metadata.timestamp_text -is [string]) 'metadata timestamp was coerced'
        Assert-True ($verified.files -is [array]) 'files is not an array'
        Assert-Equal @(Get-ChildItem -LiteralPath $fixture.case_root -File).Count 1 'unexpected verifier writes'
    }

    Invoke-Test 'empty and singleton inventories remain arrays' {
        $fixture = New-Fixture -Minimal
        $empty = Join-Path $fixture.case_root 'empty'
        $null = New-Item -ItemType Directory -Path $empty
        foreach ($root in @($empty, $fixture.payload)) {
            $manifest = New-QtShellPackageManifest -PackageRoot $root -Metadata @{}
            Write-Manifest $fixture $manifest
            $verified = Test-QtShellPackageManifest -PackageRoot $root -ManifestPath $fixture.manifest_path
            Assert-True ($manifest.files -is [array]) 'generator unwrapped files'
            Assert-True ($verified.files -is [array]) 'verifier unwrapped files'
            Assert-Equal $verified.files.Count $(if ($root -eq $empty) { 0 } else { 1 }) 'file count'
        }
    }

    Invoke-Test 'ordinal output is independent of current culture' {
        $fixture = New-Fixture
        $originalCulture = [Threading.Thread]::CurrentThread.CurrentCulture
        try {
            [Threading.Thread]::CurrentThread.CurrentCulture = [Globalization.CultureInfo]::GetCultureInfo('tr-TR')
            $first = New-QtShellPackageManifest -PackageRoot $fixture.payload -Metadata $fixture.metadata
            [Threading.Thread]::CurrentThread.CurrentCulture = [Globalization.CultureInfo]::GetCultureInfo('sv-SE')
            $second = New-QtShellPackageManifest -PackageRoot $fixture.payload -Metadata $fixture.metadata
            Assert-Equal ($first | ConvertTo-Json -Depth 100 -Compress) ($second | ConvertTo-Json -Depth 100 -Compress) 'locale changed inventory'
            Assert-Equal $first.files[0].relative_path 'Z.dat' 'ordinal uppercase ordering'
        }
        finally { [Threading.Thread]::CurrentThread.CurrentCulture = $originalCulture }
    }

    Invoke-Test 'manifest survives relocation to a Unicode path with spaces' {
        $fixture = New-Fixture
        $manifest = Save-ValidManifest $fixture
        $relocated = Join-Path $fixture.case_root 'movido ação [2]'
        Copy-Item -LiteralPath $fixture.payload -Destination $relocated -Recurse
        $verified = Test-QtShellPackageManifest -PackageRoot $relocated -ManifestPath $fixture.manifest_path
        Assert-Equal ($verified.files | ConvertTo-Json -Compress) ($manifest.files | ConvertTo-Json -Compress) 'relocated inventory'
        Assert-True (($manifest | ConvertTo-Json -Depth 100) -notlike "*$($fixture.case_root)*") 'absolute path leaked'
    }

    Invoke-Test 'digest authenticates the exact external JSON bytes when supplied' {
        $fixture = New-Fixture
        $manifest = Save-ValidManifest $fixture
        $expected = (Get-FileHash -LiteralPath $fixture.manifest_path -Algorithm SHA256).Hash
        $verified = Test-QtShellPackageManifest -PackageRoot $fixture.payload -ManifestPath $fixture.manifest_path -ExpectedManifestSha256 $expected
        Assert-Equal $verified.schema_version 1 'verified version'
        $manifest.metadata.purpose = 'opaque metadata belongs to the builder'
        Write-Manifest $fixture $manifest
        Assert-Throws { Test-QtShellPackageManifest -PackageRoot $fixture.payload -ManifestPath $fixture.manifest_path -ExpectedManifestSha256 $expected } 'manifest.*SHA-256'
        $unanchored = Test-QtShellPackageManifest -PackageRoot $fixture.payload -ManifestPath $fixture.manifest_path
        Assert-Equal $unanchored.metadata.purpose $manifest.metadata.purpose 'verifier invented metadata policy'
        Assert-Throws { Test-QtShellPackageManifest -PackageRoot $fixture.payload -ManifestPath $fixture.manifest_path -ExpectedManifestSha256 '' }
        Assert-Throws { Test-QtShellPackageManifest -PackageRoot $fixture.payload -ManifestPath $fixture.manifest_path -ExpectedManifestSha256 ('g' * 64) }
    }

    foreach ($change in @('same-size bytes', 'size', 'missing file', 'extra hidden file', 'file replaced by directory', 'case')) {
        Invoke-Test "verifier rejects $change" {
            $fixture = New-Fixture -Minimal
            $null = Save-ValidManifest $fixture
            $original = Join-Path $fixture.payload 'a.dat'
            switch ($change) {
                'same-size bytes' { [IO.File]::WriteAllText($original, 'abd') }
                'size' { [IO.File]::WriteAllText($original, 'abcd') }
                'missing file' { Remove-Item -LiteralPath $original }
                'extra hidden file' {
                    $extra = Join-Path $fixture.payload 'hidden.dat'
                    [IO.File]::WriteAllText($extra, 'extra')
                    [IO.File]::SetAttributes($extra, [IO.FileAttributes]::Hidden)
                }
                'file replaced by directory' {
                    Remove-Item -LiteralPath $original
                    $null = New-Item -ItemType Directory -Path $original
                }
                'case' {
                    Rename-Item -LiteralPath $original -NewName 'A.dat'
                }
            }
            Assert-Throws { Test-QtShellPackageManifest -PackageRoot $fixture.payload -ManifestPath $fixture.manifest_path } 'inventory|size|SHA-256|path'
        }
    }

    Invoke-Test 'hidden files are included by the generator' {
        $fixture = New-Fixture -Minimal
        [IO.File]::SetAttributes((Join-Path $fixture.payload 'a.dat'), [IO.FileAttributes]::Hidden)
        $manifest = Save-ValidManifest $fixture
        Assert-Equal $manifest.files.Count 1 'hidden file omitted'
        $null = Test-QtShellPackageManifest -PackageRoot $fixture.payload -ManifestPath $fixture.manifest_path
    }

    Invoke-Test 'path equality is ordinal rather than Unicode collation equality' {
        $fixture = New-Fixture -Minimal
        Rename-Item -LiteralPath (Join-Path $fixture.payload 'a.dat') -NewName 'café.dat'
        $manifest = New-QtShellPackageManifest -PackageRoot $fixture.payload -Metadata @{}
        Assert-Equal $manifest.files[0].relative_path 'café.dat' 'physical Unicode name'
        $manifest.files[0].relative_path = "cafe$([char]0x301).dat"
        Write-Manifest $fixture $manifest
        Assert-Throws { Test-QtShellPackageManifest -PackageRoot $fixture.payload -ManifestPath $fixture.manifest_path } 'file path'
    }

    Invoke-Test 'manifest cannot be part of the payload' {
        $fixture = New-Fixture
        $fixture.manifest_path = Join-Path $fixture.payload 'manifest.json'
        $null = Save-ValidManifest $fixture
        Assert-Throws { Test-QtShellPackageManifest -PackageRoot $fixture.payload -ManifestPath $fixture.manifest_path } 'outside.*payload'
    }

    Invoke-Test 'manifest in a prefix-sharing sibling stays external' {
        $fixture = New-Fixture
        $sibling = $fixture.payload + '-metadata'
        $null = New-Item -ItemType Directory -Path $sibling
        $fixture.manifest_path = Join-Path $sibling 'manifest.json'
        $null = Save-ValidManifest $fixture
        $null = Test-QtShellPackageManifest -PackageRoot $fixture.payload -ManifestPath $fixture.manifest_path
    }

    $invalidJsonCases = @(
        @{ name = 'top array'; json = '[]'; error = 'object' },
        @{ name = 'top null'; json = 'null'; error = 'object' },
        @{ name = 'truncated JSON'; json = '{'; error = 'JSON' },
        @{ name = 'extra top field'; from = '"schema_version":1'; to = '"extra":0,"schema_version":1'; error = 'fields' },
        @{ name = 'missing metadata'; from = '"metadata":{},'; to = ''; error = 'fields' },
        @{ name = 'duplicate top field'; from = '"schema_version":1'; to = '"schema_version":1,"schema_version":1'; error = 'fields' },
        @{ name = 'case-variant top field'; from = '"schema_version":1'; to = '"Schema_version":1'; error = 'fields' },
        @{ name = 'schema string'; from = '"schema_version":1'; to = '"schema_version":"1"'; error = 'schema_version' },
        @{ name = 'schema fraction'; from = '"schema_version":1'; to = '"schema_version":1.0'; error = 'schema_version' },
        @{ name = 'schema future'; from = '"schema_version":1'; to = '"schema_version":2'; error = 'schema_version' },
        @{ name = 'schema boolean'; from = '"schema_version":1'; to = '"schema_version":true'; error = 'schema_version' },
        @{ name = 'metadata array'; from = '"metadata":{}'; to = '"metadata":[]'; error = 'metadata' },
        @{ name = 'metadata null'; from = '"metadata":{}'; to = '"metadata":null'; error = 'metadata' },
        @{ name = 'files object'; from = '"files":[ENTRY]'; to = '"files":{}'; error = 'files' },
        @{ name = 'null file'; from = 'ENTRY'; to = 'null'; error = 'object' },
        @{ name = 'extra file field'; from = '"relative_path":"a.dat"'; to = '"extra":0,"relative_path":"a.dat"'; error = 'fields' },
        @{ name = 'missing size'; from = '"size_bytes":3,'; to = ''; error = 'fields' },
        @{ name = 'duplicate file field'; from = '"size_bytes":3'; to = '"size_bytes":3,"size_bytes":3'; error = 'fields' },
        @{ name = 'case-variant file field'; from = '"size_bytes":3'; to = '"Size_bytes":3'; error = 'fields' },
        @{ name = 'path number'; from = '"relative_path":"a.dat"'; to = '"relative_path":3'; error = 'relative_path' },
        @{ name = 'size string'; from = '"size_bytes":3'; to = '"size_bytes":"3"'; error = 'size_bytes' },
        @{ name = 'size negative'; from = '"size_bytes":3'; to = '"size_bytes":-3'; error = 'size_bytes' },
        @{ name = 'size fraction'; from = '"size_bytes":3'; to = '"size_bytes":3.0'; error = 'size_bytes' },
        @{ name = 'size overflow'; from = '"size_bytes":3'; to = '"size_bytes":9223372036854775808'; error = 'size_bytes' },
        @{ name = 'size boolean'; from = '"size_bytes":3'; to = '"size_bytes":true'; error = 'size_bytes' },
        @{ name = 'hash number'; from = '"sha256":"HASH"'; to = '"sha256":42'; error = 'sha256' },
        @{ name = 'hash invalid'; from = 'HASH'; to = ('g' * 64); error = 'sha256' },
        @{ name = 'hash short'; from = 'HASH'; to = '00'; error = 'sha256' }
    )
    foreach ($case in $invalidJsonCases) {
        Invoke-Test "closed schema rejects $($case.name)" {
            $fixture = New-Fixture -Minimal
            $entry = '{"relative_path":"a.dat","size_bytes":3,"sha256":"HASH"}'
            $json = '{"schema_version":1,"metadata":{},"files":[ENTRY]}'
            if ($case.ContainsKey('json')) { $json = $case.json }
            elseif ($case.from -in @('ENTRY', '"files":[ENTRY]')) { $json = $json.Replace($case.from, $case.to) }
            else { $json = $json.Replace('ENTRY', $entry).Replace($case.from, $case.to) }
            $json = $json.Replace('ENTRY', $entry).Replace('HASH', 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
            [IO.File]::WriteAllText($fixture.manifest_path, $json)
            Assert-Throws { Test-QtShellPackageManifest -PackageRoot $fixture.payload -ManifestPath $fixture.manifest_path } $case.error
        }
    }

    Invoke-Test 'invalid UTF-8 is rejected instead of replaced' {
        $fixture = New-Fixture -Minimal
        [IO.File]::WriteAllBytes($fixture.manifest_path, [byte[]]@(0xc3, 0x28))
        Assert-Throws { Test-QtShellPackageManifest -PackageRoot $fixture.payload -ManifestPath $fixture.manifest_path } 'UTF-8|JSON'
    }

    foreach ($unsafePath in @(
        '', '.', '..', '../a.dat', 'x/../a.dat', './a.dat', '/a.dat', '//server/a.dat',
        'C:/a.dat', 'C:a.dat', '\\server\a.dat', 'qml\a.dat', 'a.dat:stream', 'a.dat::$DATA',
        'x//a.dat', 'a.dat/', 'a.dat.', 'a.dat ', 'x./a.dat', 'x /a.dat',
        'CON', 'con.txt', 'PRN.dat', 'AUX', 'NUL.txt', 'NUL .txt', 'COM1.dll', 'LPT9', 'COM¹.dll', 'LPT²',
        'CONIN$', 'CONOUT$', 'a?.dat', 'a*.dat', 'a|.dat', 'a<.dat', 'a>.dat', 'a".dat', "a`n.dat", "a$([char]0).dat"
    )) {
        Invoke-Test "unsafe manifest path is rejected: $($unsafePath.Replace("`n", '<LF>').Replace([string][char]0, '<NUL>'))" {
            $fixture = New-Fixture -Minimal
            $manifest = New-QtShellPackageManifest -PackageRoot $fixture.payload -Metadata @{}
            $manifest.files[0].relative_path = $unsafePath
            Write-Manifest $fixture $manifest
            Assert-Throws { Test-QtShellPackageManifest -PackageRoot $fixture.payload -ManifestPath $fixture.manifest_path } 'relative path'
        }
    }

    foreach ($duplicate in @('a.dat', 'A.dat')) {
        Invoke-Test "duplicate path is rejected: $duplicate" {
            $fixture = New-Fixture -Minimal
            $manifest = New-QtShellPackageManifest -PackageRoot $fixture.payload -Metadata @{}
            $manifest.files += [pscustomobject]@{ relative_path = $duplicate; size_bytes = 3L; sha256 = $manifest.files[0].sha256 }
            Write-Manifest $fixture $manifest
            Assert-Throws { Test-QtShellPackageManifest -PackageRoot $fixture.payload -ManifestPath $fixture.manifest_path } 'duplicate'
        }
    }

    Invoke-Test 'verifier rejects nonordinal file order' {
        $fixture = New-Fixture
        $manifest = New-QtShellPackageManifest -PackageRoot $fixture.payload -Metadata @{}
        [array]::Reverse($manifest.files)
        Write-Manifest $fixture $manifest
        Assert-Throws { Test-QtShellPackageManifest -PackageRoot $fixture.payload -ManifestPath $fixture.manifest_path } 'ordinal'
    }

    foreach ($badMetadata in @('string', 1, $true, @('array'))) {
        Invoke-Test "generator rejects nonobject metadata: $($badMetadata.GetType().Name)" {
            $fixture = New-Fixture -Minimal
            Assert-Throws { New-QtShellPackageManifest -PackageRoot $fixture.payload -Metadata $badMetadata } 'metadata'
        }
    }

    foreach ($suffix in @('\.', '\..', '\a.dat:stream', '\trailing.', '\trailing ')) {
        Invoke-Test "ambiguous input root is rejected: $suffix" {
            $fixture = New-Fixture -Minimal
            Assert-Throws { New-QtShellPackageManifest -PackageRoot ($fixture.payload + $suffix) -Metadata @{} } 'path'
        }
    }

    Invoke-Test 'root must be a directory and manifest must be a regular file' {
        $fixture = New-Fixture -Minimal
        Assert-Throws { New-QtShellPackageManifest -PackageRoot (Join-Path $fixture.payload 'a.dat') -Metadata @{} } 'directory'
        Assert-Throws { Test-QtShellPackageManifest -PackageRoot $fixture.payload -ManifestPath $fixture.case_root } 'file'
        Assert-Throws { New-QtShellPackageManifest -PackageRoot (Join-Path $fixture.case_root 'missing') -Metadata @{} }
    }

    Invoke-Test 'junction descendants are rejected without following their target' {
        $fixture = New-Fixture -Minimal
        $null = Save-ValidManifest $fixture
        $outside = Join-Path $fixture.case_root 'outside'
        $null = New-Item -ItemType Directory -Path $outside
        $sentinel = Join-Path $outside 'sentinel.txt'
        [IO.File]::WriteAllText($sentinel, 'outside remains unchanged')
        $junction = Join-Path $fixture.payload 'redirected'
        $null = New-Item -ItemType Junction -Path $junction -Target $outside
        try {
            Assert-RejectedTree $fixture 'reparse'
            Assert-Equal ([IO.File]::ReadAllText($sentinel)) 'outside remains unchanged' 'outside file changed'
        }
        finally { [IO.Directory]::Delete($junction) }
    }

    Invoke-Test 'root and ancestor junctions are rejected' {
        $fixture = New-Fixture
        $null = Save-ValidManifest $fixture
        $junction = Join-Path $fixture.case_root 'root-alias'
        $null = New-Item -ItemType Junction -Path $junction -Target $fixture.payload
        try {
            foreach ($root in @($junction, (Join-Path $junction 'qml'))) {
                Assert-Throws { New-QtShellPackageManifest -PackageRoot $root -Metadata @{} } 'reparse'
                Assert-Throws { Test-QtShellPackageManifest -PackageRoot $root -ManifestPath $fixture.manifest_path } 'reparse'
            }
        }
        finally { [IO.Directory]::Delete($junction) }
    }

    Invoke-Test 'manifest ancestors cannot be junctions' {
        $fixture = New-Fixture -Minimal
        $null = Save-ValidManifest $fixture
        $junction = Join-Path $fixture.case_root 'manifest-alias'
        $null = New-Item -ItemType Junction -Path $junction -Target $fixture.case_root
        try {
            Assert-Throws { Test-QtShellPackageManifest -PackageRoot $fixture.payload -ManifestPath (Join-Path $junction 'manifest.json') } 'reparse'
        }
        finally { [IO.Directory]::Delete($junction) }
    }

    foreach ($targetKind in @('payload file', 'manifest')) {
        Invoke-Test "external hardlink to $targetKind is rejected" {
            $fixture = New-Fixture -Minimal
            $null = Save-ValidManifest $fixture
            $target = if ($targetKind -eq 'manifest') { $fixture.manifest_path } else { Join-Path $fixture.payload 'a.dat' }
            $alias = Join-Path $fixture.case_root 'hardlink.dat'
            $null = New-Item -ItemType HardLink -Path $alias -Target $target
            if ($targetKind -eq 'manifest') {
                Assert-Throws { Test-QtShellPackageManifest -PackageRoot $fixture.payload -ManifestPath $fixture.manifest_path } 'hardlink'
            }
            else { Assert-RejectedTree $fixture 'hardlink' }
        }
    }

    foreach ($targetKind in @('payload file', 'payload root', 'empty directory', 'manifest')) {
        Invoke-Test "alternate stream on $targetKind is rejected" {
            $fixture = New-Fixture -Minimal
            $emptyDirectory = Join-Path $fixture.payload 'empty'
            $null = New-Item -ItemType Directory -Path $emptyDirectory
            $null = Save-ValidManifest $fixture
            $target = switch ($targetKind) {
                'payload file' { Join-Path $fixture.payload 'a.dat' }
                'payload root' { $fixture.payload }
                'empty directory' { $emptyDirectory }
                'manifest' { $fixture.manifest_path }
            }
            Set-Content -LiteralPath $target -Stream 'synthetic-stream' -Value 'extra bytes' -NoNewline
            if ($targetKind -eq 'manifest') {
                Assert-Throws { Test-QtShellPackageManifest -PackageRoot $fixture.payload -ManifestPath $fixture.manifest_path } 'alternate.*stream'
            }
            else { Assert-RejectedTree $fixture 'alternate.*stream' }
        }
    }

    foreach ($badName in @('trailing.', 'trailing ', 'NUL.txt', 'COM¹.dll')) {
        Invoke-Test "physical ambiguous Windows name is rejected: $badName" {
            $fixture = New-Fixture -Minimal
            $null = Save-ValidManifest $fixture
            $extendedPath = '\\?\' + (Join-Path $fixture.payload $badName)
            [IO.File]::WriteAllText($extendedPath, 'synthetic hostile filename')
            try { Assert-RejectedTree $fixture 'relative path' }
            finally { [IO.File]::Delete($extendedPath) }
        }
    }

    Invoke-Test 'unreadable locked content fails closed' {
        $fixture = New-Fixture -Minimal
        $null = Save-ValidManifest $fixture
        $lock = [IO.File]::Open((Join-Path $fixture.payload 'a.dat'), [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        try { Assert-RejectedTree $fixture '.' }
        finally { $lock.Dispose() }
    }

    Invoke-Test 'only the two public functions are exported' {
        $exports = @((Get-Module QtShellPackageManifest).ExportedFunctions.Keys | Sort-Object)
        Assert-Equal ($exports -join '|') 'New-QtShellPackageManifest|Test-QtShellPackageManifest' 'module exports'
    }
}
finally {
    $resolvedRun = [IO.Path]::GetFullPath($testRun)
    $expectedParent = [IO.Path]::GetFullPath($testBase)
    if (-not [StringComparer]::OrdinalIgnoreCase.Equals([IO.Path]::GetDirectoryName($resolvedRun), $expectedParent) -or
        -not [IO.Path]::GetFileName($resolvedRun).StartsWith('manifest-', [StringComparison]::Ordinal)) {
        throw 'Refusing cleanup outside the owned synthetic run.'
    }
    Remove-Item -LiteralPath $resolvedRun -Recurse -Force
}

Write-Host "Manifest tests: $script:passed passed, $script:failed failed."
if ($script:failed -ne 0) { exit 1 }
