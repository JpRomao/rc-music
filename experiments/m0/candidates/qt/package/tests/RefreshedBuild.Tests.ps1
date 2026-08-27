#Requires -Version 7.0
[CmdletBinding()]
param([string] $Filter = '*')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$builderModule = Import-Module (Join-Path $PSScriptRoot '../QtShellPackageBuilder.psm1') -Force -PassThru

$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
$testRoot = Join-Path $repository ('out/m0/test-runs/qt-package/refreshed-build-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $testRoot
$state = @{ Passed = 0; Failed = 0 }
function Test-Case([string] $Name, [scriptblock] $Body) {
    if ($Name -notlike $Filter) { return }
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Assert-Equal($Actual, $Expected) {
    if ($Actual -cne $Expected) { throw "Expected '$Expected', found '$Actual'." }
}
function Assert-Same($Actual, $Expected) {
    if (-not [object]::ReferenceEquals($Actual, $Expected)) { throw 'Expected the original object.' }
}
function Get-Rejection([scriptblock] $Body, [string] $Pattern) {
    try { & $Body | Out-Null }
    catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw "Wrong rejection: $($_.Exception.Message)" }
        return $_
    }
    throw 'Expected rejection.'
}
function New-BuildFixture {
    $directory = Join-Path $testRoot ([Guid]::NewGuid().ToString('N'))
    $null = New-Item -ItemType Directory -Path $directory
    $arguments = @{
        ContractPath = Join-Path $directory 'synthetic-refresh.json'
        ExpectedContractSha256 = 'a' * 64
        CompilerInfoPath = Join-Path $directory 'CMakeCXXCompiler.cmake'
        CachePath = Join-Path $directory 'CMakeCache.txt'
        QtConfigPath = Join-Path $directory 'Qt6CoreConfigExtras.cmake'
        SourceRoot = Join-Path $directory 'source com espaços'
        QtRoot = Join-Path $directory 'Qt sintético'
    }
    $cache = @"
CMAKE_BUILD_TYPE:STRING=Release
BUILD_TESTING:BOOL=ON
M0_QT_SHELL_SUPPORT:BOOL=ON
CMAKE_GENERATOR:INTERNAL=Ninja
CMAKE_HOME_DIRECTORY:INTERNAL=$($arguments.SourceRoot)
Qt6_DIR:PATH=$(Join-Path $arguments.QtRoot 'lib/cmake/Qt6')
"@
    [IO.File]::WriteAllText($arguments.CachePath, $cache)
    $fixture = @{
        Directory = $directory
        Arguments = $arguments
        Cache = $cache
        Events = [Collections.Generic.List[string]]::new()
        ReceiptCalls = [Collections.Generic.List[object]]::new()
        BuildIdentities = [Collections.Generic.List[object]]::new()
        PreReceipt = [pscustomobject] @{ identity = [pscustomobject] @{ phase = 'pre' }; cmake_cache_sha256 = 'b' * 64 }
        PostReceipt = [pscustomobject] @{ identity = [pscustomobject] @{ phase = 'post' }; cmake_cache_sha256 = 'c' * 64 }
        NativeResult = [pscustomobject] @{ exit_code = 0; stdout = 'synthetic build output'; stderr = '' }
        PreException = $null
        PostException = $null
        BuildException = $null
        OnBuild = $null
    }
    $arguments.BuildAction = {
        param($identity)
        $fixture.Events.Add('build')
        $fixture.BuildIdentities.Add($identity)
        if ($null -ne $fixture.OnBuild) { & $fixture.OnBuild $fixture }
        if ($null -ne $fixture.BuildException) { throw $fixture.BuildException }
        return $fixture.NativeResult
    }.GetNewClosure()
    return $fixture
}
function Invoke-WithReceiptFake([hashtable] $Fixture, [scriptblock] $Body) {
    $original = & $builderModule { (Get-Item Function:New-QtShellMsvcRefreshReceipt).ScriptBlock }
    try {
        & $builderModule {
            param($testState)
            $script:refreshedBuildTestState = $testState
            function script:New-QtShellMsvcRefreshReceipt {
                param(
                    [Parameter(Mandatory)] [string] $ContractPath,
                    [Parameter(Mandatory)] [string] $ExpectedContractSha256,
                    [Parameter(Mandatory)] [string] $CompilerInfoPath,
                    [Parameter(Mandatory)] [string] $CachePath,
                    [Parameter(Mandatory)] [string] $QtConfigPath
                )
                $test = $script:refreshedBuildTestState
                $callNumber = $test.ReceiptCalls.Count + 1
                $test.Events.Add("receipt-$callNumber")
                $test.ReceiptCalls.Add([pscustomobject] @{
                    parameters = @{} + $PSBoundParameters
                    cache_text = [IO.File]::ReadAllText($CachePath)
                })
                if ($callNumber -eq 1) {
                    if ($null -ne $test.PreException) { throw $test.PreException }
                    return $test.PreReceipt
                }
                if ($callNumber -eq 2) {
                    if ($null -ne $test.PostException) { throw $test.PostException }
                    return $test.PostReceipt
                }
                throw 'Unexpected additional receipt authentication.'
            }
        } $Fixture
        & $Body $Fixture
    }
    finally {
        & $builderModule {
            param($originalReceipt)
            Set-Item Function:script:New-QtShellMsvcRefreshReceipt -Value $originalReceipt
            Remove-Variable -Name refreshedBuildTestState -Scope Script -ErrorAction SilentlyContinue
        } $original
        $restored = & $builderModule { (Get-Item Function:New-QtShellMsvcRefreshReceipt).ScriptBlock }
        Assert-Same $restored $original
    }
}

