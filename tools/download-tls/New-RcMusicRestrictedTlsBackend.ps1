[CmdletBinding()]
param(
    [string]$SourceRoot = 'q/96bvah3m0t0t71m990olv5gpvs/source/qtbase-patched',
    [string]$QtPrefix = 'q/f75eb9bea8280844c6d1a2b3f4/sdk',
    [string]$OutputDirectory = 'out/product/download-tls/rcmusic-schannel-offline-v1',
    [switch]$PrepareOnly,
    [string]$ToolchainFile = 'tools/windows.example.psd1'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$source = [IO.Path]::GetFullPath($SourceRoot, $repository)
$qt = [IO.Path]::GetFullPath($QtPrefix, $repository)
$output = [IO.Path]::GetFullPath($OutputDirectory, $repository)
$buildArea = [IO.Path]::GetFullPath((Join-Path $repository 'out/product/download-tls'))
if (-not $output.StartsWith($buildArea + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputOutsideBuildArea'
}
if (Test-Path -LiteralPath $output) { throw 'OutputAlreadyExists' }
for ($ancestor = [IO.DirectoryInfo]::new($output).Parent; $null -ne $ancestor; $ancestor = $ancestor.Parent) {
    if ($ancestor.Exists -and ($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'OutputReparsePoint'
    }
}

$sourceHashes = [ordered]@{
    'src/plugins/tls/schannel/qtls_schannel.cpp' = '4C9F3C8E3692073F3622CB909096013BEDEC0A9D97EF3BE5FC3938A40F02A2B8'
    'src/plugins/tls/schannel/qtls_schannel_p.h' = '0BE7CA348CAC195643ADEF7BBE3149B45DD49D8E1D7667234BED0ACBCDD61C83'
    'src/plugins/tls/schannel/qtlsbackend_schannel_p.h' = 'BBB05145253693A77726B5FEA61AC8D569A9EF68A8D1EA1FFA3C93221811AABF'
    'src/plugins/tls/schannel/qtlskey_schannel.cpp' = '47EAD5D70985ED8C3F16BE52ACD7AB8FCA80F09FC89205460AF48843A797DBEB'
    'src/plugins/tls/schannel/qtlskey_schannel_p.h' = '61AECC7D760F01FC96475397FECAF75E19DD8179AA26787D02324EC2C0FD8892'
    'src/plugins/tls/schannel/qx509_schannel.cpp' = '78131B8BF7660A409C66836343731043B37F4F36EFE979828A3DFEFE36937112'
    'src/plugins/tls/schannel/qx509_schannel_p.h' = 'BD92DAB58A8BD9DD37811E00572449E11C69E1A35E56AD27BF6938DC5F07E855'
    'src/plugins/tls/shared/qtlskey_base.cpp' = '05F704C056C9A14D1244011EBD2B0054278D51BB80CAAF1745F220E50DA71077'
    'src/plugins/tls/shared/qtlskey_base_p.h' = '0293C0E2B000758DDA5E865829EA6CBCC5E9B4FA5E9694906249F50DF285CE86'
    'src/plugins/tls/shared/qtlskey_generic.cpp' = '9B98655EF94B99689A1C19EFCC943259F85230F7843A8176F724695778AC7E7D'
    'src/plugins/tls/shared/qtlskey_generic_p.h' = 'EF7C2F6ED806780649D0BCB7C9B5F957E6A2344237899569D5A51809A3FA79BB'
    'src/plugins/tls/shared/qx509_base.cpp' = '41C02A2449C3F08C4C76F81F3EF6B88C530B13A9A22ADC8560983DAF9CF18F7E'
    'src/plugins/tls/shared/qx509_base_p.h' = '81054E6D31BF35AB06F380F9497A3514EE0C259B6A08657EB1236AA7182E76A8'
    'src/plugins/tls/shared/qx509_generic.cpp' = '5ED0C4F86DD0F28C200342409A395D2BE15C075B61033892123DE0D2741601B7'
    'src/plugins/tls/shared/qx509_generic_p.h' = 'C49D0CF81C9150A90ECFB4D746A9BDD2472A531C79310F2DFC4869CD8D397D94'
    'src/plugins/tls/shared/qsslsocket_qt.cpp' = '700D0BA4DCCE253072E433B11A0BE43C313746B982087EC55BF7959103C2CE25'
    'src/plugins/tls/shared/qwincrypt_p.h' = 'F8323AA61056E7079963EB87C003BA692947C17F5252A5D765156FD288DB3900'
    'src/plugins/tls/shared/qasn1element.cpp' = '41E85DB828FBD0C53E769BC455EC6AC616F6A7E892C0D7707FC2565C8AC1B502'
    'src/plugins/tls/shared/qasn1element_p.h' = '1FD5E9B1BE4645C63E089ADDF4AD0E2F39850AD75EBB54290CF544E4876CA33E'
    '.cmake.conf' = 'CC100DDAB04016FB09EE22A5EE2FB7D8BCF710F354EC28E7EB11FA1EF94B8D93'
    'LICENSES/LGPL-3.0-only.txt' = 'DA7EABB7BAFDF7D3AE5E9F223AA5BDC1EECE45AC569DC21B3B037520B4464768'
    'LICENSES/GPL-3.0-only.txt' = '8CEB4B9EE5ADEDDE47B31E975C1D90C73AD27B6B165A1DCD80C7C545EB65B903'
}
$sdkHashes = [ordered]@{
    'bin/Qt6Core.dll' = '3D472B9249AA3A91DABF453C7E84B2F172D2A26EC4266B0919D6BB985438497A'
    'bin/Qt6Network.dll' = '5B98C10688DD00791D43B518404C3C3378F3996A67350516B5A939ECD4E4982A'
}
$inputs = [Collections.Generic.List[object]]::new()
foreach ($group in @(@{root=$source; hashes=$sourceHashes; error='SourceHashMismatch'},
                    @{root=$qt; hashes=$sdkHashes; error='SdkHashMismatch'})) {
    foreach ($entry in $group.hashes.GetEnumerator()) {
        $file = Join-Path $group.root $entry.Key
        if (-not [IO.File]::Exists($file) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.Value) {
            throw ($group.error + ': ' + $entry.Key)
        }
        $inputs.Add([ordered]@{path=$file; sha256=$entry.Value.ToLowerInvariant()})
    }
}

function Replace-Exact([string]$Text, [string]$Old, [string]$New, [int]$Count = 1) {
    $matches = [regex]::Matches($Text, [regex]::Escape($Old)).Count
    if ($matches -ne $Count) { throw ('PatchAnchorMismatch: expected ' + $Count + ', got ' + $matches) }
    return $Text.Replace($Old, $New)
}

$cpp = [IO.File]::ReadAllText((Join-Path $source 'src/plugins/tls/schannel/qtls_schannel.cpp')).Replace("`r`n", "`n")
$cpp = Replace-Exact $cpp '#include "qtlsbackend_schannel_p.h"' "#include `"qtlsbackend_schannel_p.h`"`n#include `"../rc_music_tls_input_limits.h`""
$cpp = Replace-Exact $cpp 'return builtinBackendNames[nameIndexSchannel];' 'return QStringLiteral("rcmusic-schannel-offline");'
$cpp = Replace-Exact $cpp @'
DWORD defaultCredsFlag()
{
    return qEnvironmentVariableIsSet("QT_SCH_DEFAULT_CREDS") ? 0 : SCH_CRED_NO_DEFAULT_CREDS;
}
'@ ''
$cpp = Replace-Exact $cpp 'SCH_CRED_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT | defaultCredsFlag(),' @'
SCH_CRED_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT
            | SCH_CRED_REVOCATION_CHECK_CACHE_ONLY
            | SCH_CRED_CACHE_ONLY_URL_RETRIEVAL
            | SCH_CRED_CACHE_ONLY_URL_RETRIEVAL_ON_CREATE
            | SCH_CRED_NO_DEFAULT_CREDS,
'@
$cpp = Replace-Exact $cpp 'CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT, // dwFlags' @'
CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT
                                              | CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL
                                              | CERT_CHAIN_REVOCATION_CHECK_CACHE_ONLY
                                              | CERT_CHAIN_DISABLE_AIA, // dwFlags: no auxiliary retrieval
'@
$cpp = Replace-Exact $cpp "    const auto toRead = std::min(defaultRead, plainSocket->bytesAvailable());`n    if (toRead > 0) {" @'
    const auto toRead = std::min(defaultRead, plainSocket->bytesAvailable());
    if (!rcMusicTlsInputFits(buffer.size(), toRead))
        return -1; // Refuse before reserve/resize; the caller terminates its own socket.
    if (toRead > 0) {
'@
$boolGuard = @'
    if (!rcMusicTlsInputFits(intermediateBuffer.size(), missingData)) {
        setErrorAndEmit(d, QAbstractSocket::SslHandshakeFailedError,
                        QSslSocket::tr("TLS input limit exceeded"));
        q->abort();
        return false;
    }

    if (missingData > plainSocket->bytesAvailable())
        return true;
'@
$cpp = Replace-Exact $cpp "    if (missingData > plainSocket->bytesAvailable())`n        return true;" $boolGuard 2
$cpp = Replace-Exact $cpp '    readToBuffer(intermediateBuffer, plainSocket);' @'
    if (readToBuffer(intermediateBuffer, plainSocket) < 0) {
        setErrorAndEmit(d, QAbstractSocket::SslHandshakeFailedError,
                        QSslSocket::tr("TLS input limit exceeded"));
        q->abort();
        return false;
    }
'@ 2
$cpp = Replace-Exact $cpp '        if (missingData > plainSocket->bytesAvailable()' @'
        if (!rcMusicTlsInputFits(intermediateBuffer.size(), missingData)) {
            setErrorAndEmit(d, QAbstractSocket::SslHandshakeFailedError,
                            QSslSocket::tr("TLS input limit exceeded"));
            q->abort();
            return;
        }
        if (missingData > plainSocket->bytesAvailable()
'@
$cpp = Replace-Exact $cpp '        const qint64 bytesRead = readToBuffer(intermediateBuffer, plainSocket);' @'
        const qint64 bytesRead = readToBuffer(intermediateBuffer, plainSocket);
        if (bytesRead < 0) {
            setErrorAndEmit(d, QAbstractSocket::SslHandshakeFailedError,
                            QSslSocket::tr("TLS input limit exceeded"));
            q->abort();
            return;
        }
'@
$cpp = Replace-Exact $cpp '            // @future(maybe): Do something with this' @'
            // RC Music preserves Qt's soft-fail policy for unavailable cached
            // revocation status. An accepted certificate is NOT proof of a
            // known-good revocation status; explicit cached revocation still fails.
'@

# All input and patch checks complete before any destination is created.
$null = New-Item -ItemType Directory -Path $output
$utf8 = [Text.UTF8Encoding]::new($false)
foreach ($entry in $sourceHashes.GetEnumerator()) {
    if ($entry.Key.StartsWith('src/plugins/tls/')) {
        $relative = 'source/' + $entry.Key.Substring('src/plugins/tls/'.Length)
    } else { $relative = 'source/' + $entry.Key }
    $destination = Join-Path $output $relative
    $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
    $original = Join-Path $source $entry.Key
    if ($entry.Key -eq 'src/plugins/tls/schannel/qtls_schannel.cpp') {
        [IO.File]::WriteAllText($destination, $cpp.Replace('QSchannelBackend', 'RcMusicSchannelBackend'), $utf8)
    } elseif ($entry.Key -eq 'src/plugins/tls/schannel/qtlsbackend_schannel_p.h') {
        [IO.File]::WriteAllText($destination, [IO.File]::ReadAllText($original).Replace('QSchannelBackend', 'RcMusicSchannelBackend'), $utf8)
    } else { Copy-Item -LiteralPath $original -Destination $destination }
}
foreach ($file in @('CMakeLists.txt', 'rc_music_tls_input_limits.h')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination (Join-Path $output ('source/' + $file))
}
$null = [IO.Directory]::CreateDirectory((Join-Path $output 'source/tests'))
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'tests/TlsInputLimitsTests.cpp') -Destination (Join-Path $output 'source/tests/TlsInputLimitsTests.cpp')
$outputs = @(Get-ChildItem -LiteralPath (Join-Path $output 'source') -Recurse -File | Sort-Object FullName | ForEach-Object {
    [ordered]@{path=[IO.Path]::GetRelativePath($output, $_.FullName).Replace('\','/'); sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}
})
$receipt = [ordered]@{schema_version=1; backend='rcmusic-schannel-offline'; qt_version='6.11.2'; compiled=$false;
    revocation_policy='cached-status-soft-fail-unknown'; tls_input_limit=262144; inputs=$inputs.ToArray(); outputs=$outputs}
[IO.File]::WriteAllText((Join-Path $output 'prepared.json'), ($receipt | ConvertTo-Json -Depth 8), $utf8)
if ($PrepareOnly) { Write-Output $output; return }

$tools = Import-PowerShellDataFile -LiteralPath ([IO.Path]::GetFullPath($ToolchainFile, $repository))
foreach ($name in @('CMake','CTest','Ninja','Compiler','VisualStudioRoot')) {
    if (-not (Test-Path -LiteralPath $tools[$name])) { throw ('ToolUnavailable: ' + $name) }
}
Import-Module (Join-Path $tools.VisualStudioRoot 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $tools.VisualStudioRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64 -vcvars_ver=14.44 -winsdk=10.0.22621.0' | Out-Null
$build = Join-Path $output 'build'
& $tools.CMake -S (Join-Path $output 'source') -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$($tools.Ninja)" "-DCMAKE_CXX_COMPILER=$($tools.Compiler)" "-DCMAKE_PREFIX_PATH=$qt" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'ConfigureFailed' }
& $tools.CMake --build $build --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'BuildFailed' }
& $tools.CTest --test-dir $build --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'TestsFailed' }
$plugin = Join-Path $build 'plugins/tls/qrcmusicschanneloffline.dll'
if (-not [IO.File]::Exists($plugin)) { throw 'PluginMissing' }
$builtReceipt = [ordered]@{schema_version=1; backend=$receipt.backend; qt_version='6.11.2'; compiled=$true;
    plugin_path=$plugin; plugin_sha256=(Get-FileHash -LiteralPath $plugin).Hash.ToLowerInvariant();
    prepared_sha256=(Get-FileHash -LiteralPath (Join-Path $output 'prepared.json')).Hash.ToLowerInvariant();
    compiler=$tools.Compiler; qt_prefix=$qt; revocation_policy=$receipt.revocation_policy}
[IO.File]::WriteAllText((Join-Path $output 'built.json'), ($builtReceipt | ConvertTo-Json -Depth 5), $utf8)
Write-Output $plugin
