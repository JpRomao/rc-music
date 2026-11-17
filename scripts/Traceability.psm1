#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-TraceFields($Value, [string[]] $Required, [string[]] $Optional = @()) {
    if ($Value -isnot [Collections.IDictionary]) { throw 'Expected object fields' }
    if (@($Required | Where-Object { $_ -cnotin @($Value.Keys) }).Count -or
        @($Value.Keys | Where-Object { $_ -cnotin ($Required + $Optional) }).Count) { throw 'Invalid object fields' }
}

function Assert-TraceArray($Value, [string] $Name) {
    if ($Value -isnot [array]) { throw "$Name must be an array" }
}

function Assert-TraceJsonKeys([Text.Json.JsonElement] $Element) {
    if ($Element.ValueKind -eq [Text.Json.JsonValueKind]::Object) {
        $keys = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($property in $Element.EnumerateObject()) {
            if (-not $keys.Add($property.Name)) { throw "Duplicate JSON key: $($property.Name)" }
            Assert-TraceJsonKeys $property.Value
        }
    } elseif ($Element.ValueKind -eq [Text.Json.JsonValueKind]::Array) {
        foreach ($item in $Element.EnumerateArray()) { Assert-TraceJsonKeys $item }
    }
}

function Read-TraceFile([string] $Root, $Path) {
    # Only durable project files belong in this inventory, never output or personal data.
    $allowed = '^(?:(?:docs|tests|scripts|src|include|cmake|tools|packaging|experiments)/[^\\:]+|README\.md|agents\.md|CMakeLists\.txt)$'
    if ($Path -isnot [string] -or $Path -cnotmatch $allowed -or
        $Path -match '[\x00-\x1f<>"|?*]' -or @($Path.Split('/') | Where-Object { $_ -in @('', '.', '..') -or $_ -match '[. ]$' }).Count) {
        throw "Invalid repository reference path: $Path"
    }
    $full = [IO.Path]::GetFullPath((Join-Path $Root $Path))
    $prefix = $Root.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Reference path escapes repository' }
    $ancestor = $full
    while ($ancestor) {
        if ((Test-Path -LiteralPath $ancestor) -and
            ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Reference path traverses a reparse point'
        }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { throw "Reference file unavailable: $Path" }
    return [IO.File]::ReadAllText($full)
}

function Get-RcMusicTraceabilityReport {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string] $RepositoryRoot)
    $root = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\', '/')
    $json = Read-TraceFile $root 'docs/quality/traceability.json'
    $document = [Text.Json.JsonDocument]::Parse($json)
    try { Assert-TraceJsonKeys $document.RootElement } finally { $document.Dispose() }
    $manifest = ConvertFrom-Json -InputObject $json -AsHashtable
    Assert-TraceFields $manifest @('schemaVersion', 'requirementsFile', 'planFile', 'records')
    if ($manifest.schemaVersion -isnot [long] -or $manifest.schemaVersion -ne 1) { throw 'Unsupported traceability schema version' }
    if ($manifest.requirementsFile -isnot [string] -or $manifest.requirementsFile -cne 'docs/product/requirements.md') { throw 'Invalid requirementsFile' }
    if ($manifest.planFile -isnot [string] -or $manifest.planFile -cne 'docs/development-plan.md') { throw 'Invalid planFile' }
    Assert-TraceArray $manifest.records 'records'

    $requirements = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::Ordinal)
    $idPattern = '(FR-\d{3}|UX-\d{3}|NFR-[A-Z]+-\d{3})'
    foreach ($line in (Read-TraceFile $root $manifest.requirementsFile) -split '\r?\n') {
        if ($line -cmatch ('^\| ' + $idPattern + ' \|')) {
            $id = $Matches[1]
            if ($requirements.ContainsKey($id)) { throw "Duplicate requirement: $id" }
            $requirements.Add($id, ($line -split '\|')[2].Trim())
        }
    }
    if ($requirements.Count -eq 0) { throw 'Empty requirement inventory' }
    $planText = Read-TraceFile $root $manifest.planFile
    $start = $planText.IndexOf('### 15.1 Destino individual dos 94 requisitos', [StringComparison]::Ordinal)
    $end = $planText.IndexOf('## 16. Lacunas que o plano fecha explicitamente', [StringComparison]::Ordinal)
    if ($start -lt 0 -or $end -le $start) { throw 'Plan matrix unavailable' }
    $destinations = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::Ordinal)
    foreach ($line in $planText.Substring($start, $end - $start) -split '\r?\n') {
        if ($line -cmatch ('^\| `' + $idPattern + '` \|')) {
            $id = $Matches[1]
            if ($destinations.ContainsKey($id)) { throw "Duplicate matrix ID: $id" }
            $destinations.Add($id, ($line -split '\|')[2].Trim())
        }
    }
    if ($destinations.Count -ne $requirements.Count -or
        @($requirements.Keys | Where-Object { -not $destinations.ContainsKey($_) }).Count) { throw 'Plan inventory mismatch' }

    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $counts = [ordered]@{ 'planejado' = 0; 'em andamento' = 0; 'comprovado' = 0; 'excepcionado' = 0; 'fora de escopo' = 0 }
    $cache = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::Ordinal)
    $reverse = [ordered]@{}
    foreach ($row in $manifest.records) {
        Assert-TraceFields $row @('id', 'plan', 'status', 'verification', 'tests', 'evidence', 'remaining') @('decision')
        if ($row.id -isnot [string] -or -not $requirements.ContainsKey($row.id)) { throw "Unknown inventory ID: $($row.id)" }
        if (-not $seen.Add($row.id)) { throw "Duplicate manifest ID: $($row.id)" }
        $id = $row.id
        if ($row.plan -isnot [string] -or $row.plan -cne $destinations[$id]) { throw "Wrong destination for $id" }
        if ($row.status -isnot [string] -or $row.status -cnotin @($counts.Keys)) { throw "Invalid status for $id" }
        if ($row.verification -isnot [string] -or $row.verification -cnotin @('automated', 'manual', 'documentary', 'mixed', 'future')) { throw "Invalid verification for $id" }
        foreach ($field in @('tests', 'evidence', 'remaining')) { Assert-TraceArray $row[$field] "$id $field" }
        if ($row.evidence.Count -eq 0) { throw "Missing evidence for $id" }
        foreach ($remaining in $row.remaining) {
            if ($remaining -isnot [string] -or [string]::IsNullOrWhiteSpace($remaining)) { throw "Invalid remaining work for $id" }
        }
        if (($row.status -ceq 'comprovado') -ne ($row.remaining.Count -eq 0)) { throw "Inconsistent remaining work for $id" }
        if ($row.status -ceq 'comprovado') {
            if ($row.verification -ceq 'future') { throw "Future work cannot be proven: $id" }
            if ($row.verification -in @('automated', 'mixed') -and $row.tests.Count -eq 0) { throw "Missing tests for $id" }
        }
        $kinds = @('tests', 'evidence')
        if ($row.status -in @('excepcionado', 'fora de escopo')) {
            if (-not $row.Contains('decision')) { throw "Explicit decision required for $id" }
            Assert-TraceFields $row.decision @('path', 'marker')
            if ($id.StartsWith('FR-') -and $requirements[$id] -ceq 'P0') { throw "P0 functional requirement cannot be waived: $id" }
            $kinds += 'decision'
        } elseif ($row.Contains('decision')) { throw "Unexpected decision for $id" }
        foreach ($kind in $kinds) {
            $references = if ($kind -ceq 'decision') { @($row.decision) } else { $row[$kind] }
            foreach ($reference in $references) {
                Assert-TraceFields $reference @('path', 'marker')
                if ($reference.marker -isnot [string] -or [string]::IsNullOrWhiteSpace($reference.marker)) { throw "Invalid marker for $id" }
                if ($reference.path -isnot [string]) { throw "Invalid reference path for $id" }
                if (-not $cache.ContainsKey($reference.path)) { $cache.Add($reference.path, (Read-TraceFile $root $reference.path)) }
                if (-not $cache[$reference.path].Contains($reference.marker, [StringComparison]::Ordinal)) { throw "Missing literal marker for $id in $($reference.path): $($reference.marker)" }
                if ($kind -ceq 'decision' -and (-not $reference.path.StartsWith('docs/') -or -not $reference.marker.Contains($id))) {
                    throw "Decision must cite $id explicitly in a project document"
                }
                # JSON tuple encoding avoids collisions between path, marker and kind.
                $key = ConvertTo-Json -InputObject @($kind, $reference.path, $reference.marker) -Compress
                if (-not $reverse.Contains($key)) {
                    $reverse[$key] = [ordered]@{ kind = $kind; path = $reference.path; marker = $reference.marker
                        requirements = [Collections.Generic.List[string]]::new() }
                }
                if (-not $reverse[$key].requirements.Contains($id)) { $reverse[$key].requirements.Add($id) }
            }
        }
        $counts[$row.status]++
    }
    if ($seen.Count -ne $requirements.Count) { throw 'Manifest inventory is incomplete' }
    return [pscustomobject]@{
        schemaVersion = 1; total = $requirements.Count; statusCounts = $counts
        allRequirementsProven = ($counts['comprovado'] -eq $requirements.Count)
        records = $manifest.records; references = @($reverse.Values)
    }
}

Export-ModuleMember -Function Get-RcMusicTraceabilityReport