Test-Case 'successful refresh validates cache around one build and returns final evidence' {
    Invoke-WithReceiptFake (New-BuildFixture) {
        param($sample)
        $arguments = $sample.Arguments
        $results = @(Invoke-QtShellRefreshedBuild @arguments)
        Assert-Equal $results.Count 1
        $result = $results[0]
        Assert-Equal ($sample.Events -join ',') 'receipt-1,build,receipt-2'
        Assert-Equal $sample.BuildIdentities.Count 1
        Assert-Same $sample.BuildIdentities[0] $sample.PreReceipt.identity
        Assert-Equal $sample.ReceiptCalls.Count 2
        foreach ($call in $sample.ReceiptCalls) {
            Assert-Equal $call.parameters.Count 5
            foreach ($name in @('ContractPath', 'ExpectedContractSha256', 'CompilerInfoPath', 'CachePath', 'QtConfigPath')) {
                Assert-Equal $call.parameters[$name] $arguments[$name]
            }
        }
        Assert-Equal ($result.PSObject.Properties.Name -join ',') 'configuration,receipt,build_result'
        Assert-Equal $result.configuration.configuration 'Release'
        Assert-Equal $result.configuration.test_hooks $true
        Assert-Equal $result.configuration.generator 'Ninja'
        Assert-Same $result.receipt $sample.PostReceipt
        Assert-Same $result.build_result $sample.NativeResult
    }
}

Test-Case 'invalid pre-build Release cache prevents receipt and callback' {
    Invoke-WithReceiptFake (New-BuildFixture) {
        param($sample)
        $arguments = $sample.Arguments
        [IO.File]::WriteAllText($arguments.CachePath, $sample.Cache.Replace('STRING=Release', 'STRING=Debug'))
        $null = Get-Rejection { Invoke-QtShellRefreshedBuild @arguments } 'Release cache requires CMAKE_BUILD_TYPE=Release'
        Assert-Equal $sample.Events.Count 0
        Assert-Equal $sample.ReceiptCalls.Count 0
        Assert-Equal $sample.BuildIdentities.Count 0
    }
}

Test-Case 'pre-build receipt rejection prevents callback and final authentication' {
    Invoke-WithReceiptFake (New-BuildFixture) {
        param($sample)
        $arguments = $sample.Arguments
        $sample.PreException = [InvalidOperationException]::new('synthetic pre-build pin mismatch')
        $failure = Get-Rejection { Invoke-QtShellRefreshedBuild @arguments } 'synthetic pre-build pin mismatch'
        Assert-Same $failure.Exception $sample.PreException
        Assert-Equal ($sample.Events -join ',') 'receipt-1'
        Assert-Equal $sample.BuildIdentities.Count 0
    }
}

Test-Case 'native failure preserves output and prevents final authentication' {
    Invoke-WithReceiptFake (New-BuildFixture) {
        param($sample)
        $arguments = $sample.Arguments
        $sample.NativeResult = [pscustomobject] @{ exit_code = 7; stdout = "out one`r`nout two"; stderr = "erro de compilação`n" }
        $sample.OnBuild = { param($current) [IO.File]::WriteAllText($current.Arguments.CachePath, 'invalid after native failure') }
        $failure = Get-Rejection { Invoke-QtShellRefreshedBuild @arguments } '7'
        Assert-Same $failure.TargetObject $sample.NativeResult
        Assert-Equal ($failure.Exception.Message.Contains($sample.NativeResult.stdout)) $true
        Assert-Equal ($failure.Exception.Message.Contains($sample.NativeResult.stderr)) $true
        Assert-Equal ($sample.Events -join ',') 'receipt-1,build'
        Assert-Equal $sample.BuildIdentities.Count 1
    }
}

