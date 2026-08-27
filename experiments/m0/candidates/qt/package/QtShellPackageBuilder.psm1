#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-NormalWindowsPath([string] $Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or $Path.StartsWith('\\')) {
        throw 'A normal local Windows path is required.'
    }
    $tail = $Path.Replace('/', '\') -replace '^[A-Za-z]:\\', ''
    foreach ($part in $tail.Split('\')) {
        $device = $part.Split('.')[0].TrimEnd(' ')
        if ($part -in @('', '.', '..') -or $part -match '[\x00-\x1f<>:"|?*]' -or
            $part -match '[. ]$' -or $device -match '^(?i:CON|PRN|AUX|NUL|CONIN\$|CONOUT\$|COM[1-9¹²³]|LPT[1-9¹²³])$') {
            throw "Unsafe path name or traversal: $Path"
        }
    }
}

function Assert-NoReparseAncestor([string] $Path, [Collections.Generic.HashSet[string]] $Checked = $null) {
    $cursor = $Path
    while (-not [string]::IsNullOrEmpty($cursor)) {
        if ($null -ne $Checked -and $Checked.Contains($cursor)) { return }
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "A reparse path is not allowed: $cursor"
            }
        }
        if ($null -ne $Checked) { $null = $Checked.Add($cursor) }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
}

