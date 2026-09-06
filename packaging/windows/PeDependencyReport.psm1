#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Pure parsing of the English dumpbin report; callers own process execution and
# dependency resolution. Keep the historical public name for existing consumers.
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

Export-ModuleMember -Function ConvertFrom-QtShellPeReport
