#Requires -Version 7.0
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
Import-Module (Join-Path $repository 'packaging/windows/RcMusicInstallerComponent.psm1') -Force
Import-Module (Join-Path $repository 'packaging/windows/RcMusicDownloadComponentPe.psm1') -Force
$state = @{ Passed = 0; Failed = 0 }
function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Assert-Equal($Actual, $Expected) {
    if ($Actual -cne $Expected) { throw "Expected '$Expected', found '$Actual'." }
}
function Assert-Rejected([scriptblock] $Body) {
    try { & $Body | Out-Null } catch { return }
    throw 'Expected rejection.'
}
function Json($Value) { return ConvertTo-Json -InputObject $Value -Depth 15 -Compress }
function Add-FixtureFile($Files, $Inventory, $Reports, [string] $Relative, [string] $Component,
    [string] $Role, [string[]] $Imports = @('KERNEL32.dll')) {
    $row = [pscustomobject]@{
        source = 'Z:/absent original package/' + $Relative
        relative_path = $Relative; size_bytes = 23; sha256 = 'a' * 64
    }
    $Files.Add($row)
    $Inventory.Add([pscustomobject]@{
        source = $row.source; relative_path = $Relative; component = $Component
        role = $Role; size_bytes = $row.size_bytes; sha256 = $row.sha256
    })
    if ([IO.Path]::GetExtension($Relative) -in @('.exe', '.dll', '.pyd')) {
        $Reports.Add([pscustomobject]@{ relative_path = $Relative; machine = 'x64'; imports = $Imports })
    }
}
function New-Fixture([switch] $Youtube) {
    $files = [Collections.Generic.List[object]]::new()
    $inventory = [Collections.Generic.List[object]]::new()
    $reports = [Collections.Generic.List[object]]::new()
    $files.Add([pscustomobject]@{
        source = 'Z:/absent original player/RCMusic.exe'; relative_path = 'RCMusic.exe'
        size_bytes = 7; sha256 = 'b' * 64
    })
    $network = [ordered]@{
        Bootstrap = 'RCMusicDownloadWorker.exe'; Payload = 'rcmusic-download-network.dll'
        Core = 'Qt6Core.dll'; Network = 'Qt6Network.dll'; Msvcp140 = 'msvcp140.dll'
        Msvcp140_1 = 'msvcp140_1.dll'; Vcruntime140 = 'vcruntime140.dll'
        Vcruntime140_1 = 'vcruntime140_1.dll'; Plugin = 'qrcmusicschanneloffline.dll'
    }
    foreach ($role in $network.Keys) {
        $imports = if ($role -ceq 'Payload') { @('Qt6Core.dll', 'KERNEL32.dll') } else { @('KERNEL32.dll') }
        Add-FixtureFile $files $inventory $reports ('download-tools/network/' + $network[$role]) 'network' $role $imports
    }
    Add-FixtureFile $files $inventory $reports 'download-tools/media/RCMusicMediaWorker.exe' 'media' 'Media' @('KERNEL32.dll', 'ole32.dll')
    if ($Youtube) {
        Add-FixtureFile $files $inventory $reports 'download-tools/youtube/extractor/yt-dlp.exe' 'youtube' 'Extractor'
        Add-FixtureFile $files $inventory $reports 'download-tools/youtube/javascript/node.exe' 'youtube' 'Node'
        Add-FixtureFile $files $inventory $reports 'download-tools/youtube/extractor/_internal/python313.dll' 'youtube' 'Support' @('VCRUNTIME140.dll', 'KERNEL32.dll')
        Add-FixtureFile $files $inventory $reports 'download-tools/youtube/extractor/_internal/VCRUNTIME140.dll' 'youtube' 'Support'
        Add-FixtureFile $files $inventory $reports 'download-tools/youtube/extractor/_internal/_ssl.pyd' 'youtube' 'Support' @('python313.dll', 'KERNEL32.dll')
        Add-FixtureFile $files $inventory $reports 'download-tools/youtube/extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js' 'youtube' 'EjsCore'
        Add-FixtureFile $files $inventory $reports 'download-tools/youtube/extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js' 'youtube' 'EjsLibrary'
        foreach ($name in @('python-LICENSE.txt', 'node-LICENSE.txt', 'yt-dlp-LICENSE.txt', 'ejs-LICENSE.txt', 'pyinstaller-COPYING.txt')) {
            Add-FixtureFile $files $inventory $reports ('download-tools/youtube/notices/' + $name) 'youtube' 'Support'
        }
    }
    $closure = Get-RcMusicDownloadComponentPeClosure -Files $inventory.ToArray() -Reports $reports.ToArray()
    return [pscustomobject]@{
        Runtime = 'C:/absent exported runtime/app'
        Manifest = [pscustomobject]@{
            files = $files.ToArray()
            download_component = [pscustomobject]@{
                schema_version = 1; selection_default = $false; receipt_sha256 = 'c' * 64; header_sha256 = 'd' * 64
                activation_marker = [pscustomobject]@{ relative_path = 'download-tools/installed-v1'; content = "RCMusicDownloadTools-v1`n" }
                files = @($inventory.ToArray() | Select-Object relative_path, size_bytes, sha256)
                pe_closure = $closure
            }
        }
    }
}
function Component($Fixture) {
    return Get-RcMusicInstallerComponent -Manifest $Fixture.Manifest -RuntimeDirectory $Fixture.Runtime
}
function Add-NativeHosts($Fixture) {
    $channels = @(for ($index = 0; $index -lt 2; ++$index) {
        $youtube = $index -eq 1
        $name = if ($youtube) { 'com.rcmusic.youtube' } else { 'com.rcmusic.media' }
        $exe = if ($youtube) { 'rc_music_youtube_native_host.exe' } else { 'rc_music_native_host.exe' }
        $origin = 'chrome-extension://' + $(if ($youtube) { 'b' * 32 } else { 'a' * 32 }) + '/'
        $description = if ($youtube) { 'RC Music YouTube audio extension host' } else { 'RC Music media extension host' }
        $text = (ConvertTo-Json -InputObject ([ordered]@{
            name = $name; description = $description; path = $exe; type = 'stdio'; allowed_origins = @($origin)
        }) -Depth 4) + "`n"
        $bytes = [Text.UTF8Encoding]::new($false, $true).GetBytes($text)
        $rows = @(
            [pscustomobject]@{ relative_path = $exe; size_bytes = 31; sha256 = ('e' * 64) },
            [pscustomobject]@{ relative_path = $name + '.json'; size_bytes = $bytes.Length
                sha256 = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant() }
        )
        $Fixture.Manifest.files += $rows
        $Fixture.Manifest.download_component.files += $rows
        [pscustomobject]@{
            protocol = $(if ($youtube) { 'youtube.audio.v1' } else { 'direct.media.v1' }); host_name = $name
            origin = $origin; endpoint = "rcmusic-fixture-$index"
            receipt_sha256 = 'f' * 64; configuration_sha256 = 'a' * 64
            executable = $exe; manifest = $name + '.json'
        }
    })
    $Fixture.Manifest.download_component | Add-Member -NotePropertyName native_hosts -NotePropertyValue ([pscustomobject]@{
        schema_version = 1; shared_header_sha256 = 'b' * 64; channels = $channels
    })
}

