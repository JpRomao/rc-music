#Requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$MakeNsis, [switch]$Compile)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$recipes = Join-Path $repository 'packaging/windows'
Import-Module (Join-Path $recipes 'RcMusicInstallerPlan.psm1') -Force
Import-Module (Join-Path $recipes 'RcMusicDownloadComponentPe.psm1') -Force
$compiler = (Resolve-Path -LiteralPath $MakeNsis).ProviderPath
$toolchain = Get-Content -LiteralPath (Join-Path $recipes 'nsis-toolchain.json') -Raw | ConvertFrom-Json
if ((Get-FileHash -LiteralPath $compiler -Algorithm SHA256).Hash.ToLowerInvariant() -cne $toolchain.compilerSha256) {
    throw 'Edition preprocessor tests require the recorded NSIS compiler'
}
$parent = Join-Path $repository 'out/product/test-runs/installer-editions'
$id = [guid]::NewGuid().ToString('N')
$root = Join-Path $parent $id
$state = @{ Passed = 0; Failed = 0; Next = 0 }
function SafeRoot {
    if ($id -cnotmatch '^[0-9a-f]{32}$' -or [IO.Path]::GetFullPath($root) -cne [IO.Path]::GetFullPath((Join-Path $parent $id))) { throw 'Unsafe test root' }
    for ($path = $root; $path; $path = [IO.Path]::GetDirectoryName($path)) {
        try { $attributes = [IO.File]::GetAttributes($path) }
        catch [IO.FileNotFoundException] { continue }
        catch [IO.DirectoryNotFoundException] { continue }
        if ($attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse test ancestor' }
    }
}
function Case([string]$Name, [scriptblock]$Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Require([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Fixture([string]$Edition, [switch]$Downloads) {
    $state.Next++
    $base = Join-Path $root ([string]$state.Next)
    $app = Join-Path $base 'app'
    $null = [IO.Directory]::CreateDirectory($app)
    $files = @('RCMusic.exe', 'qt.conf', 'LOCAL-DEVELOPMENT.txt')
    if ($Edition -ceq 'CompleteExperimental') { $files += @('notices/WebView2-SDK-LICENSE.txt', 'notices/WebView2-SDK-NOTICE.txt', 'WEBVIEW2-DEVELOPMENT.txt') }
    $entries = @(foreach ($relative in $files) {
        $path = Join-Path $app $relative
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
        [IO.File]::WriteAllText($path, 'Synthetic bytes for preprocessing only')
        [pscustomobject]@{ relative_path = $relative; size_bytes = (Get-Item -LiteralPath $path).Length
            sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
    $manifest = [ordered]@{ schema_version = 1; name = 'RC Music'; architecture = 'x64'; build_testing = $false
        recipe_dirty = $false; source_commit = ('1' * 40); distribution = 'private-local-development-only'
        edition = $Edition; browser_runtime_requirement = $(if ($Edition -ceq 'Lite') { 'none' } else { 'existing-webview2-evergreen-x64' }); files = $entries }
    if ($Downloads) {
        # Only fixture bytes are shared with the planner suite. The real pure
        # closure/metadata validators remain the sole policy implementation.
        $definitions = [ordered]@{
            Bootstrap = 'RCMusicDownloadWorker.exe'; Payload = 'rcmusic-download-network.dll'
            Core = 'Qt6Core.dll'; Network = 'Qt6Network.dll'; Msvcp140 = 'msvcp140.dll'; Msvcp140_1 = 'msvcp140_1.dll'
            Vcruntime140 = 'vcruntime140.dll'; Vcruntime140_1 = 'vcruntime140_1.dll'; Plugin = 'qrcmusicschanneloffline.dll'
        }
        $componentFiles = @()
        $reports = @()
        foreach ($role in $definitions.Keys) {
            $relative = 'download-tools/network/' + $definitions[$role]
            $path = Join-Path $app $relative
            $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
            [IO.File]::WriteAllText($path, 'Owned non-executable compiler fixture: ' + $relative)
            $componentFiles += [pscustomobject]@{
                source = $path; relative_path = $relative; component = 'network'; role = $role
                size_bytes = [IO.FileInfo]::new($path).Length
                sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
            }
            $reports += [pscustomobject]@{ relative_path = $relative; machine = 'x64'; imports = @('KERNEL32.dll') }
        }
        $relative = 'download-tools/media/RCMusicMediaWorker.exe'
        $path = Join-Path $app $relative
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
        [IO.File]::WriteAllText($path, 'Owned non-executable compiler fixture: ' + $relative)
        $componentFiles += [pscustomobject]@{
            source = $path; relative_path = $relative; component = 'media'; role = 'Media'
            size_bytes = [IO.FileInfo]::new($path).Length
            sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        }
        $reports += [pscustomobject]@{ relative_path = $relative; machine = 'x64'; imports = @('KERNEL32.dll', 'ole32.dll') }
        $manifest.files += @($componentFiles | Select-Object relative_path, size_bytes, sha256)
        $manifest.download_component = [ordered]@{
            schema_version = 1; selection_default = $false
            receipt_sha256 = ('a' * 64); header_sha256 = ('b' * 64)
            activation_marker = [ordered]@{ relative_path = 'download-tools/installed-v1'; content = "RCMusicDownloadTools-v1`n" }
            files = @($componentFiles | Select-Object relative_path, size_bytes, sha256)
            pe_closure = Get-RcMusicDownloadComponentPeClosure -Files $componentFiles -Reports $reports
        }
    }
    [IO.File]::WriteAllText((Join-Path $base 'runtime-manifest.json'), ($manifest | ConvertTo-Json -Depth 12))
    $output = Join-Path $base 'never-compiled'
    $plan = New-RcMusicInstallerPlan -RuntimeDirectory $app -OutputDirectory $output -Edition $Edition
    return [pscustomobject]@{ Base = $base; Output = $output; Plan = $plan }
}
function Invoke-Nsis($Scenario, [string]$Append = '', [switch]$Build) {
    $config = Join-Path $Scenario.Base 'installer-config.nsh'
    [IO.File]::WriteAllText($config, $Scenario.Plan.config + $Append, [Text.UTF8Encoding]::new($false))
    $start = [Diagnostics.ProcessStartInfo]::new($compiler)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.WorkingDirectory = $recipes
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $start.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
    if (-not $Build) { $start.ArgumentList.Add('/SAFEPPO') }
    foreach ($argument in @('/V0', '/INPUTCHARSET', 'UTF8', '/OUTPUTCHARSET', 'UTF8',
        ('/DRCMUSIC_CONFIG=' + (ConvertTo-RcMusicNsisLiteral $config)), (Join-Path $recipes 'RcMusicInstaller.nsi'))) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $started = $false
    try {
        $started = $process.Start()
        if (-not $started) { throw 'Could not start preprocessor' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(15000)) { throw 'Preprocessor timeout' }
        return [pscustomobject]@{ ExitCode = $process.ExitCode; Text = $stdout.GetAwaiter().GetResult(); ErrorText = $stderr.GetAwaiter().GetResult() }
    } finally {
        try { if ($started -and -not $process.HasExited) { $process.Kill($true); if (-not $process.WaitForExit(5000)) { throw 'Owned preprocessor survived cleanup' } } }
        finally { $process.Dispose() }
    }
}
function Preprocess($Scenario, [string]$Append = '') { return Invoke-Nsis $Scenario $Append }
function Marker-Inputs($Scenario, [switch]$Write) {
    foreach ($entry in $Scenario.Plan.generated_files) {
        $content = "RCMusicDownloadTools-v1`n"
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes($content)
        $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
        Require ($entry.relative_path -ceq 'download-component-marker.bin' -and $entry.content -ceq $content) 'Unexpected compiler marker input'
        Require ($bytes.Length -eq 24 -and $entry.sha256 -ceq $hash) 'Compiler marker is not the pinned 24-byte LF payload'
        $path = Join-Path $Scenario.Output $entry.relative_path
        if ($Write) {
            $stream = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            try { $stream.Write($bytes); $stream.Flush($true) } finally { $stream.Dispose() }
            Require ([IO.FileInfo]::new($path).Length -eq 24 -and
                (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $hash) 'Fixture compiler marker bytes changed'
        }
        [pscustomobject]@{ source = $path; size_bytes = 24; sha256 = $hash }
    }
}
function Function-Body([string]$Text, [string]$Name) {
    $match = [regex]::Match($Text, '(?ms)^Function\s+' + [regex]::Escape($Name) + '\s*\r?\n(?<body>.*?)^FunctionEnd\s*$')
    Require $match.Success "Missing preprocessed function: $Name"
    return $match.Groups['body'].Value
}
function Section-Body([string]$Text, [string]$Name) {
    $match = [regex]::Match($Text, '(?ms)^Section\s+(?:/o\s+)?"' + [regex]::Escape($Name) + '"[^\r\n]*\r?\n(?<body>.*?)^SectionEnd\s*$')
    Require $match.Success "Missing preprocessed section: $Name"
    return $match.Groups['body'].Value
}
SafeRoot
Require (-not (Test-Path -LiteralPath $root)) 'Test root already exists'
$null = [IO.Directory]::CreateDirectory($root)
try {
    Case 'Complete welcome explains its existing runtime prerequisite and replacement policy' {
        $scenario = Fixture CompleteExperimental
        $result = Preprocess $scenario
        Require ($result.ExitCode -eq 0) "Preprocessor failed: $($result.ErrorText)"
        Require ($result.Text.Contains('Edição Completa experimental')) 'Complete edition missing from setup text'
        Require ($result.Text.Contains('WebView2 Evergreen x64 já instalado')) 'Existing runtime prerequisite missing'
        Require ($result.Text.Contains('Não baixa nem instala o runtime')) 'Runtime installation boundary missing'
        Require ($result.Text.Contains('substitui a versão atual')) 'Single installation replacement policy missing'
        Require (-not (Test-Path -LiteralPath $scenario.Output)) 'Preprocessor created installer output'
    }
    Case 'Lite welcome preserves local operation and shares the same product identity' {
        $scenario = Fixture Lite
        $result = Preprocess $scenario
        Require ($result.ExitCode -eq 0) "Preprocessor failed: $($result.ErrorText)"
        Require ($result.Text.Contains('Edição Lite')) 'Lite edition missing from setup text'
        Require (-not $result.Text.Contains('WebView2 Evergreen x64 já instalado')) 'Lite unexpectedly requires WebView2'
        Require ($result.Text.Contains('substitui a versão atual')) 'Edition replacement policy missing'
        Require ($result.Text.Contains('Name "RC Music"')) 'Product identity changed'
        Require (-not (Test-Path -LiteralPath $scenario.Output)) 'Preprocessor created installer output'
    }
    Case 'NSIS refuses inconsistent edition and browser prerequisite metadata' {
        $scenario = Fixture CompleteExperimental
        $result = Preprocess $scenario "`n!define /redef RCMUSIC_REQUIRES_WEBVIEW2 0`n"
        Require ($result.ExitCode -ne 0) 'Inconsistent edition metadata was accepted'
        Require (-not (Test-Path -LiteralPath $scenario.Output)) 'Rejected metadata created output'
    }
    Case 'an unavailable component has no components page or selectable download section' {
        $scenario = Fixture Lite
        $result = Preprocess $scenario
        Require ($result.ExitCode -eq 0) "Preprocessor failed: $($result.ErrorText)"
        Require ($result.Text -notmatch '(?im)^PageEx\s+components\s*$') 'Base-only setup added a components page'
        Require ($result.Text -notmatch '(?im)^Section\s+/o\s+.*SEC_RCMUSIC_DOWNLOADS') 'Unavailable tools became selectable'
        Require (-not (Test-Path -LiteralPath $scenario.Output)) 'Preprocessor created installer output'
    }
    Case 'available downloads are unchecked while the player section stays mandatory' {
        $scenario = Fixture Lite -Downloads
        $result = Preprocess $scenario
        Require ($result.ExitCode -eq 0) "Preprocessor failed: $($result.ErrorText)"
        Require ($result.Text -match '(?im)^PageEx\s+components\s*$') 'Optional tools have no components page'
        Require ($result.Text -match '(?im)^Section\s+/o\s+"[^"\r\n]+"\s+SEC_RCMUSIC_DOWNLOADS\s*$') 'Download checkbox is not explicitly unchecked'
        $base = Section-Body $result.Text 'Instalar RC Music'
        Require ($base -match '(?im)^\s*SectionIn\s+RO\s*$') 'The player base can be deselected'
        $init = Function-Body $result.Text '.onInit'
        Require ($init -match '(?im)^\s*StrCpy\s+\$RcDownloadSelected\s+0\s*$') 'Interactive/silent init does not default to OFF'
        Require ($init -notmatch '(?im)^\s*SectionSetFlags\s+.*(?:\b1\b|SF_SELECTED)') 'Init silently selects the optional tools'
        Require (-not (Test-Path -LiteralPath $scenario.Output)) 'Preprocessor created installer output'
    }
    Case 'GUI initialization assigns the separate base and download section sizes' {
        $scenario = Fixture Lite -Downloads
        $result = Preprocess $scenario
        Require ($result.ExitCode -eq 0) "Preprocessor failed: $($result.ErrorText)"
        $baseSize = [regex]::Match($scenario.Plan.config, '(?m)^!define RCMUSIC_BASE_SIZE_KB ([0-9]+)\r?$')
        $downloadSize = [regex]::Match($scenario.Plan.config, '(?m)^!define RCMUSIC_DOWNLOAD_SIZE_KB ([0-9]+)\r?$')
        Require ($baseSize.Success -and $downloadSize.Success) 'The compiled configuration has no separate section sizes'
        $gui = Function-Body $result.Text '.onGUIInit'
        Require (@([regex]::Matches($gui, '(?m)^\s*Call\s+(?:"RCMusic\.InitializeDownloadComponents"|RCMusic\.InitializeDownloadComponents)\s*$')).Count -eq 1) 'GUI initialization does not call the size initializer exactly once'
        $initialize = Function-Body $result.Text 'RCMusic.InitializeDownloadComponents'
        Require (@([regex]::Matches($initialize, '(?m)^\s*SectionSetSize\s+')).Count -eq 2) 'Section sizes are not assigned separately'
        Require ($initialize -match ('(?m)^\s*SectionSetSize\s+(?:0|\$\{SEC_RCMUSIC_BASE\})\s+' + $baseSize.Groups[1].Value + '\s*$')) 'The mandatory section does not receive its own size'
        Require ($initialize -match ('(?m)^\s*SectionSetSize\s+(?:1|\$\{SEC_RCMUSIC_DOWNLOADS\})\s+' + $downloadSize.Groups[1].Value + '\s*$')) 'The optional section does not receive tools plus activation-marker size'
        Require ($initialize -notmatch '(?im)^\s*SectionSetFlags\s+.*(?:\b1\b|SF_SELECTED)') 'The size initializer changes the default selection'
    }
    Case 'download choice and payload identity are frozen before install preflight' {
        $scenario = Fixture Lite -Downloads
        $result = Preprocess $scenario
        Require ($result.ExitCode -eq 0) "Preprocessor failed: $($result.ErrorText)"
        $base = Section-Body $result.Text 'Instalar RC Music'
        $selection = $base.IndexOf('Call RCMusic.SelectDownloadComponent', [StringComparison]::Ordinal)
        $preflight = $base.IndexOf('Call RCMusic.ValidatePath', [StringComparison]::Ordinal)
        Require ($selection -ge 0 -and $preflight -gt $selection) 'Payload selection follows a filesystem/ownership preflight'
        $select = Function-Body $result.Text 'RCMusic.SelectDownloadComponent'
        Require ($select -match '(?im)^\s*SectionGetFlags\s+') 'Installer selection ignores the optional checkbox'
        Require ($select.Contains('$RcDownloadSelected') -and $select.Contains('$RcPayloadId')) 'Selection does not freeze both values'
        $ids = @([regex]::Matches($select, '"([0-9a-f]{64})"') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
        Require ($ids.Count -eq 2) 'Base and download selections do not have two compiled payload identities'
    }
    Case 'installer and uninstaller markers bind the persisted download selection' {
        $scenario = Fixture Lite -Downloads
        $result = Preprocess $scenario
        Require ($result.ExitCode -eq 0) "Preprocessor failed: $($result.ErrorText)"
        foreach ($prefix in @('', 'un.')) {
            $body = Function-Body $result.Text ($prefix + 'RCMusic.VerifyMarker')
            Require ($body -match '(?im)^\s*ReadINIStr\s+[^\r\n]+"DownloadComponent"\s*$') 'Ownership marker ignores the saved selection'
            Require ($body.Contains('$RcDownloadSelected')) 'Marker selection is not compared with the selected payload identity'
        }
    }
    Case 'uninstall initialization preserves its input directory and infers only compiled payload selections' {
        $scenario = Fixture Lite -Downloads
        $result = Preprocess $scenario
        Require ($result.ExitCode -eq 0) "Preprocessor failed: $($result.ErrorText)"
        $init = Function-Body $result.Text 'un.onInit'
        Require ($init -notmatch '(?im)^\s*StrCpy\s+\$INSTDIR\s+"?\$LOCALAPPDATA') 'Uninstaller overwrites its invocation directory with a default payload'
        Require ($init.Contains('$RcDownloadSelected') -and $init.Contains('$RcPayloadId')) 'Uninstaller does not infer the selection from its own path'
        $ids = @([regex]::Matches($init, '[0-9a-f]{64}') | ForEach-Object Value | Sort-Object -Unique)
        Require ($ids.Count -eq 2) 'Uninstaller initialization accepts neither/both compiled version directories'
    }
    Case 'activation is published only between prepared verification and full payload verification' {
        $scenario = Fixture Lite -Downloads
        $result = Preprocess $scenario
        Require ($result.ExitCode -eq 0) "Preprocessor failed: $($result.ErrorText)"
        $base = Section-Body $result.Text 'Instalar RC Music'
        $prepared = $base.IndexOf('Call RCMusic.VerifyPreparedPayload', [StringComparison]::Ordinal)
        Require ($prepared -ge 0) 'Selected bytes have no verification stage before activation'
        $afterPrepared = $base.Substring($prepared + 'Call RCMusic.VerifyPreparedPayload'.Length)
        $full = $afterPrepared.IndexOf('Call RCMusic.VerifyPayload', [StringComparison]::Ordinal)
        Require ($full -ge 0) 'Activated bytes are not verified as a complete installed payload'
        $activation = $afterPrepared.Substring(0, $full)
        $markers = @(Marker-Inputs $scenario)
        Require ($markers.Count -eq 1) 'Selected tools have no unique generated compiler marker'
        $instruction = 'File "/oname=installed-v1" "' + (ConvertTo-RcMusicNsisLiteral $markers[0].source) + '"'
        Require ($activation.Contains($instruction)) 'The pinned compiler marker is not extracted between the two checks'
        $verify = Function-Body $result.Text 'RCMusic.VerifyPayload'
        Require ($verify.Contains($markers[0].sha256)) 'Full payload verification does not bind the extracted marker hash'
        $cleanup = Function-Body $result.Text 'RCMusic.CleanupFresh'
        Require ($cleanup.Contains('installed-v1')) 'Fresh-failure cleanup lost the generated activation marker'
        $uninstall = Section-Body $result.Text 'Uninstall'
        Require ($uninstall.Contains('installed-v1')) 'Uninstall enumeration lost the activation marker'
    }
    Case 'NSIS refuses an unknown component availability value' {
        $scenario = Fixture Lite
        $result = Preprocess $scenario "`n!define /redef RCMUSIC_DOWNLOAD_COMPONENT_AVAILABLE 2`n"
        Require ($result.ExitCode -ne 0) 'Malformed optional-component availability was accepted'
        Require (-not (Test-Path -LiteralPath $scenario.Output)) 'Rejected availability created output'
    }
    if ($Compile) {
        foreach ($edition in @('Lite', 'CompleteExperimental')) {
            foreach ($downloads in @($false, $true)) {
                Case "full template compiles $edition with component availability $downloads without executing setup" {
                    $scenario = Fixture $edition -Downloads:$downloads
                    Require (-not (Test-Path -LiteralPath $scenario.Output)) 'Compiler destination is not fresh'
                    $null = [IO.Directory]::CreateDirectory($scenario.Output)
                    Marker-Inputs $scenario -Write | Out-Null
                    $result = Invoke-Nsis $scenario -Build
                    Require ($result.ExitCode -eq 0) "Full NSIS compilation failed: stdout=$($result.Text) stderr=$($result.ErrorText)"
                    $setup = Join-Path $scenario.Output 'RCMusic-Setup.exe'
                    Require ([IO.File]::Exists($setup) -and [IO.FileInfo]::new($setup).Length -gt 0) 'Compiler produced no setup artifact'
                }
            }
        }
    }
} finally {
    SafeRoot
    foreach ($item in Get-ChildItem -LiteralPath $root -Recurse -Force) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse child refused during cleanup' }
    }
    Remove-Item -LiteralPath ([IO.Path]::GetFullPath($root)) -Recurse -Force
}
Write-Host "Installer edition preprocessor: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed) { exit 1 }
