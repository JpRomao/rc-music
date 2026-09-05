#Requires -Version 7.0
[CmdletBinding()]
param([string] $MakeNsis, [string] $CasePattern = '*', [string] $RuntimeDirectory)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw 'Windows x64 PowerShell is required' }
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
if ($RuntimeDirectory) { $RuntimeDirectory = (Resolve-Path -LiteralPath $RuntimeDirectory).ProviderPath }
if (-not $MakeNsis) { $MakeNsis = Join-Path $repository 'out/m0/toolchains/nsis-3.12/makensis.exe' }
$MakeNsis = (Resolve-Path -LiteralPath $MakeNsis).ProviderPath
$toolchain = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../nsis-toolchain.json') -Raw | ConvertFrom-Json
if ((Get-FileHash -LiteralPath $MakeNsis -Algorithm SHA256).Hash.ToLowerInvariant() -cne $toolchain.compilerSha256) {
    throw 'The NSIS compiler does not match the recorded local toolchain'
}
$runId = [Guid]::NewGuid().ToString('N')
# The real Qt QML tree adds long relative paths beneath the immutable 64-hex
# version. Keep its isolated fixture prefix short, still under ignored out/m0.
$testOutputBase = if ($RuntimeDirectory) { 'out/m0/it/' } else { 'out/m0/test-runs/rc-music-installer/' }
$testRoot = Join-Path $repository ($testOutputBase + $runId)
$testHive = 'Software\RCMusic.InstallerTests\' + $runId
$registryBase = $testHive + '\Software'
$installerKey = $registryBase + '\RC Music Installer'
$arpKey = $registryBase + '\Microsoft\Windows\CurrentVersion\Uninstall\RC Music'
$classesKey = $registryBase + '\Classes'
$registeredKey = $registryBase + '\RegisteredApplications'
$userChoiceKey = $registryBase + '\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.mp3\UserChoice'
$formatSignature = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData(
        [Text.Encoding]::UTF8.GetBytes("mp3|MP3 audio`nwav|WAV audio`n"))).ToLowerInvariant()
$null = New-Item -ItemType Directory -Path $testRoot
$registry = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
    [Microsoft.Win32.RegistryHive]::CurrentUser, [Microsoft.Win32.RegistryView]::Registry64)
$existing = $registry.OpenSubKey($testHive)
if ($null -ne $existing) { $existing.Dispose(); $registry.Dispose(); throw 'Test hive already exists' }
$registry.CreateSubKey($testHive).Dispose()
$state = @{ Passed = 0; Failed = 0; Number = 0 }