Test-Case 'callback exception propagates without final cache or receipt authentication' {
    Invoke-WithReceiptFake (New-BuildFixture) {
        param($sample)
        $arguments = $sample.Arguments
        $sample.BuildException = [InvalidOperationException]::new('synthetic callback failure')
        $sample.OnBuild = { param($current) [IO.File]::WriteAllText($current.Arguments.CachePath, 'invalid after callback') }
        $failure = Get-Rejection { Invoke-QtShellRefreshedBuild @arguments } 'synthetic callback failure'
        Assert-Same $failure.Exception $sample.BuildException
        Assert-Equal ($sample.Events -join ',') 'receipt-1,build'
        Assert-Equal $sample.BuildIdentities.Count 1
    }
}

Test-Case 'invalid final Release cache rejects before final receipt' {
    Invoke-WithReceiptFake (New-BuildFixture) {
        param($sample)
        $arguments = $sample.Arguments
        $sample.OnBuild = {
            param($current)
            [IO.File]::WriteAllText($current.Arguments.CachePath, $current.Cache.Replace('BUILD_TESTING:BOOL=ON', 'BUILD_TESTING:BOOL=OFF'))
        }
        $null = Get-Rejection { Invoke-QtShellRefreshedBuild @arguments } 'Release cache requires BUILD_TESTING=ON'
        Assert-Equal ($sample.Events -join ',') 'receipt-1,build'
        Assert-Equal $sample.BuildIdentities.Count 1
    }
}

Test-Case 'post-build identity drift rejects the result' {
    Invoke-WithReceiptFake (New-BuildFixture) {
        param($sample)
        $arguments = $sample.Arguments
        $sample.PostException = [InvalidOperationException]::new('synthetic post-build compiler drift')
        $failure = Get-Rejection { Invoke-QtShellRefreshedBuild @arguments } 'synthetic post-build compiler drift'
        Assert-Same $failure.Exception $sample.PostException
        Assert-Equal ($sample.Events -join ',') 'receipt-1,build,receipt-2'
        Assert-Equal $sample.BuildIdentities.Count 1
    }
}

Test-Case 'legitimate CMake regeneration returns the post-build receipt' {
    Invoke-WithReceiptFake (New-BuildFixture) {
        param($sample)
        $arguments = $sample.Arguments
        $sample.OnBuild = {
            param($current)
            [IO.File]::WriteAllText($current.Arguments.CachePath, $current.Cache + "`n// regenerated by the synthetic callback`n")
        }
        $result = Invoke-QtShellRefreshedBuild @arguments
        Assert-Equal $sample.ReceiptCalls[0].cache_text $sample.Cache
        Assert-Equal $sample.ReceiptCalls[1].cache_text ([IO.File]::ReadAllText($arguments.CachePath))
        Assert-Equal ($sample.ReceiptCalls[1].cache_text -cne $sample.ReceiptCalls[0].cache_text) $true
        Assert-Equal $result.receipt.cmake_cache_sha256 ('c' * 64)
        Assert-Same $result.receipt $sample.PostReceipt
        Assert-Same $result.build_result $sample.NativeResult
    }
}

Test-Case 'helper does not create evidence or package files' {
    Invoke-WithReceiptFake (New-BuildFixture) {
        param($sample)
        $arguments = $sample.Arguments
        $before = @(Get-ChildItem -LiteralPath $sample.Directory -Force -Recurse | Select-Object -ExpandProperty FullName)
        $null = Invoke-QtShellRefreshedBuild @arguments
        $after = @(Get-ChildItem -LiteralPath $sample.Directory -Force -Recurse | Select-Object -ExpandProperty FullName)
        Assert-Equal ($after -join "`n") ($before -join "`n")
        Assert-Equal ([IO.File]::ReadAllText($arguments.CachePath)) $sample.Cache
        foreach ($name in @('ContractPath', 'CompilerInfoPath', 'QtConfigPath')) {
            Assert-Equal (Test-Path -LiteralPath $arguments[$name]) $false
        }
    }
}

Write-Host "Refreshed build: $($state.Passed) passed, $($state.Failed) failed. Fixtures retained at $testRoot"
if ($state.Failed -ne 0) { exit 1 }
