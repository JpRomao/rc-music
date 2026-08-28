#Requires -Version 7.2
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$core = Join-Path $root 'M0.QtShellCompositePackage.psm1'
$native = Join-Path $root 'M0.QtShellCompositePackageNative.psm1'
$failures = [Collections.Generic.List[string]]::new()
function Case([string]$Name, [scriptblock]$Body) {
    try { & $Body; Write-Host "PASS: $Name" }
    catch { $failures.Add("FAIL: $Name`n$($_.Exception.Message)"); Write-Host $failures[-1] }
}
function Need([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function New-Fixture([string]$Base) {
    $source = Join-Path $Base 'origem completa'
    [IO.Directory]::CreateDirectory((Join-Path $source 'app')) | Out-Null
    [IO.Directory]::CreateDirectory((Join-Path $source 'evidence')) | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $source 'app/m0_qt_shell.exe'), [byte[]](1,2,3,4))
    [IO.File]::WriteAllText((Join-Path $source 'evidence/manifest.json'), '{"ok":true}', [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $source 'evidence/qt-shell-composite-package-receipt.v2.json'), '{"verified":true}', [Text.UTF8Encoding]::new($false))
    $paths = @('app/m0_qt_shell.exe','evidence/manifest.json','evidence/qt-shell-composite-package-receipt.v2.json')
    $entries = foreach ($relative in $paths) {
        $file = Join-Path $source ($relative.Replace('/',[IO.Path]::DirectorySeparatorChar))
        [pscustomobject][ordered]@{
            relative_path=$relative
            size_bytes=[long]([IO.FileInfo]::new($file).Length)
            sha256=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    [pscustomobject]@{ source=$source; entries=@($entries); output=(Join-Path $Base 'publicado'); scratch=(Join-Path $Base '.scratch-private') }
}
function Invoke-MutantStage($Fixture,[string]$Marker,[string]$Injection) {
    $mutant=Join-Path ([IO.Path]::GetDirectoryName($Fixture.source)) ('Mutant-' + [Guid]::NewGuid().ToString('N') + '.psm1')
    $moduleText=[IO.File]::ReadAllText($native)
    Need (($moduleText.Split($Marker).Count-1) -eq 1) "Test seam must occur exactly once: $Marker"
    [IO.File]::WriteAllText($mutant,$moduleText.Replace($Marker,"$Injection`r`n$Marker"),[Text.UTF8Encoding]::new($false))
    $module=Import-Module $mutant -Force -PassThru
    try { Invoke-M0QtShellCompositePackageNative $Fixture.source $Fixture.output $Fixture.scratch $Fixture.entries | Out-Null }
    finally { Remove-Module $module -Force; [IO.File]::Delete($mutant) }
}

Case 'private stage production modules exist with exact exports' {
    Need ([IO.File]::Exists($core)) 'Package v2 core module is missing.'
    Need ([IO.File]::Exists($native)) 'Package v2 native module is missing.'
    $coreModule = Import-Module $core -Force -PassThru
    $nativeModule = Import-Module $native -Force -PassThru
    try {
        Need ((@($coreModule.ExportedFunctions.Keys) -join ',') -ceq
            'Assert-M0QtShellCompositePackageReceipt,Invoke-M0QtShellCompositePackage') `
            'Core export surface is not exact.'
        Need ((@($nativeModule.ExportedFunctions.Keys) -join ',') -ceq
            'Invoke-M0QtShellCompositePackageNative') 'Native export surface is not exact.'
    }
    finally { Remove-Module $coreModule,$nativeModule -Force }
}

Case 'native stage uses create-new handle-relative no-replace publication and handle cleanup' {
    Need ([IO.File]::Exists($native)) 'Package v2 native module is missing.'
    $text = [IO.File]::ReadAllText($native)
    foreach ($needle in @('CreateFileW','CREATE_NEW','NtSetInformationFile','RenameNoReplaceRelative',
            'CreateDirectoryRelative','MarkDeleteOnClose','GetIdentity','GetLinkCount')) {
        Need ($text.Contains($needle,[StringComparison]::Ordinal)) "Native stage lacks $needle."
    }
    foreach ($forbidden in @('Copy-Item','Move-Item','Remove-Item -Recurse','[IO.File]::Copy(')) {
        Need (-not $text.Contains($forbidden,[StringComparison]::OrdinalIgnoreCase)) `
            "Authenticated stage uses forbidden path primitive: $forbidden"
    }
}

Case 'stage rejects collisions and source reparse ADS hardlink or alias before output creation' {
    Need ([IO.File]::Exists($native)) 'Package v2 native module is missing.'
    $text = [IO.File]::ReadAllText($native)
    foreach ($needle in @('collision','ReparsePoint','alternate data stream','link-count',
            'OrdinalIgnoreCase','before copy','before publication')) {
        Need ($text.Contains($needle,[StringComparison]::OrdinalIgnoreCase)) `
            "Native stage lacks fail-closed contract for $needle."
    }
}

Case 'stage copies payload into distinct single-link physical identities and exact ledgers' {
    Need ([IO.File]::Exists($native)) 'Package v2 native module is missing.'
    $text = [IO.File]::ReadAllText($native)
    foreach ($needle in @('source_identity','destination_identity','directory_ledger','file_ledger',
            'size_bytes','sha256','single-link')) {
        Need ($text.Contains($needle,[StringComparison]::OrdinalIgnoreCase)) `
            "Native stage lacks authenticated copy invariant $needle."
    }
}

Case 'native stage publishes exact app evidence envelope and leaves source unchanged' {
    if (-not $IsWindows) { return }
    $base = Join-Path ([IO.Path]::GetTempPath()) ('m0-pkg-v2-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($base) | Out-Null
    try {
        $fixture=New-Fixture $base
        $before=@($fixture.entries | ForEach-Object sha256)
        $module=Import-Module $native -Force -PassThru
        try { $result=Invoke-M0QtShellCompositePackageNative -SourceRoot $fixture.source -OutputRoot $fixture.output -ScratchRoot $fixture.scratch -Entries $fixture.entries }
        finally { Remove-Module $module -Force }
        Need ([IO.Directory]::Exists($fixture.output)) 'Published root is missing.'
        Need (-not [IO.Directory]::Exists($fixture.scratch)) 'Scratch name remained after atomic publication.'
        Need (($result.PSObject.Properties.Name -join ',') -ceq 'exit_code,package_root,payload_root,evidence_root,entries,published_root_identity') 'Native result shape is not closed.'
        Need ($result.payload_root -ceq 'app') 'Payload root differs.'
        Need ($result.evidence_root -ceq 'evidence') 'Evidence root differs.'
        Need ((@([IO.DirectoryInfo]::new($fixture.output).GetFileSystemInfos() | ForEach-Object Name | Sort-Object) -join ',') -ceq 'app,evidence') 'Published envelope is not exact.'
        $after=@($fixture.entries | ForEach-Object { (Get-FileHash -LiteralPath (Join-Path $fixture.source ($_.relative_path.Replace('/',[IO.Path]::DirectorySeparatorChar))) -Algorithm SHA256).Hash.ToLowerInvariant() })
        Need (($before -join ',') -ceq ($after -join ',')) 'Source changed during staging.'
    } finally { if ([IO.Directory]::Exists($base)) { [IO.Directory]::Delete($base,$true) } }
}

Case 'native stage rejects output collision before creating scratch' {
    if (-not $IsWindows) { return }
    $base=Join-Path ([IO.Path]::GetTempPath()) ('m0-pkg-v2-collision-' + [Guid]::NewGuid().ToString('N')); [IO.Directory]::CreateDirectory($base)|Out-Null
    try {
        $fixture=New-Fixture $base; [IO.Directory]::CreateDirectory($fixture.output)|Out-Null
        $module=Import-Module $native -Force -PassThru
        try { $threw=$false; try { Invoke-M0QtShellCompositePackageNative $fixture.source $fixture.output $fixture.scratch $fixture.entries | Out-Null } catch { $threw=$true }; Need $threw 'Existing output was accepted.' }
        finally { Remove-Module $module -Force }
        Need (-not [IO.Directory]::Exists($fixture.scratch)) 'Scratch was created despite preflight collision.'
    } finally { if ([IO.Directory]::Exists($base)) { [IO.Directory]::Delete($base,$true) } }
}

Case 'prepublication mutation is rejected and authenticated scratch is removed' {
    if (-not $IsWindows) { return }
    $base=Join-Path ([IO.Path]::GetTempPath()) ('m0-pkg-v2-mutation-' + [Guid]::NewGuid().ToString('N')); [IO.Directory]::CreateDirectory($base)|Out-Null
    try {
        $fixture=New-Fixture $base
        $mutant=Join-Path $base 'MutantNative.psm1'
        $moduleText=[IO.File]::ReadAllText($native)
        $marker='    # TEST-FIXTURE-INSERTION-POINT: PACKAGE_PREPUBLICATION'
        Need (($moduleText.Split($marker).Count-1) -eq 1) 'Prepublication seam must occur exactly once.'
        [IO.File]::WriteAllText($mutant,$moduleText.Replace($marker, "        [IO.File]::WriteAllText((Join-Path `$layout.scratch_root 'app/m0_qt_shell.exe'),'mutated')`r`n$marker"),[Text.UTF8Encoding]::new($false))
        $module=Import-Module $mutant -Force -PassThru
        try { $threw=$false; try { Invoke-M0QtShellCompositePackageNative $fixture.source $fixture.output $fixture.scratch $fixture.entries | Out-Null } catch { $threw=$true }; Need $threw 'Mutated candidate was published.' }
        finally { Remove-Module $module -Force }
        Need (-not [IO.Directory]::Exists($fixture.output)) 'Output appeared after mutation rejection.'
        Need (-not [IO.Directory]::Exists($fixture.scratch)) 'Authenticated scratch was not cleaned after mutation rejection.'
        Need ([IO.File]::Exists((Join-Path $fixture.source 'app/m0_qt_shell.exe'))) 'Source victim was touched by cleanup.'
    } finally { if ([IO.Directory]::Exists($base)) { [IO.Directory]::Delete($base,$true) } }
}

Case 'source envelope rejects an unlisted empty directory' {
    if (-not $IsWindows) { return }
    $base=Join-Path ([IO.Path]::GetTempPath()) ('m0-pkg-v2-source-empty-' + [Guid]::NewGuid().ToString('N')); [IO.Directory]::CreateDirectory($base)|Out-Null
    try { $f=New-Fixture $base; [IO.Directory]::CreateDirectory((Join-Path $f.source 'app/empty'))|Out-Null; $m=Import-Module $native -Force -PassThru
        try { $threw=$false; try { Invoke-M0QtShellCompositePackageNative $f.source $f.output $f.scratch $f.entries|Out-Null } catch {$threw=$true}; Need $threw 'Unlisted empty source directory was accepted.' } finally {Remove-Module $m -Force}
        Need (-not [IO.Directory]::Exists($f.output)) 'Invalid source was published.'; Need (-not [IO.Directory]::Exists($f.scratch)) 'Invalid source created scratch.'
    } finally { if([IO.Directory]::Exists($base)){[IO.Directory]::Delete($base,$true)} }
}

Case 'source envelope rejects a case-aliased top-level directory' {
    if (-not $IsWindows) { return }
    $base=Join-Path ([IO.Path]::GetTempPath()) ('m0-pkg-v2-source-case-' + [Guid]::NewGuid().ToString('N')); [IO.Directory]::CreateDirectory($base)|Out-Null
    try { $f=New-Fixture $base; [IO.Directory]::Move((Join-Path $f.source 'app'),(Join-Path $f.source 'APP')); $m=Import-Module $native -Force -PassThru
        try { $threw=$false; try { Invoke-M0QtShellCompositePackageNative $f.source $f.output $f.scratch $f.entries|Out-Null } catch {$threw=$true}; Need $threw 'Case-aliased source topology was accepted.' } finally {Remove-Module $m -Force}
    } finally { if([IO.Directory]::Exists($base)){[IO.Directory]::Delete($base,$true)} }
}

foreach ($mutation in @(
    [pscustomobject]@{name='extra leaf'; code="        [IO.File]::WriteAllText((Join-Path `$layout.scratch_root 'late-extra'),'x')"},
    [pscustomobject]@{name='extra empty directory'; code="        [IO.Directory]::CreateDirectory((Join-Path `$layout.scratch_root 'late-empty')) | Out-Null"},
    [pscustomobject]@{name='changed owned leaf'; code="        [IO.File]::WriteAllText((Join-Path `$layout.scratch_root 'app/m0_qt_shell.exe'),'changed')"}
)) {
    Case "post-audit pre-rename $($mutation.name) is rejected without publication" {
        if (-not $IsWindows) { return }
        $base=Join-Path ([IO.Path]::GetTempPath()) ('m0-pkg-v2-late-' + [Guid]::NewGuid().ToString('N')); [IO.Directory]::CreateDirectory($base)|Out-Null
        try { $f=New-Fixture $base; Need ([IO.File]::ReadAllText($native).Contains('    # TEST-FIXTURE-INSERTION-POINT: PACKAGE_POSTAUDIT_PRERENAME')) 'Post-audit/pre-rename seam is missing.'; $threw=$false; try { Invoke-MutantStage $f '    # TEST-FIXTURE-INSERTION-POINT: PACKAGE_POSTAUDIT_PRERENAME' $mutation.code } catch {$threw=$true}; Need $threw 'Late mutation was published.'
            Need (-not [IO.Directory]::Exists($f.output)) 'Output appeared after late mutation.'; Need (-not [IO.Directory]::Exists($f.scratch)) 'Authenticated scratch remained after late mutation.'
        } finally { if([IO.Directory]::Exists($base)){[IO.Directory]::Delete($base,$true)} }
    }
}

Case 'failure immediately after scratch creation cleans by retained root handle' {
    if (-not $IsWindows) { return }
    $base=Join-Path ([IO.Path]::GetTempPath()) ('m0-pkg-v2-root-fail-' + [Guid]::NewGuid().ToString('N')); [IO.Directory]::CreateDirectory($base)|Out-Null
    try { $f=New-Fixture $base; Need ([IO.File]::ReadAllText($native).Contains('    # TEST-FIXTURE-INSERTION-POINT: PACKAGE_SCRATCH_ROOT_CREATED')) 'Scratch-root-created seam is missing.'; $threw=$false; try { Invoke-MutantStage $f '    # TEST-FIXTURE-INSERTION-POINT: PACKAGE_SCRATCH_ROOT_CREATED' "        throw 'causal failure after root create'" } catch {$threw=$true}; Need $threw 'Injected root-creation failure did not fail.'
        Need (-not [IO.Directory]::Exists($f.scratch)) 'Root created before failure was orphaned.'; Need ([IO.File]::Exists((Join-Path $f.source 'app/m0_qt_shell.exe'))) 'Source victim was touched.'
    } finally { if([IO.Directory]::Exists($base)){[IO.Directory]::Delete($base,$true)} }
}

Case 'retained destination parent prevents substitution before relative leaf create' {
    if (-not $IsWindows) { return }
    $base=Join-Path ([IO.Path]::GetTempPath()) ('m0-pkg-v2-parent-swap-' + [Guid]::NewGuid().ToString('N')); [IO.Directory]::CreateDirectory($base)|Out-Null
    try { $f=New-Fixture $base; Need ([IO.File]::ReadAllText($native).Contains('    # TEST-FIXTURE-INSERTION-POINT: PACKAGE_BEFORE_LEAF_CREATE')) 'Before-leaf-create seam is missing.'; $victim=Join-Path $base 'victim'; [IO.Directory]::CreateDirectory($victim)|Out-Null; [IO.File]::WriteAllText((Join-Path $victim 'sentinel'),'safe'); $code="        `$p=Join-Path `$layout.scratch_root 'app'; [IO.Directory]::Move(`$p,(`$p+'.old')); [IO.Directory]::CreateSymbolicLink(`$p,'$($victim.Replace("'","''"))')|Out-Null"
        $threw=$false; try { Invoke-MutantStage $f '    # TEST-FIXTURE-INSERTION-POINT: PACKAGE_BEFORE_LEAF_CREATE' $code } catch {$threw=$true}; Need $threw 'Parent substitution attempt was not blocked.'; Need ([IO.File]::ReadAllText((Join-Path $victim 'sentinel')) -ceq 'safe') 'External victim changed.'; Need (-not [IO.Directory]::Exists($f.output)) 'Output appeared after parent substitution.'
    } finally { if([IO.Directory]::Exists($base)){[IO.Directory]::Delete($base,$true)} }
}

if ($failures.Count) { throw "Package v2 staging Reds: $($failures.Count) failure(s)." }