function Assert-Equal($Actual, $Expected) {
    if ($Actual -cne $Expected) { throw "Expected '$Expected', got '$Actual'" }
}
function Assert-TestPath([string] $Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($testRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Filesystem fixture operation escaped the isolated test root'
    }
}
function Assert-TestKey([string] $Key) {
    if (-not $Key.StartsWith($testHive + '\', [StringComparison]::Ordinal)) {
        throw 'Registry fixture operation escaped the isolated test hive'
    }
}
function Set-TestValue([string] $Key, [string] $Name, [string] $Value) {
    Assert-TestKey $Key
    $handle = $registry.CreateSubKey($Key)
    try { $handle.SetValue($Name, $Value, [Microsoft.Win32.RegistryValueKind]::String) }
    finally { $handle.Dispose() }
}
function Get-TestValue([string] $Key, [string] $Name) {
    Assert-TestKey $Key
    $handle = $registry.OpenSubKey($Key)
    try {
        if ($null -eq $handle -or -not ($handle.GetValueNames() -ccontains $Name)) { return $null }
        return [pscustomobject]@{ Kind = $handle.GetValueKind($Name); Value = $handle.GetValue(
                $Name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames) }
    } finally { if ($null -ne $handle) { $handle.Dispose() } }
}
function Assert-RegistryString([string] $Key, [string] $Name, [string] $Expected) {
    $value = Get-TestValue $Key $Name
    if ($null -eq $value) { throw "Missing registry value: $Key [$Name]" }
    Assert-Equal $value.Kind ([Microsoft.Win32.RegistryValueKind]::String)
    Assert-Equal $value.Value $Expected
}
function Assert-RegistryAbsent([string] $Key, [string] $Name) {
    if ($null -ne (Get-TestValue $Key $Name)) { throw "Unexpected registry value: $Key [$Name]" }
}
function Get-RegistrySnapshot([string] $Key = $testHive) {
    if ($Key -cne $testHive) { Assert-TestKey $Key }
    $handle = $registry.OpenSubKey($Key)
    if ($null -eq $handle) { return }
    try {
        foreach ($name in ($handle.GetValueNames() | Sort-Object -CaseSensitive)) {
            [pscustomobject]@{ Key = $Key; Name = $name; Kind = $handle.GetValueKind($name).ToString()
                Value = $handle.GetValue($name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames) } |
                ConvertTo-Json -Compress
        }
        foreach ($child in ($handle.GetSubKeyNames() | Sort-Object -CaseSensitive)) { Get-RegistrySnapshot ($Key + '\' + $child) }
    } finally { $handle.Dispose() }
}
function Reset-TestRegistry {
    if ($testHive -cne ('Software\RCMusic.InstallerTests\' + $runId) -or $runId -cnotmatch '^[0-9a-f]{32}$') {
        throw 'Unsafe registry reset target'
    }
    $registry.DeleteSubKeyTree($testHive, $false)
    $registry.CreateSubKey($testHive).Dispose()
}
function Get-TreeSnapshot([string] $Directory) {
    # Read-only comparison also permits the explicitly supplied runtime source.
    if (-not $RuntimeDirectory -or [IO.Path]::GetFullPath($Directory) -ine $RuntimeDirectory) { Assert-TestPath $Directory }
    if (-not (Test-Path -LiteralPath $Directory)) { return '<absent>' }
    $pending = [Collections.Generic.Stack[IO.FileSystemInfo]]::new()
    $pending.Push((Get-Item -LiteralPath $Directory -Force))
    $entries = [Collections.Generic.List[string]]::new()
    while ($pending.Count -gt 0) {
        $item = $pending.Pop()
        $relative = [IO.Path]::GetRelativePath($Directory, $item.FullName)
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            $entries.Add('link|' + $relative + '|' + $item.LinkTarget)
        } elseif (($item.Attributes -band [IO.FileAttributes]::Directory) -ne 0) {
            $entries.Add('directory|' + $relative)
            foreach ($child in ([IO.DirectoryInfo] $item).EnumerateFileSystemInfos()) { $pending.Push($child) }
        } else {
            $entries.Add('file|' + $relative + '|' + (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash)
        }
    }
    return ($entries | Sort-Object -CaseSensitive) -join "`n"
}
function Invoke-InstallerProcess([string] $Program, [string[]] $Arguments = @(), [string] $RawArguments = '',
    [ValidateRange(1000, 60000)] [int] $TimeoutMilliseconds = 20000) {
    $start = [Diagnostics.ProcessStartInfo]::new($Program)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.WorkingDirectory = $testRoot
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    if ($RawArguments) { $start.Arguments = $RawArguments }
    else { foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) } }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $started = $false
    try {
        $started = $process.Start()
        if (-not $started) { throw 'Could not start isolated installer process' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutMilliseconds)) {
            throw "$([IO.Path]::GetFileName($Program)) exceeded its $TimeoutMilliseconds ms process deadline"
        }
        if (-not [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]] @($stdout, $stderr), 5000)) {
            throw 'Installer output streams remained open after exit'
        }
        return [pscustomobject]@{ ExitCode = $process.ExitCode
            Output = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult() }
    } finally {
        try {
            if ($started -and -not $process.HasExited) {
                $process.Kill($true)
                if (-not $process.WaitForExit(5000)) { throw 'Owned installer process survived cleanup' }
            }
        } finally { $process.Dispose() }
    }
}
function ConvertTo-NsisLiteral([string] $Value, [ValidateRange(1, 2)] [int] $Passes = 1, [switch] $Runtime) {
    if ($Value -match '[\r\n]') { throw 'NSIS fixture literals cannot contain line breaks' }
    # Delay each dollar until the last preprocessing pass. Runtime strings then
    # need two dollars; compile-time File/OutFile paths need one literal dollar.
    $dollar = '${U+24}' + ('{U+24}' * ($Passes - 1))
    if ($Runtime) { $dollar += $dollar }
    return $Value.Replace('$', $dollar).Replace('"', '$\"')
}
function Write-TestText([string] $Path, [string] $Text) {
    Assert-TestPath $Path
    $parent = [IO.Path]::GetDirectoryName($Path)
    if (-not [IO.Directory]::Exists($parent)) { $null = New-Item -ItemType Directory -Path $parent }
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function New-TestInstaller([string] $Directory, [string] $InstallRoot, [string] $Version,
    [string] $SourceRuntime = '', [switch] $FailExtraction, [switch] $LiteralPaths) {
    $files = @()
    if ($SourceRuntime) {
        $manifestPath = Join-Path ([IO.Path]::GetDirectoryName($SourceRuntime.TrimEnd('\', '/'))) 'runtime-manifest.json'
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        if ($manifest.schema_version -ne 1 -or $manifest.build_testing -ne $false -or $manifest.architecture -cne 'x64') {
            throw 'Real installer fixture requires an identified x64 BUILD_TESTING=OFF runtime'
        }
        $relativePaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in $manifest.files) {
            $relative = $entry.relative_path.Replace('/', '\')
            $segments = $relative.Split('\')
            if ([IO.Path]::IsPathRooted($relative) -or $relative -match '[\x00-\x1f<>:"|?*]' -or
                    @($segments | Where-Object { -not $_ -or $_ -in @('.', '..') -or $_ -match '[. ]$' }).Count -gt 0 -or
                    -not $relativePaths.Add($relative) -or $relative -in @('.rcmusic-install.ini', 'Uninstall.exe')) {
                throw 'Unsafe or duplicate real-runtime manifest path'
            }
            $path = Join-Path $SourceRuntime $relative
            if ((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw 'Real-runtime fixture cannot contain reparse files'
            }
            $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($hash -cne $entry.sha256 -or (Get-Item -LiteralPath $path).Length -ne $entry.size_bytes) {
                throw "Real-runtime source does not match manifest: $relative"
            }
            $files += [pscustomobject]@{ Relative = $relative; Source = $path; Hash = $hash }
        }
        if (-not $relativePaths.Contains('RCMusic.exe')) { throw 'Real-runtime manifest does not include RCMusic.exe' }
        $sourceTree = Get-TreeSnapshot $SourceRuntime
        if ($sourceTree -match '(?m)^link\|' -or @($sourceTree -split "`n" | Where-Object { $_.StartsWith('file|') }).Count -ne $files.Count) {
            throw 'Real-runtime source tree has links or files outside its manifest'
        }
    } else {
        $source = Join-Path $Directory ('source-' + $Version)
        $content = [ordered]@{
            'RCMusic.exe' = "inert synthetic executable marker $Version`n"
            'Qt6Core.dll' = "inert synthetic dependency $Version`n"
            'qml\nested 日本\view.qml' = "synthetic nested QML $Version`n"
            'plugins\common\stable.dll' = "synthetic common dependency`n"
        }
        if ($LiteralPaths) { $content['qml\$R0 ${RCMUSIC_PAYLOAD_ID}\日本.qml'] = "literal NSIS path fixture`n" }
        if ($Version -ceq 'A') { $content['plugins\obsolete\old.dll'] = "only A dependency`n" }
        else { $content['plugins\new\added.dll'] = "only B dependency`n" }
        foreach ($entry in $content.GetEnumerator()) {
            $path = Join-Path $source $entry.Key
            Write-TestText $path $entry.Value
            $files += [pscustomobject]@{ Relative = $entry.Key; Source = $path
                Hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
        }
    }
    $identity = ($files | Sort-Object Relative | ForEach-Object { $_.Relative + '|' + $_.Hash }) -join "`n"
    $payloadId = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData(
            [Text.Encoding]::UTF8.GetBytes($identity))).ToLowerInvariant()
    $variant = $Version + $(if ($FailExtraction) { '-fail-extraction' } else { '' })
    $setup = Join-Path $Directory ('setup-' + $variant + '.exe')
    $config = Join-Path $Directory ('config-' + $variant + '.nsh')
    $lines = [Collections.Generic.List[string]]::new()
    foreach ($define in ([ordered]@{ RCMUSIC_OUTPUT = $setup; RCMUSIC_INSTALL_ROOT = $InstallRoot
                RCMUSIC_REGISTRY_BASE = $registryBase; RCMUSIC_PAYLOAD_ID = $payloadId
                RCMUSIC_FORMAT_SIGNATURE = $formatSignature }).GetEnumerator()) {
        $isRuntime = $define.Key -in @('RCMUSIC_INSTALL_ROOT', 'RCMUSIC_REGISTRY_BASE')
        $lines.Add('!define ' + $define.Key + ' "' + (ConvertTo-NsisLiteral $define.Value -Passes 2 -Runtime:$isRuntime) + '"')
    }
    $lines.Add('!define RCMUSIC_ASSOCIATIONS_ENABLED 1')
    $lines.Add('!macro RCMUSIC_FOR_EACH_FORMAT CALLBACK')
    $lines.Add('  !insertmacro ${CALLBACK} "mp3" "MP3 audio"')
    $lines.Add('  !insertmacro ${CALLBACK} "wav" "WAV audio"')
    $lines.Add('!macroend')
    $lines.Add('!macro RCMUSIC_PAYLOAD_EXTRACT')
    $extractedCount = 0
    foreach ($file in $files) {
        $relativeParent = [IO.Path]::GetDirectoryName($file.Relative)
        $output = '$INSTDIR' + $(if ($relativeParent) { '\' + (ConvertTo-NsisLiteral $relativeParent -Runtime) } else { '' })
        $lines.Add('  SetOutPath "' + $output + '"')
        $lines.Add('  File "/oname=' + (ConvertTo-NsisLiteral ([IO.Path]::GetFileName($file.Relative)) -Runtime) + '" "' +
            (ConvertTo-NsisLiteral $file.Source) + '"')
        $extractedCount++
        if ($FailExtraction -and $extractedCount -eq 1) {
            # Compile-time fixture injection only; no product runtime test flag.
            $lines.Add('  Goto rcmusic_install_failed')
        }
    }
    $lines.Add('!macroend')
    $fileTokens = [Collections.Generic.List[string]]::new()
    for ($index = 0; $index -lt $files.Count; $index++) {
        $token = 'RCMUSIC_PATH_F' + ($index + 1).ToString('D4')
        $fileTokens.Add($token)
        $lines.Add('!define ' + $token + ' "' + (ConvertTo-NsisLiteral $files[$index].Relative -Passes 2 -Runtime) + '"')
    }
    $lines.Add('!macro RCMUSIC_PAYLOAD_FILES CALLBACK')
    for ($index = 0; $index -lt $files.Count; $index++) {
        $lines.Add('  !insertmacro ${CALLBACK} ' + $fileTokens[$index] + ' "' + $files[$index].Hash + '"')
    }
    $lines.Add('!macroend')
    $directories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($file in $files) {
        $directoryName = [IO.Path]::GetDirectoryName($file.Relative)
        while ($directoryName) {
            $null = $directories.Add($directoryName)
            $directoryName = [IO.Path]::GetDirectoryName($directoryName)
        }
    }
    $directoryTokens = [Collections.Generic.List[string]]::new()
    $orderedDirectories = @($directories | Sort-Object { $_.Length } -Descending)
    for ($index = 0; $index -lt $orderedDirectories.Count; $index++) {
        $token = 'RCMUSIC_PATH_D' + ($index + 1).ToString('D4')
        $directoryTokens.Add($token)
        $lines.Add('!define ' + $token + ' "' + (ConvertTo-NsisLiteral $orderedDirectories[$index] -Passes 2 -Runtime) + '"')
    }
    $lines.Add('!macro RCMUSIC_PAYLOAD_DIRECTORIES CALLBACK')
    foreach ($token in $directoryTokens) {
        $lines.Add('  !insertmacro ${CALLBACK} ' + $token)
    }
    $lines.Add('!macroend')
    Write-TestText $config (($lines -join "`n") + "`n")
    # Compiling the full Qt payload has a separate build deadline; the actual
    # install/uninstall processes keep the same 20-second behavioral timeout.
    $compileTimeout = if ($SourceRuntime) { 60000 } else { 20000 }
    $compiled = Invoke-InstallerProcess $MakeNsis @('/V2', '/WX', '/INPUTCHARSET', 'UTF8', ('/DRCMUSIC_CONFIG=' + (ConvertTo-NsisLiteral $config)),
        (Join-Path $PSScriptRoot '../RcMusicInstaller.nsi')) -TimeoutMilliseconds $compileTimeout
    if ($compiled.ExitCode -ne 0) { throw ('NSIS fixture compilation failed: ' + $compiled.Output) }
    return [pscustomobject]@{ Setup = $setup; InstallRoot = $InstallRoot; Directory = (Join-Path $InstallRoot $payloadId)
        PayloadId = $payloadId; Files = $files; FixtureDirectory = $Directory }
}
function New-Scenario([bool] $WithUpgrade = $false, [string] $SourceRuntime = '', [switch] $LiteralPaths) {
    $state.Number++
    $directory = Join-Path $testRoot ('case-' + $state.Number + ' & (日本)')
    $root = if ($SourceRuntime) { Join-Path $testRoot 'r 日本' } else { Join-Path $directory 'install root' }
    $appA = New-TestInstaller $directory $root $(if ($SourceRuntime) { 'Real' } else { 'A' }) $SourceRuntime -LiteralPaths:$LiteralPaths
    $appB = if ($WithUpgrade) { New-TestInstaller $directory $root 'B' } else { $null }
    return [pscustomobject]@{ Directory = $directory; Root = $root; A = $appA; B = $appB }
}
function Install-App($App, [int] $Expected = 0) {
    $result = Invoke-InstallerProcess $App.Setup @('/S')
    if ($result.ExitCode -ne $Expected) { throw "Installer expected $Expected, got $($result.ExitCode). $($result.Output)" }
}
function Copy-Uninstaller($App) {
    $copy = Join-Path $App.FixtureDirectory ('uninstall-copy-' + [Guid]::NewGuid().ToString('N') + '.exe')
    Copy-Item -LiteralPath (Join-Path $App.Directory 'Uninstall.exe') -Destination $copy
    return $copy
}
function Uninstall-App($App, [int] $Expected = 0, [string] $Executable = '') {
    if (-not $Executable) { $Executable = Copy-Uninstaller $App }
    # Copy outside the version to allow deletion of the installed Uninstall.exe.
    # _?= is the final raw tail, not shell syntax: keep one retained process.
    $result = Invoke-InstallerProcess $Executable @() ('/S _?=' + $App.Directory)
    if ($result.ExitCode -ne $Expected) { throw "Uninstaller expected $Expected, got $($result.ExitCode). $($result.Output)" }
}
function Assert-Payload($App) {
    foreach ($file in $App.Files) {
        $installed = Join-Path $App.Directory $file.Relative
        if (-not [IO.File]::Exists($installed)) { throw "Missing installed file: $($file.Relative)" }
        Assert-Equal (Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash.ToLowerInvariant() $file.Hash
    }
    $markerPath = Join-Path $App.Directory '.rcmusic-install.ini'
    if (-not [IO.File]::Exists($markerPath)) { throw 'Missing installation owner marker' }
    $marker = @{}
    $inSection = $false
    foreach ($line in (Get-Content -LiteralPath $markerPath)) {
        if ($line -match '^\[(.+)\]$') { $inSection = $Matches[1] -ceq 'RC Music'; continue }
        if ($inSection -and $line -match '^([^=]+)=(.*)$') { $marker[$Matches[1]] = $Matches[2] }
    }
    Assert-Equal $marker.Product 'RCMusicInstaller-v1'
    Assert-Equal $marker.PayloadId $App.PayloadId
    Assert-Equal $marker.FormatSignature $formatSignature
    Assert-Equal $marker.UninstallerHash.ToLowerInvariant() (
        Get-FileHash -LiteralPath (Join-Path $App.Directory 'Uninstall.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Assert-Installed($App) {
    Assert-Payload $App
    Assert-RegistryString $installerKey 'Product' 'RCMusicInstaller-v1'
    Assert-RegistryString $installerKey 'ActivePayload' $App.PayloadId
    Assert-RegistryString $installerKey 'FormatSignature' $formatSignature
    Assert-RegistryString $arpKey 'DisplayName' 'RC Music'
    Assert-RegistryString $arpKey 'DisplayVersion' $App.PayloadId
    Assert-RegistryString $arpKey 'InstallLocation' $App.Directory
    Assert-RegistryString $arpKey 'UninstallString' ('"' + (Join-Path $App.Directory 'Uninstall.exe') + '"')
    $exe = Join-Path $App.Directory 'RCMusic.exe'
    Assert-RegistryString ($registryBase + '\RC Music') 'AssociationOwner' $exe
    foreach ($extension in @('mp3', 'wav')) {
        Assert-RegistryString ($classesKey + '\RCMusic.Audio.' + $extension + '\shell\open\command') '' ('"' + $exe + '" -- "%1"')
    }
}
function Set-ForeignSentinels($Scenario) {
    Write-TestText (Join-Path $Scenario.Root 'media\external.mp3') 'synthetic external media, never played'
    Write-TestText (Join-Path $Scenario.Root 'preferences.json') '{"foreign":"preserve"}'
    Write-TestText (Join-Path $Scenario.Root 'sentinel.txt') 'external sentinel'
    Set-TestValue $userChoiceKey 'ProgId' 'Foreign.Player'
    Set-TestValue $userChoiceKey 'Hash' 'synthetic-userchoice-never-touch'
    Set-TestValue ($classesKey + '\.mp3') '' 'Foreign.Player'
    Set-TestValue $registeredKey 'Foreign Player' 'Software\Foreign\Capabilities'
}
function Assert-ForeignSentinels($Scenario) {
    Assert-Equal ([IO.File]::ReadAllText((Join-Path $Scenario.Root 'media\external.mp3'))) 'synthetic external media, never played'
    Assert-Equal ([IO.File]::ReadAllText((Join-Path $Scenario.Root 'preferences.json'))) '{"foreign":"preserve"}'
    Assert-Equal ([IO.File]::ReadAllText((Join-Path $Scenario.Root 'sentinel.txt'))) 'external sentinel'
    Assert-RegistryString $userChoiceKey 'ProgId' 'Foreign.Player'
    Assert-RegistryString $userChoiceKey 'Hash' 'synthetic-userchoice-never-touch'
    Assert-RegistryString ($classesKey + '\.mp3') '' 'Foreign.Player'
    Assert-RegistryString $registeredKey 'Foreign Player' 'Software\Foreign\Capabilities'
}
function Test-Case([string] $Name, [scriptblock] $Body) {
    if ($Name -notlike $CasePattern) { return }
    Reset-TestRegistry
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}

try {
    Test-Case 'installs nested authenticated payload, ownership, ARP and isolated associations; reinstall is idempotent' {
        $case = New-Scenario
        Set-ForeignSentinels $case
        Install-App $case.A
        Assert-Installed $case.A
        $beforeFiles = Get-TreeSnapshot $case.Root
        $beforeRegistry = @(Get-RegistrySnapshot) -join "`n"
        Install-App $case.A
        Assert-Installed $case.A
        Assert-Equal (Get-TreeSnapshot $case.Root) $beforeFiles
        Assert-Equal (@(Get-RegistrySnapshot) -join "`n") $beforeRegistry
        Assert-ForeignSentinels $case
    }
    Test-Case 'upgrade removes old version and obsolete dependencies; stale uninstaller cannot remove B' {
        $case = New-Scenario $true
        Set-ForeignSentinels $case
        Install-App $case.A
        Assert-Installed $case.A
        $stale = Copy-Uninstaller $case.A
        Install-App $case.B
        Assert-Installed $case.B
        if (Test-Path -LiteralPath $case.A.Directory) { throw 'Old version or obsolete dependencies survived upgrade' }
        if (Test-Path -LiteralPath (Join-Path $case.B.Directory 'plugins\obsolete\old.dll')) { throw 'Obsolete file leaked into B' }
        $beforeFiles = Get-TreeSnapshot $case.Root
        $beforeRegistry = @(Get-RegistrySnapshot) -join "`n"
        Uninstall-App $case.A -Executable $stale
        Assert-Equal (Get-TreeSnapshot $case.Root) $beforeFiles
        Assert-Equal (@(Get-RegistrySnapshot) -join "`n") $beforeRegistry
        Assert-ForeignSentinels $case
    }
    Test-Case 'literal dollar variables and preprocessor tokens survive compilation, installation and uninstall' {
        $case = New-Scenario -LiteralPaths
        Install-App $case.A
        Assert-Installed $case.A
        Assert-Equal ([IO.File]::ReadAllText((Join-Path $case.A.Directory 'qml\$R0 ${RCMUSIC_PAYLOAD_ID}\日本.qml'))) "literal NSIS path fixture`n"
        Uninstall-App $case.A
        if (Test-Path -LiteralPath $case.A.Directory) { throw 'Literal-path payload was not fully removed' }
    }
    Test-Case 'uninstall B removes only owned payload and registration, preserving media, preferences and foreign values' {
        $case = New-Scenario $true
        Set-ForeignSentinels $case
        $baselineFiles = Get-TreeSnapshot $case.Root
        $baselineRegistry = @(Get-RegistrySnapshot) -join "`n"
        Install-App $case.A
        Install-App $case.B
        Assert-Installed $case.B
        Uninstall-App $case.B
        Assert-Equal (Get-TreeSnapshot $case.Root) $baselineFiles
        Assert-Equal (@(Get-RegistrySnapshot) -join "`n") $baselineRegistry
        Assert-ForeignSentinels $case
    }
    Test-Case 'unmarked version collision is rejected without changing files or registry' {
        $case = New-Scenario
        Write-TestText (Join-Path $case.A.Directory 'foreign.txt') 'unmarked target belongs to someone else'
        $beforeFiles = Get-TreeSnapshot $case.Root
        $beforeRegistry = @(Get-RegistrySnapshot) -join "`n"
        Install-App $case.A 1
        Assert-Equal (Get-TreeSnapshot $case.Root) $beforeFiles
        Assert-Equal (@(Get-RegistrySnapshot) -join "`n") $beforeRegistry
    }
    Test-Case 'junction install root is rejected without mutating the linked destination' {
        $case = New-Scenario
        $external = Join-Path $case.Directory 'external junction target'
        Write-TestText (Join-Path $external 'sentinel.txt') 'junction target must remain untouched'
        Assert-TestPath $case.Root
        $null = New-Item -ItemType Junction -Path $case.Root -Target $external
        $beforeFiles = Get-TreeSnapshot $external
        $beforeRegistry = @(Get-RegistrySnapshot) -join "`n"
        Install-App $case.A 1
        Assert-Equal (Get-TreeSnapshot $external) $beforeFiles
        Assert-Equal (@(Get-RegistrySnapshot) -join "`n") $beforeRegistry
    }
    Test-Case 'modified owned file rejects uninstall before partial deletion' {
        $case = New-Scenario
        Install-App $case.A
        Assert-Installed $case.A
        Write-TestText (Join-Path $case.A.Directory 'qml\nested 日本\view.qml') 'user modification must survive'
        $beforeFiles = Get-TreeSnapshot $case.Root
        $beforeRegistry = @(Get-RegistrySnapshot) -join "`n"
        Uninstall-App $case.A 1
        Assert-Equal (Get-TreeSnapshot $case.Root) $beforeFiles
        Assert-Equal (@(Get-RegistrySnapshot) -join "`n") $beforeRegistry
    }
    Test-Case 'locked owned file rejects uninstall before partial deletion' {
        $case = New-Scenario
        Install-App $case.A
        Assert-Installed $case.A
        $beforeFiles = Get-TreeSnapshot $case.Root
        $beforeRegistry = @(Get-RegistrySnapshot) -join "`n"
        $locked = [IO.File]::Open((Join-Path $case.A.Directory 'plugins\obsolete\old.dll'),
            [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        try { Uninstall-App $case.A 1 } finally { $locked.Dispose() }
        Assert-Equal (Get-TreeSnapshot $case.Root) $beforeFiles
        Assert-Equal (@(Get-RegistrySnapshot) -join "`n") $beforeRegistry
    }
    Test-Case 'modified old version rejects upgrade without publishing B or deleting A' {
        $case = New-Scenario $true
        Install-App $case.A
        Assert-Installed $case.A
        Write-TestText (Join-Path $case.A.Directory 'plugins\obsolete\old.dll') 'modified dependency'
        $beforeFiles = Get-TreeSnapshot $case.Root
        $beforeRegistry = @(Get-RegistrySnapshot) -join "`n"
        Install-App $case.B 1
        Assert-Equal (Get-TreeSnapshot $case.Root) $beforeFiles
        Assert-Equal (@(Get-RegistrySnapshot) -join "`n") $beforeRegistry
    }
    Test-Case 'locked old version rejects upgrade without partial cleanup' {
        $case = New-Scenario $true
        Install-App $case.A
        Assert-Installed $case.A
        $beforeFiles = Get-TreeSnapshot $case.Root
        $beforeRegistry = @(Get-RegistrySnapshot) -join "`n"
        $locked = [IO.File]::Open((Join-Path $case.A.Directory 'plugins\obsolete\old.dll'),
            [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        try { Install-App $case.B 1 } finally { $locked.Dispose() }
        Assert-Equal (Get-TreeSnapshot $case.Root) $beforeFiles
        Assert-Equal (@(Get-RegistrySnapshot) -join "`n") $beforeRegistry
    }
    Test-Case 'changed ownership marker rejects uninstall without partial deletion' {
        $case = New-Scenario
        Install-App $case.A
        Assert-Installed $case.A
        $marker = Join-Path $case.A.Directory '.rcmusic-install.ini'
        Write-TestText $marker ([IO.File]::ReadAllText($marker).Replace('RCMusicInstaller-v1', 'ForeignInstaller-v1'))
        $beforeFiles = Get-TreeSnapshot $case.Root
        $beforeRegistry = @(Get-RegistrySnapshot) -join "`n"
        Uninstall-App $case.A 1
        Assert-Equal (Get-TreeSnapshot $case.Root) $beforeFiles
        Assert-Equal (@(Get-RegistrySnapshot) -join "`n") $beforeRegistry
    }
    Test-Case 'association collision preserves installed payload and permits an explicit retry' {
        $case = New-Scenario
        Set-TestValue $registeredKey 'RC Music' 'Software\Foreign\Capabilities'
        Install-App $case.A 1
        Assert-Payload $case.A
        Assert-RegistryString $registeredKey 'RC Music' 'Software\Foreign\Capabilities'
        $key = $registry.OpenSubKey($registeredKey, $true)
        try { $key.DeleteValue('RC Music', $true) } finally { $key.Dispose() }
        Install-App $case.A
        Assert-Installed $case.A
        Uninstall-App $case.A
        if (Test-Path -LiteralPath $case.A.Directory) { throw 'Retried installation left an owned payload after uninstall' }
        Assert-RegistryAbsent $installerKey 'ActivePayload'
        Assert-RegistryAbsent $arpKey 'DisplayName'
        Assert-RegistryAbsent $registeredKey 'RC Music'
    }
    Test-Case 'pending cleanup blocks uninstall of the active version without any mutation' {
        $case = New-Scenario $true
        Install-App $case.B
        Assert-Installed $case.B
        if (Test-Path -LiteralPath $case.A.Directory) { throw 'Pending-cleanup fixture must not install A' }
        Set-TestValue $installerKey 'PendingCleanup' $case.A.PayloadId
        $beforeFiles = Get-TreeSnapshot $case.Root
        $beforeRegistry = @(Get-RegistrySnapshot) -join "`n"
        Uninstall-App $case.B 1
        Assert-Equal (Get-TreeSnapshot $case.Root) $beforeFiles
        Assert-Equal (@(Get-RegistrySnapshot) -join "`n") $beforeRegistry
    }
    Test-Case 'failed fresh extraction removes only its partial version and permits the same payload retry' {
        $case = New-Scenario
        Set-ForeignSentinels $case
        $failure = New-TestInstaller $case.Directory $case.Root 'A' -FailExtraction
        Assert-Equal $failure.PayloadId $case.A.PayloadId
        $beforeFiles = Get-TreeSnapshot $case.Root
        $beforeRegistry = @(Get-RegistrySnapshot) -join "`n"
        Install-App $failure 1
        Assert-Equal (Get-TreeSnapshot $case.Root) $beforeFiles
        Assert-Equal (@(Get-RegistrySnapshot) -join "`n") $beforeRegistry
        Install-App $case.A
        Assert-Installed $case.A
        Assert-ForeignSentinels $case
    }
    Test-Case 'uninstall preserves an unknown file inside the owned version while removing every owned file' {
        $case = New-Scenario
        Install-App $case.A
        Assert-Installed $case.A
        $unknown = Join-Path $case.A.Directory 'user-note.txt'
        Write-TestText $unknown 'unknown user file must survive'
        Uninstall-App $case.A
        Assert-Equal ([IO.File]::ReadAllText($unknown)) 'unknown user file must survive'
        foreach ($relative in @($case.A.Files.Relative) + @('.rcmusic-install.ini', 'Uninstall.exe')) {
            if (Test-Path -LiteralPath (Join-Path $case.A.Directory $relative)) { throw "Owned file survived uninstall: $relative" }
        }
        Assert-RegistryAbsent $installerKey 'ActivePayload'
        Assert-RegistryAbsent $arpKey 'DisplayName'
        Assert-RegistryAbsent $registeredKey 'RC Music'
    }
    if ($RuntimeDirectory) {
        Test-Case 'real runtime installs and uninstalls with exact bytes' {
            $sourceBefore = Get-TreeSnapshot $RuntimeDirectory
            $case = New-Scenario -SourceRuntime $RuntimeDirectory
            Set-ForeignSentinels $case
            $baselineFiles = Get-TreeSnapshot $case.Root
            $baselineRegistry = @(Get-RegistrySnapshot) -join "`n"
            Install-App $case.A
            Assert-Installed $case.A
            $installedTree = Get-TreeSnapshot $case.A.Directory
            Assert-Equal @($installedTree -split "`n" | Where-Object { $_.StartsWith('file|') }).Count ($case.A.Files.Count + 2)
            Assert-Equal (Get-TreeSnapshot $RuntimeDirectory) $sourceBefore
            Uninstall-App $case.A
            Assert-Equal (Get-TreeSnapshot $case.Root) $baselineFiles
            Assert-Equal (@(Get-RegistrySnapshot) -join "`n") $baselineRegistry
            Assert-ForeignSentinels $case
            Assert-Equal (Get-TreeSnapshot $RuntimeDirectory) $sourceBefore
            Write-Host "Verified $($case.A.Files.Count) real runtime files; source unchanged; RCMusic.exe was never launched"
        }
    }
} finally {
    # Retain all generated binaries/fixtures under ignored out/m0. Never recurse
    # through or remove an install root/junction from this harness. Only the
    # fresh GUID registry subtree is removed, not any real association root.
    try {
        if ($testHive -cne ('Software\RCMusic.InstallerTests\' + $runId) -or $runId -cnotmatch '^[0-9a-f]{32}$') {
            throw 'Unsafe registry cleanup target'
        }
        $registry.DeleteSubKeyTree($testHive, $false)
    } finally { $registry.Dispose() }
}
Write-Host "Installer contracts: $($state.Passed) passed, $($state.Failed) failed. Outputs: $testRoot"
if ($state.Passed + $state.Failed -eq 0) { throw 'No installer test matched CasePattern' }
if ($state.Failed -gt 0) { exit 1 }
