#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw 'Windows x64 PowerShell is required.' }
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$testRoot = Join-Path $repository ('out/product/runtime-recipe-' + [guid]::NewGuid().ToString('N'))
$sourceNames = @('New-RcMusicLocalRuntime.ps1', 'RcMusicRuntimePlan.psm1', 'PeDependencyReport.psm1',
    'qt.conf', 'LOCAL-DEVELOPMENT.txt')
$ffmpegRecipeNames = @('RcMusicFfmpegRuntime.psm1', 'ffmpeg-runtime.v1.json')
$networkRecipeNames = @('RcMusicQtNetworkRuntime.psm1', 'qt-network-runtime.v1.json')
$noticeNames = @('notices/ffmpeg/NOTICE.txt', 'notices/ffmpeg/FFmpeg-COPYING.LGPLv2.1.txt',
    'notices/ffmpeg/FFmpeg-LICENSE.txt', 'notices/ffmpeg/FFmpeg-CREDITS.txt',
    'notices/ffmpeg/FFmpeg-IJG.txt', 'notices/ffmpeg/Boost-LICENSE.txt', 'notices/ffmpeg/zlib-LICENSE.txt',
    'notices/qt-multimedia/NOTICE.txt', 'notices/qt-multimedia/Signalsmith-Stretch-LICENSE.txt',
    'notices/qt-multimedia/Signalsmith-DSP-LICENSE.txt', 'notices/qt-multimedia/dr-libs-LICENSE.txt',
    'notices/qt-multimedia/dr-wav-NOTICE.txt', 'notices/qt-multimedia/TLSF-LICENSE.txt',
    'notices/taglib/NOTICE.txt', 'notices/taglib/TagLib-AUTHORS.txt',
    'notices/taglib/TagLib-COPYING.LGPL.txt', 'notices/taglib/TagLib-COPYING.MPL.txt',
    'notices/taglib/utfcpp-LICENSE.txt')
