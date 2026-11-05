[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$validationErrors = [System.Collections.Generic.List[string]]::new()

function Add-ValidationError {
    param([Parameter(Mandatory)][string]$Message)

    $validationErrors.Add($Message)
}

function Get-RelativeRepositoryPath {
    param([Parameter(Mandatory)][string]$FullName)

    return $FullName.Substring($repositoryRoot.Length + 1).Replace('\', '/')
}

Push-Location $repositoryRoot
try {
    $requiredFiles = @(
        'agents.md',
        'README.md',
        'docs/product/requirements.md',
        'docs/architecture/architecture.md',
        'docs/architecture/decisions/README.md',
        'docs/quality/testing-strategy.md',
        'docs/quality/quality-gates.md',
        'docs/development-plan.md',
        'docs/roadmap.md',
        'scripts/validate-docs.ps1'
    )

    foreach ($requiredFile in $requiredFiles) {
        if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
            Add-ValidationError "Arquivo contratual ausente: $requiredFile"
        }
    }

    $markdownRelativePaths = @(& git ls-files --cached --others --exclude-standard -- '*.md')
    if ($LASTEXITCODE -ne 0) {
        Add-ValidationError 'Não foi possível enumerar os Markdown versionados e não ignorados com git ls-files.'
    }
    $markdownFiles = @(
        $markdownRelativePaths |
            Sort-Object -Unique |
            ForEach-Object { Get-Item -LiteralPath (Join-Path $repositoryRoot $_) }
    )

    $linkPattern = [regex]'\[[^\]]*\]\((?<target>[^)]+)\)'
    foreach ($markdownFile in $markdownFiles) {
        $lineNumber = 0
        foreach ($line in Get-Content -LiteralPath $markdownFile.FullName) {
            $lineNumber++
            foreach ($linkMatch in $linkPattern.Matches($line)) {
                $target = $linkMatch.Groups['target'].Value.Trim()
                if ($target.StartsWith('<') -and $target.EndsWith('>')) {
                    $target = $target.Substring(1, $target.Length - 2)
                }

                if ($target -match '^(https?://|mailto:|#)') {
                    continue
                }

                $pathPart = ($target -split '#', 2)[0]
                if ([string]::IsNullOrWhiteSpace($pathPart)) {
                    continue
                }

                $resolvedTarget = Join-Path $markdownFile.DirectoryName $pathPart
                if (-not (Test-Path -LiteralPath $resolvedTarget)) {
                    $relativeFile = Get-RelativeRepositoryPath $markdownFile.FullName
                    Add-ValidationError "Link relativo quebrado em ${relativeFile}:${lineNumber}: $target"
                }
            }
        }
    }

    $requirementPattern = 'FR-\d{3}|UX-\d{3}|NFR-[A-Z]+-\d{3}'
    $requirementLines = Get-Content -LiteralPath 'docs/product/requirements.md'
    $requirementIds = @(
        $requirementLines | ForEach-Object {
            if ($_ -match "^\| ($requirementPattern) \|") {
                $Matches[1]
            }
        }
    )

    $duplicateRequirementIds = @(
        $requirementIds | Group-Object | Where-Object Count -ne 1 | ForEach-Object Name
    )
    if ($duplicateRequirementIds.Count -gt 0) {
        Add-ValidationError "IDs de requisito duplicados: $($duplicateRequirementIds -join ', ')"
    }

    $functionalCount = @($requirementIds | Where-Object { $_ -like 'FR-*' }).Count
    $uxCount = @($requirementIds | Where-Object { $_ -like 'UX-*' }).Count
    $nonFunctionalCount = @($requirementIds | Where-Object { $_ -like 'NFR-*' }).Count
    if ($requirementIds.Count -ne 84 -or $functionalCount -ne 42 -or $uxCount -ne 10 -or $nonFunctionalCount -ne 32) {
        Add-ValidationError "Contagem de requisitos divergente: total=$($requirementIds.Count), FR=$functionalCount, UX=$uxCount, NFR=$nonFunctionalCount"
    }

    $unknownRequirementIds = [System.Collections.Generic.HashSet[string]]::new()
    $referencePattern = [regex]"(?<![A-Z0-9-])($requirementPattern)(?![A-Z0-9-])"
    foreach ($markdownFile in $markdownFiles) {
        $markdownText = [System.IO.File]::ReadAllText($markdownFile.FullName)
        foreach ($referenceMatch in $referencePattern.Matches($markdownText)) {
            $referenceId = $referenceMatch.Groups[1].Value
            if ($referenceId -notin $requirementIds) {
                $null = $unknownRequirementIds.Add($referenceId)
            }
        }
    }
    if ($unknownRequirementIds.Count -gt 0) {
        Add-ValidationError "Referências a requisitos desconhecidos: $(@($unknownRequirementIds) -join ', ')"
    }

    $shorthandPattern = [regex]'(?<![A-Z0-9-])(?<prefix>FR-|UX-|NFR-[A-Z]+-)(?<sequence>\d{3}(?:[/–]\d{3})+)'
    $shorthandPartPattern = [regex]'(?<separator>^|[/–])(?<value>\d{3})'
    $invalidShorthandIds = [System.Collections.Generic.HashSet[string]]::new()
    foreach ($markdownFile in $markdownFiles) {
        $markdownText = [System.IO.File]::ReadAllText($markdownFile.FullName)
        foreach ($shorthandMatch in $shorthandPattern.Matches($markdownText)) {
            $prefix = $shorthandMatch.Groups['prefix'].Value
            $previousNumber = -1
            foreach ($partMatch in $shorthandPartPattern.Matches($shorthandMatch.Groups['sequence'].Value)) {
                $separator = $partMatch.Groups['separator'].Value
                $currentNumber = [int]$partMatch.Groups['value'].Value
                $numbersToValidate = @($currentNumber)
                if ($separator -eq '–') {
                    if ($previousNumber -lt 0 -or $currentNumber -le $previousNumber) {
                        $relativeFile = Get-RelativeRepositoryPath $markdownFile.FullName
                        $null = $invalidShorthandIds.Add("${relativeFile}:$($shorthandMatch.Value)")
                        continue
                    }
                    $numbersToValidate = @(($previousNumber + 1)..$currentNumber)
                }

                foreach ($numberToValidate in $numbersToValidate) {
                    $expandedId = $prefix + $numberToValidate.ToString('000')
                    if ($expandedId -notin $requirementIds) {
                        $relativeFile = Get-RelativeRepositoryPath $markdownFile.FullName
                        $null = $invalidShorthandIds.Add("${relativeFile}:$($shorthandMatch.Value)->$expandedId")
                    }
                }
                $previousNumber = $currentNumber
            }
        }
    }
    if ($invalidShorthandIds.Count -gt 0) {
        Add-ValidationError "Intervalos/listas de requisitos inválidos: $(@($invalidShorthandIds) -join ', ')"
    }

    $planLines = Get-Content -LiteralPath 'docs/development-plan.md'
    $matrixStart = [Array]::IndexOf($planLines, '### 15.1 Destino individual dos 84 requisitos')
    $matrixEnd = [Array]::IndexOf($planLines, '## 16. Lacunas que o plano fecha explicitamente')
    $matrixDestinations = @{}
    if ($matrixStart -lt 0 -or $matrixEnd -le $matrixStart) {
        Add-ValidationError 'A matriz individual de rastreabilidade não foi encontrada no plano.'
    }
    else {
        $matrixRowPattern = [regex]('^\| `(' + $requirementPattern + ')` \|')
        $matrixIds = @(
            $planLines[($matrixStart + 1)..($matrixEnd - 1)] | ForEach-Object {
                $matrixMatch = $matrixRowPattern.Match($_)
                if ($matrixMatch.Success) {
                    $matrixId = $matrixMatch.Groups[1].Value
                    $matrixColumns = $_ -split '\|'
                    $matrixDestinations[$matrixId] = $matrixColumns[2].Trim()
                    $matrixId
                }
            }
        )
        $missingMatrixIds = @($requirementIds | Where-Object { $_ -notin $matrixIds })
        $extraMatrixIds = @($matrixIds | Where-Object { $_ -notin $requirementIds })
        $duplicateMatrixIds = @(
            $matrixIds | Group-Object | Where-Object Count -ne 1 | ForEach-Object Name
        )
        if ($matrixIds.Count -ne $requirementIds.Count -or $missingMatrixIds.Count -gt 0 -or $extraMatrixIds.Count -gt 0 -or $duplicateMatrixIds.Count -gt 0) {
            Add-ValidationError "Matriz inválida: linhas=$($matrixIds.Count); ausentes=$($missingMatrixIds -join ','); extras=$($extraMatrixIds -join ','); duplicados=$($duplicateMatrixIds -join ',')"
        }
    }

    $definedChecks = @(
        $planLines | ForEach-Object {
            if ($_ -match '^\| `(CHK-[A-Z0-9]+)` \|') {
                $Matches[1]
            }
        }
    )
    $usedChecks = @(
        [regex]::Matches(($planLines -join "`n"), 'CHK-[A-Z0-9]+') |
            ForEach-Object Value |
            Sort-Object -Unique
    )
    $undefinedChecks = @($usedChecks | Where-Object { $_ -notin $definedChecks })
    $duplicateChecks = @($definedChecks | Group-Object | Where-Object Count -ne 1 | ForEach-Object Name)
    if ($undefinedChecks.Count -gt 0 -or $duplicateChecks.Count -gt 0) {
        Add-ValidationError "Catálogo de checks inválido: não definidos=$($undefinedChecks -join ','); duplicados=$($duplicateChecks -join ',')"
    }

    $stepIds = @()
    foreach ($planLine in $planLines) {
        if ($planLine -match '^\| ((?:D0|M[0-6]|R1)\.\d+(?:\.\d+)?) \|') {
            $stepIds += $Matches[1]
            if ($planLine -notmatch 'CHK-CLOSE') {
                Add-ValidationError "Etapa sem CHK-CLOSE explícito: $($Matches[1])"
            }
        }
    }
    $duplicateStepIds = @($stepIds | Group-Object | Where-Object Count -ne 1 | ForEach-Object Name)
    if ($duplicateStepIds.Count -gt 0) {
        Add-ValidationError "IDs de etapa duplicados: $($duplicateStepIds -join ', ')"
    }

    $stepReferencePattern = [regex]'(?<![A-Z0-9.])((?:D\d+|M\d+|R\d+)\.\d+(?:\.\d+)?)(?![A-Z0-9.])'
    $referencedStepIds = @(
        $stepReferencePattern.Matches(($planLines -join "`n")) |
            ForEach-Object { $_.Groups[1].Value } |
            Sort-Object -Unique
    )
    $unknownStepIds = @($referencedStepIds | Where-Object { $_ -notin $stepIds })
    if ($unknownStepIds.Count -gt 0) {
        Add-ValidationError "Referências a etapas desconhecidas: $($unknownStepIds -join ', ')"
    }

    foreach ($matrixDestination in $matrixDestinations.GetEnumerator()) {
        if ($matrixDestination.Value -eq 'Backlog') {
            continue
        }
        $destinationStepIds = @(
            $stepReferencePattern.Matches($matrixDestination.Value) |
                ForEach-Object { $_.Groups[1].Value }
        )
        if ($destinationStepIds.Count -eq 0) {
            Add-ValidationError "Destino sem etapa concreta para $($matrixDestination.Key): $($matrixDestination.Value)"
        }
    }

    $allowedAdrStates = @('Proposto', 'Aceito', 'Substituído', 'Rejeitado')
    $adrFiles = @(
        Get-ChildItem -LiteralPath 'docs/architecture/decisions' -File -Filter '*.md' |
            Where-Object { $_.Name -match '^\d{4}-' }
    )
    foreach ($adrFile in $adrFiles) {
        $stateLines = @(
            Get-Content -LiteralPath $adrFile.FullName | ForEach-Object {
                if ($_ -match '^\*\*Estado:\*\*\s*(.+?)\s*$') {
                    $Matches[1].Trim()
                }
            }
        )
        if ($stateLines.Count -ne 1 -or $stateLines[0] -notin $allowedAdrStates) {
            Add-ValidationError "Estado ADR inválido ou ausente em $($adrFile.Name): $($stateLines -join ', ')"
        }
    }

    $workingDiffCheck = @(& git diff --check 2>&1)
    if ($LASTEXITCODE -ne 0) {
        Add-ValidationError "git diff --check falhou: $($workingDiffCheck -join ' | ')"
    }

    $stagedDiffCheck = @(& git diff --cached --check 2>&1)
    if ($LASTEXITCODE -ne 0) {
        Add-ValidationError "git diff --cached --check falhou: $($stagedDiffCheck -join ' | ')"
    }

    if ($validationErrors.Count -eq 0) {
        Write-Host "OK: $($markdownFiles.Count) Markdown, $($requirementIds.Count) requisitos, $($definedChecks.Count) checks e $($stepIds.Count) etapas validados."
    }
    else {
        foreach ($validationError in $validationErrors) {
            Write-Error $validationError -ErrorAction Continue
        }
    }
}
finally {
    Pop-Location
}

if ($validationErrors.Count -gt 0) {
    exit 1
}
