#Requires -Version 7.0

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:State = [ordered]@{ Passed = 0; Failed = 0 }
$testRoot = Split-Path -Parent $PSScriptRoot
$sourceLauncher = Join-Path $testRoot 'Invoke-M0QtShellComposite.ps1'
$qtBaseReceiptSha256 = '8a4e3c13b2cbf1103ef2a7c417160eddc17db4fdc00dcea505e249eccab28619'
$utf8NoBom = [Text.UTF8Encoding]::new($false)

function Test-Case {
    param([Parameter(Mandatory)] [string] $Name, [Parameter(Mandatory)] [scriptblock] $Body)

    try {
        & $Body
        $script:State.Passed++
        Write-Host "PASS $Name"
    }
    catch {
        $script:State.Failed++
        Write-Host "FAIL $Name"
        Write-Host $_.Exception.ToString()
    }
}

function Assert-True {
    param([Parameter(Mandatory)] [bool] $Condition, [Parameter(Mandatory)] [string] $Message)
    if (-not $Condition) { throw $Message }
}

function Get-Sha256 {
    param([Parameter(Mandatory)] [string] $LiteralPath)
    return (Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath $LiteralPath -Algorithm SHA256).Hash.ToLowerInvariant()
}

function ConvertTo-SingleQuotedLiteral {
    param([Parameter(Mandatory)] [string] $Value)
    return "'" + $Value.Replace("'", "''") + "'"
}

function Invoke-ChildPowerShell {
    param([Parameter(Mandatory)] [string[]] $Arguments)

    $hostPath = (Get-Command pwsh -ErrorAction Stop).Source
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $hostPath
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in @('-NoLogo', '-NoProfile', '-NonInteractive') + $Arguments) {
        $startInfo.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) { throw 'Failed to start child PowerShell.' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        return [pscustomobject]@{
            exit_code = $process.ExitCode
            stdout = $stdout.GetAwaiter().GetResult()
            stderr = $stderr.GetAwaiter().GetResult()
        }
    }
    finally { $process.Dispose() }
}

