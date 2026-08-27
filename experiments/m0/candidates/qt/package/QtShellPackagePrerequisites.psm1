#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# These checks require quiescent local inputs. Hashing and later execution do not
# share handles: this is not protection against a concurrent filesystem writer.
function ConvertTo-PrerequisitePath([string] $Path, [string] $Label = 'path') {
    $normal = $Path.Replace('/', '\')
    if ($normal -notmatch '^[A-Za-z]:\\' -or $normal -match '[\x00-\x1f<>"|?*]' -or $normal.Substring(2).Contains(':')) {
        throw "$Label must be an ordinary absolute local Windows path."
    }
    $normal = ($normal -replace '\\{2,}', '\').TrimEnd('\')
    foreach ($part in $normal.Substring(3).Split('\')) {
        if ($part -in @('', '.', '..') -or $part -match '[. ]$') { throw "$Label contains an ambiguous path component." }
    }
    return [IO.Path]::GetFullPath($normal)
}

function Get-PrerequisiteRegularFilePath([string] $Path) {
    $full = ConvertTo-PrerequisitePath $Path
    $item = Get-Item -LiteralPath $full -Force
    if ($item.PSIsContainer) { throw "Expected a regular file: $full" }
    $cursor = $full
    while ($cursor) {
        $entry = Get-Item -LiteralPath $cursor -Force
        if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Rejected reparse point: $cursor" }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
    return $full
}

function Read-PrerequisitePinnedFile([string] $Path, [string] $Sha256, [long] $SizeBytes = -1) {
    $full = Get-PrerequisiteRegularFilePath $Path
    $bytes = [IO.File]::ReadAllBytes($full)
    if ($SizeBytes -ge 0 -and $bytes.LongLength -ne $SizeBytes) { throw "Pinned size mismatch: $full" }
    $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
    if ($hash -cne $Sha256) { throw "Pinned SHA256 mismatch: $full" }
    return [pscustomobject] @{ path = $full; sha256 = $hash; size_bytes = $bytes.LongLength; bytes = $bytes }
}

function Get-QtShellPackageEnvironment {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [Collections.IDictionary] $Values,
        [Parameter(Mandatory)] [string] $CompilerPath,
        [Parameter(Mandatory)] [string] $ToolsetVersion,
        [Parameter(Mandatory)] [string] $ProgramFilesX86,
        [Parameter(Mandatory)] [string] $WindowsDirectory
    )
    if ($ToolsetVersion -notmatch '^14\.44\.\d+$') { throw 'ToolsetVersion must be from the authenticated MSVC 14.44 toolset.' }
    $compiler = ConvertTo-PrerequisitePath $CompilerPath 'CompilerPath'
    $suffix = "\VC\Tools\MSVC\$ToolsetVersion\bin\Hostx64\x64\cl.exe"
    if (-not $compiler.EndsWith($suffix, [StringComparison]::OrdinalIgnoreCase)) { throw 'CompilerPath must be the selected Hostx64/x64 cl.exe.' }
    $installation = $compiler.Substring(0, $compiler.Length - $suffix.Length)
    $toolset = "$installation\VC\Tools\MSVC\$ToolsetVersion"
    $programFiles = ConvertTo-PrerequisitePath $ProgramFilesX86 'ProgramFilesX86'
    $windows = ConvertTo-PrerequisitePath $WindowsDirectory 'WindowsDirectory'
    $sdk = "$programFiles\Windows Kits\10"
    $netfx = "$programFiles\Windows Kits\NETFXSDK\4.8"
    $sdkVersion = '10.0.22621.0'
    $expected = [ordered] @{
        VCToolsInstallDir = $toolset
        VCToolsVersion = $ToolsetVersion
        WindowsSdkDir = $sdk
        WindowsSDKVersion = $sdkVersion
        VSCMD_ARG_HOST_ARCH = 'x64'
        VSCMD_ARG_TGT_ARCH = 'x64'
        INCLUDE = "$toolset\include;$sdk\include\$sdkVersion\ucrt;$sdk\include\$sdkVersion\um;$sdk\include\$sdkVersion\shared;$sdk\include\$sdkVersion\winrt;$sdk\include\$sdkVersion\cppwinrt;$netfx\include\um"
        LIB = "$toolset\lib\x64;$netfx\lib\um\x64;$sdk\lib\$sdkVersion\ucrt\x64;$sdk\lib\$sdkVersion\um\x64"
        LIBPATH = "$toolset\lib\x64;$toolset\lib\x86\store\references;$sdk\UnionMetadata\$sdkVersion;$sdk\References\$sdkVersion;$windows\Microsoft.NET\Framework64\v4.0.30319"
        VSINSTALLDIR = $installation
        VCINSTALLDIR = "$installation\VC"
        WindowsSDKLibVersion = $sdkVersion
    }
    foreach ($flag in @('CL', '_CL_', 'LINK', '_LINK_')) {
        if (-not [string]::IsNullOrWhiteSpace([string] $Values[$flag])) { throw "Unexpected compiler/linker flags in $flag." }
    }
    $snapshot = [ordered] @{}
    foreach ($name in $expected.Keys) {
        $value = [string] $Values[$name]
        if ([string]::IsNullOrEmpty($value)) {
            if ($name -in @('VSINSTALLDIR', 'VCINSTALLDIR', 'WindowsSDKLibVersion')) { $snapshot[$name] = ''; continue }
            throw "Missing environment field $name."
        }
        if ($name -in @('VCToolsInstallDir', 'WindowsSdkDir', 'VSINSTALLDIR', 'VCINSTALLDIR')) {
            $value = ConvertTo-PrerequisitePath $value $name
        }
        elseif ($name -in @('INCLUDE', 'LIB', 'LIBPATH')) {
            $value = (@($value.Split(';') | ForEach-Object { ConvertTo-PrerequisitePath $_ $name }) -join ';')
        }
        elseif ($name -in @('VCToolsVersion', 'WindowsSDKVersion', 'WindowsSDKLibVersion')) {
            $value = $value.TrimEnd('\', '/')
        }
        if (-not [string]::Equals($value, $expected[$name], [StringComparison]::OrdinalIgnoreCase)) { throw "Environment field $name differs from the selected compiler/SDK roots." }
        $snapshot[$name] = $expected[$name]
    }
    # An allowlist, not an environment dump: no PATH, credentials or arbitrary vars.
    return [pscustomobject] $snapshot
}

function Get-PrerequisiteToolPins($Descriptor, [string] $QtRoot, [string] $ProgramFiles) {
    $roots = @{
        program_files = $ProgramFiles
        vs2022_community = "$ProgramFiles\Microsoft Visual Studio\2022\Community"
        qt_toolchain_root = $QtRoot
    }
    $selections = @(
        @{ name = 'cmake'; base = 'program_files'; records = $Descriptor.host.tools }
        @{ name = 'ninja'; base = 'vs2022_community'; records = $Descriptor.host.tools }
        @{ name = 'windeployqt'; base = 'qt_toolchain_root'; records = $Descriptor.qt.tools }
        @{ name = 'qtpaths'; base = 'qt_toolchain_root'; records = $Descriptor.qt.selected_artifacts }
    )
    foreach ($selection in $selections) {
        $records = @($selection.records | Where-Object { $_.id -ceq $selection.name })
        if ($records.Count -ne 1) { throw "Missing or duplicated historical tool pin: $($selection.name)" }
        $record = $records[0]
        if ($selection.name -ne 'qtpaths' -and $record.path_base -cne $selection.base) { throw "Unexpected path base: $($selection.name)" }
        $path = ConvertTo-PrerequisitePath (Join-Path $roots[$selection.base] $record.relative_path) $selection.name
        [pscustomobject] @{ name = $selection.name; path = $path; sha256 = $record.sha256; size_bytes = [long] $record.size_bytes }
    }
    # The frozen descriptor has no qmlimportscanner/codegen pin. Do not invent one.
}

function Get-PrerequisiteCacheTools([string] $CacheText, [string] $NinjaPath, [string] $SdkRoot) {
    $expected = [ordered] @{
        CMAKE_MAKE_PROGRAM = @{ name = 'ninja'; path = $NinjaPath }
        CMAKE_RC_COMPILER = @{ name = 'rc'; path = "$SdkRoot\bin\10.0.22621.0\x64\rc.exe" }
        CMAKE_MT = @{ name = 'mt'; path = "$SdkRoot\bin\10.0.22621.0\x64\mt.exe" }
    }
    $result = [ordered] @{}
    foreach ($name in $expected.Keys) {
        $cacheEntries = [regex]::Matches($CacheText, ('(?m)^' + [regex]::Escape($name) + ':[^=\r\n]+=(?<value>[^\r\n]*)\r?$'))
        if ($cacheEntries.Count -ne 1) { throw "Cache requires exactly one $name." }
        $path = ConvertTo-PrerequisitePath $cacheEntries[0].Groups['value'].Value $name
        $wanted = ConvertTo-PrerequisitePath $expected[$name].path $name
        if (-not [string]::Equals($path, $wanted, [StringComparison]::OrdinalIgnoreCase)) { throw "Cache $name differs from the selected executable." }
        $result[$expected[$name].name] = $wanted
    }
    return [pscustomobject] $result
}

function Get-QtShellPackagePrerequisites {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [string] $QtRoot,
        [Parameter(Mandatory)] [string] $CachePath,
        [Parameter(Mandatory)] [string] $CompilerPath,
        [Parameter(Mandatory)] [string] $ToolsetVersion
    )
    if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw 'Package prerequisites require Windows x64 PowerShell.' }
    $repository = ConvertTo-PrerequisitePath $RepositoryRoot 'RepositoryRoot'
    $contractPath = Join-Path $repository 'experiments/m0/provisioning/qt-shell/qt-shell-toolchain-contract.v1.json'
    $contractBytes = Read-PrerequisitePinnedFile $contractPath 'ac00a56584f2aed24732d44f9b26107084d37a73467fc56a860feeb850181952'
    $contract = [Text.Encoding]::UTF8.GetString($contractBytes.bytes) | ConvertFrom-Json
    $descriptorBytes = $null
    foreach ($member in $contract.files) {
        $verified = Read-PrerequisitePinnedFile (Join-Path $repository $member.path) $member.sha256 $member.size_bytes
        if ($member.path -ceq 'experiments/m0/provisioning/qt-shell/qt-shell-toolchain.v1.json') { $descriptorBytes = $verified.bytes }
    }
    $descriptor = [Text.Encoding]::UTF8.GetString($descriptorBytes) | ConvertFrom-Json
    $qt = ConvertTo-PrerequisitePath $QtRoot 'QtRoot'
    $expectedQt = ConvertTo-PrerequisitePath (Join-Path $repository $descriptor.path_roots.qt_toolchain_root) 'QtRoot'
    if (-not [string]::Equals($qt, $expectedQt, [StringComparison]::OrdinalIgnoreCase)) { throw 'QtRoot differs from the frozen toolchain root.' }
    $values = @{}
    foreach ($name in @('VCToolsInstallDir', 'VCToolsVersion', 'WindowsSdkDir', 'WindowsSDKVersion', 'VSCMD_ARG_HOST_ARCH', 'VSCMD_ARG_TGT_ARCH', 'INCLUDE', 'LIB', 'LIBPATH', 'VSINSTALLDIR', 'VCINSTALLDIR', 'WindowsSDKLibVersion', 'CL', '_CL_', 'LINK', '_LINK_')) {
        $values[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    $environment = Get-QtShellPackageEnvironment -Values $values -CompilerPath $CompilerPath -ToolsetVersion $ToolsetVersion -ProgramFilesX86 ([Environment]::GetFolderPath('ProgramFilesX86')) -WindowsDirectory ([Environment]::GetFolderPath('Windows'))
    $pins = @(Get-PrerequisiteToolPins $descriptor $qt ([Environment]::GetFolderPath('ProgramFiles')))
    $cache = [IO.File]::ReadAllText((Get-PrerequisiteRegularFilePath $CachePath))
    $cacheTools = Get-PrerequisiteCacheTools $cache $pins[1].path $environment.WindowsSdkDir
    # RC/MT are constrained by SDK location and presence, not a historical byte pin.
    $null = Get-PrerequisiteRegularFilePath $cacheTools.rc
    $null = Get-PrerequisiteRegularFilePath $cacheTools.mt
    $observed = foreach ($pin in $pins) {
        $verified = Read-PrerequisitePinnedFile $pin.path $pin.sha256 $pin.size_bytes
        [pscustomobject] @{ name = $pin.name; path = $verified.path; sha256 = $verified.sha256; size_bytes = $verified.size_bytes }
    }
    # Compiler/linker/dumpbin identity comes from the separate refresh receipt.
    # This does not authenticate transitive DLL loading, PATH or every SDK/codegen input.
    return [pscustomobject] @{ tools = @($observed); environment = $environment }
}

Export-ModuleMember -Function Get-QtShellPackageEnvironment, Get-QtShellPackagePrerequisites