Test-Case 'native host executables and portable manifests remain in the optional selected inventory' {
    $fixture = New-Fixture -Youtube
    Add-NativeHosts $fixture
    $before = Json $fixture
    $result = Component $fixture
    Assert-Equal @($result.files).Count 26
    foreach ($name in @('rc_music_native_host.exe', 'rc_music_youtube_native_host.exe', 'com.rcmusic.media.json', 'com.rcmusic.youtube.json')) {
        Assert-Equal @($result.files | Where-Object relative_path -CEQ $name).Count 1
    }
    Assert-Equal (Json $fixture) $before
}
Test-Case 'native files cannot silently become mandatory files without matching component metadata' {
    foreach ($withDownloads in @($false, $true)) {
        $fixture = New-Fixture -Youtube; Add-NativeHosts $fixture
        if ($withDownloads) { $fixture.Manifest.download_component.PSObject.Properties.Remove('native_hosts') }
        else {
            $fixture.Manifest.PSObject.Properties.Remove('download_component')
            $fixture.Manifest.files = @($fixture.Manifest.files | Where-Object relative_path -CNotLike 'download-tools/*')
        }
        Assert-Rejected { Component $fixture }
    }
}
Test-Case 'native metadata requires both exact channels, independent origins and endpoints, and complete tool runtime' {
    foreach ($variant in @('missing', 'duplicate', 'protocol', 'host', 'origin', 'endpoint', 'same-origin', 'same-endpoint', 'no-youtube', 'exe', 'manifest', 'schema', 'pin', 'extra')) {
        $fixture = New-Fixture -Youtube; Add-NativeHosts $fixture
        $native = $fixture.Manifest.download_component.native_hosts
        switch ($variant) {
            missing { $native.channels = @($native.channels[0]) }
            duplicate { $native.channels = @($native.channels[0], $native.channels[0]) }
            protocol { $native.channels[0].protocol = 'youtube.audio.v1' }
            host { $native.channels[0].host_name = 'com.rcmusic.youtube' }
            origin { $native.channels[0].origin = 'chrome-extension://*/' }
            endpoint { $native.channels[0].endpoint = '../pipe' }
            same-origin { $native.channels[1].origin = $native.channels[0].origin }
            same-endpoint { $native.channels[1].endpoint = $native.channels[0].endpoint.ToUpperInvariant() }
            no-youtube {
                $fixture = New-Fixture
                Add-NativeHosts $fixture
            }
            exe { $native.channels[0].executable = 'download-tools/native/rc_music_native_host.exe' }
            manifest { $native.channels[0].manifest = 'other.json' }
            schema { $native.schema_version = '1' }
            pin { $native.channels[0].receipt_sha256 = 'A' * 64 }
            extra { $native | Add-Member -NotePropertyName unknown -NotePropertyValue $true }
        }
        Assert-Rejected { Component $fixture }
    }
}
Test-Case 'native manifest inventory binds the generated relative host path and exact allowed origin' {
    foreach ($variant in @('missing-exe', 'missing-manifest', 'manifest-hash', 'manifest-size', 'extra-root')) {
        $fixture = New-Fixture -Youtube; Add-NativeHosts $fixture
        switch ($variant) {
            missing-exe {
                $fixture.Manifest.files = @($fixture.Manifest.files | Where-Object relative_path -CNE 'rc_music_native_host.exe')
                $fixture.Manifest.download_component.files = @($fixture.Manifest.download_component.files | Where-Object relative_path -CNE 'rc_music_native_host.exe')
            }
            missing-manifest {
                $fixture.Manifest.files = @($fixture.Manifest.files | Where-Object relative_path -CNE 'com.rcmusic.media.json')
                $fixture.Manifest.download_component.files = @($fixture.Manifest.download_component.files | Where-Object relative_path -CNE 'com.rcmusic.media.json')
            }
            manifest-hash {
                $row = @($fixture.Manifest.files | Where-Object relative_path -CEQ 'com.rcmusic.media.json')[0]
                $row.sha256 = 'e' * 64
            }
            manifest-size {
                $row = @($fixture.Manifest.files | Where-Object relative_path -CEQ 'com.rcmusic.media.json')[0]
                $row.size_bytes++
            }
            extra-root {
                $row = [pscustomobject]@{ relative_path = 'another_host.exe'; size_bytes = 23; sha256 = 'e' * 64 }
                $fixture.Manifest.files += $row; $fixture.Manifest.download_component.files += $row
            }
        }
        Assert-Rejected { Component $fixture }
    }
}