$requiredRecipeNames = $sourceNames + $ffmpegRecipeNames + $networkRecipeNames + $noticeNames
$multimediaStagerName = 'New-RcMusicQtMultimediaSdk.ps1'
$networkStagerName = 'New-RcMusicQtNetworkSdk.ps1'
$ffmpegNames = @('avcodec-61.dll', 'avformat-61.dll', 'avutil-59.dll', 'swresample-5.dll', 'swscale-8.dll')
$componentNames = @('RcMusicDownloadComponent.psm1', 'RcMusicDownloadComponentPe.psm1')
$nativeNames = @('RcMusicNativeHostComponent.psm1', 'RcMusicYoutubeNativeManifest.psm1')
$state = @{ Passed = 0; Failed = 0 }
function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Assert-Equal($Actual, $Expected) {
    if ($Actual -cne $Expected) { throw "Expected '$Expected', found '$Actual'." }
}
function Assert-Rejected([scriptblock] $Operation, [string] $Pattern) {
    try { $null = & $Operation }
    catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw "Wrong rejection: $($_.Exception.Message)" }
        return
    }
    throw 'Expected the operation to be rejected.'
}
function Assert-SafeTestRoot {
    $parent = [IO.Path]::GetFullPath((Join-Path $repository 'out/product')).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $resolved = [IO.Path]::GetFullPath($testRoot)
    if (-not $resolved.StartsWith($parent, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -cnotmatch '\Aruntime-recipe-[0-9a-f]{32}\z') { throw 'Unsafe test root.' }
    $ancestor = $resolved
    while ($ancestor) {
        try { $attributes = [IO.File]::GetAttributes($ancestor) }
        catch [IO.FileNotFoundException] { $attributes = 0 }
        catch [IO.DirectoryNotFoundException] { $attributes = 0 }
        if ($attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unsafe reparse ancestor.' }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
}
function Read-Ast([string] $Path) {
    $parseErrors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile($Path, [ref]$null, [ref]$parseErrors)
    if ($parseErrors.Count -ne 0) { throw "Invalid PowerShell: $Path" }
    return $ast
}
function Read-RecipeFiles([string] $Edition = 'Lite', [switch] $DownloadComponent, [switch] $NativeHostComponent) {
    $ast = Read-Ast $exportedRecipe
    $functions = @($ast.FindAll({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Get-RuntimeRecipeFiles'
    }, $true))
    Assert-Equal $functions.Count 1
    # Execute only this inventory function, never the script body. The AST keeps
    # the exported file identity so PSScriptRoot resolves beside the copies.
    return @(& $functions[0].Body.GetScriptBlock() -Edition $Edition -DownloadComponent:$DownloadComponent `
        -NativeHostComponent:$NativeHostComponent)
}
function Invoke-RecipeChild([string[]] $Arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new((Get-Process -Id $PID).Path)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.WorkingDirectory = $foreignDirectory
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in @('-NoProfile', '-NonInteractive', '-File', $exportedRecipe) + $Arguments) {
        $start.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $started = $false
    try {
        $started = $process.Start()
        if (-not $started) { throw 'PowerShell recipe child could not start.' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(15000)) { throw 'PowerShell recipe child exceeded 15 seconds.' }
        return [pscustomobject]@{ ExitCode = $process.ExitCode
            Stdout = $stdout.GetAwaiter().GetResult(); Stderr = $stderr.GetAwaiter().GetResult() }
    } finally {
        try {
            if ($started -and -not $process.HasExited) {
                $process.Kill($true)
                if (-not $process.WaitForExit(5000)) { throw 'Owned PowerShell child survived cleanup.' }
            }
        } finally { $process.Dispose() }
    }
}
function Get-TestFilePin([string] $Path) {
    [ordered]@{ size_bytes = (Get-Item -LiteralPath $Path).Length
        sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
}
function Write-TestFfmpegManifest($Manifest) {
    [IO.File]::WriteAllText($ffmpegManifest, ($Manifest | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
}
function Read-TestFfmpegRuntime {
    $module = Join-Path $exportDirectory $ffmpegRecipeNames[0]
    if (-not [IO.File]::Exists($module)) { throw 'Required validator module is absent.' }
    Import-Module $module -Force
    Get-RcMusicFfmpegRuntime -ManifestPath $ffmpegManifest -DllDirectory $ffmpegBin `
        -PluginPath $ffmpegPlugin -Destination (Join-Path $testRoot 'unused-ffmpeg-runtime')
}
Assert-SafeTestRoot
if (Test-Path -LiteralPath $testRoot) { throw 'Test root already exists.' }
$null = New-Item -ItemType Directory -Path $testRoot
try {
    $exportRoot = Join-Path $testRoot 'source exportado 日本'
    $exportDirectory = Join-Path $exportRoot 'packaging/windows'
    $buildDirectory = Join-Path $exportRoot 'out/product/build inválido'
    $foreignDirectory = Join-Path $testRoot 'cwd alheio'
    $null = New-Item -ItemType Directory -Path $exportDirectory, $buildDirectory, $foreignDirectory
    $exportedInputs = $ffmpegRecipeNames + $networkRecipeNames + $noticeNames + $multimediaStagerName +
        $networkStagerName + $componentNames + $nativeNames
    foreach ($name in $sourceNames + @($exportedInputs | Where-Object {
        Test-Path -LiteralPath (Join-Path $repository ('packaging/windows/' + $_))
    })) {
        $original = Join-Path $repository ('packaging/windows/' + $name)
        $copy = Join-Path $exportDirectory $name
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($copy))
        Copy-Item -LiteralPath $original -Destination $copy
        Assert-Equal ((Get-FileHash -LiteralPath $copy).Hash) ((Get-FileHash -LiteralPath $original).Hash)
    }
    Copy-Item -LiteralPath (Join-Path $repository 'packaging/windows/WEBVIEW2-DEVELOPMENT.txt') -Destination $exportDirectory
    $exportedRecipe = Join-Path $exportDirectory 'New-RcMusicLocalRuntime.ps1'
    $cache = "CMAKE_BUILD_TYPE:STRING=Debug`nBUILD_TESTING:BOOL=OFF`nCMAKE_HOME_DIRECTORY:INTERNAL=$exportRoot`nRC_MUSIC_BUILD_DESKTOP:BOOL=ON`n"
    [IO.File]::WriteAllText((Join-Path $buildDirectory 'CMakeCache.txt'), $cache, [Text.UTF8Encoding]::new($false))
    Test-Case 'exported PowerShell files parse without historical files' {
        Assert-Equal (Test-Path -LiteralPath (Join-Path $exportRoot 'experiments')) $false
        foreach ($name in ($sourceNames + $ffmpegRecipeNames + $networkRecipeNames) | Where-Object {
            $_ -match '\.ps(?:1|m1)$' -and (Test-Path -LiteralPath (Join-Path $exportDirectory $_))
        }) {
            $null = Read-Ast (Join-Path $exportDirectory $name)
        }
    }
    Test-Case 'the real exported recipe rejects Debug from a foreign cwd before tools or output creation' {
        $result = Invoke-RecipeChild @('-BuildDirectory', $buildDirectory, '-OutputDirectory', 'out/product/rejected-runtime')
        Assert-Equal $result.ExitCode 1
        Assert-Equal $result.Stdout ''
        if ($result.Stderr -notmatch 'Unsupported desktop build configuration:\s*CMAKE_BUILD_TYPE') {
            throw "Wrong preflight rejection: $($result.Stderr)"
        }
        Assert-Equal (Test-Path -LiteralPath (Join-Path $exportRoot 'out/product/rejected-runtime')) $false
        Assert-Equal (Test-Path -LiteralPath (Join-Path $foreignDirectory 'out')) $false
    }
    Test-Case 'the product requires an explicit build directory without starting a build' {
        $result = Invoke-RecipeChild @('-OutputDirectory', 'out/product/missing-build')
        Assert-Equal $result.ExitCode 1
        if ($result.Stderr -notmatch 'BuildDirectory') { throw 'Missing build parameter was not identified.' }
        Assert-Equal (Test-Path -LiteralPath (Join-Path $exportRoot 'out/product/missing-build')) $false
    }
    foreach ($arguments in @(
        @('-DownloadPackageReceipt', 'C:/missing/receipt.json'),
        @('-DownloadPackageReceiptSha256', ('a' * 64)),
        @('-DownloadPackageReceipt', '', '-DownloadPackageReceiptSha256', ('a' * 64)),
        @('-DownloadPackageReceipt', 'C:/missing/receipt.json', '-DownloadPackageReceiptSha256', ('A' * 64))
    )) {
        Test-Case "partial or invalid download package inputs fail before build preflight: $($arguments -join ' ')" {
            $result = Invoke-RecipeChild (@('-BuildDirectory', $buildDirectory, '-OutputDirectory',
                'out/product/rejected-downloads') + $arguments)
            Assert-Equal $result.ExitCode 1
            Assert-Equal $result.Stdout ''
            if ($result.Stderr -notmatch 'Download package') { throw "Wrong download preflight: $($result.Stderr)" }
            Assert-Equal (Test-Path -LiteralPath (Join-Path $exportRoot 'out/product/rejected-downloads')) $false
        }
    }
    Test-Case 'optional recipe provenance includes both download component validators' {
        $files = @(Read-RecipeFiles -DownloadComponent)
        Assert-Equal $files.Count ($requiredRecipeNames.Count + 2)
        foreach ($name in $componentNames) {
            $entry = @($files | Where-Object path -CEQ ('packaging/windows/' + $name))
            Assert-Equal $entry.Count 1
            Assert-Equal $entry[0].sha256 ((Get-FileHash -LiteralPath (Join-Path $exportDirectory $name)).Hash.ToLowerInvariant())
        }
        Assert-Equal @(Read-RecipeFiles).Count $requiredRecipeNames.Count
        Assert-Equal @(Read-RecipeFiles -Edition CompleteExperimental -DownloadComponent).Count ($requiredRecipeNames.Count + 3)
    }
    foreach ($arguments in @(
        @('-MainNativeBuildReceipt', 'C:/missing/main.json'),
        @('-MainNativeBuildReceiptSha256', ('a' * 64)),
        @('-YoutubeNativeBuildReceipt', 'C:/missing/youtube.json'),
        @('-YoutubeNativeBuildReceiptSha256', ('b' * 64)),
        @('-MainNativeBuildReceipt', '', '-MainNativeBuildReceiptSha256', ('a' * 64),
            '-YoutubeNativeBuildReceipt', 'C:/missing/youtube.json', '-YoutubeNativeBuildReceiptSha256', ('b' * 64)),
        @('-MainNativeBuildReceipt', 'C:/missing/main.json', '-MainNativeBuildReceiptSha256', ('A' * 64),
            '-YoutubeNativeBuildReceipt', 'C:/missing/youtube.json', '-YoutubeNativeBuildReceiptSha256', ('b' * 64)),
        @('-MainNativeBuildReceipt', 'C:/missing/main.json', '-MainNativeBuildReceiptSha256', ('a' * 64),
            '-YoutubeNativeBuildReceipt', 'C:/missing/youtube.json', '-YoutubeNativeBuildReceiptSha256', ('b' * 64))
    )) {
        Test-Case "native hosts require both pinned receipts and download tools before any output: $($arguments -join ' ')" {
            $result = Invoke-RecipeChild (@('-BuildDirectory', $buildDirectory, '-OutputDirectory',
                'out/product/rejected-native') + $arguments)
            Assert-Equal $result.ExitCode 1
            Assert-Equal $result.Stdout ''
            if ($result.Stderr -notmatch 'Native host component') { throw "Wrong native preflight: $($result.Stderr)" }
            Assert-Equal (Test-Path -LiteralPath (Join-Path $exportRoot 'out/product/rejected-native')) $false
        }
    }
    Test-Case 'native recipe provenance binds its reader and component planner only for explicit opt-in' {
        $files = @(Read-RecipeFiles -DownloadComponent -NativeHostComponent)
        Assert-Equal $files.Count ($requiredRecipeNames.Count + 4)
        foreach ($name in $nativeNames) {
            $entry = @($files | Where-Object path -CEQ ('packaging/windows/' + $name))
            Assert-Equal $entry.Count 1
            Assert-Equal $entry[0].sha256 ((Get-FileHash -LiteralPath (Join-Path $exportDirectory $name)).Hash.ToLowerInvariant())
        }
        Assert-Equal @(Read-RecipeFiles -Edition CompleteExperimental -DownloadComponent -NativeHostComponent).Count ($requiredRecipeNames.Count + 5)
    }
    Test-Case 'the recipe binds the default files, qualified FFmpeg inputs and runtime notices' {
        $files = @(Read-RecipeFiles)
        Assert-Equal $files.Count $requiredRecipeNames.Count
        Assert-Equal (($files.path | Sort-Object -CaseSensitive) -join '|') (($requiredRecipeNames | ForEach-Object {
            'packaging/windows/' + $_
        } | Sort-Object -CaseSensitive) -join '|')
        foreach ($entry in $files) {
            if ($entry.sha256 -cnotmatch '\A[0-9a-f]{64}\z') { throw 'Invalid recipe SHA256.' }
            Assert-Equal $entry.sha256 ((Get-FileHash -LiteralPath (Join-Path $exportRoot $entry.path)).Hash.ToLowerInvariant())
        }
    }
    Test-Case 'Complete explicitly reaches its SDK preflight before any tool, output or Git operation' {
        $cacheFile = Join-Path $buildDirectory 'CMakeCache.txt'
        $previousCache = [IO.File]::ReadAllText($cacheFile)
        try {
            $completeCache = "CMAKE_BUILD_TYPE:STRING=Release`nBUILD_TESTING:BOOL=OFF`nCMAKE_HOME_DIRECTORY:INTERNAL=$exportRoot`nRC_MUSIC_BUILD_DESKTOP:BOOL=ON`nRC_MUSIC_BUILD_EMBEDDED_BROWSER:BOOL=ON`n"
            foreach ($option in @('BROWSER_INTEGRATION', 'DOWNLOADS', 'DOWNLOAD_NETWORK', 'DOWNLOAD_PERSISTENCE', 'NATIVE_MESSAGING')) {
                $completeCache += "RC_MUSIC_BUILD_$($option):BOOL=OFF`n"
            }
            [IO.File]::WriteAllText($cacheFile, $completeCache, [Text.UTF8Encoding]::new($false))
            $result = Invoke-RecipeChild @('-Edition', 'CompleteExperimental', '-BuildDirectory', $buildDirectory, '-OutputDirectory', 'out/product/rejected-complete')
            Assert-Equal $result.ExitCode 1
            Assert-Equal $result.Stdout ''
            if ($result.Stderr -notmatch 'RC_MUSIC_WEBVIEW2_SDK_ROOT') { throw "Wrong Complete preflight: $($result.Stderr)" }
            Assert-Equal (Test-Path -LiteralPath (Join-Path $exportRoot 'out/product/rejected-complete')) $false
        } finally { [IO.File]::WriteAllText($cacheFile, $previousCache, [Text.UTF8Encoding]::new($false)) }
    }
    Test-Case 'Complete recipe provenance additionally binds its own exported prerequisite notice' {
        $files = @(Read-RecipeFiles -Edition CompleteExperimental)
        Assert-Equal $files.Count ($requiredRecipeNames.Count + 1)
        $extra = @($files | Where-Object path -CEQ 'packaging/windows/WEBVIEW2-DEVELOPMENT.txt')
        Assert-Equal $extra.Count 1
        Assert-Equal $extra[0].sha256 ((Get-FileHash -LiteralPath (Join-Path $exportDirectory 'WEBVIEW2-DEVELOPMENT.txt')).Hash.ToLowerInvariant())
        Assert-Equal @(Read-RecipeFiles -Edition Lite).Count $requiredRecipeNames.Count
    }
    Test-Case 'changing only the exported parser changes only that recorded hash' {
        $before = @(Read-RecipeFiles)
        $parserCopy = Join-Path $exportDirectory 'PeDependencyReport.psm1'
        $parserOriginal = Join-Path $repository 'packaging/windows/PeDependencyReport.psm1'
        $originalHash = (Get-FileHash -LiteralPath $parserOriginal).Hash
        [IO.File]::AppendAllText($parserCopy, "`n# Synthetic exported-copy mutation.`n", [Text.UTF8Encoding]::new($false))
        $after = @(Read-RecipeFiles)
        $changed = @($after | Where-Object {
            $entry = $_
            $previous = @($before | Where-Object { $_.path -ceq $entry.path })
            $previous.Count -ne 1 -or $previous[0].sha256 -cne $entry.sha256
        })
        Assert-Equal $changed.Count 1
        Assert-Equal $changed[0].path 'packaging/windows/PeDependencyReport.psm1'
        Assert-Equal $changed[0].sha256 ((Get-FileHash -LiteralPath $parserCopy).Hash.ToLowerInvariant())
        Assert-Equal ((Get-FileHash -LiteralPath $parserOriginal).Hash) $originalHash
    }

    $multimediaRoot = Join-Path $testRoot 'Qt Multimedia 日本'
    $ffmpegBin = Join-Path $multimediaRoot 'bin'
    $ffmpegPlugin = Join-Path $multimediaRoot 'plugins/multimedia/ffmpegmediaplugin.dll'
    $ffmpegManifest = Join-Path $exportDirectory $ffmpegRecipeNames[1]
    $multimediaConfig = Join-Path $multimediaRoot 'lib/cmake/Qt6Multimedia'
    $null = New-Item -ItemType Directory -Path $ffmpegBin, (Split-Path -Parent $ffmpegPlugin), $multimediaConfig
    $ffmpegFiles = @(foreach ($name in $ffmpegNames) {
        $path = Join-Path $ffmpegBin $name
        [IO.File]::WriteAllText($path, "Synthetic FFmpeg fixture: $name")
        [ordered]@{ name = $name } + (Get-TestFilePin $path)
    })
    [IO.File]::WriteAllText($ffmpegPlugin, 'Synthetic Qt multimedia plugin')
    $ffmpegPins = [ordered]@{ schema_version = 1; product_version = '7.1.5-rcmusic1'
        qualified_build_receipt_sha256 = 'a' * 64; plugin = Get-TestFilePin $ffmpegPlugin; files = $ffmpegFiles }
    Write-TestFfmpegManifest $ffmpegPins

    Test-Case 'FFmpeg verification binds the complete runtime inventory and receipt without creating output' {
        $runtime = Read-TestFfmpegRuntime
        Assert-Equal $runtime.product_version $ffmpegPins.product_version
        Assert-Equal $runtime.qualified_build_receipt_sha256 $ffmpegPins.qualified_build_receipt_sha256
        $expectedPaths = @($ffmpegNames) + 'plugins/multimedia/ffmpegmediaplugin.dll'
        Assert-Equal (($runtime.files.relative_path | Sort-Object -CaseSensitive) -join '|') `
            (($expectedPaths | Sort-Object -CaseSensitive) -join '|')
        foreach ($entry in $runtime.files) {
            $file = if ($entry.relative_path -in $ffmpegNames) { Join-Path $ffmpegBin $entry.relative_path } else { $ffmpegPlugin }
            $expected = Get-TestFilePin $file
            Assert-Equal $entry.size_bytes $expected.size_bytes
            Assert-Equal $entry.sha256 $expected.sha256
        }
        Assert-Equal (Test-Path -LiteralPath (Join-Path $testRoot 'unused-ffmpeg-runtime')) $false
    }
    Test-Case 'FFmpeg verification rejects duplicate DLL names even with valid hashes and five entries' {
        try {
            $ffmpegPins.files = @($ffmpegFiles[0], $ffmpegFiles[0]) + $ffmpegFiles[2..4]
            Write-TestFfmpegManifest $ffmpegPins
            Assert-Rejected { Read-TestFfmpegRuntime } 'FFmpeg'
        } finally {
            $ffmpegPins.files = $ffmpegFiles
            Write-TestFfmpegManifest $ffmpegPins
        }
    }
    Test-Case 'FFmpeg verification rejects a substituted Qt plugin of the original size' {
        $original = [IO.File]::ReadAllBytes($ffmpegPlugin)
        $changed = [byte[]] $original.Clone()
        $changed[0] = $changed[0] -bxor 1
        try {
            [IO.File]::WriteAllBytes($ffmpegPlugin, $changed)
            Assert-Rejected { Read-TestFfmpegRuntime } 'ffmpegmediaplugin\.dll'
        } finally { [IO.File]::WriteAllBytes($ffmpegPlugin, $original) }
    }
    Test-Case 'the standard Lite recipe rejects stale multimedia and network DLLs before running tools or creating output' {
        $cachePath = Join-Path $buildDirectory 'CMakeCache.txt'
        $previousCache = [IO.File]::ReadAllText($cachePath)
        $dll = Join-Path $ffmpegBin $ffmpegNames[0]
        $original = [IO.File]::ReadAllBytes($dll)
        $changed = [byte[]] $original.Clone()
        $changed[0] = $changed[0] -bxor 1
        try {
            $releaseCache = @('CMAKE_BUILD_TYPE:STRING=Release', 'BUILD_TESTING:BOOL=OFF',
                "CMAKE_HOME_DIRECTORY:INTERNAL=$exportRoot", 'RC_MUSIC_BUILD_DESKTOP:BOOL=ON',
                "Qt6_DIR:PATH=$multimediaConfig", "Qt6Multimedia_DIR:PATH=$multimediaConfig",
                "TagLib_DIR:PATH=$multimediaConfig", "CMAKE_CXX_COMPILER:FILEPATH=$testRoot/unused-compiler.exe",
                "CMAKE_COMMAND:INTERNAL=$testRoot/unused-cmake.exe")
            [IO.File]::WriteAllLines($cachePath, $releaseCache)
            [IO.File]::WriteAllBytes($dll, $changed)
            $result = Invoke-RecipeChild @('-BuildDirectory', $buildDirectory, '-OutputDirectory', 'out/product/rejected-ffmpeg')
            Assert-Equal $result.ExitCode 1
            Assert-Equal $result.Stdout ''
            if ($result.Stderr -notmatch 'avcodec-61\.dll') { throw "Wrong FFmpeg preflight: $($result.Stderr)" }
            Assert-Equal (Test-Path -LiteralPath (Join-Path $exportRoot 'out/product/rejected-ffmpeg')) $false
            [IO.File]::WriteAllBytes($dll, $original)
            [IO.File]::WriteAllText((Join-Path $ffmpegBin 'Qt6Network.dll'), 'Unqualified Qt Network')
            $result = Invoke-RecipeChild @('-BuildDirectory', $buildDirectory, '-OutputDirectory', 'out/product/rejected-network')
            Assert-Equal $result.ExitCode 1
            Assert-Equal $result.Stdout ''
            if ($result.Stderr -notmatch 'Qt6Network\.dll') { throw "Wrong Qt Network preflight: $($result.Stderr)" }
            Assert-Equal (Test-Path -LiteralPath (Join-Path $exportRoot 'out/product/rejected-network')) $false
        } finally {
            [IO.File]::WriteAllBytes($dll, $original)
            [IO.File]::WriteAllText($cachePath, $previousCache, [Text.UTF8Encoding]::new($false))
        }
    }
    Test-Case 'the SDK stager copies the vendor SDK with the five pinned replacements' {
        $stager = Join-Path $exportDirectory $multimediaStagerName
        if (-not [IO.File]::Exists($stager)) { throw 'Required SDK stager is absent.' }
        $vendorRoot = Join-Path $testRoot 'vendor Multimedia 日本'
        $vendorBin = Join-Path $vendorRoot 'bin'
        $vendorConfig = Join-Path $vendorRoot 'lib/cmake/Qt6Multimedia'
        $vendorPlugin = Join-Path $vendorRoot 'plugins/multimedia/ffmpegmediaplugin.dll'
        $null = New-Item -ItemType Directory -Path $vendorBin, $vendorConfig, (Split-Path -Parent $vendorPlugin)
        $nonFfmpeg = @('lib/cmake/Qt6Multimedia/Qt6MultimediaConfig.cmake', 'bin/Qt6Multimedia.dll',
            'plugins/multimedia/ffmpegmediaplugin.dll')
        [IO.File]::WriteAllText((Join-Path $vendorRoot $nonFfmpeg[0]), '# Synthetic Qt Multimedia config')
        [IO.File]::WriteAllText((Join-Path $vendorRoot $nonFfmpeg[1]), 'Synthetic Qt Multimedia library')
        Copy-Item -LiteralPath $ffmpegPlugin -Destination $vendorPlugin
        foreach ($name in $ffmpegNames) {
            [IO.File]::WriteAllText((Join-Path $vendorBin $name), "Synthetic vendor FFmpeg: $name")
        }
        $vendorPaths = $nonFfmpeg + @($ffmpegNames | ForEach-Object { 'bin/' + $_ })
        $originalPins = @{}
        foreach ($relative in $vendorPaths) { $originalPins[$relative] = Get-TestFilePin (Join-Path $vendorRoot $relative) }

        $output = Join-Path $exportRoot 'out/product/corrected Qt Multimedia 日本'
        $null = & $stager -VendorSdkRoot $vendorRoot -FfmpegBinDirectory $ffmpegBin -OutputDirectory $output
        foreach ($relative in $vendorPaths) {
            Assert-Equal (Get-TestFilePin (Join-Path $vendorRoot $relative)).sha256 $originalPins[$relative].sha256
        }
        foreach ($relative in $nonFfmpeg) {
            Assert-Equal (Get-TestFilePin (Join-Path $output $relative)).sha256 $originalPins[$relative].sha256
        }
        foreach ($entry in $ffmpegFiles) {
            Assert-Equal (Get-TestFilePin (Join-Path $output ('bin/' + $entry.name))).sha256 $entry.sha256
        }
    }
    Test-Case 'Qt Network verification rejects same-size changes and stages the qualified DLL without altering the SDK' {
        $vendorRoot = Join-Path $testRoot 'vendor Qt 日本'
        $vendorBin = Join-Path $vendorRoot 'bin'
        $vendorConfig = Join-Path $vendorRoot 'lib/cmake/Qt6'
        $candidate = Join-Path $testRoot 'candidate Qt 日本'
        $null = New-Item -ItemType Directory -Path $vendorBin, $vendorConfig, $candidate
        $config = Join-Path $vendorConfig 'Qt6Config.cmake'
        $vendorDll = Join-Path $vendorBin 'Qt6Network.dll'
        $candidateDll = Join-Path $candidate 'Qt6Network.dll'
        $vendorSymbols = Join-Path $vendorBin 'Qt6Network.pdb'
        $candidateSymbols = Join-Path $candidate 'Qt6Network.pdb'
        [IO.File]::WriteAllText($config, '# Synthetic Qt config')
        [IO.File]::WriteAllText($vendorDll, 'Original Qt Network')
        [IO.File]::WriteAllText($candidateDll, 'Patched! Qt Network')
        [IO.File]::WriteAllText($vendorSymbols, 'Original symbols')
        [IO.File]::WriteAllText($candidateSymbols, 'Patched symbols')
        $vendorPin = Get-TestFilePin $vendorDll
        $candidatePin = Get-TestFilePin $candidateDll
        Assert-Equal $vendorPin.size_bytes $candidatePin.size_bytes
        $manifest = Join-Path $exportDirectory $networkRecipeNames[1]
        $pins = [ordered]@{ schema_version = 1; qualified_build_receipt_sha256 = 'b' * 64 } + $candidatePin
        [IO.File]::WriteAllText($manifest, ($pins | ConvertTo-Json))
        $output = Join-Path $exportRoot 'out/product/corrected Qt 日本'
        $module = Join-Path $exportDirectory $networkRecipeNames[0]
        if (-not [IO.File]::Exists($module)) { throw 'Required Qt Network validator module is absent.' }
        Import-Module $module -Force
        Assert-Rejected {
            Get-RcMusicQtNetworkRuntime -ManifestPath $manifest -DllDirectory $vendorBin -Destination $output
        } 'Qt6Network\.dll'
        Assert-Equal (Test-Path -LiteralPath $output) $false
        $qualified = Get-RcMusicQtNetworkRuntime -ManifestPath $manifest -DllDirectory $candidate -Destination $output
        Assert-Equal $qualified.sha256 $candidatePin.sha256
        Assert-Equal $qualified.qualified_build_receipt_sha256 $pins.qualified_build_receipt_sha256
        Assert-Equal (Test-Path -LiteralPath $output) $false
        $null = & (Join-Path $exportDirectory $networkStagerName) -VendorSdkRoot $vendorRoot `
            -DllDirectory $candidate -OutputDirectory $output
        Assert-Equal (Get-TestFilePin $vendorDll).sha256 $vendorPin.sha256
        Assert-Equal (Get-TestFilePin (Join-Path $output 'bin/Qt6Network.dll')).sha256 $candidatePin.sha256
        Assert-Equal ([IO.File]::ReadAllText($vendorSymbols)) 'Original symbols'
        Assert-Equal ([IO.File]::ReadAllText((Join-Path $output 'bin/Qt6Network.pdb'))) 'Patched symbols'
        Assert-Equal (Get-TestFilePin (Join-Path $output 'lib/cmake/Qt6/Qt6Config.cmake')).sha256 (Get-TestFilePin $config).sha256
    }
} finally {
    Assert-SafeTestRoot
    Remove-Item -LiteralPath ([IO.Path]::GetFullPath($testRoot)) -Recurse -Force
}
Write-Host "Runtime recipe: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -gt 0) { exit 1 }