function New-LauncherFixture {
    param(
        [switch] $SwapPublishedEvidenceAfterReturn,
        [switch] $ForVerification,
        [string] $ResultMutation = '',
        [string] $VerifierResultMutation = ''
    )

    $root = Join-Path ([IO.Path]::GetTempPath()) ('m0-composite-launcher-' + [Guid]::NewGuid().ToString('N'))
    $recipeRoot = Join-Path $root 'experiments/m0/provisioning/qt-shell'
    $null = [IO.Directory]::CreateDirectory($recipeRoot)
    $null = [IO.Directory]::CreateDirectory((Join-Path $root 'q'))

    $launcher = Join-Path $recipeRoot 'Invoke-M0QtShellComposite.ps1'
    $orchestrator = Join-Path $recipeRoot 'M0.QtShellComposite.psm1'
    $runner = Join-Path $recipeRoot 'M0.QtShellCompositeNative.psm1'
    $contract = Join-Path $recipeRoot 'qt-shell-composite-contract.v2.json'
    $maliciousContract = Join-Path $root 'swapped-contract.json'
    [IO.File]::Copy($sourceLauncher, $launcher)
    [IO.File]::WriteAllText($runner, "# authenticated runner fixture`n", $utf8NoBom)
    if ($SwapPublishedEvidenceAfterReturn) {
        [IO.File]::WriteAllText((Join-Path $root 'swap-published-evidence.fixture'), '')
    }
    if (-not [string]::IsNullOrEmpty($ResultMutation)) {
        [IO.File]::WriteAllText((Join-Path $root 'result-mutation.fixture'), $ResultMutation)
    }
    if (-not [string]::IsNullOrEmpty($VerifierResultMutation)) {
        [IO.File]::WriteAllText(
            (Join-Path $root 'verification-result-mutation.fixture'), $VerifierResultMutation)
    }

    $orchestratorSource = @'
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Test-FixtureWriteDenied {
    param([Parameter(Mandatory)] [string] $LiteralPath)
    try {
        $stream = [IO.FileStream]::new(
            $LiteralPath,
            [IO.FileMode]::Open,
            [IO.FileAccess]::Write,
            [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        try { return $false }
        finally { $stream.Dispose() }
    }
    catch [IO.IOException] { return $true }
}

function Test-FixtureReplacementDenied {
    param([Parameter(Mandatory)] [string] $LiteralPath)
    $candidate = $LiteralPath + '.replacement'
    [IO.File]::WriteAllText($candidate, 'replacement')
    try {
        try {
            [IO.File]::Move($candidate, $LiteralPath, $true)
            return $false
        }
        catch {
            $reason = $_.Exception
            while ($null -ne $reason.InnerException) { $reason = $reason.InnerException }
            if ($reason -is [IO.IOException] -or $reason -is [UnauthorizedAccessException]) { return $true }
            throw
        }
    }
    finally {
        if ([IO.File]::Exists($candidate)) { [IO.File]::Delete($candidate) }
    }
}

$script:ImportWriteDenied = Test-FixtureWriteDenied -LiteralPath $PSCommandPath

function Assert-M0QtShellCompositeContract { param() }

function Assert-M0QtShellCompositeReceipt {
    param(
        [string] $LiteralPath,
        [string] $ExpectedReceiptSha256,
        [string] $ContractPath,
        [string] $ExpectedContractSha256,
        [string] $ExpectedQtBaseReceiptSha256,
        [string] $RepositoryRoot
    )

    $targets = [ordered]@{
        contract = $ContractPath
        orchestrator = $PSCommandPath
        runner = Join-Path $RepositoryRoot 'experiments/m0/provisioning/qt-shell/M0.QtShellCompositeNative.psm1'
        entrypoint = Join-Path $RepositoryRoot 'experiments/m0/provisioning/qt-shell/Invoke-M0QtShellComposite.ps1'
    }
    $observed = [ordered]@{
        orchestrator_import_write_denied = $script:ImportWriteDenied
        expected_receipt_sha256 = $ExpectedReceiptSha256
        expected_contract_sha256 = $ExpectedContractSha256
        expected_qtbase_receipt_sha256 = $ExpectedQtBaseReceiptSha256
        receipt_path = [IO.Path]::GetFullPath($LiteralPath)
    }
    foreach ($entry in $targets.GetEnumerator()) {
        $observed["$($entry.Key)_write_denied"] = Test-FixtureWriteDenied -LiteralPath $entry.Value
        $observed["$($entry.Key)_replacement_denied"] = Test-FixtureReplacementDenied -LiteralPath $entry.Value
    }
    [IO.File]::WriteAllText(
        (Join-Path $RepositoryRoot 'verification-lease-observations.json'),
        ($observed | ConvertTo-Json -Depth 10))
    $receipt = [pscustomobject][ordered]@{
        schema_version = [long]2
        receipt_id = 'fixture-composite-receipt'
        composite_id = 'fixture-composite'
        captured_utc = '2026-08-28T00:00:00.0000000+00:00'
        contract_sha256 = $ExpectedContractSha256
        execution = [pscustomobject][ordered]@{ exit_code = [long]0; stdout = ''; stderr = '' }
        qtbase_receipt = [pscustomobject][ordered]@{
            relative_path = 'q/parent/evidence/qtbase-refresh-receipt.v2.json'
            size_bytes = [long]1
            sha256 = $ExpectedQtBaseReceiptSha256
        }
        sdk = [pscustomobject][ordered]@{
            root_relative_path = 'sdk'
            file_count = [long]1
            total_size_bytes = [long]1
            inventory_relative_path = 'evidence/composite-inventory.tsv'
            inventory_size_bytes = [long]1
            inventory_sha256 = '2' * 64
        }
        evidence = [pscustomobject][ordered]@{
            relative_path = 'manifest.json'
            size_bytes = [long]1
            sha256 = '3' * 64
        }
        limits = @('fixture-only')
    }
    $mutationPath = Join-Path $RepositoryRoot 'verification-result-mutation.fixture'
    if ([IO.File]::Exists($mutationPath)) {
        switch ([IO.File]::ReadAllText($mutationPath)) {
            'extra-property' {
                $receipt | Add-Member -NotePropertyName unexpected -NotePropertyValue 'open-shape'
            }
            'wrong-order' {
                $receipt.PSObject.Properties.Remove('schema_version')
                $receipt | Add-Member -NotePropertyName schema_version -NotePropertyValue ([long]2)
            }
            'malformed-execution' { $receipt.execution.exit_code = '0' }
            'malformed-sdk' { $receipt.sdk.file_count = '1' }
            'extra-evidence-property' {
                $receipt.evidence | Add-Member -NotePropertyName unexpected -NotePropertyValue 'open-shape'
            }
            default { throw 'Unknown verifier result mutation fixture.' }
        }
    }
    return $receipt
}

function Invoke-M0QtShellComposite {
    param(
        [string] $ContractPath,
        [string] $ExpectedContractSha256,
        [string] $ExpectedQtBaseReceiptSha256,
        [string] $RepositoryRoot,
        [string] $RunRoot
    )

    $targets = [ordered]@{
        contract = $ContractPath
        orchestrator = $PSCommandPath
        runner = Join-Path $RepositoryRoot 'experiments/m0/provisioning/qt-shell/M0.QtShellCompositeNative.psm1'
        entrypoint = Join-Path $RepositoryRoot 'experiments/m0/provisioning/qt-shell/Invoke-M0QtShellComposite.ps1'
    }
    $observed = [ordered]@{ orchestrator_import_write_denied = $script:ImportWriteDenied }
    foreach ($entry in $targets.GetEnumerator()) {
        $observed["$($entry.Key)_write_denied"] = Test-FixtureWriteDenied -LiteralPath $entry.Value
        $observed["$($entry.Key)_replacement_denied"] = Test-FixtureReplacementDenied -LiteralPath $entry.Value
    }

    $null = [IO.Directory]::CreateDirectory($RunRoot)
    $marker = Join-Path $RunRoot 'lease-observations.json'
    [IO.File]::WriteAllText($marker, ($observed | ConvertTo-Json -Depth 10))
    $unlocked = @($observed.GetEnumerator() | Where-Object { -not $_.Value })
    if ($unlocked.Count -ne 0) {
        throw "Authenticated bootstrap files were mutable: $(@($unlocked.Key) -join ', ')"
    }

    $manifest = Join-Path $RunRoot 'manifest.json'
    $receipt = Join-Path $RunRoot 'receipt.json'
    $manifestBytes = [Text.UTF8Encoding]::new($false).GetBytes('{"state":"original-manifest"}')
    $receiptBytes = [Text.UTF8Encoding]::new($false).GetBytes('{"state":"original-receipt"}')
    [IO.File]::WriteAllBytes($manifest, $manifestBytes)
    [IO.File]::WriteAllBytes($receipt, $receiptBytes)
    $result = [pscustomobject][ordered]@{
        composite_id = 'fixture-composite'
        run_id = [IO.Path]::GetFileName([IO.Path]::GetFullPath($RunRoot))
        contract_sha256 = $ExpectedContractSha256
        manifest_path = $manifest
        manifest_sha256 = [Convert]::ToHexString(
            [Security.Cryptography.SHA256]::HashData($manifestBytes)).ToLowerInvariant()
        receipt_path = $receipt
        receipt_sha256 = [Convert]::ToHexString(
            [Security.Cryptography.SHA256]::HashData($receiptBytes)).ToLowerInvariant()
    }
    $mutationPath = Join-Path $RepositoryRoot 'result-mutation.fixture'
    if ([IO.File]::Exists($mutationPath)) {
        switch ([IO.File]::ReadAllText($mutationPath)) {
            'extra-property' {
                $result | Add-Member -NotePropertyName unexpected -NotePropertyValue 'open-shape'
            }
            'invalid-digest' { $result.receipt_sha256 = 'A' * 64 }
            'wrong-composite-id' { $result.composite_id = 'substituted-composite' }
            'wrong-manifest-path' {
                $alternate = Join-Path $RunRoot 'alternate-manifest.json'
                [IO.File]::WriteAllBytes($alternate, $manifestBytes)
                $result.manifest_path = $alternate
            }
            'wrong-receipt-path' {
                $alternate = Join-Path $RunRoot 'alternate-receipt.json'
                [IO.File]::WriteAllBytes($alternate, $receiptBytes)
                $result.receipt_path = $alternate
            }
            default { throw 'Unknown launcher result mutation fixture.' }
        }
    }
    try { return $result }
    finally {
        if ([IO.File]::Exists((Join-Path $RepositoryRoot 'swap-published-evidence.fixture'))) {
            [IO.File]::WriteAllText($manifest, '{"state":"substituted-manifest"}')
            [IO.File]::WriteAllText($receipt, '{"state":"substituted-receipt"}')
        }
    }
}

Export-ModuleMember -Function @(
    'Assert-M0QtShellCompositeContract',
    'Assert-M0QtShellCompositeReceipt',
    'Invoke-M0QtShellComposite'
)
'@
    [IO.File]::WriteAllText($orchestrator, $orchestratorSource, $utf8NoBom)

    $recipe = @(
        [pscustomobject][ordered]@{
            id = 'orchestrator'
            relative_path = 'experiments/m0/provisioning/qt-shell/M0.QtShellComposite.psm1'
            size_bytes = [long](Get-Item -LiteralPath $orchestrator).Length
            sha256 = Get-Sha256 $orchestrator
        },
        [pscustomobject][ordered]@{
            id = 'runner'
            relative_path = 'experiments/m0/provisioning/qt-shell/M0.QtShellCompositeNative.psm1'
            size_bytes = [long](Get-Item -LiteralPath $runner).Length
            sha256 = Get-Sha256 $runner
        },
        [pscustomobject][ordered]@{
            id = 'entrypoint'
            relative_path = 'experiments/m0/provisioning/qt-shell/Invoke-M0QtShellComposite.ps1'
            size_bytes = [long](Get-Item -LiteralPath $launcher).Length
            sha256 = Get-Sha256 $launcher
        }
    )
    $document = [pscustomobject][ordered]@{
        schema_version = [long]2
        composite_id = 'fixture-composite'
        provenance = [pscustomobject][ordered]@{
            qtbase_refresh = [pscustomobject][ordered]@{
                receipt_relative_path = 'q/parent/evidence/qtbase-refresh-receipt.v2.json'
                receipt_size_bytes = [long]1
                receipt_sha256 = $qtBaseReceiptSha256
            }
        }
        recipe = [pscustomobject][ordered]@{
            runner_export = 'Invoke-M0QtShellCompositeNative'
            files = $recipe
        }
        isolation = [pscustomobject][ordered]@{
            sdk_relative_path = 'sdk'
            global_inventory_relative_path = 'evidence/composite-inventory.tsv'
            manifest_relative_path = 'manifest.json'
            receipt_relative_path = 'receipt.json'
        }
        limits = @('fixture-only')
    }
    [IO.File]::WriteAllText($contract, ($document | ConvertTo-Json -Depth 20), $utf8NoBom)
    $contractSha256 = Get-Sha256 $contract
    $document.provenance.qtbase_refresh.receipt_sha256 = '0' * 64
    [IO.File]::WriteAllText($maliciousContract, ($document | ConvertTo-Json -Depth 20), $utf8NoBom)

    $verificationReceiptPath = Join-Path $root 'q/verified/evidence/qt-shell-composite-receipt.v2.json'
    $verificationReceiptSha256 = '1' * 64
    if ($ForVerification) {
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($verificationReceiptPath))
        # Deliberately differs from the external digest. The authenticated fake verifier records
        # the argument; the launcher must never replace it with a path-derived digest.
        [IO.File]::WriteAllText($verificationReceiptPath, '{"fixture":"receipt-bytes"}', $utf8NoBom)
    }

    return [pscustomobject]@{
        root = $root
        launcher = $launcher
        contract = $contract
        malicious_contract = $maliciousContract
        contract_sha256 = $contractSha256
        qtbase_receipt_sha256 = $qtBaseReceiptSha256
        run_root = Join-Path $root 'q/run'
        verification_receipt_path = $verificationReceiptPath
        verification_receipt_sha256 = $verificationReceiptSha256
    }
}

function Get-LauncherArguments {
    param([Parameter(Mandatory)] $Fixture)
    return @(
        '-File', $Fixture.launcher,
        '-ContractPath', $Fixture.contract,
        '-ExpectedContractSha256', $Fixture.contract_sha256,
        '-ExpectedQtBaseReceiptSha256', $Fixture.qtbase_receipt_sha256,
        '-RepositoryRoot', $Fixture.root,
        '-RunRoot', $Fixture.run_root
    )
}

function Invoke-LauncherSerialized {
    param([Parameter(Mandatory)] $Fixture)

    $wrapper = Join-Path $Fixture.root 'invoke-launcher-serialized.ps1'
    $launcherLiteral = ConvertTo-SingleQuotedLiteral $Fixture.launcher
    $contractLiteral = ConvertTo-SingleQuotedLiteral $Fixture.contract
    $hashLiteral = ConvertTo-SingleQuotedLiteral $Fixture.contract_sha256
    $receiptLiteral = ConvertTo-SingleQuotedLiteral $Fixture.qtbase_receipt_sha256
    $rootLiteral = ConvertTo-SingleQuotedLiteral $Fixture.root
    $runLiteral = ConvertTo-SingleQuotedLiteral $Fixture.run_root
    $wrapperSource = @"
`$ErrorActionPreference = 'Stop'
`$result = & $launcherLiteral -ContractPath $contractLiteral -ExpectedContractSha256 $hashLiteral ``
    -ExpectedQtBaseReceiptSha256 $receiptLiteral -RepositoryRoot $rootLiteral -RunRoot $runLiteral
`$result | ConvertTo-Json -Compress
"@
    [IO.File]::WriteAllText($wrapper, $wrapperSource, $utf8NoBom)
    return Invoke-ChildPowerShell -Arguments @('-File', $wrapper)
}

function Invoke-VerificationSerialized {
    param([Parameter(Mandatory)] $Fixture)

    $wrapper = Join-Path $Fixture.root 'invoke-verification-serialized.ps1'
    $launcherLiteral = ConvertTo-SingleQuotedLiteral $Fixture.launcher
    $contractLiteral = ConvertTo-SingleQuotedLiteral $Fixture.contract
    $contractHashLiteral = ConvertTo-SingleQuotedLiteral $Fixture.contract_sha256
    $qtBaseReceiptLiteral = ConvertTo-SingleQuotedLiteral $Fixture.qtbase_receipt_sha256
    $rootLiteral = ConvertTo-SingleQuotedLiteral $Fixture.root
    $receiptLiteral = ConvertTo-SingleQuotedLiteral $Fixture.verification_receipt_path
    $receiptHashLiteral = ConvertTo-SingleQuotedLiteral $Fixture.verification_receipt_sha256
    $wrapperSource = @"
`$ErrorActionPreference = 'Stop'
`$result = & $launcherLiteral -ContractPath $contractLiteral ``
    -ExpectedContractSha256 $contractHashLiteral ``
    -ExpectedQtBaseReceiptSha256 $qtBaseReceiptLiteral ``
    -RepositoryRoot $rootLiteral -ReceiptPath $receiptLiteral ``
    -ExpectedReceiptSha256 $receiptHashLiteral
`$result | ConvertTo-Json -Compress
"@
    [IO.File]::WriteAllText($wrapper, $wrapperSource, $utf8NoBom)
    return Invoke-ChildPowerShell -Arguments @('-File', $wrapper)
}

function Assert-LauncherRejected {
    param(
        [Parameter(Mandatory)] $Fixture,
        [Parameter(Mandatory)] [string] $Pattern,
        [Parameter(Mandatory)] [string] $Context
    )

    $result = Invoke-ChildPowerShell -Arguments (Get-LauncherArguments $Fixture)
    Assert-True ($result.exit_code -ne 0) "$Context was accepted by the production launcher."
    Assert-True (($result.stderr + $result.stdout) -match $Pattern) `
        "$Context failed without a causal diagnostic. stderr: $($result.stderr) stdout: $($result.stdout)"
}

Test-Case 'launcher leases contract and all recipe files through import and invocation' {
    $fixture = New-LauncherFixture
    try {
        $result = Invoke-ChildPowerShell -Arguments (Get-LauncherArguments $fixture)
        Assert-True ($result.exit_code -eq 0) `
            "Launcher did not preserve physical leases. stderr: $($result.stderr) stdout: $($result.stdout)"
        $marker = Join-Path $fixture.run_root 'lease-observations.json'
        Assert-True ([IO.File]::Exists($marker)) 'Authenticated orchestrator did not record lease observations.'
        $observed = Get-Content -Raw -LiteralPath $marker | ConvertFrom-Json
        foreach ($property in $observed.PSObject.Properties) {
            Assert-True ($property.Value -eq $true) "Lease observation '$($property.Name)' was false."
        }
    }
    finally {
        if ([IO.Directory]::Exists($fixture.root)) { [IO.Directory]::Delete($fixture.root, $true) }
    }
}

