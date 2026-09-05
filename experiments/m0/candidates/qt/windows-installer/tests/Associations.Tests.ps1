#Requires -Version 7.0
[CmdletBinding()]
param([string] $MakeNsis)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
if (-not $MakeNsis) { $MakeNsis = Join-Path $repository 'out/m0/toolchains/nsis-3.12/makensis.exe' }
$MakeNsis = (Resolve-Path -LiteralPath $MakeNsis).Path
$toolchain = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../nsis-toolchain.json') -Raw | ConvertFrom-Json
if ((Get-FileHash -LiteralPath $MakeNsis -Algorithm SHA256).Hash.ToLowerInvariant() -cne $toolchain.compilerSha256) {
    throw 'The NSIS compiler does not match the recorded local toolchain'
}
$runId = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path $repository ('out/m0/test-runs/rc-music-associations/' + $runId)
$null = New-Item -ItemType Directory -Path $testRoot
$testHive = 'Software\RCMusic.AssociationTests\' + $runId
$registryBase = $testHive + '\Software'
$registry = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
    [Microsoft.Win32.RegistryHive]::CurrentUser, [Microsoft.Win32.RegistryView]::Registry64)
$existingTestHive = $registry.OpenSubKey($testHive)
if ($null -ne $existingTestHive) { $existingTestHive.Dispose(); throw 'Test hive already exists' }
$null = $registry.CreateSubKey($testHive).Dispose()
$state = @{ Passed = 0; Failed = 0 }

