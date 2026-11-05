#Requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string] $MakeNsis, [switch] $Compile)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$sourceDirectory = Join-Path $repository 'packaging/windows'
$compiler = (Resolve-Path -LiteralPath $MakeNsis).ProviderPath
$toolchain = Get-Content -LiteralPath (Join-Path $sourceDirectory 'nsis-toolchain.json') -Raw | ConvertFrom-Json
if ((Get-FileHash -LiteralPath $compiler -Algorithm SHA256).Hash.ToLowerInvariant() -cne $toolchain.compilerSha256) {
    throw 'Browser host tests require the recorded NSIS compiler'
}
$parent = Join-Path $repository 'out/product/test-runs/browser-hosts'
$id = [guid]::NewGuid().ToString('N')
$root = Join-Path $parent $id
$state = @{ Passed = 0; Failed = 0 }
$preprocessed = @{}
$protected = @('RcBrowserExtensionsSelected', 'RcVariantMask', 'RcDownloadSelected', 'RcPayloadId', 'INSTDIR',
    'RcActive', 'RcPending', 'RcPrevious', 'RcPreviousDirectory', 'RcPreviousHash', 'RcPreviousFormatSignature')
function Require([bool] $Condition, [string] $Message) { if (-not $Condition) { throw $Message } }
function Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function SafeRoot {
    Require ($id -cmatch '\A[0-9a-f]{32}\z' -and [IO.Path]::GetFullPath($root) -ceq [IO.Path]::GetFullPath((Join-Path $parent $id))) 'Unsafe browser-host fixture root'
    for ($path = $root; $path; $path = [IO.Path]::GetDirectoryName($path)) {
        try { $attributes = [IO.File]::GetAttributes($path) }
        catch [IO.FileNotFoundException] { continue }
        catch [IO.DirectoryNotFoundException] { continue }
        Require (-not ($attributes -band [IO.FileAttributes]::ReparsePoint)) 'Unsafe browser-host reparse ancestor'
    }
}
function Function-Body([string] $Text, [string] $Name) {
    $match = [regex]::Match($Text, ('(?ms)^Function\s+' + [regex]::Escape($Name) + '\s*\r?\n(.*?)^FunctionEnd\s*$'))
    Require $match.Success ('Missing browser-host function: ' + $Name)
    return $match.Groups[1].Value
}
function Invoke-Compiler([string] $Script, [switch] $Build) {
    $start = [Diagnostics.ProcessStartInfo]::new($compiler)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.WorkingDirectory = $root
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $start.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
    if (-not $Build) { $start.ArgumentList.Add('/SAFEPPO') }
    foreach ($argument in @('/V0', '/WX', '/INPUTCHARSET', 'UTF8', '/OUTPUTCHARSET', 'UTF8', $Script)) {
        $start.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $started = $false
    try {
        $started = $process.Start()
        Require $started 'NSIS compiler did not start'
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(30000)) { throw 'NSIS compiler exceeded the bounded fixture deadline' }
        return [pscustomobject]@{ ExitCode = $process.ExitCode; Text = $stdout.GetAwaiter().GetResult(); ErrorText = $stderr.GetAwaiter().GetResult() }
    } finally {
        try {
            if ($started -and -not $process.HasExited) {
                $process.Kill($true)
                Require ($process.WaitForExit(5000)) 'Owned compiler survived cleanup'
            }
        } finally { $process.Dispose() }
    }
}
function Fixture([ValidateSet('None', 'Main', 'Both')][string] $Mode) {
    $directory = Join-Path $root $Mode
    $null = [IO.Directory]::CreateDirectory($directory)
    $main = [IO.File]::ReadAllText((Join-Path $sourceDirectory 'RcMusicInstaller.nsi'))
    $readMacro = [regex]::Match($main, '(?ms)^!macro RCMUSIC_INSTALL_READ KEY NAME\r?\n.*?^!macroend\s*$')
    Require $readMacro.Success 'The real strict REG_SZ reader is missing'
    $lines = [Collections.Generic.List[string]]::new()
    foreach ($line in @('Unicode true', 'SetCompressor zlib', '!include LogicLib.nsh',
            ('OutFile "' + (Join-Path $directory 'browser-host-fixture.exe') + '"'), 'Name "RC Music browser-host compiler fixture"',
            'RequestExecutionLevel user', '!define RCMUSIC_BROWSER_EXTENSIONS_AVAILABLE 1',
            ('!define RCMUSIC_REGISTRY_BASE "Software\RCMusicBrowserHostFixture_' + $id + '"'),
            '!define RCMUSIC_INSTALL_ROOT "$TEMP\RCMusicBrowserHostFixture"', 'Var RcStatus', 'Var RcValue')) { $lines.Add($line) }
    foreach ($name in $protected | Where-Object { $_ -cne 'INSTDIR' }) { $lines.Add('Var ' + $name) }
    $lines.Add($readMacro.Value)
    $lines.Add('!macro RCMUSIC_BROWSER_HOSTS CALLBACK')
    if ($Mode -cne 'None') {
        $lines.Add('    ${If} $RcBrowserExtensionsSelected == 1')
        foreach ($name in @('media') + $(if ($Mode -ceq 'Both') { @('youtube') } else { @() })) {
            foreach ($browser in @('BraveSoftware\Brave-Browser', 'Google\Chrome', 'Microsoft\Edge')) {
                $lines.Add('        !insertmacro ${CALLBACK} "' + $browser + '\NativeMessagingHosts\com.rcmusic.' + $name + '" "com.rcmusic.' + $name + '.json"')
            }
        }
        $lines.Add('    ${EndIf}')
    }
    $lines.Add('!macroend')
    $lines.Add('!include "' + (Join-Path $sourceDirectory 'RcMusicBrowserHosts.nsh') + '"')
    $lines.Add('Section "Compiler fixture only; never execute"')
    foreach ($name in $protected) {
        $lines.Add('    StrCpy $' + $name + ' "0"')
        $lines.Add('    DetailPrint "$' + $name + '"')
    }
    $lines.Add('    Call RCMusic.PreflightBrowserHosts')
    $lines.Add('    Call RCMusic.RegisterBrowserHosts')
    $lines.Add('    DetailPrint "$RcStatus $RcValue"')
    $lines.Add('    WriteUninstaller "$INSTDIR\Uninstall.exe"')
    $lines.Add('SectionEnd')
    $lines.Add('Section "Uninstall"')
    $lines.Add('    Call un.RCMusic.UnregisterBrowserHosts')
    $lines.Add('SectionEnd')
    $script = Join-Path $directory 'fixture.nsi'
    [IO.File]::WriteAllText($script, ($lines -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
    return $script
}
function Preprocessed([string] $Mode) {
    if (-not $preprocessed.ContainsKey($Mode)) {
        $result = Invoke-Compiler (Fixture $Mode)
        Require ($result.ExitCode -eq 0) ("Browser-host preprocessing failed: " + $result.ErrorText)
        $preprocessed[$Mode] = $result.Text
    }
    return $preprocessed[$Mode]
}

SafeRoot
Require (-not (Test-Path -LiteralPath $root)) 'Browser-host GUID fixture must be fresh'
$null = [IO.Directory]::CreateDirectory($root)
try {
    Case 'preflight expands only selected HKCU default reads with no registry writes' {
        $body = Function-Body (Preprocessed Both) 'RCMusic.PreflightBrowserHosts'
        Require ([regex]::Matches($body, 'RegGetValueW').Count -eq 6) 'Both channels require six strict reads'
        Require (-not ($body -match '(?im)^\s*(WriteReg|DeleteReg)')) 'Preflight mutates registration'
        Require ($body.Contains('$RcActive') -and $body.Contains('$INSTDIR\com.rcmusic.media.json') -and $body.Contains('$INSTDIR\com.rcmusic.youtube.json')) 'Preflight does not compare current and active-owned manifest locations'
        Require ($body -match 'StrCpy\s+\$RcStatus\s+0') 'Preflight cannot return success'
    }
    Case 'registration rechecks each default before writing and verifies the exact written value' {
        $body = Function-Body (Preprocessed Both) 'RCMusic.RegisterBrowserHosts'
        $writes = [regex]::Matches($body, '(?m)^\s*WriteRegStr\s+HKCU\s+"([^"\r\n]+)"\s+""\s+"([^"\r\n]+)"')
        Require ($writes.Count -eq 6) 'Both native channels require six user registrations'
        Require ([regex]::Matches($body, 'RegGetValueW').Count -ge 12) 'Each registration needs strict pre-write and readback checks'
        $previous = 0
        foreach ($write in $writes) {
            Require ($write.Groups[1].Value.StartsWith(('Software\RCMusicBrowserHostFixture_' + $id + '\'), [StringComparison]::Ordinal)) 'Registry writes escape the fixture/user-owned base'
            Require ($body.Substring($previous, $write.Index - $previous).Contains('RegGetValueW')) 'A later registration has no immediate re-preflight'
            Require ($write.Groups[2].Value -match '\$INSTDIR\\com\.rcmusic\.(media|youtube)\.json\z') 'Registration writes a non-owned manifest location'
            $previous = $write.Index + $write.Length
        }
        Require ($body.Substring($previous).Contains('RegGetValueW')) 'The last write has no readback'
        Require ($body -match 'StrCpy\s+\$RcStatus\s+0' -and $body -match 'StrCpy\s+\$RcStatus\s+1') 'Registration lacks explicit success/failure results'
    }
    Case 'unregistration compares current defaults and removes only those values and empty host keys' {
        $body = Function-Body (Preprocessed Both) 'un.RCMusic.UnregisterBrowserHosts'
        Require ([regex]::Matches($body, 'RegGetValueW').Count -ge 6) 'Unregistration must read every selected default strictly'
        Require ([regex]::Matches($body, '(?m)^\s*DeleteRegValue\s+HKCU\s+"[^"\r\n]+"\s+""').Count -eq 6) 'Unregistration must delete only matching default values'
        Require ([regex]::Matches($body, '(?m)^\s*DeleteRegKey\s+/ifempty\s+HKCU\s+"[^"\r\n]+"').Count -eq 6) 'Host key cleanup must be nonrecursive and conditional on emptiness'
        Require (-not ($body -match '(?im)^\s*DeleteRegKey\s+(?!/ifempty)')) 'Unregistration recursively removes a registry tree'
        Require ($body.Contains('$INSTDIR\com.rcmusic.media.json') -and $body.Contains('$INSTDIR\com.rcmusic.youtube.json')) 'Unregistration does not compare the current payload manifest paths'
        Require ($body -match 'StrCpy\s+\$RcStatus\s+1') 'Unregistration cannot report an inconclusive access/error state'
    }
    Case 'host routines preserve payload selection transaction state and use no HKLM policy or external process' {
        $text = Preprocessed Both
        foreach ($function in @('RCMusic.PreflightBrowserHosts', 'RCMusic.RegisterBrowserHosts', 'un.RCMusic.UnregisterBrowserHosts')) {
            $body = Function-Body $text $function
            foreach ($name in $protected) {
                Require (-not ($body -match ('(?im)^\s*(?:StrCpy|Pop|ReadRegStr|IntOp)\s+\$' + [regex]::Escape($name) + '(?:\s|$)'))) ('Routine modifies caller state: ' + $name)
            }
            Require (-not ($body -match '(?i)\bHKLM\b|0x80000002|\\Policies\\|\bExec(?:Wait|Shell)?\b')) 'Host registration broadens into system policy or process execution'
        }
    }
    Case 'one channel expands only its three browser registrations' {
        $body = Function-Body (Preprocessed Main) 'RCMusic.RegisterBrowserHosts'
        Require ([regex]::Matches($body, '(?m)^\s*WriteRegStr\s+HKCU').Count -eq 3) 'A single selected channel registers extra hosts'
        Require ($body.Contains('com.rcmusic.media.json') -and -not $body.Contains('com.rcmusic.youtube')) 'Main-only registration includes YouTube'
    }
    Case 'an empty selected-channel inventory is a successful no-op for all three routines' {
        $text = Preprocessed None
        foreach ($function in @('RCMusic.PreflightBrowserHosts', 'RCMusic.RegisterBrowserHosts', 'un.RCMusic.UnregisterBrowserHosts')) {
            $body = Function-Body $text $function
            Require (-not ($body -match 'RegGetValueW|(?im)^\s*(?:WriteReg|DeleteReg)')) 'Empty selection touches registration'
            Require ($body -match 'StrCpy\s+\$RcStatus\s+0') 'Empty inventory did not succeed'
        }
    }
    if ($Compile) {
        Case 'both selected channels compile to an owned NSIS fixture without executing setup or uninstaller' {
            $result = Invoke-Compiler (Fixture Both) -Build
            Require ($result.ExitCode -eq 0) ("Browser-host compilation failed: " + $result.ErrorText)
            Require ([IO.File]::Exists((Join-Path $root 'Both/browser-host-fixture.exe'))) 'Compiler produced no owned artifact'
        }
    }
} finally {
    if ($state.Failed -eq 0) {
        SafeRoot
        foreach ($item in Get-ChildItem -LiteralPath $root -Recurse -Force) {
            Require (-not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) 'Unsafe reparse child during fixture cleanup'
        }
        Remove-Item -LiteralPath ([IO.Path]::GetFullPath($root)) -Recurse -Force
    } else { Write-Host "[INFO] Preserved owned browser-host fixtures: $root" }
}
Write-Host "Browser host source/compiler checks: $($state.Passed) passed / $($state.Failed) failed"
if ($state.Failed) { exit 1 }
