#Requires -Version 7.0
[CmdletBinding(DefaultParameterSetName = 'Prepare')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Prepare')][switch] $PrepareOnly,
    [Parameter(Mandatory, ParameterSetName = 'Prepare')][string] $MakeNsis,
    [Parameter(Mandatory, ParameterSetName = 'Run')][switch] $RunPrepared,
    [Parameter(Mandatory, ParameterSetName = 'Run')][string] $ReceiptPath,
    [Parameter(Mandatory, ParameterSetName = 'Run')][string] $ReceiptSha256
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Preparation compiles synthetic installers only. RunPrepared is a separate,
# explicitly reviewed native test action: it runs those installers and writes
# only its GUID-scoped HKCU fixture. It never starts player/host/browser/media.
if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw 'Windows x64 PowerShell is required' }
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$recipes = Join-Path $repository 'packaging/windows'
$parent = Join-Path $repository 'out/product/test-runs/installer-extensions-native'
$purpose = 'rcmusic-installer-extensions-native-fixture-v1'
$utf8 = [Text.UTF8Encoding]::new($false, $true)
$scriptPath = [IO.Path]::GetFullPath($PSCommandPath)

function Require([bool] $Value, [string] $Message) { if (-not $Value) { throw $Message } }
function Hash([string] $Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function SafePath([string] $Path) {
    $full = [IO.Path]::GetFullPath($Path)
    Require ($full -cmatch '\A[A-Za-z]:\\' -and $full -notmatch '["\x00-\x1f]') 'An ordinary absolute local path is required'
    for ($node = $full; $node; $node = [IO.Path]::GetDirectoryName($node)) {
        try { $attributes = [IO.File]::GetAttributes($node) }
        catch [IO.FileNotFoundException] { continue }
        catch [IO.DirectoryNotFoundException] { continue }
        Require (-not ($attributes -band [IO.FileAttributes]::ReparsePoint)) ('Reparse fixture path: ' + $node)
    }
    return $full
}
function Below([string] $Path, [string] $Root) {
    $full = SafePath $Path
    $prefix = (SafePath $Root).TrimEnd('\') + '\'
    Require ($full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) 'Fixture path escaped its owned root'
    return $full
}
function Record([string] $Path) {
    $full = SafePath $Path
    Require ([IO.File]::Exists($full)) ('Missing fixture input: ' + $full)
    [pscustomobject]@{ path = $full; size_bytes = [IO.FileInfo]::new($full).Length; sha256 = Hash $full }
}
function CheckRecord($Entry) {
    Require ($Entry.sha256 -cmatch '\A[0-9a-f]{64}\z' -and $Entry.size_bytes -ge 0) 'Invalid fixture pin'
    $actual = Record $Entry.path
    Require ($actual.path -ceq $Entry.path -and $actual.size_bytes -eq $Entry.size_bytes -and
        $actual.sha256 -ceq $Entry.sha256) ('Fixture input changed: ' + $Entry.path)
}
function WriteNew([string] $Path, [string] $Text) {
    $bytes = $utf8.GetBytes($Text)
    $stream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $stream.Write($bytes); $stream.Flush($true) } finally { $stream.Dispose() }
}
function Files([string] $Root) {
    if (-not [IO.Directory]::Exists($Root)) { return @() }
    $all = @(Get-ChildItem -LiteralPath $Root -Recurse -Force)
    foreach ($entry in $all) { $null = SafePath $entry.FullName }
    return @($all | Where-Object { -not $_.PSIsContainer } | Sort-Object FullName -CaseSensitive | ForEach-Object { Record $_.FullName })
}
function StartHidden([string] $Exe, [string[]] $Arguments, [string] $WorkingDirectory, [int] $TimeoutMs = 30000) {
    $null = SafePath $Exe
    $null = SafePath $WorkingDirectory
    $start = [Diagnostics.ProcessStartInfo]::new($Exe)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.WorkingDirectory = $WorkingDirectory
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $started = $false
    try {
        $started = $process.Start()
        Require $started 'Owned fixture process did not start'
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        Require ($process.WaitForExit($TimeoutMs)) 'Owned fixture process exceeded its deadline; preserve state for review'
        [pscustomobject]@{ exit_code = $process.ExitCode; stdout = $stdout.GetAwaiter().GetResult(); stderr = $stderr.GetAwaiter().GetResult() }
    } finally {
        try {
            if ($started -and -not $process.HasExited) {
                $process.Kill($true)
                Require ($process.WaitForExit(5000)) 'Owned fixture process survived termination; preserve state'
            }
        } finally { $process.Dispose() }
    }
}

if ($PSCmdlet.ParameterSetName -ceq 'Prepare') {
    Require $PrepareOnly.IsPresent 'PrepareOnly must be explicitly true; no implicit execution is supported'
    # Nothing before the Run branch opens or creates any Registry key.
    Import-Module (Join-Path $recipes 'RcMusicInstallerPlan.psm1') -Force
    Import-Module (Join-Path $PSScriptRoot 'ExtensionInstallerFixture.psm1') -Force
    $compiler = SafePath (Resolve-Path -LiteralPath $MakeNsis).ProviderPath
    $toolchain = Get-Content -LiteralPath (Join-Path $recipes 'nsis-toolchain.json') -Raw | ConvertFrom-Json
    Require ((Hash $compiler) -ceq $toolchain.compilerSha256) 'Use the recorded NSIS compiler'
    $id = [guid]::NewGuid().ToString('N')
    $root = SafePath (Join-Path $parent $id)
    Require (-not (Test-Path -LiteralPath $root)) 'Native fixture root must be fresh'
    $null = [IO.Directory]::CreateDirectory($root)
    WriteNew (Join-Path $root 'OWNER.json') (([ordered]@{ purpose = $purpose; root_id = $id } | ConvertTo-Json -Compress) + "`n")
    $install = Join-Path $root 'installed'
    $shortcuts = Join-Path $root 'shortcuts'
    $registry = 'Software\RCMusicExtensionTests\' + $id
    $fixture = New-RcMusicExtensionInstallerFixture -Directory (Join-Path $root 'fixture') -RepositoryRoot $repository
    $plan = New-RcMusicInstallerPlan -RuntimeDirectory $fixture.App -OutputDirectory $fixture.Output `
        -MainExtensionPackageReceipt $fixture.MainReceipt -MainExtensionPackageReceiptSha256 $fixture.MainReceiptSha256 `
        -YoutubeExtensionPackageReceipt $fixture.YoutubeReceipt -YoutubeExtensionPackageReceiptSha256 $fixture.YoutubeReceiptSha256
    Require ($plan.recipe_hashes.Count -eq 14 -and -not $plan.associations_enabled) 'Expected the current 14-source recipe with associations OFF'
    Require ((@($plan.payload_variants.mask | Sort-Object) -join ',') -ceq '0,1,3,5,7') 'The five expected selections are unavailable'
    $sourcePins = @(Get-RcMusicInstallerRecipeHashes)
    Require (($sourcePins | ConvertTo-Json -Compress) -ceq ($plan.recipe_hashes | ConvertTo-Json -Compress)) 'Recipe changed while planning'
    $null = [IO.Directory]::CreateDirectory($plan.output)
    foreach ($generated in $plan.generated_files) {
        Require ($generated.relative_path -ceq 'download-component-marker.bin' -and
            $generated.content -ceq "RCMusicDownloadTools-v1`n") 'Unexpected generated marker'
        $path = Join-Path $plan.output $generated.relative_path
        WriteNew $path $generated.content
        Require ([IO.FileInfo]::new($path).Length -eq 24 -and (Hash $path) -ceq $generated.sha256) 'Marker pin mismatch'
    }
    $sources = Join-Path $root 'sources'
    $null = [IO.Directory]::CreateDirectory($sources)
    foreach ($entry in $sourcePins) {
        [IO.File]::Copy((Join-Path $repository $entry.path), (Join-Path $sources ([IO.Path]::GetFileName($entry.path))), $false)
    }
    $template = [IO.File]::ReadAllText((Join-Path $recipes 'RcMusicInstaller.nsi'), $utf8)
    $initPattern = '(?ms)(^Function \.onInit\r?\n.*?)(^FunctionEnd\s*$)'
    Require ([regex]::Matches($template, $initPattern).Count -eq 1) 'Expected exactly one installer onInit'
    $compiled = @()
    foreach ($mask in @(0, 1, 3, 5, 7)) {
        $directory = Join-Path $root ('selection-' + $mask)
        $null = [IO.Directory]::CreateDirectory($directory)
        $exe = Join-Path $directory 'RCMusic-Setup.exe'
        $source = Join-Path $directory 'RcMusicInstaller.native-fixture.nsi'
        $configPath = Join-Path $directory 'installer-config.nsh'
        # Mask 0 uses the unmodified defaults, proving that silent installation
        # does not select remote tools/extensions. Only nonzero private copies
        # receive the literal selection hook at the end of onInit.
        $private = $template
        if ($mask -ne 0) {
            $private = [regex]::Replace($template, $initPattern, '${1}    Call RCMusic.TestOnlySelect' + "`r`n" + '${2}')
            $private += "`n; Private test copy only: never copied back into the product template.`nFunction RCMusic.TestOnlySelect`n"
            foreach ($selection in @(@('SEC_RCMUSIC_DOWNLOADS', 1), @('SEC_RCMUSIC_MAIN_EXTENSION', 2), @('SEC_RCMUSIC_YOUTUBE_EXTENSION', 4))) {
                $flag = [int](($mask -band $selection[1]) -ne 0)
                $private += '    SectionSetFlags ${' + $selection[0] + '} ' + $flag + "`n"
            }
            $private += "FunctionEnd`n"
        }
        WriteNew $source $private
        $config = $plan.config + "`n" +
            '!define /redef RCMUSIC_OUTPUT "' + (ConvertTo-RcMusicNsisLiteral $exe -Passes 2) + '"' + "`n" +
            '!define /redef RCMUSIC_INSTALL_ROOT "' + (ConvertTo-RcMusicNsisLiteral $install -Runtime) + '"' + "`n" +
            '!define /redef RCMUSIC_SHORTCUT_ROOT "' + (ConvertTo-RcMusicNsisLiteral $shortcuts -Runtime) + '"' + "`n" +
            '!define /redef RCMUSIC_REGISTRY_BASE "' + $registry + '"' + "`n"
        WriteNew $configPath $config
        $result = StartHidden $compiler @('/V2', '/WX', '/NOCD', '/INPUTCHARSET', 'UTF8',
            ('/DRCMUSIC_CONFIG=' + (ConvertTo-RcMusicNsisLiteral $configPath)), $source) $sources 60000
        WriteNew (Join-Path $directory 'compile.stdout.txt') $result.stdout
        WriteNew (Join-Path $directory 'compile.stderr.txt') $result.stderr
        Require ($result.exit_code -eq 0 -and [string]::IsNullOrWhiteSpace($result.stderr)) ('Fixture compilation failed for mask ' + $mask)
        $variant = @($plan.payload_variants | Where-Object mask -EQ $mask)[0]
        $compiled += [pscustomobject]@{ mask = $mask; payload_id = $variant.payload_id; executable = Record $exe
            nsis_source = Record $source; configuration = Record $configPath }
    }
    foreach ($entry in $sourcePins) { Require ((Hash (Join-Path $repository $entry.path)) -ceq $entry.sha256) 'Recipe changed while compiling' }
    foreach ($entry in $plan.files) { Require ((Hash $entry.source) -ceq $entry.sha256) 'Synthetic input changed while compiling' }
    $receipt = [ordered]@{ schema_version = 1; purpose = $purpose; root_id = $id; root = $root
        install_root = $install; shortcut_root = $shortcuts; registry_base = $registry
        script = Record $scriptPath; fixture_helper = Record (Join-Path $PSScriptRoot 'ExtensionInstallerFixture.psm1')
        compiler = Record $compiler; recipe_files = $sourcePins; runtime_manifest = Record $fixture.ManifestPath
        extension_receipts = @((Record $fixture.MainReceipt), (Record $fixture.YoutubeReceipt))
        files = @($plan.files | Select-Object relative_path, size_bytes, sha256)
        tool_paths = @($plan.download_component.files.relative_path)
        marker = $plan.download_component.marker; installers = $compiled; prepared_inventory = @(Files $root)
        limits = 'Synthetic Lite installers only; no app/host/browser/media execution, no UAC/network/AppContainer; not release evidence.' }
    $receiptPath = Join-Path $root 'prepared.json'
    WriteNew $receiptPath (($receipt | ConvertTo-Json -Depth 15) + "`n")
    [pscustomobject]@{ prepared_receipt = $receiptPath; sha256 = Hash $receiptPath; native_executed = $false }
    return
}

Require $RunPrepared.IsPresent 'RunPrepared must be explicitly true and separately authorized'
Require ($ReceiptSha256 -cmatch '\A[0-9a-f]{64}\z') 'An externally reviewed lowercase receipt SHA256 is required'
$receiptFull = SafePath $ReceiptPath
Require ((Hash $receiptFull) -ceq $ReceiptSha256) 'Prepared receipt pin mismatch'
$receipt = [IO.File]::ReadAllText($receiptFull, $utf8) | ConvertFrom-Json
Require ($receipt.schema_version -eq 1 -and $receipt.purpose -ceq $purpose -and
    $receipt.root_id -cmatch '\A[0-9a-f]{32}\z') 'Unexpected native test receipt'
$root = SafePath (Join-Path $parent $receipt.root_id)
Require ($receipt.root -ceq $root -and $receiptFull -ceq (Join-Path $root 'prepared.json') -and
    $receipt.install_root -ceq (Join-Path $root 'installed') -and
    $receipt.shortcut_root -ceq (Join-Path $root 'shortcuts') -and
    $receipt.registry_base -ceq ('Software\RCMusicExtensionTests\' + $receipt.root_id)) 'Prepared fixture scope mismatch'
Require ($receipt.script.path -ceq $scriptPath -and $receipt.fixture_helper.path -ceq
    (Join-Path $PSScriptRoot 'ExtensionInstallerFixture.psm1')) 'Prepared script identity mismatch'
CheckRecord $receipt.script
CheckRecord $receipt.fixture_helper
CheckRecord $receipt.compiler
Require ($receipt.recipe_files.Count -eq 14 -and $receipt.installers.Count -eq 5 -and
    (@($receipt.installers.mask | Sort-Object) -join ',') -ceq '0,1,3,5,7') 'Incomplete prepared native inventory'
foreach ($entry in $receipt.recipe_files) {
    Require ($entry.path -cmatch '\Apackaging/windows/[^/\\]+\z') 'Recipe escaped its fixed directory'
    Require ((Hash (Join-Path $repository $entry.path)) -ceq $entry.sha256) 'Current recipe differs from reviewed native preparation'
}
CheckRecord $receipt.runtime_manifest
Require ($receipt.runtime_manifest.path -ceq (Join-Path $root 'fixture/runtime-manifest.json') -and
    $receipt.extension_receipts.Count -eq 2 -and
    $receipt.extension_receipts[0].path -ceq (Join-Path $root 'fixture/package-main/package-receipt.json') -and
    $receipt.extension_receipts[1].path -ceq (Join-Path $root 'fixture/package-youtube/package-receipt.json')) 'Prepared fixture inputs escaped their fixed locations'
foreach ($entry in $receipt.extension_receipts) { CheckRecord $entry }
foreach ($installer in $receipt.installers) {
    Require ($installer.payload_id -cmatch '\A[0-9a-f]{64}\z') 'Invalid prepared payload identity'
    foreach ($entry in @($installer.executable, $installer.nsis_source, $installer.configuration)) {
        $null = Below $entry.path $root
        CheckRecord $entry
    }
}
foreach ($entry in $receipt.prepared_inventory) { $null = Below $entry.path $root; CheckRecord $entry }
$actualFiles = @((Files $root).path | Where-Object { $_ -cne $receiptFull })
Require (($actualFiles -join "`n") -ceq (@($receipt.prepared_inventory.path) -join "`n")) 'Unrecognized file in prepared fixture; preserve it'
Require (-not (Test-Path -LiteralPath $receipt.install_root) -and
    -not (Test-Path -LiteralPath $receipt.shortcut_root)) 'Prepared fixture is no longer fresh; preserve existing install/shortcut state'
$owner = Get-Content -LiteralPath (Join-Path $root 'OWNER.json') -Raw | ConvertFrom-Json
Require ($owner.purpose -ceq $purpose -and $owner.root_id -ceq $receipt.root_id) 'Fixture ownership token mismatch'
# All pins, paths and ownership are validated above, before the first HKCU call.
$registry = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser, [Microsoft.Win32.RegistryView]::Registry64)
$base = $receipt.registry_base
function RegValue([string] $Suffix, [string] $Name = '') {
    $key = $registry.OpenSubKey($base + $(if ($Suffix) { '\' + $Suffix } else { '' }), $false)
    if ($null -eq $key) { return $null }
    try { $key.GetValue($Name, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames) } finally { $key.Dispose() }
}
function RegistrySnapshot {
    function Visit([string] $Relative) {
        $key = $registry.OpenSubKey($base + $(if ($Relative) { '\' + $Relative } else { '' }), $false)
        if ($null -eq $key) { return }
        try {
            [pscustomobject]@{ key = $Relative; values = @($key.GetValueNames() | Sort-Object -CaseSensitive | ForEach-Object {
                [ordered]@{ name = $_; kind = [string]$key.GetValueKind($_); value = $key.GetValue($_, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames) }
            }) }
            foreach ($name in @($key.GetSubKeyNames() | Sort-Object -CaseSensitive)) { Visit ($Relative + $(if ($Relative) { '\' } else { '' }) + $name) }
        } finally { $key.Dispose() }
    }
    return @(Visit '') | ConvertTo-Json -Depth 12 -Compress
}
function HostSuffixes([int] $Mask) {
    foreach ($pair in @(@(2, 'com.rcmusic.media'), @(4, 'com.rcmusic.youtube'))) {
        if ($Mask -band $pair[0]) {
            foreach ($browser in @('BraveSoftware\Brave-Browser', 'Google\Chrome', 'Microsoft\Edge')) {
                [pscustomobject]@{ suffix = $browser + '\NativeMessagingHosts\' + $pair[1]; manifest = $pair[1] + '.json' }
            }
        }
    }
}
function Installer([int] $Mask) { @($receipt.installers | Where-Object mask -EQ $Mask)[0] }
$uninstallerPins = @{}
function Install([int] $Mask) {
    $item = Installer $Mask
    CheckRecord $item.executable
    $result = StartHidden $item.executable.path @('/S') $root
    Require ($result.exit_code -eq 0) ('Synthetic setup failed for mask ' + $Mask)
    $directory = Join-Path $receipt.install_root $item.payload_id
    $uninstallerPins[$directory] = Record (Join-Path $directory 'Uninstall.exe')
    return $directory
}
function CheckInstalled([int] $Mask, [string] $Directory) {
    $null = Below $Directory $receipt.install_root
    foreach ($file in $receipt.files) {
        $path = Join-Path $Directory $file.relative_path
        $group = if ($file.relative_path.StartsWith('browser-extensions/main/')) { 2 }
            elseif ($file.relative_path.StartsWith('browser-extensions/youtube/')) { 4 }
            elseif ($receipt.tool_paths -ccontains $file.relative_path) { 1 } else { 0 }
        $selected = $group -eq 0 -or ($Mask -band $group) -ne 0
        Require ([IO.File]::Exists($path) -eq $selected) ('Wrong file selection: ' + $file.relative_path)
        if ($selected) { Require ([IO.FileInfo]::new($path).Length -eq $file.size_bytes -and (Hash $path) -ceq $file.sha256) 'Installed synthetic bytes differ' }
    }
    $marker = Join-Path $Directory 'download-tools/installed-v1'
    Require ([IO.File]::Exists($marker) -eq (($Mask -band 1) -ne 0)) 'Wrong activation marker selection'
    if ($Mask -band 1) { Require ([IO.FileInfo]::new($marker).Length -eq 24 -and (Hash $marker) -ceq $receipt.marker.sha256) 'Activation marker differs' }
    $selectedHosts = @(HostSuffixes $Mask | ForEach-Object { $_.suffix })
    foreach ($hostEntry in @(HostSuffixes 6)) {
        $expected = if ($selectedHosts -ccontains $hostEntry.suffix) { Join-Path $Directory $hostEntry.manifest } else { $null }
        Require ((RegValue $hostEntry.suffix) -ceq $expected) ('Wrong isolated host registration: ' + $hostEntry.suffix)
    }
    $ini = [IO.File]::ReadAllText((Join-Path $Directory '.rcmusic-install.ini'))
    Require ($ini -match ('(?m)^DownloadComponent=' + ($Mask -band 1) + '\r?$') -and
        $ini -match ('(?m)^BrowserExtensionsSelection=' + ($Mask -band 6) + '\r?$')) 'Frozen selection was not persisted'
}
function Uninstall([string] $Directory, [switch] $Verify) {
    $exe = Join-Path $Directory 'Uninstall.exe'
    $null = Below $exe $receipt.install_root
    Require ([IO.File]::Exists($exe)) 'Owned uninstaller missing'
    Require ($uninstallerPins.ContainsKey($Directory)) 'No uninstaller pin from the just-completed owned setup'
    CheckRecord $uninstallerPins[$Directory]
    # _?= MUST be last and its path unquoted, per NSIS's special tail parser.
    # The pinned original is launched in place; no shell or temp launcher is used.
    $arguments = @('/S') + $(if ($Verify) { @('/VERIFY') } else { @() })
    $start = [Diagnostics.ProcessStartInfo]::new($exe)
    $start.UseShellExecute = $false; $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden; $start.WorkingDirectory = $root
    $start.Arguments = ($arguments -join ' ') + ' _?=' + $Directory
    $process = [Diagnostics.Process]::new(); $process.StartInfo = $start; $started = $false
    try {
        $started = $process.Start(); Require $started 'Owned uninstaller did not start'
        Require ($process.WaitForExit(30000)) 'Owned uninstaller exceeded deadline; preserve fixture'
        Require ($process.ExitCode -eq 0) 'Owned uninstaller refused native fixture'
    } finally {
        try { if ($started -and -not $process.HasExited) { $process.Kill($true); Require ($process.WaitForExit(5000)) 'Owned uninstaller survived termination' } }
        finally { $process.Dispose() }
    }
}
$results = [Collections.Generic.List[object]]::new()
$complete = $false
try {
    $existing = $registry.OpenSubKey($base, $false)
    if ($null -ne $existing) { $existing.Dispose(); throw 'GUID Registry scope already exists; nothing changed' }
    $owned = $registry.CreateSubKey($base, $true)
    try { $owned.SetValue('HarnessOwner', $receipt.root_id); $owned.SetValue('HarnessPurpose', $purpose) } finally { $owned.Dispose() }
    $null = [IO.Directory]::CreateDirectory($receipt.install_root)
    $sentinel = Join-Path $receipt.install_root 'personal-data.keep'
    WriteNew $sentinel 'Owned personal-data preservation sentinel; never a payload file.'
    $sentinelPin = Record $sentinel
    foreach ($mask in @(0, 1, 3, 5, 7)) {
        $directory = Install $mask
        CheckInstalled $mask $directory
        $beforeFiles = @(Files $receipt.install_root; Files $receipt.shortcut_root) | ConvertTo-Json -Depth 5 -Compress
        $beforeRegistry = RegistrySnapshot
        Uninstall $directory -Verify
        Require ((@(Files $receipt.install_root; Files $receipt.shortcut_root) | ConvertTo-Json -Depth 5 -Compress) -ceq $beforeFiles -and
            (RegistrySnapshot) -ceq $beforeRegistry) '/VERIFY changed payload, shortcut or Registry'
        # Give every selected host a foreign named value and subkey. Removing the
        # owned default must preserve both; these sentinels belong to this GUID.
        foreach ($hostEntry in @(HostSuffixes $mask)) {
            $key = $registry.CreateSubKey($base + '\' + $hostEntry.suffix, $true)
            try { $key.SetValue('ForeignNamedValue', $receipt.root_id) } finally { $key.Dispose() }
            $key = $registry.CreateSubKey($base + '\' + $hostEntry.suffix + '\ForeignChild', $true)
            try { $key.SetValue('Owner', $receipt.root_id) } finally { $key.Dispose() }
        }
        Uninstall $directory
        Require (-not [IO.Directory]::Exists($directory)) 'Owned payload directory remains after uninstall'
        foreach ($hostEntry in @(HostSuffixes $mask)) {
            Require ($null -eq (RegValue $hostEntry.suffix) -and (RegValue $hostEntry.suffix 'ForeignNamedValue') -ceq $receipt.root_id -and
                (RegValue ($hostEntry.suffix + '\ForeignChild') 'Owner') -ceq $receipt.root_id) 'Uninstall did not preserve foreign Registry siblings'
            $key = $registry.OpenSubKey($base + '\' + $hostEntry.suffix, $true)
            try { $key.DeleteValue('ForeignNamedValue', $true); $key.DeleteSubKey('ForeignChild', $true) } finally { $key.Dispose() }
        }
        CheckRecord $sentinelPin
        $results.Add([pscustomobject]@{ mask = $mask; selection_verified = $true; verify_read_only = $true; uninstall_preserved_foreign = $true })
    }
    $old = Install 3
    CheckInstalled 3 $old
    $next = Install 5
    CheckInstalled 5 $next
    Require (-not [IO.Directory]::Exists($old)) 'Upgrade did not remove its previous owned payload'
    Uninstall $next
    Require (-not [IO.Directory]::Exists($next)) 'Upgrade target did not uninstall'
    CheckRecord $sentinelPin
    $results.Add([pscustomobject]@{ upgrade = '3->5'; own_previous_removed = $true; registrations_migrated = $true })
    $complete = $true
} finally {
    # Never recursively delete a Registry tree or filesystem fixture. On failure
    # leave all bytes/state for review. Empty GUID Registry scaffolding is also
    # intentionally retained with its ownership values for explicit cleanup.
    $registry.Dispose()
    WriteNew (Join-Path $root 'native-run.json') (([ordered]@{ schema_version = 1; purpose = $purpose; success = $complete
        prepared_receipt_sha256 = $ReceiptSha256; results = $results.ToArray(); root_preserved = $root
        registry_scope_preserved = $base; native_processes = 'synthetic NSIS setup/uninstaller only' } | ConvertTo-Json -Depth 8) + "`n")
}
[pscustomobject]@{ success = $complete; result = Join-Path $root 'native-run.json'; registry_scope = $base }