function Assert-Equal($Actual, $Expected) {
    if ($Actual -cne $Expected) { throw "Expected '$Expected', got '$Actual'" }
}
function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Assert-TestKey([string] $Key) {
    if (-not $Key.StartsWith($testHive + '\', [StringComparison]::Ordinal)) {
        throw 'Registry operation escaped the isolated test hive'
    }
}
function Set-TestValue([string] $Key, [string] $Name, [string] $Value,
    [Microsoft.Win32.RegistryValueKind] $Kind = [Microsoft.Win32.RegistryValueKind]::String) {
    Assert-TestKey $Key
    $handle = $registry.CreateSubKey($Key)
    try { $handle.SetValue($Name, $Value, $Kind) } finally { $handle.Dispose() }
}
function Get-TestValue([string] $Key, [string] $Name) {
    Assert-TestKey $Key
    $handle = $registry.OpenSubKey($Key)
    try {
        if ($null -eq $handle -or -not ($handle.GetValueNames() -ccontains $Name)) { return $null }
        return [pscustomobject]@{
            Kind = $handle.GetValueKind($Name)
            Value = $handle.GetValue($Name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        }
    } finally { if ($null -ne $handle) { $handle.Dispose() } }
}
function Assert-String([string] $Key, [string] $Name, [string] $Expected) {
    $entry = Get-TestValue $Key $Name
    if ($null -eq $entry) { throw "Missing registry value: $Key [$Name]" }
    Assert-Equal $entry.Kind ([Microsoft.Win32.RegistryValueKind]::String)
    Assert-Equal $entry.Value $Expected
}
function Assert-Absent([string] $Key, [string] $Name) {
    if ($null -ne (Get-TestValue $Key $Name)) { throw "Unexpected registry value: $Key [$Name]" }
}
function Reset-TestRegistry {
    if ($testHive -cne ('Software\RCMusic.AssociationTests\' + $runId) -or $runId -cnotmatch '^[0-9a-f]{32}$') {
        throw 'Unsafe registry reset target'
    }
    $registry.DeleteSubKeyTree($testHive, $false)
    $registry.CreateSubKey($testHive).Dispose()
}
function Get-TestSnapshot([string] $Key = $testHive) {
    $handle = $registry.OpenSubKey($Key)
    if ($null -eq $handle) { return }
    try {
        foreach ($name in ($handle.GetValueNames() | Sort-Object -CaseSensitive)) {
            [pscustomobject]@{ Key = $Key; Name = $name; Kind = $handle.GetValueKind($name).ToString()
                Value = $handle.GetValue($name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames) } |
                ConvertTo-Json -Compress
        }
        foreach ($child in ($handle.GetSubKeyNames() | Sort-Object -CaseSensitive)) {
            Get-TestSnapshot ($Key + '\' + $child)
        }
    } finally { $handle.Dispose() }
}
function Invoke-TestProcess([string] $Program, [string[]] $Arguments, [string] $RawArguments = '') {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Program
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    if ($RawArguments) { $start.Arguments = $RawArguments }
    else { foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) } }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { throw 'Unable to start contract process' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(20000)) { $process.Kill(); $process.WaitForExit(); throw 'Contract process timed out' }
        $output = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
        return [pscustomobject]@{ ExitCode = $process.ExitCode; Output = $output }
    } finally { $process.Dispose() }
}
function New-TestInstaller([string] $Name) {
    $directory = Join-Path $testRoot ($Name + ' & (日本)')
    $installer = Join-Path $testRoot ($Name + '-setup.exe')
    $result = Invoke-TestProcess $MakeNsis @('/V2', '/WX',
        ('/DRCMUSIC_TEST_PREFIX=' + $testHive),
        ('/DRCMUSIC_TEST_INSTALL_DIR=' + $directory),
        ('/DRCMUSIC_TEST_OUTPUT=' + $installer),
        (Join-Path $PSScriptRoot 'AssociationsHarness.nsi'))
    if ($result.ExitCode -ne 0) { throw ('NSIS compile failed: ' + $result.Output) }
    return [pscustomobject]@{ Directory = $directory; Setup = $installer; Exe = (Join-Path $directory 'RCMusic.exe') }
}
function Install-TestApp($App, [int] $ExpectedCode = 0) {
    $result = Invoke-TestProcess $App.Setup @('/S')
    if ($result.ExitCode -ne $ExpectedCode) {
        throw "Install $($App.Directory): expected $ExpectedCode, got $($result.ExitCode). $($result.Output)"
    }
}
function Uninstall-TestApp($App, [int] $ExpectedCode = 0) {
    # NSIS requires _?= as the last, unquoted tail to run the actual uninstaller
    # synchronously instead of starting a temporary copy and returning early.
    $result = Invoke-TestProcess (Join-Path $App.Directory 'Uninstall.exe') @() ('/S _?=' + $App.Directory)
    if ($result.ExitCode -ne $ExpectedCode) {
        throw "Uninstall $($App.Directory): expected $ExpectedCode, got $($result.ExitCode). $($result.Output)"
    }
}

function Get-TestAclSnapshot($Security) {
    return [pscustomobject]@{
        Protected = $Security.AreAccessRulesProtected
        Rules = @($Security.GetAccessRules($true, $true, [Security.Principal.SecurityIdentifier]) | ForEach-Object {
            [pscustomobject]@{ Identity = $_.IdentityReference.Value; Rights = [int] $_.RegistryRights
                Type = [int] $_.AccessControlType; Inherited = $_.IsInherited
                Inheritance = [int] $_.InheritanceFlags; Propagation = [int] $_.PropagationFlags }
        })
    } | ConvertTo-Json -Compress -Depth 4
}

function With-TestWriteDenied([string] $Key, [scriptblock] $Body) {
    Assert-TestKey $Key
    $handle = $registry.CreateSubKey($Key, [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree)
    $beforeAcl = [Microsoft.Win32.RegistryAclExtensions]::GetAccessControl($handle)
    $accessSection = [Security.AccessControl.AccessControlSections]::Access
    $beforeDescriptor = $beforeAcl.GetSecurityDescriptorBinaryForm()
    $beforeSnapshot = Get-TestAclSnapshot $beforeAcl
    $deniedAcl = [Microsoft.Win32.RegistryAclExtensions]::GetAccessControl($handle)
    $rule = [Security.AccessControl.RegistryAccessRule]::new(
        [Security.Principal.WindowsIdentity]::GetCurrent().User,
        [Security.AccessControl.RegistryRights]::SetValue, [Security.AccessControl.AccessControlType]::Deny)
    $deniedAcl.AddAccessRule($rule)
    try {
        [Microsoft.Win32.RegistryAclExtensions]::SetAccessControl($handle, $deniedAcl)
        & $Body
    } finally {
        try {
            # A freshly read RegistrySecurity has no modified sections, so passing
            # it straight to SetAccessControl is a no-op. Explicitly mark Access.
            $restoreAcl = [Security.AccessControl.RegistrySecurity]::new()
            $restoreAcl.SetSecurityDescriptorBinaryForm($beforeDescriptor, $accessSection)
            [Microsoft.Win32.RegistryAclExtensions]::SetAccessControl($handle, $restoreAcl)
            $restoredAcl = [Microsoft.Win32.RegistryAclExtensions]::GetAccessControl($handle)
            if ((Get-TestAclSnapshot $restoredAcl) -cne $beforeSnapshot) {
                throw 'Test DACL restoration failed'
            }
        } finally { $handle.Dispose() }
    }
}

try {
    $appA = New-TestInstaller 'installation-A'
    $appB = New-TestInstaller 'installation-B'
    $capabilities = $registryBase + '\RC Music\Capabilities'
    $registered = $registryBase + '\RegisteredApplications'
    $classes = $registryBase + '\Classes'
    $userChoice = $registryBase + '\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.mp3\UserChoice'
    Test-Case 'removes a previously installed owned registration rather than doing nothing' {
        # Seed a valid previous installation independently of the register function.
        Install-TestApp $appA
        Set-TestValue ($registryBase + '\RC Music') 'AssociationProduct' 'RCMusic-v1'
        Set-TestValue ($registryBase + '\RC Music') 'AssociationOwner' $appA.Exe
        Set-TestValue $capabilities 'ApplicationName' 'RC Music'
        Set-TestValue $capabilities 'ApplicationDescription' 'Reprodutor de áudio local'
        Set-TestValue $registered 'RC Music' $capabilities
        foreach ($extension in @('mp3', 'wav')) {
            $progId = 'RCMusic.Audio.' + $extension
            Set-TestValue ($classes + '\' + $progId) 'AssociationProduct' 'RCMusic-v1'
            Set-TestValue ($classes + '\' + $progId) 'AssociationOwner' $appA.Exe
            Set-TestValue ($classes + '\' + $progId) '' ($extension.ToUpperInvariant() + ' audio')
            Set-TestValue ($classes + '\' + $progId + '\shell\open\command') '' ('"' + $appA.Exe + '" -- "%1"')
            Set-TestValue ($classes + '\.' + $extension + '\OpenWithProgids') $progId ''
            Set-TestValue ($capabilities + '\FileAssociations') ('.' + $extension) $progId
        }
        Uninstall-TestApp $appA
        Assert-Absent $registered 'RC Music'
        Assert-Absent ($capabilities + '\FileAssociations') '.mp3'
        Assert-Absent ($classes + '\.mp3\OpenWithProgids') 'RCMusic.Audio.mp3'
        Assert-Absent ($classes + '\RCMusic.Audio.mp3\shell\open\command') ''
    }
    Reset-TestRegistry
    Set-TestValue ($classes + '\.mp3') '' 'Foreign.Player'
    Set-TestValue ($classes + '\.mp3\OpenWithProgids') 'Foreign.Player' ''
    Set-TestValue $userChoice 'ProgId' 'Foreign.Player'
    Set-TestValue $userChoice 'Hash' 'sentinel-never-touch'
    Set-TestValue $registered 'Foreign Player' 'Software\Foreign\Capabilities'

    Test-Case 'registers the explicit formats and quoted executable without claiming defaults' {
        Install-TestApp $appA
        Assert-String $capabilities 'ApplicationName' 'RC Music'
        $description = Get-TestValue $capabilities 'ApplicationDescription'
        if ($null -eq $description -or [string]::IsNullOrWhiteSpace($description.Value)) { throw 'Description missing' }
        Assert-String $registered 'RC Music' $capabilities
        foreach ($extension in @('mp3', 'wav')) {
            $progId = 'RCMusic.Audio.' + $extension
            Assert-String ($capabilities + '\FileAssociations') ('.' + $extension) $progId
            Assert-String ($classes + '\.' + $extension + '\OpenWithProgids') $progId ''
            Assert-String ($classes + '\' + $progId + '\shell\open\command') '' ('"' + $appA.Exe + '" -- "%1"')
        }
        Assert-Absent ($capabilities + '\FileAssociations') '.flac'
    }
    Test-Case 'does not change existing defaults, user choice, or third-party entries' {
        Assert-String ($classes + '\.mp3') '' 'Foreign.Player'
        Assert-String ($classes + '\.mp3\OpenWithProgids') 'Foreign.Player' ''
        Assert-String $userChoice 'ProgId' 'Foreign.Player'
        Assert-String $userChoice 'Hash' 'sentinel-never-touch'
        Assert-String $registered 'Foreign Player' 'Software\Foreign\Capabilities'
    }
    Test-Case 'reinstall is idempotent and upgrade publishes the new executable' {
        Install-TestApp $appA
        Install-TestApp $appB
        Assert-String ($classes + '\RCMusic.Audio.mp3\shell\open\command') '' ('"' + $appB.Exe + '" -- "%1"')
        Assert-String ($registryBase + '\RC Music') 'AssociationOwner' $appB.Exe
    }
    Test-Case 'an old uninstaller cannot remove the newer installation registration' {
        Uninstall-TestApp $appA
        Assert-String $registered 'RC Music' $capabilities
        Assert-String ($capabilities + '\FileAssociations') '.mp3' 'RCMusic.Audio.mp3'
        Assert-String ($classes + '\.mp3\OpenWithProgids') 'RCMusic.Audio.mp3' ''
        Assert-String ($classes + '\RCMusic.Audio.mp3\shell\open\command') '' ('"' + $appB.Exe + '" -- "%1"')
    }
    Test-Case 'uninstall removes its unchanged values but preserves edits and unknown values' {
        Set-TestValue ($classes + '\RCMusic.Audio.mp3') 'ForeignValue' 'preserve-me'
        Set-TestValue ($classes + '\RCMusic.Audio.mp3\shell\open\command') '' 'foreign modified command'
        Set-TestValue ($capabilities + '\FileAssociations') '.wav' 'Foreign.Wave'
        Uninstall-TestApp $appB
        Assert-Absent $registered 'RC Music'
        Assert-Absent ($capabilities + '\FileAssociations') '.mp3'
        Assert-Absent ($classes + '\.mp3\OpenWithProgids') 'RCMusic.Audio.mp3'
        Assert-String ($classes + '\RCMusic.Audio.mp3') 'ForeignValue' 'preserve-me'
        Assert-String ($classes + '\RCMusic.Audio.mp3\shell\open\command') '' 'foreign modified command'
        Assert-String ($capabilities + '\FileAssociations') '.wav' 'Foreign.Wave'
        Assert-String $userChoice 'Hash' 'sentinel-never-touch'
        Assert-String ($classes + '\.mp3') '' 'Foreign.Player'
    }
    Test-Case 'repeated uninstall is harmless' {
        Uninstall-TestApp $appB
        Assert-String ($classes + '\RCMusic.Audio.mp3') 'ForeignValue' 'preserve-me'
        Assert-String $userChoice 'Hash' 'sentinel-never-touch'
    }
    Test-Case 'foreign ProgID collision rejects before writing any registration values' {
        Reset-TestRegistry
        Set-TestValue ($classes + '\RCMusic.Audio.wav') '' 'Foreign format handler'
        $before = @(Get-TestSnapshot) -join [char]10
        Install-TestApp $appA 1
        Assert-Equal (@(Get-TestSnapshot) -join [char]10) $before
    }
    Test-Case 'foreign RegisteredApplications collision is not overwritten' {
        Reset-TestRegistry
        Set-TestValue $registered 'RC Music' 'Software\Foreign\Capabilities'
        $before = @(Get-TestSnapshot) -join [char]10
        Install-TestApp $appA 1
        Assert-Equal (@(Get-TestSnapshot) -join [char]10) $before
    }
    Test-Case 'uninstall preserves identical text whose registry type was changed' {
        Reset-TestRegistry
        Install-TestApp $appA
        $expanded = [Microsoft.Win32.RegistryValueKind]::ExpandString
        Set-TestValue $registered 'RC Music' $capabilities $expanded
        Set-TestValue ($classes + '\.mp3\OpenWithProgids') 'RCMusic.Audio.mp3' '' $expanded
        Uninstall-TestApp $appA
        Assert-Equal (Get-TestValue $registered 'RC Music').Kind $expanded
        Assert-Equal (Get-TestValue ($classes + '\.mp3\OpenWithProgids') 'RCMusic.Audio.mp3').Kind $expanded
    }
    Test-Case 'write denial reports failure and does not pretend the registration completed' {
        Reset-TestRegistry
        Install-TestApp $appA
        With-TestWriteDenied $capabilities { Install-TestApp $appB 1 }
    }
    Test-Case 'upgrade can retry after a late write denial is removed' {
        Reset-TestRegistry
        Install-TestApp $appA
        With-TestWriteDenied ($classes + '\RCMusic.Audio.wav\shell\open\command') { Install-TestApp $appB 1 }
        Install-TestApp $appB
        Assert-String ($classes + '\RCMusic.Audio.wav\shell\open\command') '' ('"' + $appB.Exe + '" -- "%1"')
        Uninstall-TestApp $appB
        Assert-Absent $registered 'RC Music'
    }
    Test-Case 'first installation can retry after a late write denial is removed' {
        Reset-TestRegistry
        With-TestWriteDenied ($classes + '\.wav\OpenWithProgids') { Install-TestApp $appA 1 }
        Install-TestApp $appA
        Assert-String $registered 'RC Music' $capabilities
        Uninstall-TestApp $appA
        Assert-Absent $registered 'RC Music'
        Assert-Absent ($classes + '\RCMusic.Audio.wav\shell\open\command') ''
    }
    Test-Case 'uninstall can retry after deletion is denied without orphaning owned entries' {
        Reset-TestRegistry
        Install-TestApp $appA
        With-TestWriteDenied ($classes + '\RCMusic.Audio.mp3\shell\open\command') { Uninstall-TestApp $appA 1 }
        Uninstall-TestApp $appA
        Assert-Absent ($classes + '\RCMusic.Audio.mp3\shell\open\command') ''
        Assert-Absent $registered 'RC Music'
    }
} finally {
    # Delete only the fresh GUID subtree created by this run, never Classes or
    # any real association root. Output binaries remain under ignored out/m0.
    if ($testHive -cne ('Software\RCMusic.AssociationTests\' + $runId) -or $runId -cnotmatch '^[0-9a-f]{32}$') {
        throw 'Unsafe registry cleanup target'
    }
    $registry.DeleteSubKeyTree($testHive, $false)
    $registry.Dispose()
}
Write-Host "Association contracts: $($state.Passed) passed, $($state.Failed) failed. Outputs: $testRoot"
if ($state.Failed -gt 0) { exit 1 }
