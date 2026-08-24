[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('root', 'child', 'grandchild')]
    [string] $Role,

    [Parameter(Mandatory)]
    [ValidatePattern('^[a-f0-9]{32}$')]
    [string] $Nonce,

    [Parameter(Mandatory)]
    [string] $ReadyRoot,

    [Parameter(Mandatory)]
    [uint32] $ParentPid
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion -ne [Version]'7.6.5') {
    throw [PlatformNotSupportedException]::new("M0-E1 requires PowerShell 7.6.5; found $($PSVersionTable.PSVersion).")
}

$readyRootPath = [IO.Path]::GetFullPath($ReadyRoot).TrimEnd('\', '/')
if (-not (Test-Path -LiteralPath $readyRootPath -PathType Container)) {
    throw [IO.DirectoryNotFoundException]::new('M0P050 synthetic process ready root is missing.')
}
$readyRootItem = Get-Item -LiteralPath $readyRootPath -Force
if (($readyRootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
    ($readyRootItem.PSObject.Properties['LinkType'] -and -not [string]::IsNullOrEmpty([string]$readyRootItem.LinkType))) {
    throw [IO.InvalidDataException]::new('M0P050 synthetic process ready root cannot be a link.')
}

$nextRole = switch ($Role) {
    'root' { 'child' }
    'child' { 'grandchild' }
    'grandchild' { $null }
}
$child = $null
if ($null -ne $nextRole) {
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = (Get-Process -Id $PID).Path
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    foreach ($argument in @(
        '-NoLogo', '-NoProfile', '-NonInteractive', '-File', $PSCommandPath,
        '-Role', $nextRole, '-Nonce', $Nonce, '-ReadyRoot', $readyRootPath,
        '-ParentPid', [string]$PID
    )) {
        $null = $startInfo.ArgumentList.Add($argument)
    }
    $child = [Diagnostics.Process]::Start($startInfo)
}

$nonceBytes = [Text.Encoding]::UTF8.GetBytes($Nonce)
$nonceHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($nonceBytes)).ToLowerInvariant()
$markerPath = Join-Path $readyRootPath "$Role.ready.json"
$temporaryMarkerPath = Join-Path $readyRootPath "$Role.$PID.ready.tmp"
$markerText = "{`"role`":`"$Role`",`"pid`":$PID,`"parent_pid`":$ParentPid,`"nonce_sha256`":`"$nonceHash`"}`n"
$markerBytes = [Text.UTF8Encoding]::new($false).GetBytes($markerText)
$stream = [IO.FileStream]::new($temporaryMarkerPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
try {
    $stream.Write($markerBytes, 0, $markerBytes.Length)
    $stream.Flush($true)
}
finally {
    $stream.Dispose()
}
[IO.File]::Move($temporaryMarkerPath, $markerPath, $false)

$exitGatePath = Join-Path $readyRootPath "$Role.exit"
try {
    while ($true) {
        if (Test-Path -LiteralPath $exitGatePath -PathType Leaf) { break }
        [Threading.Thread]::Sleep(25)
    }
}
finally {
    if ($null -ne $child) {
        $child.Dispose()
    }
}
