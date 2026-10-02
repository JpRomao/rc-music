[CmdletBinding(DefaultParameterSetName = 'Artifacts')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Artifacts')][string]$DumpbinPath,
    [Parameter(Mandatory = $true, ParameterSetName = 'Artifacts')][string]$BootstrapPath,
    [Parameter(Mandatory = $true, ParameterSetName = 'Artifacts')][string]$PayloadPath,
    [Parameter(Mandatory = $true, ParameterSetName = 'Artifacts')][string]$QtBinDirectory,
    [Parameter(Mandatory = $true, ParameterSetName = 'Artifacts')][string]$CrtDirectory,
    [Parameter(Mandatory = $true, ParameterSetName = 'Artifacts')][string]$TlsPluginPath,
    [Parameter(Mandatory = $true, ParameterSetName = 'SelfTest')][switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# This is a read-only build-artifact check, not the authenticated launcher.
# The supplied dumpbin executable and the Windows installation are trusted.

function Get-TrustedInputPath {
    param([string]$Path, [string]$Leaf, [bool]$Directory = $false)
    if ($Path -notmatch '^[A-Za-z]:[\\/]' -or $Path.IndexOf([char]0) -ge 0) {
        throw "An existing absolute local path is required for $Leaf."
    }
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.PSIsContainer -ne $Directory -or
        (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) -or
        (-not $Directory -and $item.Name -ine $Leaf)) {
        throw "Unexpected input type or fixed filename for $Leaf."
    }
    return $item.FullName
}

function Read-WorkerPeHeaders {
    param([string]$Text, [string]$Role)
    $expectedType = if ($Role -eq 'Bootstrap') { 'EXECUTABLE IMAGE' } else { 'DLL' }
    $type = [regex]::Matches($Text, '(?m)^File Type:\s*(.+?)\s*\r?$')
    $machine = [regex]::Matches($Text, '(?m)^\s*([0-9A-Fa-f]+) machine \(x64\)\s*\r?$')
    $magic = [regex]::Matches($Text, '(?m)^\s*20B magic # \(PE32\+\)\s*\r?$')
    $image = [regex]::Matches($Text, '(?m)^\s*([0-9A-Fa-f]+) size of image\s*\r?$')
    $delay = [regex]::Matches($Text,
        '(?m)^\s*([0-9A-Fa-f]+) \[\s*([0-9A-Fa-f]+)\] RVA \[size\] of Delay Import Directory\s*\r?$')
    if ($type.Count -ne 1 -or $type[0].Groups[1].Value -cne $expectedType -or
        $machine.Count -ne 1 -or [Convert]::ToUInt32($machine[0].Groups[1].Value, 16) -ne 0x8664 -or
        $magic.Count -ne 1 -or $image.Count -ne 1 -or $delay.Count -ne 1) {
        throw "Unrecognized x64 PE32+ headers for $Role; no profile approval."
    }
    if ([Convert]::ToUInt32($delay[0].Groups[1].Value, 16) -ne 0 -or
        [Convert]::ToUInt32($delay[0].Groups[2].Value, 16) -ne 0) {
        throw "Delay imports are not allowed in the reviewed profile: $Role."
    }
    $imageBytes = [Convert]::ToUInt32($image[0].Groups[1].Value, 16)
    if ($imageBytes -eq 0) {
        throw "An empty SizeOfImage is not allowed: $Role."
    }
    return $imageBytes
}

function Read-WorkerPeImports {
    param([string]$Text, [string]$Role)
    # The allowed API-set names are deliberately exact, not '*.dll' or a broad
    # API-set regex. A new toolchain/Qt dependency requires explicit review.
    $allowed = @(
        'KERNEL32.dll', 'ADVAPI32.dll', 'AUTHZ.dll', 'bcrypt.dll', 'CRYPT32.dll',
        'DNSAPI.dll', 'IPHLPAPI.dll', 'MPR.dll', 'ncrypt.dll', 'NETAPI32.dll',
        'ntdll.dll', 'ole32.dll', 'OLEAUT32.dll', 'Secur32.dll', 'SHELL32.dll',
        'USER32.dll', 'USERENV.dll', 'VERSION.dll', 'WINHTTP.dll', 'WINMM.dll',
        'WS2_32.dll', 'icuuc.dll', 'Qt6Core.dll', 'Qt6Network.dll',
        'msvcp140.dll', 'msvcp140_1.dll', 'vcruntime140.dll', 'vcruntime140_1.dll',
        'api-ms-win-core-synch-l1-2-0.dll', 'api-ms-win-core-winrt-l1-1-0.dll',
        'api-ms-win-core-winrt-error-l1-1-1.dll', 'api-ms-win-crt-runtime-l1-1-0.dll',
        'api-ms-win-crt-heap-l1-1-0.dll', 'api-ms-win-crt-string-l1-1-0.dll',
        'api-ms-win-crt-stdio-l1-1-0.dll', 'api-ms-win-crt-environment-l1-1-0.dll',
        'api-ms-win-crt-math-l1-1-0.dll', 'api-ms-win-crt-utility-l1-1-0.dll',
        'api-ms-win-crt-time-l1-1-0.dll', 'api-ms-win-crt-filesystem-l1-1-0.dll',
        'api-ms-win-crt-convert-l1-1-0.dll', 'api-ms-win-crt-locale-l1-1-0.dll'
    )
    if ($Text -notmatch 'Section contains the following imports:') {
        throw "Missing import table output for $Role."
    }
    # Recognize every descriptor by its following Import Address Table, not
    # its extension or first name character. Leading whitespace, uppercase,
    # non-DLL/Unicode/path-like names must be checked, never silently skipped.
    $tables = [regex]::Matches($Text, '(?m)^[ \t]+[0-9A-Fa-f]+ Import Address Table[ \t]*\r?$')
    $descriptors = [regex]::Matches($Text,
        '(?m)^ {4}([^\r\n]+)\r?\n[ \t]+[0-9A-Fa-f]+ Import Address Table[ \t]*\r?$')
    if ($tables.Count -eq 0 -or $descriptors.Count -ne $tables.Count) {
        throw "Missing or unrecognized import descriptor for $Role."
    }
    $names = @($descriptors | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    if ($Role -eq 'Bootstrap') {
        if ($names.Count -ne 1 -or $names[0] -ine 'KERNEL32.dll') {
            throw 'The bootstrap must import only KERNEL32.dll (no Qt or dynamic VC runtime).'
        }
    } else {
        foreach ($name in $names) {
            if ($allowed -inotcontains $name) {
                throw "Unreviewed DLL import for ${Role}: $name."
            }
        }
    }
    return $names
}

function Invoke-WorkerDumpbin {
    param([string]$Executable, [string]$Option, [string]$Artifact)
    $output = & $Executable /nologo $Option $Artifact 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed ($LASTEXITCODE) while checking $Option."
    }
    return ($output | Out-String)
}

function Test-WorkerPeParsers {
    # All fixtures are strings in memory; this mode opens no paths/processes.
    $header = "File Type: DLL`n8664 machine (x64)`n20B magic # (PE32+)`n16000 size of image`n0 [ 0] RVA [size] of Delay Import Directory`n"
    $bootstrap = "Section contains the following imports:`n    KERNEL32.dll`n       123 Import Address Table`n"
    if ((Read-WorkerPeHeaders -Text $header -Role 'Payload') -ne 90112 -or
        @(Read-WorkerPeImports -Text $bootstrap -Role 'Bootstrap').Count -ne 1 -or
        @(Read-WorkerPeImports -Text ($bootstrap.Replace('KERNEL32.dll', 'IPHLPAPI.DLL')) -Role 'Network')[0] -cne 'IPHLPAPI.DLL') {
        throw 'A valid parser fixture was rejected or changed.'
    }
    $cases = [Collections.Generic.List[object]]::new()
    foreach ($entry in @(
        @{ Text = $header.Replace('0 [ 0]', '1 [ 0]'); Expected = 'Delay imports' },
        @{ Text = $header.Replace('0 [ 0]', '0 [ 1]'); Expected = 'Delay imports' },
        @{ Text = $header.Replace('0 [ 0] RVA [size] of Delay Import Directory', ''); Expected = 'Unrecognized' },
        @{ Text = $header + $header; Expected = 'Unrecognized' },
        @{ Text = $header.Replace('8664', '14C'); Expected = 'Unrecognized' },
        @{ Text = $header.Replace('20B', '10B'); Expected = 'Unrecognized' },
        @{ Text = $header.Replace('16000 size of image', '0 size of image'); Expected = 'empty SizeOfImage' }
    )) {
        $cases.Add(@{ Kind = 'Headers'; Text = $entry.Text; Role = 'Payload'; Expected = $entry.Expected })
    }
    $cases.Add(@{ Kind = 'Headers'; Text = $header; Role = 'Bootstrap'; Expected = 'Unrecognized' })
    $cases.Add(@{ Kind = 'Imports'; Text = '    KERNEL32.dll'; Role = 'Bootstrap'; Expected = 'Missing import table' })
    $cases.Add(@{ Kind = 'Imports'; Text = 'Section contains the following imports:'; Role = 'Payload'; Expected = 'unrecognized import descriptor' })
    $cases.Add(@{ Kind = 'Imports'; Text = $bootstrap + "       456 Import Address Table`n"; Role = 'Bootstrap'; Expected = 'unrecognized import descriptor' })
    $cases.Add(@{ Kind = 'Imports'; Text = $bootstrap.Replace('KERNEL32.dll', 'Qt6Core.dll'); Role = 'Bootstrap'; Expected = 'bootstrap must import' })
    foreach ($name in @('unreviewed.DLL', 'unreviewed.exe', 'bad name.dll', '日本.dll', 'C:\foreign.dll',
        ' unreviewed.dll', ([string][char]9 + 'unreviewed.dll'))) {
        foreach ($role in @('Bootstrap', 'Payload')) {
            $expected = if ($role -eq 'Bootstrap') { 'bootstrap must import' } else { 'Unreviewed DLL import' }
            $cases.Add(@{ Kind = 'Imports'; Text = $bootstrap + "    $name`n       456 Import Address Table`n";
                Role = $role; Expected = $expected })
        }
    }
    foreach ($case in $cases) {
        $failure = $null
        try {
            if ($case.Kind -eq 'Headers') {
                $null = Read-WorkerPeHeaders -Text $case.Text -Role $case.Role
            } else {
                $null = Read-WorkerPeImports -Text $case.Text -Role $case.Role
            }
        } catch {
            $failure = $_.Exception.Message
        }
        if ($null -eq $failure -or $failure -notlike ('*' + $case.Expected + '*')) {
            throw "Parser failed to reject a fixture for $($case.Role): $($case.Expected)."
        }
    }
    [pscustomobject]@{ result = 'passed'; mode = 'parser-self-test'; cases = $cases.Count + 3 } | ConvertTo-Json
}

if ($PSCmdlet.ParameterSetName -eq 'SelfTest') {
    Test-WorkerPeParsers
    return
}

$dumpbin = Get-TrustedInputPath -Path $DumpbinPath -Leaf 'dumpbin.exe'
$qtBin = Get-TrustedInputPath -Path $QtBinDirectory -Leaf 'Qt directory' -Directory $true
$crtBin = Get-TrustedInputPath -Path $CrtDirectory -Leaf 'CRT directory' -Directory $true
$definitions = @(
    @{ Role = 'Bootstrap'; Leaf = 'RCMusicDownloadWorker.exe'; Path = $BootstrapPath },
    @{ Role = 'Payload'; Leaf = 'rcmusic-download-network.dll'; Path = $PayloadPath },
    @{ Role = 'Core'; Leaf = 'Qt6Core.dll'; Path = (Join-Path $qtBin 'Qt6Core.dll') },
    @{ Role = 'Network'; Leaf = 'Qt6Network.dll'; Path = (Join-Path $qtBin 'Qt6Network.dll') },
    @{ Role = 'Msvcp140'; Leaf = 'msvcp140.dll'; Path = (Join-Path $crtBin 'msvcp140.dll') },
    @{ Role = 'Msvcp140_1'; Leaf = 'msvcp140_1.dll'; Path = (Join-Path $crtBin 'msvcp140_1.dll') },
    @{ Role = 'Vcruntime140'; Leaf = 'vcruntime140.dll'; Path = (Join-Path $crtBin 'vcruntime140.dll') },
    @{ Role = 'Vcruntime140_1'; Leaf = 'vcruntime140_1.dll'; Path = (Join-Path $crtBin 'vcruntime140_1.dll') },
    @{ Role = 'Plugin'; Leaf = 'qrcmusicschanneloffline.dll'; Path = $TlsPluginPath }
)
$held = [Collections.Generic.List[IDisposable]]::new()
$artifacts = [Collections.Generic.List[object]]::new()
$results = [Collections.Generic.List[object]]::new()
try {
    # Ordinary writes/deletes/renames are refused during this local snapshot.
    # These managed file handles do not replace the launcher's native parent
    # anchors, file identity checks, ACL, hash pins or process/pipe lifetime.
    $held.Add([IO.File]::Open($dumpbin, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read))
    foreach ($definition in $definitions) {
        $path = Get-TrustedInputPath -Path $definition.Path -Leaf $definition.Leaf
        $stream = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        $held.Add($stream)
        if ($stream.Length -le 0 -or $stream.Length -gt 128MB) {
            throw "The artifact exceeds the launcher's 128 MiB file bound: $($definition.Role)."
        }
        $artifacts.Add([pscustomobject]@{ Role = $definition.Role; Path = $path; Stream = $stream })
    }
    foreach ($artifact in $artifacts) {
        $hash = [Security.Cryptography.SHA256]::Create()
        try {
            $sha256 = [BitConverter]::ToString($hash.ComputeHash($artifact.Stream)).Replace('-', '').ToLowerInvariant()
        } finally {
            $hash.Dispose()
        }
        $headers = Invoke-WorkerDumpbin -Executable $dumpbin -Option '/headers' -Artifact $artifact.Path
        $imageBytes = Read-WorkerPeHeaders -Text $headers -Role $artifact.Role
        $imports = Invoke-WorkerDumpbin -Executable $dumpbin -Option '/imports' -Artifact $artifact.Path
        $names = @(Read-WorkerPeImports -Text $imports -Role $artifact.Role)
        $results.Add([pscustomobject]@{
            role = $artifact.Role; path = $artifact.Path; bytes = $artifact.Stream.Length
            sha256 = $sha256; size_of_image = $imageBytes; imports = $names
        })
    }
    [pscustomobject]@{
        profile = 'rcmusic-network-worker-x64-nine-artifacts-v1'
        result = 'passed'
        dumpbin = $dumpbin
        artifacts = $results.ToArray()
        limitations = 'Static import/header snapshot only; Windows DLL trust, loader redirection, forwarded exports, dynamic loads, manifests and authenticated process lifetime require separate review.'
    } | ConvertTo-Json -Depth 5
} finally {
    for ($index = $held.Count - 1; $index -ge 0; --$index) {
        $held[$index].Dispose()
    }
}
