[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $SourceRoot,
    [Parameter(Mandatory)] [string] $PythonRoot,
    [Parameter(Mandatory)] [string] $OutputDirectory,
    [Parameter(Mandatory)] [string] $ToolchainRoot,
    [Parameter(Mandatory)] [string] $WindowsSdkRoot,
    [Parameter(Mandatory)] [ValidatePattern('\A[0-9]+(\.[0-9]+){3}\z')] [string] $WindowsSdkVersion
)

$ErrorActionPreference = 'Stop'

# Use the Python 3.14.10 sources with the CPython Expat backport 79c504454a52990bfa02374fdb625fdce32844b0.
$source = (Resolve-Path -LiteralPath $SourceRoot).ProviderPath
$runtime = (Resolve-Path -LiteralPath $PythonRoot).ProviderPath
$toolchain = (Resolve-Path -LiteralPath $ToolchainRoot).ProviderPath
$sdk = (Resolve-Path -LiteralPath $WindowsSdkRoot).ProviderPath
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)

$cl = Join-Path $toolchain 'bin/Hostx64/x64/cl.exe'
$link = Join-Path $toolchain 'bin/Hostx64/x64/link.exe'
$rc = Join-Path $sdk "bin/$WindowsSdkVersion/x64/rc.exe"
$pythonImportLib = Join-Path $runtime 'libs/python314.lib'
$expectedImportLibHash = '7ED7F519BB3C0CB542F52EEEE1CD4278513977D96DEEAEAF877F9EFA9D32DCDF'

foreach ($tool in @($cl, $link, $rc)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Required x64 build tool is missing at $tool"
    }
}
if ((Get-FileHash -LiteralPath $pythonImportLib -Algorithm SHA256).Hash -ne $expectedImportLibHash) {
    throw 'The python314.lib SHA-256 does not match the official Windows x64 CPython 3.14.10 runtime.'
}
if (-not (Select-String -Quiet -LiteralPath (Join-Path $source 'Include/patchlevel.h') -Pattern '^#define PY_VERSION\s+"3\.14\.10"\s*$')) {
    throw 'Expected the CPython 3.14.10 source release.'
}
$expatHeader = Join-Path $source 'Modules/expat/expat.h'
foreach ($versionDefine in @('MAJOR_VERSION\s+2', 'MINOR_VERSION\s+10', 'MICRO_VERSION\s+0')) {
    if (-not (Select-String -Quiet -LiteralPath $expatHeader -Pattern "^#\s*define XML_$versionDefine\s*$")) {
        throw 'Expected the Expat 2.10.0 source backport.'
    }
}
if (Test-Path -LiteralPath $output) {
    throw "Output directory already exists at $output"
}

$expatFiles = @('xcs.c', 'xmlparse.c', 'xmlrole.c', 'xmltok.c')
$compilerIncludes = @(
    (Join-Path $source 'Modules/expat'),
    (Join-Path $source 'Include'),
    (Join-Path $source 'Include/internal'),
    (Join-Path $source 'Include/internal/mimalloc'),
    (Join-Path $source 'PC'),
    (Join-Path $toolchain 'include'),
    (Join-Path $sdk "Include/$WindowsSdkVersion/ucrt"),
    (Join-Path $sdk "Include/$WindowsSdkVersion/shared"),
    (Join-Path $sdk "Include/$WindowsSdkVersion/um")
) | ForEach-Object { "/I$_" }
$linkerPaths = @(
    (Join-Path $toolchain 'lib/x64'),
    (Join-Path $sdk "Lib/$WindowsSdkVersion/ucrt/x64"),
    (Join-Path $sdk "Lib/$WindowsSdkVersion/um/x64")
) | ForEach-Object { "/LIBPATH:$_" }
$resourceText = Get-Content -Raw -LiteralPath (Join-Path $source 'PC/python_nt.rc')

New-Item -ItemType Directory -Path $output | Out-Null
foreach ($module in @('pyexpat', '_elementtree')) {
    $buildDirectory = Join-Path $output $module
    New-Item -ItemType Directory -Path $buildDirectory | Out-Null
    Push-Location -LiteralPath $buildDirectory
    try {
        $sources = @((Join-Path $source "Modules/$module.c")) + @(
            $expatFiles | ForEach-Object { Join-Path $source "Modules/expat/$_" }
        )
        $defines = @('/D_CRT_SECURE_NO_WARNINGS', '/DXML_STATIC', '/DWIN32', '/DNDEBUG', '/DPyStats', '/D_WINDLL')
        if ($module -eq 'pyexpat') { $defines += '/DPYEXPAT_EXPORTS' }
        & $cl /nologo /c /TC /MD /MP /O2 /Oi /GL /GF /GS /guard:cf /Gy /W3 /utf-8 /Z7 @defines @compilerIncludes @sources
        if ($LASTEXITCODE -ne 0) { throw "cl.exe failed for $module" }

        $resourceScript = Join-Path $buildDirectory 'python_nt.rc'
        [IO.File]::WriteAllText($resourceScript, "#define ORIGINAL_FILENAME `"$module.pyd`"`r`n$resourceText")
        # PCbuild uses 10150 for the 3.14.10 final release version field.
        & $rc /nologo /l0x0409 /DFIELD3=10150 /DNDEBUG @compilerIncludes /fopython_nt.res $resourceScript
        if ($LASTEXITCODE -ne 0) { throw "rc.exe failed for $module" }

        $objects = @("$module.obj") + @($expatFiles | ForEach-Object { [IO.Path]::ChangeExtension($_, '.obj') })
        $moduleOutput = Join-Path $output "$module.pyd"
        & $link /NOLOGO /DLL /MACHINE:X64 /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT /GUARD:CF /MANIFEST:NO /LTCG /INCREMENTAL:NO /OPT:REF,NOICF /Brepro "/OUT:$moduleOutput" @linkerPaths @objects python_nt.res $pythonImportLib kernel32.lib
        if ($LASTEXITCODE -ne 0) { throw "link.exe failed for $module" }
    } finally {
        Pop-Location
    }
}

Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $output 'pyexpat.pyd'), (Join-Path $output '_elementtree.pyd')