function Resolve-NormalChildPath {
    param([Parameter(Mandatory)] [string] $BasePath, [Parameter(Mandatory)] [string] $CandidatePath)
    Assert-NormalWindowsPath $BasePath
    Assert-NormalWindowsPath $CandidatePath
    if (-not [IO.Path]::IsPathFullyQualified($BasePath)) { throw 'Base path must be absolute.' }
    $base = [IO.Path]::GetFullPath($BasePath)
    $resolved = [IO.Path]::GetFullPath($CandidatePath, $base)
    if (-not $resolved.StartsWith($base.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside its scoped parent: $CandidatePath"
    }
    return $resolved
}

function Resolve-QtShellPackageChildPath {
    param([Parameter(Mandatory)] [string] $BasePath, [Parameter(Mandatory)] [string] $CandidatePath)
    $resolved = Resolve-NormalChildPath $BasePath $CandidatePath
    Assert-NoReparseAncestor $resolved
    return $resolved
}

function New-QtShellPackageDirectory {
    param([Parameter(Mandatory)] [string] $Parent, [Parameter(Mandatory)] [string] $Name)
    if ($Name.Contains('/') -or $Name.Contains('\')) { throw 'Package name must be one path component.' }
    $destination = Resolve-QtShellPackageChildPath $Parent $Name
    if (Test-Path -LiteralPath $destination) { throw "Package destination already exists: $destination" }
    $null = New-Item -ItemType Directory -Path $destination -ErrorAction Stop
    return $destination
}

function ConvertTo-QtShellDeploymentPlan {
    param(
        [Parameter(Mandatory)] $DeploymentJson,
        [Parameter(Mandatory)] [string] $QtRoot,
        [Parameter(Mandatory)] [string] $PackageRoot,
        [string] $ApplicationQmlRoot = ''
    )
    if (@($DeploymentJson.files).Count -eq 0) { throw 'The deployment plan is empty.' }
    $entries = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
    # A plan is built on quiescent inputs. Avoid re-reading the same parents for
    # every QML asset; this is not a handle-based concurrent-writer guarantee.
    $checkedPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($mapping in $DeploymentJson.files) {
        $origin = 'qt-toolchain'
        $sourceRoot = $QtRoot
        if (-not [string]::IsNullOrEmpty($ApplicationQmlRoot) -and
            [IO.Path]::GetFullPath([string] $mapping.source).StartsWith([IO.Path]::GetFullPath($ApplicationQmlRoot).TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
            $origin = 'application-qml'
            $sourceRoot = $ApplicationQmlRoot
        }
        $source = Resolve-NormalChildPath $sourceRoot ([string] $mapping.source)
        $destination = Resolve-NormalChildPath $PackageRoot (Join-Path ([string] $mapping.target) ([IO.Path]::GetFileName($source)))
        Assert-NoReparseAncestor $source $checkedPaths
        Assert-NoReparseAncestor $destination $checkedPaths
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Deployment source is missing: $source" }
        $name = [IO.Path]::GetFileName($source)
        $extension = [IO.Path]::GetExtension($name).ToLowerInvariant()
        if (($extension -notin @('.dll', '.qml', '.qmltypes', '.png') -and $name -cne 'qmldir') -or
            $name -match '^Qt6(?:Test|QuickTest|QuickControlsTestUtils|QmlDebug)' -or
            $name -match '^Qt6.*d\.dll$' -or ($origin -eq 'application-qml' -and $extension -eq '.dll')) {
            throw "Development or unexpected payload file: $name"
        }
        $relative = [IO.Path]::GetRelativePath($PackageRoot, $destination).Replace('\', '/')
        if ($entries.ContainsKey($relative)) { throw "Duplicate deployment destination: $relative" }
        $file = Get-Item -LiteralPath $source -Force
        $entries.Add($relative, [pscustomobject] [ordered] @{
            relative_path = $relative
            origin = $origin
            source_path = [IO.Path]::GetRelativePath($sourceRoot, $source).Replace('\', '/')
            size_bytes = $file.Length
            sha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
        })
    }
    [string[]] $keys = @($entries.Keys)
    [Array]::Sort($keys, [StringComparer]::Ordinal)
    return [pscustomobject] [ordered] @{ schema_version = 1; files = @($keys | ForEach-Object { $entries[$_] }) }
}

function Test-QtShellReleaseCache {
    param(
        [Parameter(Mandatory)] [string] $CacheText,
        [Parameter(Mandatory)] [string] $SourceRoot,
        [Parameter(Mandatory)] [string] $QtRoot
    )
    $cache = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::Ordinal)
    foreach ($line in $CacheText -split '\r?\n') {
        if ($line -match '^(?<name>[^/#][^:=]*):[^=]*=(?<value>.*)$') {
            if (-not $cache.TryAdd($Matches.name, $Matches.value)) { throw "Duplicate cache key: $($Matches.name)" }
        }
    }
    $required = [ordered] @{
        CMAKE_BUILD_TYPE = 'Release'
        BUILD_TESTING = 'ON'
        M0_QT_SHELL_SUPPORT = 'ON'
        CMAKE_GENERATOR = 'Ninja'
    }
    foreach ($key in $required.Keys) {
        if (-not $cache.ContainsKey($key) -or $cache[$key] -cne $required[$key]) {
            throw "Release cache requires $key=$($required[$key])."
        }
    }
    foreach ($pair in @(@('CMAKE_HOME_DIRECTORY', $SourceRoot), @('Qt6_DIR', (Join-Path $QtRoot 'lib/cmake/Qt6')))) {
        if (-not $cache.ContainsKey($pair[0]) -or
            -not [IO.Path]::GetFullPath($cache[$pair[0]]).Equals([IO.Path]::GetFullPath($pair[1]), [StringComparison]::OrdinalIgnoreCase)) {
            throw "Release cache has an unexpected source or Qt path: $($pair[0])"
        }
    }
    return [pscustomobject] [ordered] @{ configuration = 'Release'; test_hooks = $true; generator = 'Ninja' }
}

function Get-QtShellCompilerIdentity {
    param([Parameter(Mandatory)] [string] $CompilerInfoText, [Parameter(Mandatory)] [string] $QtConfigText)
    if ($CompilerInfoText -notmatch '(?m)^set\(CMAKE_CXX_COMPILER_VERSION "(19\.43\.34808\.0)"\)\r?$') {
        throw 'Compiler drift requires review of the pinned package recipe.'
    }
    $shellMsvc = $Matches[1]
    if ($CompilerInfoText -notmatch '(?m)^set\(CMAKE_CXX_COMPILER "([^"]+)"\)\r?$') { throw 'Compiler identity is missing.' }
    $compiler = $Matches[1]
    if ($QtConfigText -notmatch '(?m)^QT_VERSION = 6\.11\.2\r?$' -or
        $QtConfigText -notmatch '(?m)^QT_MSVC_MAJOR_VERSION = 19\r?$' -or
        $QtConfigText -notmatch '(?m)^QT_MSVC_MINOR_VERSION = 44\r?$' -or
        $QtConfigText -notmatch '(?m)^QT_MSVC_PATCH_VERSION = 35227\r?$') {
        throw 'Qt build compiler/version drift requires review of the package recipe.'
    }
    return [pscustomobject] [ordered] @{ shell_msvc = $shellMsvc; qt_build_msvc = '19.44.35227'; compiler = $compiler }
}

function Assert-QtShellRefreshObject($Value, [string[]] $Fields, [string] $Label) {
    if ($Value -isnot [pscustomobject]) { throw "Refresh contract $Label must be an object." }
    $names = @($Value.PSObject.Properties.Name)
    if ($names.Count -ne $Fields.Count -or @($names | Where-Object { $Fields -cnotcontains $_ }).Count -ne 0) {
        throw "Refresh contract $Label has missing or unknown fields."
    }
}

function Get-QtShellRefreshPins($Contract) {
    Assert-QtShellRefreshObject $Contract @(
        'schema_version', 'refresh_id', 'historical_contract_sha256', 'installation_root',
        'toolset_version', 'qt_version', 'qt_build_msvc', 'provisioning', 'tools'
    ) 'root'
    if (($Contract.schema_version -isnot [int] -and $Contract.schema_version -isnot [long]) -or
        $Contract.schema_version -ne 1 -or $Contract.refresh_id -cne 'm0.2-qt-msvc-refresh-v1') {
        throw 'Unsupported refresh contract schema or identity.'
    }
    if ($Contract.historical_contract_sha256 -cne 'ac00a56584f2aed24732d44f9b26107084d37a73467fc56a860feeb850181952') {
        throw 'The refresh contract must preserve the historical baseline.'
    }
    if ($Contract.qt_version -cne '6.11.2' -or $Contract.qt_build_msvc -cne '19.44.35227') {
        throw 'The refresh contract cannot change the Qt baseline.'
    }
    if ($Contract.installation_root -isnot [string] -or -not [IO.Path]::IsPathFullyQualified($Contract.installation_root)) {
        throw 'The refresh installation root must be an absolute local path.'
    }
    Assert-NormalWindowsPath $Contract.installation_root
    if ($Contract.toolset_version -isnot [string] -or $Contract.toolset_version -cnotmatch '^14\.44\.[0-9]+$') {
        throw 'The refresh contract requires the MSVC 14.44 toolset family.'
    }
    Assert-QtShellRefreshObject $Contract.provisioning @('bootstrapper_version', 'bootstrapper_sha256', 'component_id') 'provisioning'
    if ($Contract.provisioning.bootstrapper_version -isnot [string] -or
        $Contract.provisioning.bootstrapper_version -cnotmatch '^18\.[0-9]+\.[0-9]+\.[0-9]+$' -or
        $Contract.provisioning.bootstrapper_sha256 -isnot [string] -or
        $Contract.provisioning.bootstrapper_sha256 -cnotmatch '^[a-f0-9]{64}$' -or
        $Contract.provisioning.component_id -cne 'Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64') {
        throw 'The refresh contract has invalid MSVC provisioning provenance.'
    }
    if ($Contract.tools -isnot [array] -or $Contract.tools.Count -ne 3) { throw 'The refresh contract requires exactly three tools.' }
    $pins = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
    foreach ($tool in $Contract.tools) {
        Assert-QtShellRefreshObject $tool @(
            'name', 'relative_path', 'size_bytes', 'sha256', 'file_version', 'product_version', 'signer_subject', 'signer_thumbprint'
        ) 'tool'
        if (@('cl', 'link', 'dumpbin') -cnotcontains $tool.name -or -not $pins.TryAdd($tool.name, $tool)) {
            throw 'The refresh contract has an unknown or duplicate tool.'
        }
        $expectedRelative = "VC/Tools/MSVC/$($Contract.toolset_version)/bin/Hostx64/x64/$($tool.name).exe"
        if ($tool.relative_path -cne $expectedRelative) { throw "The refresh tool path is not in its pinned Hostx64/x64 directory: $($tool.name)" }
        if (($tool.size_bytes -isnot [int] -and $tool.size_bytes -isnot [long]) -or $tool.size_bytes -le 0 -or
            $tool.sha256 -isnot [string] -or $tool.sha256 -cnotmatch '^[a-f0-9]{64}$') {
            throw 'The refresh contract tool hash or size is invalid.'
        }
        if ($tool.signer_subject -isnot [string] -or $tool.signer_subject -cnotmatch '^CN=Microsoft Corporation(?:,|$)' -or
            $tool.signer_thumbprint -isnot [string] -or $tool.signer_thumbprint -cnotmatch '^[A-F0-9]{40}$') {
            throw 'The refresh contract requires a pinned Microsoft tool signer.'
        }
    }
    if ($pins['cl'].file_version -isnot [string] -or $pins['cl'].file_version -cnotmatch '^19\.44\.[0-9]+\.[0-9]+$') {
        throw 'The refresh compiler must belong to the MSVC 14.44 family.'
    }
    $productVersion = '14.' + $pins['cl'].file_version.Substring(3)
    foreach ($name in @('cl', 'link', 'dumpbin')) {
        $tool = $pins[$name]
        $expectedVersion = if ($name -ceq 'cl') { $pins['cl'].file_version } else { $productVersion }
        if ($tool.file_version -cne $expectedVersion -or $tool.product_version -cne $productVersion) {
            throw 'The refresh contract compiler, linker and dumpbin versions must be coherent.'
        }
    }
    if ([version] $productVersion -lt [version] '14.44.35227.0') {
        throw 'The pinned linker is older than the Qt 19.44.35227 build compiler.'
    }
    return $pins
}

function Get-QtShellRefreshTextValue([string] $Text, [string] $Name, [string] $Kind) {
    $escaped = [regex]::Escape($Name)
    if ($Kind -ceq 'CMake') {
        $mentions = [regex]::Matches($Text, '(?m)^[\t ]*set\(' + $escaped + '(?:[\t ]|\))')
        $matches = [regex]::Matches($Text, '(?m)^set\(' + $escaped + ' (?:"([^"\r\n]*)"|([^\s()"]+))\)\r?$')
    }
    elseif ($Kind -ceq 'cache') {
        $mentions = [regex]::Matches($Text, '(?m)^[\t ]*' + $escaped + ':')
        $matches = [regex]::Matches($Text, '(?m)^' + $escaped + ':[^=\r\n]+=([^\r\n]*)\r?$')
    }
    else {
        $mentions = [regex]::Matches($Text, '(?m)^[\t ]*' + $escaped + '[\t ]*=')
        $matches = [regex]::Matches($Text, '(?m)^' + $escaped + ' = ([^\r\n]*)\r?$')
    }
    if ($mentions.Count -ne 1 -or $matches.Count -ne 1) { throw "$Kind $Name must occur exactly once in its expected form." }
    if ($Kind -ceq 'CMake' -and $matches[0].Groups[2].Success) { return $matches[0].Groups[2].Value }
    return $matches[0].Groups[1].Value
}

function Assert-QtShellRefreshPath([string] $Actual, [string] $Expected, [string] $Label) {
    Assert-NormalWindowsPath $Actual
    if (-not [IO.Path]::IsPathFullyQualified($Actual) -or
        -not [IO.Path]::GetFullPath($Actual).Equals([IO.Path]::GetFullPath($Expected), [StringComparison]::OrdinalIgnoreCase)) {
        throw "The $Label path differs from the refresh contract."
    }
}

function Get-QtShellMsvcRefreshIdentity {
    param(
        [Parameter(Mandatory)] $Contract,
        [Parameter(Mandatory)] [string] $CompilerInfoText,
        [Parameter(Mandatory)] [string] $CacheText,
        [Parameter(Mandatory)] [string] $QtConfigText,
        [Parameter(Mandatory)] [object[]] $ToolEvidence
    )
    $pins = Get-QtShellRefreshPins $Contract
    if ((Get-QtShellRefreshTextValue $CompilerInfoText 'CMAKE_CXX_COMPILER_VERSION' 'CMake') -cne $pins['cl'].file_version) {
        throw 'The CMake compiler version differs from the pinned MSVC refresh.'
    }
    if ((Get-QtShellRefreshTextValue $CompilerInfoText 'CMAKE_CXX_COMPILER_LINKER_VERSION' 'CMake') -cne $pins['link'].file_version) {
        throw 'The CMake linker version differs from the pinned MSVC refresh.'
    }
    foreach ($key in @('CMAKE_CXX_COMPILER_ID', 'CMAKE_CXX_COMPILER_LINKER_ID')) {
        if ((Get-QtShellRefreshTextValue $CompilerInfoText $key 'CMake') -cne 'MSVC') { throw "The refresh requires MSVC: $key" }
    }
    if ((Get-QtShellRefreshTextValue $CompilerInfoText 'CMAKE_CXX_COMPILER_ARCHITECTURE_ID' 'CMake') -cne 'x64') {
        throw 'The refresh requires an x64 compiler.'
    }
    $paths = @{}
    foreach ($name in @('cl', 'link', 'dumpbin')) {
        $paths[$name] = [IO.Path]::GetFullPath((Join-Path $Contract.installation_root $pins[$name].relative_path))
    }
    foreach ($pair in @(@('CMAKE_CXX_COMPILER', 'cl', 'compiler'), @('CMAKE_LINKER', 'link', 'linker'))) {
        Assert-QtShellRefreshPath (Get-QtShellRefreshTextValue $CompilerInfoText $pair[0] 'CMake') $paths[$pair[1]] $pair[2]
        Assert-QtShellRefreshPath (Get-QtShellRefreshTextValue $CacheText $pair[0] 'cache') $paths[$pair[1]] $pair[2]
    }
    # CMake's compiler-linker probe may report an 8.3 spelling. Its version is
    # checked above; the actual CMAKE_LINKER path must match in both build files.
    foreach ($pair in @(@('QT_ARCH', 'x86_64'), @('QT_VERSION', '6.11.2'), @('QT_MSVC_MAJOR_VERSION', '19'),
            @('QT_MSVC_MINOR_VERSION', '44'), @('QT_MSVC_PATCH_VERSION', '35227'))) {
        if ((Get-QtShellRefreshTextValue $QtConfigText $pair[0] 'Qt') -cne $pair[1]) { throw "Qt baseline drift: $($pair[0])" }
    }
    if ($ToolEvidence.Count -ne 3) { throw 'Exactly three tool observations are required.' }
    $observed = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
    foreach ($tool in $ToolEvidence) {
        Assert-QtShellRefreshObject $tool @(
            'name', 'path', 'size_bytes', 'sha256', 'file_version', 'product_version',
            'signature_status', 'signer_subject', 'signer_thumbprint'
        ) 'tool observation'
        if (-not $pins.ContainsKey($tool.name) -or -not $observed.TryAdd($tool.name, $tool)) {
            throw 'Unknown or duplicate tool observation.'
        }
        $pin = $pins[$tool.name]
        Assert-QtShellRefreshPath $tool.path $paths[$tool.name] "tool $($tool.name)"
        if (($tool.size_bytes -isnot [int] -and $tool.size_bytes -isnot [long]) -or $tool.size_bytes -ne $pin.size_bytes) {
            throw "Tool size differs from its pin: $($tool.name)"
        }
        foreach ($field in @('sha256', 'file_version', 'product_version', 'signer_subject', 'signer_thumbprint')) {
            if ($tool.$field -isnot [string] -or $tool.$field -cne $pin.$field) { throw "Tool identity differs from its pin: $($tool.name).$field" }
        }
        if ($tool.signature_status -cne 'Valid') { throw "Tool signature is not valid: $($tool.name)" }
    }
    return [pscustomobject] [ordered] @{
        shell_msvc = $pins['cl'].file_version; linker_msvc = $pins['link'].file_version
        qt_version = $Contract.qt_version; qt_build_msvc = $Contract.qt_build_msvc
        toolset_version = $Contract.toolset_version; architecture = 'x64'
        compiler = $paths['cl']; linker = $paths['link']; dumpbin = $paths['dumpbin']
        tools = @('cl', 'link', 'dumpbin' | ForEach-Object { $observed[$_] })
    }
}

function Read-QtShellRefreshText([string] $Path) {
    Assert-NormalWindowsPath $Path
    if (-not [IO.Path]::IsPathFullyQualified($Path)) { throw 'Refresh evidence needs an absolute path.' }
    Assert-NoReparseAncestor $Path
    $bytes = [IO.File]::ReadAllBytes($Path)
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { $digest = [BitConverter]::ToString($hasher.ComputeHash($bytes)).Replace('-', '').ToLowerInvariant() }
    finally { $hasher.Dispose() }
    return [pscustomobject] @{
        text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes).TrimStart([char]0xfeff)
        sha256 = $digest
    }
}

function New-QtShellMsvcRefreshReceipt {
    param(
        [Parameter(Mandatory)] [string] $ContractPath,
        [Parameter(Mandatory)] [string] $ExpectedContractSha256,
        [Parameter(Mandatory)] [string] $CompilerInfoPath,
        [Parameter(Mandatory)] [string] $CachePath,
        [Parameter(Mandatory)] [string] $QtConfigPath
    )
    $contractFile = Read-QtShellRefreshText $ContractPath
    if ($ExpectedContractSha256 -cnotmatch '^[a-f0-9]{64}$' -or $contractFile.sha256 -cne $ExpectedContractSha256) {
        throw 'The MSVC refresh contract hash is not authenticated.'
    }
    $contract = $contractFile.text | ConvertFrom-Json -Depth 12
    $pins = Get-QtShellRefreshPins $contract
    $compilerInfo = Read-QtShellRefreshText $CompilerInfoPath
    $cache = Read-QtShellRefreshText $CachePath
    $qtConfig = Read-QtShellRefreshText $QtConfigPath
    $tools = @('cl', 'link', 'dumpbin' | ForEach-Object {
        $pin = $pins[$_]
        $path = Resolve-QtShellPackageChildPath $contract.installation_root $pin.relative_path
        $item = Get-Item -LiteralPath $path -Force
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($item.PSIsContainer -or $item.Length -ne $pin.size_bytes -or $hash -cne $pin.sha256) {
            throw "Tool bytes do not match the refresh pin: $($pin.name)"
        }
        $signature = Get-AuthenticodeSignature -LiteralPath $path
        [pscustomobject] [ordered] @{
            name = $pin.name; path = $path; size_bytes = $item.Length; sha256 = $hash
            file_version = [string] $item.VersionInfo.FileVersion; product_version = [string] $item.VersionInfo.ProductVersion
            signature_status = [string] $signature.Status
            signer_subject = if ($null -eq $signature.SignerCertificate) { '' } else { $signature.SignerCertificate.Subject }
            signer_thumbprint = if ($null -eq $signature.SignerCertificate) { '' } else { $signature.SignerCertificate.Thumbprint }
        }
    })
    $identity = Get-QtShellMsvcRefreshIdentity -Contract $contract -CompilerInfoText $compilerInfo.text -CacheText $cache.text -QtConfigText $qtConfig.text -ToolEvidence $tools
    return [pscustomobject] [ordered] @{
        schema_version = 1; refresh_id = $contract.refresh_id
        historical_contract_sha256 = $contract.historical_contract_sha256
        refresh_contract_sha256 = $contractFile.sha256
        compiler_info_sha256 = $compilerInfo.sha256; cmake_cache_sha256 = $cache.sha256; qt_config_sha256 = $qtConfig.sha256
        identity = $identity
        limitation = 'Authenticates quiescent files and CMake identities, not a concurrent-writer, compile/link, runtime or redistribution proof.'
    }
}

function Invoke-QtShellRefreshedBuild {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ContractPath,
        [Parameter(Mandatory)] [string] $ExpectedContractSha256,
        [Parameter(Mandatory)] [string] $CompilerInfoPath,
        [Parameter(Mandatory)] [string] $CachePath,
        [Parameter(Mandatory)] [string] $QtConfigPath,
        [Parameter(Mandatory)] [string] $SourceRoot,
        [Parameter(Mandatory)] [string] $QtRoot,
        [Parameter(Mandatory)] [scriptblock] $BuildAction
    )
    $receiptArguments = @{
        ContractPath = $ContractPath; ExpectedContractSha256 = $ExpectedContractSha256
        CompilerInfoPath = $CompilerInfoPath; CachePath = $CachePath; QtConfigPath = $QtConfigPath
    }
    $null = Test-QtShellReleaseCache -CacheText (Read-QtShellRefreshText $CachePath).text -SourceRoot $SourceRoot -QtRoot $QtRoot
    $before = New-QtShellMsvcRefreshReceipt @receiptArguments
    $buildResult = & $BuildAction $before.identity
    if ($buildResult.exit_code -ne 0) {
        $failure = [InvalidOperationException]::new("Refreshed build failed (exit_code=$($buildResult.exit_code)).`n$($buildResult.stdout)`n$($buildResult.stderr)")
        $errorRecord = [Management.Automation.ErrorRecord]::new($failure, 'QtShellRefreshedBuildFailed', [Management.Automation.ErrorCategory]::InvalidResult, $buildResult)
        $PSCmdlet.ThrowTerminatingError($errorRecord)
    }
    $configuration = Test-QtShellReleaseCache -CacheText (Read-QtShellRefreshText $CachePath).text -SourceRoot $SourceRoot -QtRoot $QtRoot
    $receipt = New-QtShellMsvcRefreshReceipt @receiptArguments
    return [pscustomobject] [ordered] @{ configuration = $configuration; receipt = $receipt; build_result = $buildResult }
}

function ConvertFrom-QtShellPeReport {
    param([Parameter(Mandatory)] [string] $Report)
    if ($Report -notmatch '(?m)^\s*8664 machine \(x64\)\s*$') { throw 'PE report does not identify x64.' }
    $imports = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($match in [regex]::Matches($Report, '(?m)^[\t ]+([A-Za-z0-9_.+-]+\.dll)[\t ]*\r?$')) {
        $null = $imports.Add($match.Groups[1].Value)
    }
    if ($imports.Count -eq 0) { throw 'PE report contains no imports.' }
    [string[]] $orderedImports = @($imports)
    [Array]::Sort($orderedImports, [StringComparer]::Ordinal)
    return [pscustomobject] [ordered] @{ machine = 'x64'; imports = $orderedImports }
}

function Assert-QtShellRuntimeFiles {
    param([Parameter(Mandatory)] [string[]] $RelativePaths)
    $required = @(
        'm0_qt_shell.exe', 'qt.conf', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll',
        'Qt6Qml.dll', 'Qt6Quick.dll', 'Qt6QuickControls2.dll', 'Qt6QuickLayouts.dll', 'Qt6Svg.dll',
        'plugins/platforms/qwindows.dll', 'plugins/platforms/qoffscreen.dll',
        'plugins/iconengines/qsvgicon.dll', 'plugins/imageformats/qsvg.dll',
        'qml/QtQuick/qmldir', 'qml/QtQuick/Controls/qmldir', 'qml/QtQuick/Controls/Basic/qmldir',
        'qml/QtQuick/Templates/qmldir', 'qml/QtQuick/Layouts/qmldir'
    )
    foreach ($file in $required) {
        if ($RelativePaths -cnotcontains $file) { throw "Required runtime file is missing: $file" }
    }
    foreach ($path in $RelativePaths) {
        $name = [IO.Path]::GetFileName($path)
        if (($name -match '\.exe$' -and $path -cne 'm0_qt_shell.exe') -or
            $name -match '^(?:Qt6(?:Test|QuickTest|QuickControlsTestUtils|QmlDebug)|vcruntime|msvcp|concrt|vccorlib)' -or
            $name -match '^Qt6.*d\.dll$' -or $path -match '(?:^|/)qmltooling/' -or
            $name -match '\.(?:mp3|mp4|m4a|aac|flac|wav|ogg|opus|wma|pdb|lib|obj|exp)$') {
            throw "Forbidden local evaluation payload: $path"
        }
    }
}

Export-ModuleMember -Function Resolve-QtShellPackageChildPath, New-QtShellPackageDirectory, ConvertTo-QtShellDeploymentPlan, Test-QtShellReleaseCache, Get-QtShellCompilerIdentity, Get-QtShellMsvcRefreshIdentity, New-QtShellMsvcRefreshReceipt, Invoke-QtShellRefreshedBuild, ConvertFrom-QtShellPeReport, Assert-QtShellRuntimeFiles