Test-Case 'launcher parses the authenticated contract from its held stream' {
    $fixture = New-LauncherFixture
    try {
        $wrapper = Join-Path $fixture.root 'invoke-with-contract-swap.ps1'
        $launcherLiteral = ConvertTo-SingleQuotedLiteral $fixture.launcher
        $contractLiteral = ConvertTo-SingleQuotedLiteral $fixture.contract
        $maliciousLiteral = ConvertTo-SingleQuotedLiteral $fixture.malicious_contract
        $hashLiteral = ConvertTo-SingleQuotedLiteral $fixture.contract_sha256
        $receiptLiteral = ConvertTo-SingleQuotedLiteral $fixture.qtbase_receipt_sha256
        $rootLiteral = ConvertTo-SingleQuotedLiteral $fixture.root
        $runLiteral = ConvertTo-SingleQuotedLiteral $fixture.run_root
        $wrapperSource = @"
`$ErrorActionPreference = 'Stop'
`$global:M0CompositeSwapHook = `$false
function Get-FileHash {
    param([string] `$LiteralPath, [string] `$Algorithm)
    `$result = Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath `$LiteralPath -Algorithm `$Algorithm
    if (-not `$global:M0CompositeSwapHook -and [string]::Equals(
            [IO.Path]::GetFullPath(`$LiteralPath), [IO.Path]::GetFullPath($contractLiteral),
            [StringComparison]::OrdinalIgnoreCase)) {
        `$global:M0CompositeSwapHook = `$true
        [IO.File]::WriteAllBytes($contractLiteral, [IO.File]::ReadAllBytes($maliciousLiteral))
    }
    return `$result
}
& $launcherLiteral -ContractPath $contractLiteral -ExpectedContractSha256 $hashLiteral ``
    -ExpectedQtBaseReceiptSha256 $receiptLiteral -RepositoryRoot $rootLiteral -RunRoot $runLiteral
"@
        [IO.File]::WriteAllText($wrapper, $wrapperSource, $utf8NoBom)
        $result = Invoke-ChildPowerShell -Arguments @('-File', $wrapper)
        Assert-True ($result.exit_code -eq 0) `
            "Launcher reread a swapped contract path. stderr: $($result.stderr) stdout: $($result.stdout)"
        Assert-True ((Get-Sha256 $fixture.contract) -ceq $fixture.contract_sha256) `
            'The contract swap hook unexpectedly changed the physically leased contract.'
    }
    finally {
        if ([IO.Directory]::Exists($fixture.root)) { [IO.Directory]::Delete($fixture.root, $true) }
    }
}

Test-Case 'launcher preserves core hash anchors and rejects swapped handoff artifacts' {
    $fixture = New-LauncherFixture -SwapPublishedEvidenceAfterReturn
    try {
        $result = Invoke-LauncherSerialized $fixture
        $manifest = Join-Path $fixture.run_root 'manifest.json'
        $receipt = Join-Path $fixture.run_root 'receipt.json'
        Assert-True ([IO.File]::Exists($manifest) -and [IO.File]::Exists($receipt)) `
            'The causal fixture did not publish and substitute both handoff artifacts.'
        $substitutedManifestSha256 = Get-Sha256 $manifest
        $substitutedReceiptSha256 = Get-Sha256 $receipt
        $output = $result.stdout + $result.stderr
        Assert-True ($output -notmatch [regex]::Escape($substitutedManifestSha256) -and
            $output -notmatch [regex]::Escape($substitutedReceiptSha256)) `
            "Launcher emitted a substituted artifact hash as a trusted anchor. output: $output"
        Assert-True ($result.exit_code -ne 0) `
            'Launcher accepted artifacts substituted after the core result was fixed.'
        Assert-True ($output -match 'manifest|receipt|handoff|SHA-256|anchor|changed|differs') `
            "Swapped handoff failed without a causal diagnostic. output: $output"
    }
    finally {
        if ([IO.Directory]::Exists($fixture.root)) { [IO.Directory]::Delete($fixture.root, $true) }
    }
}

Test-Case 'launcher requires the exact closed core result shape and digest format' {
    foreach ($mutation in @('extra-property', 'invalid-digest')) {
        $fixture = New-LauncherFixture -ResultMutation $mutation
        try {
            Assert-LauncherRejected $fixture 'result|property|shape|digest|SHA-256|hash' `
                "Core result mutation '$mutation'"
        }
        finally {
            if ([IO.Directory]::Exists($fixture.root)) { [IO.Directory]::Delete($fixture.root, $true) }
        }
    }
}

