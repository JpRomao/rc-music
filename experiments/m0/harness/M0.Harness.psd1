@{
    RootModule = 'M0.Harness.psm1'
    ModuleVersion = '0.1.0'
    GUID = '384b53d1-25d7-41a1-a1c4-cfb3b7b192ec'
    Author = 'Music Player project'
    Description = 'Repository-local contracts and measurement helpers for the disposable M0 comparison.'
    PowerShellVersion = '7.6.5'
    FunctionsToExport = @(
        'Resolve-M0ScopedPath',
        'Get-M0Sha256',
        'Assert-M0FileHash',
        'Assert-M0HarnessManifest',
        'Assert-M0JsonDocument',
        'Assert-M0ProtocolContract',
        'Assert-M0EnvironmentContract',
        'Get-M0CandidateRoot',
        'Assert-M0NoDynamicExecution',
        'Assert-M0Layout',
        'Get-M0TrustedCommonInputs',
        'New-M0HarnessManifest'
    )
    CmdletsToExport = @()
    VariablesToExport = @()
    AliasesToExport = @()
    PrivateData = @{
        PSData = @{
            Tags = @('M0', 'benchmark', 'TDD')
        }
    }
}