Test-Case 'ordinary runtimes return null without mutating the manifest' {
    $fixture = New-Fixture
    $fixture.Manifest.PSObject.Properties.Remove('download_component')
    $fixture.Manifest.files = @($fixture.Manifest.files | Where-Object relative_path -CEQ 'RCMusic.exe')
    $before = Json $fixture
    Assert-Equal (Component $fixture) $null
    Assert-Equal (Json $fixture) $before
}
Test-Case 'undeclared tools are rejected with normalized case-insensitive separators' {
    foreach ($relative in @('download-tools/network/Qt6Core.dll', 'DoWnLoAd-ToOlS\unlisted.bin', 'download-tools')) {
        $fixture = New-Fixture
        $fixture.Manifest.PSObject.Properties.Remove('download_component')
        $fixture.Manifest.files = @([pscustomobject]@{ relative_path = $relative; size_bytes = 1; sha256 = 'a' * 64 })
        Assert-Rejected { Component $fixture }
    }
}
Test-Case 'direct component exposes only ten tool files and the fixed LF marker descriptor' {
    $fixture = New-Fixture
    $before = Json $fixture
    $result = Component $fixture
    Assert-Equal $result.available $true
    Assert-Equal @($result.files).Count 10
    Assert-Equal (Json $result.files) (Json @($fixture.Manifest.files | Where-Object relative_path -CNE 'RCMusic.exe'))
    Assert-Equal $result.marker.relative_path 'download-tools/installed-v1'
    Assert-Equal $result.marker.content "RCMusicDownloadTools-v1`n"
    Assert-Equal $result.marker.size_bytes 24
    $expectedHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::ASCII.GetBytes("RCMusicDownloadTools-v1`n"))).ToLowerInvariant()
    Assert-Equal $result.marker.sha256 $expectedHash
    Assert-Equal $result.receipt_sha256 ('c' * 64)
    Assert-Equal $result.header_sha256 ('d' * 64)
    Assert-Equal (Json $fixture) $before
}
Test-Case 'nested YouTube inventory works without accessing original package or runtime files' {
    $fixture = New-Fixture -Youtube
    $before = Json $fixture
    $result = Component $fixture
    Assert-Equal $result.available $true
    Assert-Equal @($result.files).Count 22
    Assert-Equal @($result.files | Where-Object relative_path -CLike '*_ssl.pyd').Count 1
    Assert-Equal (Json $fixture) $before
}
Test-Case 'the production notice names include PyInstaller COPYING and require every real notice' {
    $fixture = New-Fixture -Youtube
    Assert-Equal (Component $fixture).available $true
    foreach ($name in @('python-LICENSE.txt', 'node-LICENSE.txt', 'yt-dlp-LICENSE.txt', 'ejs-LICENSE.txt', 'pyinstaller-COPYING.txt')) {
        $fixture = New-Fixture -Youtube
        $relative = 'download-tools/youtube/notices/' + $name
        $fixture.Manifest.files = @($fixture.Manifest.files | Where-Object relative_path -CNE $relative)
        $fixture.Manifest.download_component.files = @($fixture.Manifest.download_component.files | Where-Object relative_path -CNE $relative)
        Assert-Rejected { Component $fixture }
    }
    $fixture = New-Fixture -Youtube
    foreach ($collection in @($fixture.Manifest.files, $fixture.Manifest.download_component.files)) {
        @($collection | Where-Object relative_path -CEQ 'download-tools/youtube/notices/pyinstaller-COPYING.txt')[0].relative_path =
            'download-tools/youtube/notices/pyinstaller-LICENSE.txt'
    }
    Assert-Rejected { Component $fixture }
}
Test-Case 'metadata schema and default selection require exact closed typed fields' {
    foreach ($value in @($null, $false, 'component')) {
        $fixture = New-Fixture; $fixture.Manifest.download_component = $value
        Assert-Rejected { Component $fixture }
    }
    foreach ($value in @(0, 2, '1', 1.0, $true)) {
        $fixture = New-Fixture; $fixture.Manifest.download_component.schema_version = $value
        Assert-Rejected { Component $fixture }
    }
    foreach ($value in @($true, 0, 'false', $null)) {
        $fixture = New-Fixture; $fixture.Manifest.download_component.selection_default = $value
        Assert-Rejected { Component $fixture }
    }
    foreach ($field in @('schema_version', 'selection_default', 'receipt_sha256', 'header_sha256', 'activation_marker', 'files', 'pe_closure')) {
        $fixture = New-Fixture; $fixture.Manifest.download_component.PSObject.Properties.Remove($field)
        Assert-Rejected { Component $fixture }
    }
    $fixture = New-Fixture; $fixture.Manifest.download_component | Add-Member -NotePropertyName unexpected -NotePropertyValue 'field'
    Assert-Rejected { Component $fixture }
}
Test-Case 'pins are lowercase bounded strings without final newline tolerance' {
    foreach ($field in @('receipt_sha256', 'header_sha256')) {
        foreach ($value in @(('A' * 64), ('a' * 63), (('a' * 64) + "`n"), (('a' * 64) + "`r`n"), $null, 12)) {
            $fixture = New-Fixture; $fixture.Manifest.download_component.$field = $value
            Assert-Rejected { Component $fixture }
        }
    }
}
Test-Case 'marker schema accepts only the exact versioned path and LF bytes' {
    foreach ($value in @('RCMusicDownloadTools-v1', "RCMusicDownloadTools-v1`r`n", "RCMusicDownloadTools-v1`n`n", 'other', $null)) {
        $fixture = New-Fixture; $fixture.Manifest.download_component.activation_marker.content = $value
        Assert-Rejected { Component $fixture }
    }
    foreach ($value in @('download-tools/installed-v2', 'DOWNLOAD-TOOLS/installed-v1', '../installed-v1', "download-tools/installed-v1`n")) {
        $fixture = New-Fixture; $fixture.Manifest.download_component.activation_marker.relative_path = $value
        Assert-Rejected { Component $fixture }
    }
    $fixture = New-Fixture; $fixture.Manifest.download_component.activation_marker | Add-Member -NotePropertyName sha256 -NotePropertyValue ('a' * 64)
    Assert-Rejected { Component $fixture }
}
Test-Case 'metadata and complete manifest tool subsets must be a one-to-one size-and-hash match' {
    foreach ($variant in @('missing', 'extra', 'duplicate', 'base', 'hash', 'size', 'marker')) {
        $fixture = New-Fixture; $component = $fixture.Manifest.download_component
        switch ($variant) {
            missing { $component.files = @($component.files | Select-Object -Skip 1) }
            extra { $fixture.Manifest.files += [pscustomobject]@{ relative_path = 'download-tools/extra.bin'; size_bytes = 1; sha256 = 'a' * 64 } }
            duplicate { $component.files += $component.files[0] }
            base { $component.files += [pscustomobject]@{ relative_path = 'RCMusic.exe'; size_bytes = 7; sha256 = 'b' * 64 } }
            hash { $component.files[0].sha256 = 'e' * 64 }
            size { $component.files[0].size_bytes = 24 }
            marker {
                $row = [pscustomobject]@{ relative_path = 'download-tools/installed-v1'; size_bytes = 24; sha256 = 'a' * 64 }
                $fixture.Manifest.files += $row; $component.files += $row
            }
        }
        Assert-Rejected { Component $fixture }
    }
}
Test-Case 'file records enforce bounds safe paths and reject aliases in either inventory' {
    foreach ($value in @(0, -1, 134217729, '23', 23.5, $null)) {
        $fixture = New-Fixture; $fixture.Manifest.download_component.files[0].size_bytes = $value
        Assert-Rejected { Component $fixture }
    }
    foreach ($relative in @('../Qt6Core.dll', 'download-tools/network/../Qt6Core.dll', 'download-tools/network/NUL.dll', 'download-tools/network/a.dll:stream')) {
        $fixture = New-Fixture; $fixture.Manifest.download_component.files[0].relative_path = $relative
        Assert-Rejected { Component $fixture }
    }
    foreach ($which in @('manifest', 'metadata')) {
        $fixture = New-Fixture
        $alias = [pscustomobject]@{ relative_path = 'DOWNLOAD-TOOLS\NETWORK\RCMUSICDOWNLOADWORKER.EXE'; size_bytes = 23; sha256 = 'a' * 64 }
        if ($which -ceq 'manifest') { $fixture.Manifest.files += $alias } else { $fixture.Manifest.download_component.files += $alias }
        Assert-Rejected { Component $fixture }
    }
    $fixture = New-Fixture; $fixture.Manifest.download_component.files[0] | Add-Member -NotePropertyName source -NotePropertyValue 'Z:/not authority'
    Assert-Rejected { Component $fixture }
}
Test-Case 'subset matching accepts normalized case and separators without rewriting caller records' {
    $fixture = New-Fixture
    foreach ($entry in $fixture.Manifest.download_component.files) { $entry.relative_path = $entry.relative_path.ToUpperInvariant().Replace('/', '\') }
    $before = Json $fixture
    Assert-Equal @((Component $fixture).files).Count 10
    Assert-Equal (Json $fixture) $before
}
Test-Case 'fixed network media and YouTube roles cannot be removed or moved' {
    foreach ($relative in @('download-tools/network/Qt6Network.dll', 'download-tools/media/RCMusicMediaWorker.exe', 'download-tools/youtube/javascript/node.exe')) {
        $fixture = New-Fixture -Youtube
        $fixture.Manifest.files = @($fixture.Manifest.files | Where-Object relative_path -CNE $relative)
        $fixture.Manifest.download_component.files = @($fixture.Manifest.download_component.files | Where-Object relative_path -CNE $relative)
        $fixture.Manifest.download_component.pe_closure.reports = @($fixture.Manifest.download_component.pe_closure.reports | Where-Object relative_path -CNE $relative)
        Assert-Rejected { Component $fixture }
    }
    $fixture = New-Fixture
    foreach ($collection in @($fixture.Manifest.files, $fixture.Manifest.download_component.files)) {
        @($collection | Where-Object relative_path -CEQ 'download-tools/network/Qt6Core.dll')[0].relative_path = 'download-tools/media/Qt6Core.dll'
    }
    Assert-Rejected { Component $fixture }
}
Test-Case 'PE reports must cover every tool PE and preserve x64 and derived load domains' {
    foreach ($variant in @('missing', 'extra', 'duplicate', 'machine', 'domain', 'unknown')) {
        $fixture = New-Fixture -Youtube; $closure = $fixture.Manifest.download_component.pe_closure
        switch ($variant) {
            missing { $closure.reports = @($closure.reports | Select-Object -Skip 1) }
            extra { $closure.reports += [pscustomobject]@{ relative_path = 'RCMusic.exe'; machine = 'x64'; domain = 'network'; imports = @('kernel32.dll') } }
            duplicate { $closure.reports += $closure.reports[0] }
            machine { $closure.reports[0].machine = 'x86' }
            domain { $closure.reports[0].domain = 'node' }
            unknown { $closure.reports[0] | Add-Member -NotePropertyName undocumented -NotePropertyValue $true }
        }
        Assert-Rejected { Component $fixture }
    }
}
Test-Case 'claimed private and Windows edges must equal the recomputed closure' {
    foreach ($variant in @('private-missing', 'private-extra', 'private-target', 'windows-missing', 'windows-extra', 'windows-name', 'unknown')) {
        $fixture = New-Fixture -Youtube; $closure = $fixture.Manifest.download_component.pe_closure
        switch ($variant) {
            private-missing { $closure.private_imports = @($closure.private_imports | Select-Object -Skip 1) }
            private-extra { $closure.private_imports += $closure.private_imports[0] }
            private-target { $closure.private_imports[0].resolved_path = 'Qt6Core.dll' }
            windows-missing { $closure.windows_external = @($closure.windows_external | Select-Object -Skip 1) }
            windows-extra { $closure.windows_external += $closure.windows_external[0] }
            windows-name { $closure.windows_external[0].name = 'unreviewed.dll' }
            unknown { $closure | Add-Member -NotePropertyName arbitrary -NotePropertyValue @() }
        }
        Assert-Rejected { Component $fixture }
    }
}
Test-Case 'Node imports cannot be satisfied by a sibling extractor CRT or player Qt' {
    foreach ($name in @('VCRUNTIME140.dll', 'Qt6Core.dll')) {
        $fixture = New-Fixture -Youtube
        $node = @($fixture.Manifest.download_component.pe_closure.reports | Where-Object domain -CEQ 'node')[0]
        $node.imports = @('kernel32.dll', $name)
        Assert-Rejected { Component $fixture }
    }
}
Write-Host "Passed: $($state.Passed); Failed: $($state.Failed)"
if ($state.Failed -gt 0) { exit 1 }
