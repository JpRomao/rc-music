#Requires -Version 7.0
[CmdletBinding()]
param([switch] $IncludeLegacy)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$modulePath = Join-Path $repository 'packaging/windows/PeDependencyReport.psm1'
$state = @{ Passed = 0; Failed = 0 }
function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Assert-Equal($Actual, $Expected) {
    if ($Actual -cne $Expected) { throw "Expected '$Expected', found '$Actual'." }
}
function Assert-Rejected([scriptblock] $Body, [string] $Pattern) {
    try { & $Body | Out-Null }
    catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw "Wrong rejection: $($_.Exception.Message)" }
        return
    }
    throw 'Expected rejection.'
}
function Read-Ast([string] $Path) {
    $parseErrors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile($Path, [ref]$null, [ref]$parseErrors)
    if ($parseErrors.Count -ne 0) { throw "Invalid PowerShell: $Path" }
    return $ast
}
function Parse-Report([string] $Report) {
    if ($null -eq $script:parser) { throw 'Independent parser module is unavailable.' }
    return & $script:parser $Report
}
$testRoot = Join-Path $repository ('out/product/pe-report-' + [guid]::NewGuid().ToString('N'))
function Assert-SafeTestRoot {
    $parent = [IO.Path]::GetFullPath((Join-Path $repository 'out/product')).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $resolved = [IO.Path]::GetFullPath($testRoot)
    if (-not $resolved.StartsWith($parent, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -cnotmatch '\Ape-report-[0-9a-f]{32}\z') { throw 'Unsafe test root.' }
    $ancestor = $resolved
    while ($ancestor) {
        try { $attributes = [IO.File]::GetAttributes($ancestor) }
        catch [IO.FileNotFoundException] { $attributes = 0 }
        catch [IO.DirectoryNotFoundException] { $attributes = 0 }
        if ($attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unsafe reparse ancestor.' }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
}
$script:parser = $null
$isolatedModule = $null
Assert-SafeTestRoot
if (Test-Path -LiteralPath $testRoot) { throw 'Test root already exists.' }
$null = New-Item -ItemType Directory -Path $testRoot
try {
    Test-Case 'the standalone module exports its parser from a subset without experiments' {
        $subset = Join-Path $testRoot 'source com espaços 日本'
        $null = New-Item -ItemType Directory -Path $subset
        $copy = Join-Path $subset 'PeDependencyReport.psm1'
        Copy-Item -LiteralPath $modulePath -Destination $copy
        Assert-Equal ((Get-FileHash -LiteralPath $copy).Hash) ((Get-FileHash -LiteralPath $modulePath).Hash)
        $script:isolatedModule = Import-Module $copy -Force -PassThru
        Assert-Equal ($script:isolatedModule.ExportedFunctions.Keys -join ',') 'ConvertFrom-QtShellPeReport'
        Assert-Equal (Test-Path -LiteralPath (Join-Path $subset 'experiments')) $false
        $script:parser = $script:isolatedModule.ExportedFunctions['ConvertFrom-QtShellPeReport']
    }
    Test-Case 'PE report inventories imports and delay imports, and enforces x64' {
        $report = "FILE HEADER VALUES`n            8664 machine (x64)`n    Image has the following dependencies:`n        Qt6Core.dll`n        KERNEL32.dll`n    Image has the following delay load dependencies:`n        USER32.dll`n        Qt6Core.dll`n"
        $result = Parse-Report $report
        Assert-Equal $result.machine 'x64'
        Assert-Equal ($result.imports -join ',') 'KERNEL32.dll,Qt6Core.dll,USER32.dll'
        Assert-Rejected { Parse-Report ($report.Replace('8664 machine (x64)', '14C machine (x86)')) } 'x64'
        Assert-Rejected { Parse-Report 'not a PE report' } 'x64|report'
    }
    Test-Case 'CRLF reports preserve ordinal order and case-insensitive first-spelling de-duplication' {
        $report = " 8664 machine (x64)`r`n  z.dll`r`n  A.dll`r`n  Z.dll`r`n  a.dll`r`n"
        Assert-Equal ((Parse-Report $report).imports -join ',') 'A.dll,z.dll'
    }
    Test-Case 'x64 without dependency lines is rejected' {
        Assert-Rejected { Parse-Report " 8664 machine (x64)`n no dependencies" } 'no imports'
    }
    Test-Case 'the product parser has one implementation and no module imports' {
        $ast = Read-Ast $modulePath
        $functions = @($ast.FindAll({ param($node)
            $node -is [Management.Automation.Language.FunctionDefinitionAst]
        }, $true))
        Assert-Equal $functions.Count 1
        Assert-Equal $functions[0].Name 'ConvertFrom-QtShellPeReport'
        $imports = @($ast.FindAll({ param($node)
            $node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -ceq 'Import-Module'
        }, $true))
        Assert-Equal $imports.Count 0
    }
    # Compatibility is opt-in: the default product checks never read experiments.
    if ($IncludeLegacy) {
        $legacyPath = Join-Path $repository 'experiments/m0/candidates/qt/package/QtShellPackageBuilder.psm1'
        $recipePath = Join-Path $repository 'experiments/m0/candidates/qt/package/New-QtShellPackage.ps1'
        Test-Case 'the legacy builder reexports the exact promoted implementation without a local copy' {
            $legacy = Import-Module $legacyPath -Force -PassThru
            $export = $legacy.ExportedFunctions['ConvertFrom-QtShellPeReport']
            Assert-Equal ([IO.Path]::GetFullPath($export.ScriptBlock.File)) ([IO.Path]::GetFullPath($modulePath))
            $localParser = @((Read-Ast $legacyPath).FindAll({ param($node)
                $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'ConvertFrom-QtShellPeReport'
            }, $true))
            Assert-Equal $localParser.Count 0
            Assert-Equal ((& $export " 8664 machine (x64)`n  Qt6Core.dll").imports -join ',') 'Qt6Core.dll'
        }
        Test-Case 'the legacy recipe hashes the real parser with explicit repository-relative provenance' {
            $ast = Read-Ast $recipePath
            $assignment = $ast.Find({ param($node)
                $node -is [Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -ceq '$recipeFiles'
            }, $true)
            if ($null -eq $assignment) { throw 'Recipe inventory assignment is absent.' }
            . ([scriptblock]::Create($assignment.Extent.Text))
            Assert-Equal (@($recipeFiles | Where-Object { $_ -ceq 'packaging/windows/PeDependencyReport.psm1' }).Count) 1
            Assert-Equal $recipeFiles.Count 7
            foreach ($path in $recipeFiles) {
                if ([IO.Path]::IsPathRooted($path) -or $path -match '(?:^|/)\.\.(?:/|$)') { throw 'Recipe path must be repository-relative without traversal.' }
            }
            $metadata = $ast.Find({ param($node)
                $node -is [Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -ceq '$metadata'
            }, $true)
            $table = $metadata.Right.Find({ param($node) $node -is [Management.Automation.Language.HashtableAst] }, $true)
            $baseField = @($table.KeyValuePairs | Where-Object { $_.Item1.Value -ceq 'recipe_path_base' })
            Assert-Equal $baseField.Count 1
            Assert-Equal (& ([scriptblock]::Create($baseField[0].Item2.Extent.Text))) 'repository'
            $recipeField = @($table.KeyValuePairs | Where-Object { $_.Item1.Value -ceq 'recipe' })
            function Get-Sha256([string] $Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
            $entries = @(& ([scriptblock]::Create($recipeField[0].Item2.Extent.Text)))
            Assert-Equal $entries.Count 7
            $parserEntry = @($entries | Where-Object { $_.relative_path -ceq 'packaging/windows/PeDependencyReport.psm1' })
            Assert-Equal $parserEntry.Count 1
            Assert-Equal $parserEntry[0].sha256 (Get-Sha256 $modulePath)
            # Exercise the unchanged metadata decoder without building a package or
            # writing a manifest. Metadata is an open object, unlike file entries.
            $manifestModule = Import-Module (Join-Path ([IO.Path]::GetDirectoryName($legacyPath)) 'QtShellPackageManifest.psm1') -Force -PassThru
            $json = @{ recipe_path_base = 'repository'; recipe = $entries } | ConvertTo-Json -Depth 6 -Compress
            $roundtrip = & $manifestModule {
                param([string] $Json)
                $document = [Text.Json.JsonDocument]::Parse($Json)
                try { return ConvertFrom-QtPackageMetadata $document.RootElement }
                finally { $document.Dispose() }
            } $json
            Assert-Equal $roundtrip.recipe_path_base 'repository'
            Assert-Equal $roundtrip.recipe.Count 7
            Assert-Equal ($roundtrip.recipe[-1].relative_path) 'packaging/windows/PeDependencyReport.psm1'
            Assert-Equal ($roundtrip.recipe[-1].sha256) (Get-Sha256 $modulePath)
        }
        Test-Case 'the legacy recipe includes the promoted parser in its dirty-state inventory' {
            $status = (Read-Ast $recipePath).Find({ param($node)
                $node -is [Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -ceq '$recipeStatus'
            }, $true)
            if ($null -eq $status -or -not $status.Extent.Text.Contains("'packaging/windows/PeDependencyReport.psm1'")) {
                throw 'Recipe status omits the promoted parser.'
            }
        }
    }
} finally {
    if ($null -ne $isolatedModule) { Remove-Module $isolatedModule -Force }
    Assert-SafeTestRoot
    Remove-Item -LiteralPath ([IO.Path]::GetFullPath($testRoot)) -Recurse -Force
}
Write-Host "PE dependency report: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -gt 0) { exit 1 }