foreach ($bindingCase in @(
        [pscustomobject]@{ mutation = 'wrong-composite-id'; label = 'composite identity' },
        [pscustomobject]@{ mutation = 'wrong-manifest-path'; label = 'manifest path' },
        [pscustomobject]@{ mutation = 'wrong-receipt-path'; label = 'receipt path' })) {
    Test-Case "launcher binds core $($bindingCase.label) to the authenticated contract" {
        $mutation = $bindingCase.mutation
        $fixture = New-LauncherFixture -ResultMutation $mutation
        try {
            Assert-LauncherRejected $fixture 'composite|manifest|receipt|path|contract|drift|exact' `
                "Core result binding mutation '$mutation'"
        }
        finally {
            if ([IO.Directory]::Exists($fixture.root)) { [IO.Directory]::Delete($fixture.root, $true) }
        }
    }
}

Test-Case 'independent verification authenticates the verifier and preserves every external hash' {
    $fixture = New-LauncherFixture -ForVerification
    try {
        Assert-True ((Get-Sha256 $fixture.verification_receipt_path) -cne
                $fixture.verification_receipt_sha256) `
            'The causal receipt fixture unexpectedly equals the external hash.'
        $result = Invoke-VerificationSerialized $fixture
        Assert-True ($result.exit_code -eq 0) `
            "Authenticated independent verification failed. stderr: $($result.stderr) stdout: $($result.stdout)"
        $launcherResult = $result.stdout | ConvertFrom-Json
        Assert-True ($launcherResult.contract_sha256 -ceq $fixture.contract_sha256) `
            'The verification result replaced the external composite contract hash.'
        Assert-True ($launcherResult.qtbase_receipt_sha256 -ceq $fixture.qtbase_receipt_sha256) `
            'The verification result replaced the external Qtbase receipt hash.'
        Assert-True ($launcherResult.receipt_sha256 -ceq $fixture.verification_receipt_sha256) `
            'The verification result replaced the external composite receipt hash.'
        Assert-True ($launcherResult.verified -eq $true) `
            'The authenticated launcher did not return an explicit verified result.'
        $marker = Join-Path $fixture.root 'verification-lease-observations.json'
        Assert-True ([IO.File]::Exists($marker)) `
            'The authenticated verifier did not record its lease and anchor observations.'
        $observed = Get-Content -Raw -LiteralPath $marker | ConvertFrom-Json
        foreach ($name in @(
                'orchestrator_import_write_denied', 'contract_write_denied',
                'contract_replacement_denied', 'orchestrator_write_denied',
                'orchestrator_replacement_denied', 'runner_write_denied',
                'runner_replacement_denied', 'entrypoint_write_denied',
                'entrypoint_replacement_denied')) {
            Assert-True ($observed.$name -eq $true) `
                "Independent verifier bootstrap lease observation '$name' was false."
        }
        Assert-True ($observed.expected_receipt_sha256 -ceq $fixture.verification_receipt_sha256) `
            'The launcher replaced the external composite receipt hash.'
        Assert-True ($observed.expected_contract_sha256 -ceq $fixture.contract_sha256) `
            'The launcher replaced the external composite contract hash.'
        Assert-True ($observed.expected_qtbase_receipt_sha256 -ceq $fixture.qtbase_receipt_sha256) `
            'The launcher replaced the external Qtbase receipt hash.'
        Assert-True ($observed.receipt_path -ceq [IO.Path]::GetFullPath($fixture.verification_receipt_path)) `
            'The launcher changed the independently verified receipt path.'
    }
    finally {
        if ([IO.Directory]::Exists($fixture.root)) { [IO.Directory]::Delete($fixture.root, $true) }
    }
}

foreach ($verifierMutation in @(
        'extra-property', 'wrong-order', 'malformed-execution', 'malformed-sdk',
        'extra-evidence-property')) {
    Test-Case "independent verification rejects closed-result mutation $verifierMutation" {
        $fixture = New-LauncherFixture -ForVerification -VerifierResultMutation $verifierMutation
        try {
            $result = Invoke-VerificationSerialized $fixture
            Assert-True ($result.exit_code -ne 0) `
                "Verifier result mutation '$verifierMutation' was accepted. stdout: $($result.stdout)"
            Assert-True (($result.stdout + $result.stderr) -match
                    'verifier|result|shape|property|order|type|execution|sdk|evidence|anchor') `
                "Verifier result mutation '$verifierMutation' failed without a causal diagnostic."
        }
        finally {
            if ([IO.Directory]::Exists($fixture.root)) { [IO.Directory]::Delete($fixture.root, $true) }
        }
    }
}

Test-Case 'launcher rejects ADS and hardlinked authenticated inputs' {
    foreach ($kind in @('ads', 'hardlink')) {
        $fixture = New-LauncherFixture
        try {
            $runner = Join-Path $fixture.root `
                'experiments/m0/provisioning/qt-shell/M0.QtShellCompositeNative.psm1'
            if ($kind -ceq 'ads') {
                [IO.File]::WriteAllText($runner + ':untrusted', 'named stream')
                Assert-LauncherRejected $fixture 'alternate data stream|ADS|stream' 'Recipe ADS'
            }
            else {
                New-Item -ItemType HardLink -Path ($runner + '.alias') -Target $runner | Out-Null
                Assert-LauncherRejected $fixture 'single-link|hardlink|link' 'Recipe hardlink'
            }
        }
        finally {
            if ([IO.Directory]::Exists($fixture.root)) { [IO.Directory]::Delete($fixture.root, $true) }
        }
    }
}

Test-Case 'launcher rejects a reparse point in authenticated ancestry' {
    $fixture = New-LauncherFixture
    try {
        $recipeRoot = Join-Path $fixture.root 'experiments/m0/provisioning/qt-shell'
        $physicalRoot = Join-Path $fixture.root 'physical-qt-shell'
        [IO.Directory]::Move($recipeRoot, $physicalRoot)
        New-Item -ItemType Junction -Path $recipeRoot -Target $physicalRoot | Out-Null
        Assert-LauncherRejected $fixture 'reparse|physical.*alias|junction|symbolic link' `
            'Recipe ancestry reparse point'
    }
    finally {
        if ([IO.Directory]::Exists($recipeRoot) -and
            ((Get-Item -LiteralPath $recipeRoot -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            [IO.Directory]::Delete($recipeRoot)
        }
        if ([IO.Directory]::Exists($fixture.root)) { [IO.Directory]::Delete($fixture.root, $true) }
    }
}

Write-Host "Tests: $($script:State.Passed) passed, $($script:State.Failed) failed."
if ($script:State.Failed -ne 0) { exit 1 }
