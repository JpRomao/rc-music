Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..'))
$contractRoot = Join-Path $repositoryRoot 'experiments\m0\provisioning\qt-shell'
$schemaPath = Join-Path $contractRoot 'qt-shell-toolchain.schema.json'
$evidenceSchemaPath = Join-Path $contractRoot 'qt-shell-toolchain-evidence.schema.json'
$stageManifestSchemaPath = Join-Path $contractRoot 'qt-shell-stage-manifest.schema.json'
$descriptorPath = Join-Path $contractRoot 'qt-shell-toolchain.v1.json'
$goldenPath = Join-Path $contractRoot 'qt-shell-toolchain-golden.v1.json'
$stageManifestPath = Join-Path $contractRoot 'qt-shell-stage-manifest.v1.json'
$contractPath = Join-Path $contractRoot 'qt-shell-toolchain-contract.v1.json'
$contractSidecarPath = "$contractPath.sha256"
$modulePath = Join-Path $contractRoot 'M0.QtShellProvisioning.psm1'
$validatorPath = Join-Path $contractRoot 'Test-M0QtShellToolchain.ps1'
$candidateDescriptorPath = Join-Path $repositoryRoot 'experiments\m0\candidates\qt\candidate.v1.json'
$candidateShellPath = Join-Path $repositoryRoot 'experiments\m0\candidates\qt\shell'
$candidateDescriptorSha256 = 'ada293f2458636cf1460991a1ef6b70cbb77cbe53d377ef14d439b2df5a14128'
$m01HarnessManifestSha256 = '3cc3889e84918d68f3ba72ded0c43d42eeeb7f2871874fdff38b45f43a8dd490'
$expectedContractSha256 = 'ac00a56584f2aed24732d44f9b26107084d37a73467fc56a860feeb850181952'
$expectedContractLeafPaths = @(
    'experiments/m0/provisioning/qt-shell/qt-shell-toolchain.schema.json',
    'experiments/m0/provisioning/qt-shell/qt-shell-toolchain-evidence.schema.json',
    'experiments/m0/provisioning/qt-shell/qt-shell-stage-manifest.schema.json',
    'experiments/m0/provisioning/qt-shell/qt-shell-toolchain.v1.json',
    'experiments/m0/provisioning/qt-shell/qt-shell-toolchain-golden.v1.json',
    'experiments/m0/provisioning/qt-shell/qt-shell-stage-manifest.v1.json',
    'experiments/m0/provisioning/qt-shell/M0.QtShellProvisioning.psm1',
    'experiments/m0/provisioning/qt-shell/Test-M0QtShellToolchain.ps1'
)

$expectedArchiveIds = @(
    'qt-d3dcompiler-47-x64',
    'qt-opengl32sw-mesa-11.2.2',
    'qtbase',
    'qtdeclarative',
    'qtsvg',
    'qttools',
    'qttranslations'
)

$expectedArchiveSha256 = @(
    'b1c379f739fa59a495a388c0878757e3d90fb72e0df771237a1ee4a2c1675d15',
    '59a086716cfd4bdec035698d501ba45526f8d2ecb5c0c428a356d8be0b5d2c79',
    'fd984b7264361b4dd3fd2a417702ca1258e4086268f2ee6a69b9a393d9c3f6bb',
    '5591ca564c1a9299a45a15b6b1d324c86e0aaed7cf6c47b7e6fe464aecae2587',
    '417f44499c835b2303f3ff78043179bb23442f33d5d2816fbf7e8dbf275b1c89',
    '5f2b387a1f8055102b1388ff4ea743124ed6389d442f75c659728bb5c2a56946',
    '1c9516ce25d286ecf97e7aaaa5c8fa395823f21af5fa3dcb7bd06f648453dd77'
)

$expectedArchiveSizes = @(1380117L, 5474526L, 39618573L, 172484222L, 684353L, 27033907L, 1953635L)
$expectedArchiveSha1 = @(
    '15012129f9503b4cefdb1bc8a82d0ff42efc10e6',
    '753a1934e93a7402ce1584b71212f38c45770d3c',
    '1fca9483183d426c1a3a984f1382382fa51f48de',
    'f3ac5149eb76cb0279ba84e126734cf520214519',
    'f458d7f238795d61a3e53fa5211cf2d6b1aa08ef',
    '0a94da47fb97103660b5bfec7361ae51b05f7323',
    '2826dd1fcb9489d243c25a812f3edd7319ca2e45'
)
$expectedArchiveFileNames = @(
    '6.11.2-0-202608131017d3dcompiler_47-x64.7z',
    '6.11.2-0-202608131017opengl32sw-64-mesa_11_2_2-signed_sha256.7z',
    '6.11.2-0-202608131017qtbase-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z',
    '6.11.2-0-202608131017qtdeclarative-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z',
    '6.11.2-0-202608131017qtsvg-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z',
    '6.11.2-0-202608131017qttools-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z',
    '6.11.2-0-202608131017qttranslations-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z'
)
$expectedHostToolIds = @('cmake', 'ctest', 'ninja', 'clang-format', 'cl')
$expectedQtToolIds = @('qmlformat', 'qmllint', 'windeployqt', 'qmltestrunner')
$expectedQtToolSha256 = @(
    '919c3134408abad8eaa0daf70a2296511419ed66e68e78cab587a7f49ffd8d7a',
    '2c4d5d6c5e280f1bb83be5de0e9ecce8d2d47906274d1fb22ac3bc300744edb6',
    'd7a4db063f16cb420c8f89a8393ae92749b0f08a74736c929f795ff9e7b9395b',
    '76b311512a6850ddf8efab1d6d978952ab4b2d06bcc35b0181c07576f051831a'
)
$expectedQtToolSizes = @(2347832L, 104248L, 274232L, 24888L)
$expectedQtToolLicenseComponentIds = @(
    'SPDXRef-Package-qtdeclarative-qt-tool-qmlformat-dc825212e031',
    'SPDXRef-Package-qtdeclarative-qt-tool-qmllint-dc825212e031',
    'SPDXRef-Package-qtbase-qt-tool-windeployqt-9ce86868aeba',
    'SPDXRef-Package-qtdeclarative-qt-tool-qmltestrunner-dc825212e031'
)
$expectedQtToolLicenseSha256 = @(
    '0753ffa69fa629b4f52ebfe41cb5ea3a142160a1cd4d34aae9ff3f8a052c76e0',
    '0753ffa69fa629b4f52ebfe41cb5ea3a142160a1cd4d34aae9ff3f8a052c76e0',
    '5acdc05bb79c66e31da70c8de4e9d37ba2f1d94199a21f8d430b888a76fe88c0',
    '0753ffa69fa629b4f52ebfe41cb5ea3a142160a1cd4d34aae9ff3f8a052c76e0'
)
$expectedQtToolLicenseSizes = @(3812459L, 3812459L, 2347731L, 3812459L)
$expectedQtToolLicensePaths = @(
    'sbom/qtdeclarative-6.11.2.spdx',
    'sbom/qtdeclarative-6.11.2.spdx',
    'sbom/qtbase-6.11.2.spdx',
    'sbom/qtdeclarative-6.11.2.spdx'
)
$expectedQtArtifactIds = @(
    'qmake',
    'qtpaths',
    'qt6core',
    'qt6gui',
    'qt6network',
    'qt6opengl',
    'qt6qml',
    'qt6qmlcompiler',
    'qt6qmlmeta',
    'qt6qmlmodels',
    'qt6qmlworkerscript',
    'qt6quick',
    'qt6quickcontrols2',
    'qt6quickcontrols2basic',
    'qt6quickcontrols2basicstyleimpl',
    'qt6quickcontrols2impl',
    'qt6quickshapes',
    'qt6quicktemplates2',
    'qt6svg',
    'qt6widgets',
    'qt6test',
    'qt6quicktest',
    'qwindows',
    'qsvg',
    'qtqml-models-plugin',
    'qtqml-plugin',
    'qtqml-workerscript-plugin',
    'qtquick-controls-basic-impl-plugin',
    'basic-style',
    'qtquick-controls-impl-plugin',
    'qtquick-controls-plugin',
    'qtquick-plugin',
    'qtquick-shapes-plugin',
    'qtquick-templates-plugin',
    'qtquick-window-plugin',
    'qttest-plugin'
)
$expectedQtArtifactRelativePaths = @(
    'bin/qmake.exe',
    'bin/qtpaths.exe',
    'bin/Qt6Core.dll',
    'bin/Qt6Gui.dll',
    'bin/Qt6Network.dll',
    'bin/Qt6OpenGL.dll',
    'bin/Qt6Qml.dll',
    'bin/Qt6QmlCompiler.dll',
    'bin/Qt6QmlMeta.dll',
    'bin/Qt6QmlModels.dll',
    'bin/Qt6QmlWorkerScript.dll',
    'bin/Qt6Quick.dll',
    'bin/Qt6QuickControls2.dll',
    'bin/Qt6QuickControls2Basic.dll',
    'bin/Qt6QuickControls2BasicStyleImpl.dll',
    'bin/Qt6QuickControls2Impl.dll',
    'bin/Qt6QuickShapes.dll',
    'bin/Qt6QuickTemplates2.dll',
    'bin/Qt6Svg.dll',
    'bin/Qt6Widgets.dll',
    'bin/Qt6Test.dll',
    'bin/Qt6QuickTest.dll',
    'plugins/platforms/qwindows.dll',
    'plugins/imageformats/qsvg.dll',
    'qml/QtQml/Models/modelsplugin.dll',
    'qml/QtQml/qmlplugin.dll',
    'qml/QtQml/WorkerScript/workerscriptplugin.dll',
    'qml/QtQuick/Controls/Basic/impl/qtquickcontrols2basicstyleimplplugin.dll',
    'qml/QtQuick/Controls/Basic/qtquickcontrols2basicstyleplugin.dll',
    'qml/QtQuick/Controls/impl/qtquickcontrols2implplugin.dll',
    'qml/QtQuick/Controls/qtquickcontrols2plugin.dll',
    'qml/QtQuick/qtquick2plugin.dll',
    'qml/QtQuick/Shapes/qmlshapesplugin.dll',
    'qml/QtQuick/Templates/qtquicktemplates2plugin.dll',
    'qml/QtQuick/Window/quickwindowplugin.dll',
    'qml/QtTest/quicktestplugin.dll'
)
$expectedQtArtifactSizes = @(
    1529144L,
    89912L,
    10363704L,
    9546552L,
    1751352L,
    1988408L,
    5337912L,
    2529592L,
    159032L,
    990008L,
    79672L,
    6569272L,
    103224L,
    1831224L,
    91448L,
    325432L,
    345912L,
    2069304L,
    609080L,
    6497080L,
    382264L,
    311608L,
    991032L,
    39736L,
    31032L,
    31032L,
    31032L,
    31032L,
    31544L,
    31032L,
    41272L,
    31032L,
    31032L,
    31544L,
    31544L,
    31032L
)
$expectedQtArtifactSha256 = @(
    'f28be48b9161d59785ee831ad30ac70371dfd0c075f0e7e686d0becec5aa1e9c',
    'a501a6b7e18dc3bed8c91dc6eb3956199e7a5cbb02bec187f964b33212fe011f',
    '22c113a5644b875b2c85482723cd5d2e6a3a4029169778fcc78adde08ef3b6aa',
    '19265a4e76efc12ef5f5bcf59be6c6a5b9b59ec4c89660797738bd2da7f719cc',
    '75ff0896a618f4f5b602f391eb4917a55fd738b2e751d2cf18244a0de3619184',
    'd1d212853c2a858c00d9560616ba67f6238c2d11ffa5549a4251fcc566d16477',
    '257670ffa28fc84d4ec9f818de27e2e4611d9a0cffcb657179a210931e36671b',
    'e1395e1c39aef740bf2675224c768a83e289bc35977664014eea0b5e942e2b0b',
    'a5d4d0d6bf3bd688bad0c2700091d984c4323c6d8982e57289c82ddd7896d603',
    '47d78068ea9ab3e2fd591ca4976cf413247101a00072f31133c16959e05c4627',
    '5006f5b5d13d5904c9dc265d48ed4ef43f5e81a2efefb89daae7fb835b8f3e49',
    'ed2917b9905a5b867ecc192fa4429170494af0dcdaacc68053fe88948319cd08',
    'd2ea80c71cf74443b45df94cea9287cc38176fed4d5f965b7d639c465d0cfa20',
    '2009b1f7b363dcf1aead4cddac27c8d71e724b910adb63d4ca776ee10a888dfa',
    '1928dc6ccc8cc9757217db43b6a51a9fb4359006cee289c45458790da101e783',
    '2e0c0eac81356301dc6a8beae4bb9ecfa1b4cf6d782010cc72ef7d45bf5945f5',
    '1c5a58d4209f908fa0a67d5c8947fff8753e7967b31bb18bd49296561a99e0f5',
    '547e587424053031c1d240aeb901ff2167d0e1b41e29b7bc63a5b1a70e19860a',
    '1052c17a9df0eb82023e0e4c369bbd0af58e4c5fea3c27a4b5ee83b60c015492',
    'e6d0b7e2e697de6ad985b97f9ed8b381c1c2172fcae87b6ab08455b3b7330f32',
    '43601fc45d2f4e4def09c744914b864a26b82d5193418913191678ebc3a437fb',
    'f8734c5a7050646d1c14be933515a91d75bf945d64a278e0d0c3e19f5e1c0627',
    'd94c584a18829d11fbf3aefd07e7b1e189e0cf063a0515826960037efa7465df',
    '486b395e23e258d03d54cdba045f129e9b1321f9ff12f99fe58293e0351b8caf',
    'd958a874cfe9d13f0541d8a2eca0990af1b50032441cde4e46f8dd61a029a85b',
    '30c9aeb95ad7fcebd5f0932de3013c60070040556839cddfbe9a843e5b278460',
    'ac904a031f0616f36c4094dd52f27eb16e46619cf50a0250ffff96a6530cf5a3',
    '91ab5a302f05955537715471bb07fefd7558b43484526636fa338961e60b9d12',
    '5eddc1e16344d1bf776c1c5df5d769b6d91782c6b3f5e6161d93d8b83f640be0',
    '8af0ed36e2c393e15291891a797ba7a7ded5869903f7687e2ced24ec08ca387a',
    'bdca6f4ecabc04679b1f25b8889a6f35cb7f96baff08f78c42ed4982e87c2b11',
    '70992c374c6de43779482a9bd36d0fa44cff5960f1136a4137b390bd772f99ee',
    'b7eccc8a117764042e775b6bbe20a567ac0ecbfe517ce791f56fa5152ccd7a90',
    '15232cee5bbe5bb6ad66b3a132513311f90feb751303fb8679318ffbc4b17728',
    'c4f4e2203eadd10419ddb2cf0e1cbf913ebbfab46965f215cb4dd233b968454f',
    '1cf567afee52f2f9dacd5681f44df4b459cd0044d36452954af7d691de739ad0'
)
$expectedHostToolVersions = @('3.31.5', '3.31.5', '1.12.1', '19.1.1', '19.43.34808.0')
$expectedHostToolSha256 = @(
    '8893e3f7647e2d1e0b18af84e53f136a976cefbfd2c15aaddc4b934936a3637f',
    '00c10d3167ec15b1dde9c3054cc4483175d03dac6f6fe31dfaeb3e037a152a61',
    '5020138b3757035df9dca9a2243624d5810ffa6ae24444bd95f752cbd1b89123',
    '2443021af562b5e9279dee04362c13cd2c04268a4b2e962f6856b75b503fda16',
    'f2f3609d389341cde8071e22351507c6408215a9fb0874bd00bf882df870cf0f'
)
$expectedHostToolSizes = @(12079056L, 12987856L, 3466696L, 3697232L, 867912L)
$expectedHostToolBases = @('program_files', 'program_files', 'vs2022_community', 'vs2022_community', 'vs2022_community')
$expectedHostToolRelativePaths = @(
    'CMake/bin/cmake.exe',
    'CMake/bin/ctest.exe',
    'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe',
    'VC/Tools/Llvm/x64/bin/clang-format.exe',
    'VC/Tools/MSVC/14.43.34808/bin/Hostx64/x64/cl.exe'
)

$script:passed = 0
$script:failed = 0
$script:liveReceipt = $null

function Assert-True {
    param(
        [Parameter(Mandatory)]
        [bool] $Condition,

        [Parameter(Mandatory)]
        [string] $Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Assert-Equal {
    param(
        [AllowNull()]
        [object] $Actual,

        [AllowNull()]
        [object] $Expected,

        [Parameter(Mandatory)]
        [string] $Message
    )

    if (-not [object]::Equals($Actual, $Expected)) {
        throw "$Message Expected '$Expected', got '$Actual'."
    }
}

function Get-Sha256 {
    param([Parameter(Mandatory)][string] $LiteralPath)

    return (Get-FileHash -LiteralPath $LiteralPath -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Read-JsonDocument {
    param([Parameter(Mandatory)][string] $LiteralPath)

    if (-not (Test-Path -LiteralPath $LiteralPath -PathType Leaf)) {
        throw "Required contract leaf is missing: $([IO.Path]::GetFileName($LiteralPath))"
    }

    return Get-Content -LiteralPath $LiteralPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
}

function Get-JsonStringAtoms {
    param(
        [AllowNull()][object] $Value,
        [string] $Pointer = '$'
    )

    if ($null -eq $Value) { return }
    if ($Value -is [string]) {
        [pscustomobject]@{ pointer = $Pointer; role = 'value'; value = [string]$Value }
        return
    }
    if ($Value -is [Collections.IDictionary]) {
        foreach ($key in $Value.Keys) {
            [pscustomobject]@{ pointer = "$Pointer/<property-name>"; role = 'property-name'; value = [string]$key }
            Get-JsonStringAtoms -Value $Value[$key] -Pointer "$Pointer/$key"
        }
        return
    }
    if ($Value -is [Management.Automation.PSCustomObject]) {
        foreach ($property in $Value.PSObject.Properties) {
            [pscustomobject]@{ pointer = "$Pointer/<property-name>"; role = 'property-name'; value = $property.Name }
            Get-JsonStringAtoms -Value $property.Value -Pointer "$Pointer/$($property.Name)"
        }
        return
    }
    if ($Value -is [Collections.IEnumerable]) {
        $index = 0
        foreach ($item in $Value) {
            Get-JsonStringAtoms -Value $item -Pointer "$Pointer/$index"
            $index++
        }
    }
}

function Test-M0QtShellSensitivePathValue {
    param(
        [Parameter(Mandatory)][AllowEmptyString()][string] $Value,
        [switch] $AllowScriptNormalizationToken
    )

    $candidate = $Value.Trim()
    if ($Value -cmatch '(?i)Joao|file:///|%USERPROFILE%|\$env:USERPROFILE') { return $true }
    if ($Value -cmatch '(?i)(?<![A-Za-z0-9_])[A-Z]:[\\/]') { return $true }
    if ($AllowScriptNormalizationToken -and
        $candidate -cin @('/', '/qml', '\', '\\', '\\?\', '\\?\UNC\')) {
        return $false
    }
    if ($candidate -cmatch '^\\\\[?.]\\.+') { return $true }
    if ($candidate -cmatch '^/(?!/)') { return $true }
    if ($candidate -cmatch '^//[^/\s]+/') { return $true }
    if ($candidate -cmatch '^\\\\(?![?.]\\)[^\\/\s]+[\\/]') { return $true }
    return $false
}

function Copy-JsonDocument {
    param([Parameter(Mandatory)][object] $Value)

    return $Value | ConvertTo-Json -Compress -Depth 100 | ConvertFrom-Json -Depth 100 -DateKind String
}

function Assert-SequenceEqual {
    param(
        [AllowEmptyCollection()]
        [object[]] $Actual,

        [AllowEmptyCollection()]
        [object[]] $Expected,

        [Parameter(Mandatory)]
        [string] $Message
    )

    Assert-Equal -Actual ($Actual | ConvertTo-Json -Compress -Depth 20) `
        -Expected ($Expected | ConvertTo-Json -Compress -Depth 20) -Message $Message
}

function Assert-LicenseEvidenceContract {
    param(
        [Parameter(Mandatory)]
        [object] $License,

        [Parameter(Mandatory)]
        [string] $Context
    )

    $propertyNames = @($License.PSObject.Properties.Name)
    Assert-True ($License.authoritative_url -cmatch '^https://') "$Context lacks an authoritative HTTPS license URL."
    switch ($License.evidence_status) {
        'physical_verified' {
            Assert-True ($License.spdx_expression.Length -gt 0) "$Context lacks an SPDX/license expression."
            Assert-True ($License.component_id.Length -gt 0) "$Context lacks a physical component identifier."
            Assert-True ($License.text_sha256 -cmatch '^[0-9a-f]{64}$') "$Context lacks a canonical physical license digest."
            Assert-True ([long]$License.text_size_bytes -gt 0) "$Context lacks a positive physical license size."
            Assert-True ($License.local_path_base.Length -gt 0) "$Context lacks a physical license path base."
            Assert-True ($License.local_relative_path.Length -gt 0) "$Context lacks a physical license relative path."
        }
        'physical_sbom_noassertion_distribution_blocker' {
            Assert-Equal -Actual $License.spdx_assertion -Expected 'NOASSERTION' `
                -Message "$Context falsely concluded an aggregate component license."
            Assert-Equal -Actual $License.general_terms_url_metadata_only -Expected $true `
                -Message "$Context promoted general Qt licensing terms into component-specific evidence."
            Assert-True ($License.component_id.Length -gt 0) "$Context lacks a physical SBOM component identifier."
            Assert-True ($License.text_sha256 -cmatch '^[0-9a-f]{64}$') "$Context lacks a canonical physical SBOM digest."
            Assert-True ([long]$License.text_size_bytes -gt 0) "$Context lacks a positive physical SBOM size."
            Assert-True ($License.local_path_base.Length -gt 0) "$Context lacks a physical SBOM path base."
            Assert-True ($License.local_relative_path.Length -gt 0) "$Context lacks a physical SBOM relative path."
            Assert-Equal -Actual $License.packaged -Expected $false `
                -Message "$Context was packaged despite a NOASSERTION distribution blocker."
            Assert-Equal -Actual $License.blocker -Expected 'component-sbom-noassertion-blocks-distribution' `
                -Message "$Context lacks its exact NOASSERTION distribution blocker."
            Assert-True ($propertyNames -cnotcontains 'spdx_expression') `
                "$Context falsely promoted general terms into an SPDX expression."
        }
        'metadata_unverified_distribution_blocker' {
            Assert-True ($License.spdx_expression.Length -gt 0) "$Context lacks license metadata."
            Assert-Equal -Actual $License.packaged -Expected $false -Message "$Context was packaged despite unverified distribution metadata."
            Assert-True ($License.blocker.Length -gt 0) "$Context lacks its explicit distribution blocker."
            foreach ($forbidden in @('component_id', 'text_sha256', 'text_size_bytes', 'local_path_base', 'local_relative_path')) {
                Assert-True ($propertyNames -cnotcontains $forbidden) "$Context falsely claims physical field '$forbidden'."
            }
        }
        'metadata_unverified_not_packaged' {
            Assert-True ($License.spdx_expression.Length -gt 0) "$Context lacks license metadata."
            Assert-Equal -Actual $License.packaged -Expected $false -Message "$Context was packaged despite metadata-only licensing."
            Assert-Equal -Actual $License.verification_note `
                -Expected 'host-tool-license-metadata-not-physically-verified-and-tool-not-packaged' `
                -Message "$Context lacks its metadata-only verification note."
            foreach ($forbidden in @('component_id', 'text_sha256', 'text_size_bytes', 'local_path_base', 'local_relative_path')) {
                Assert-True ($propertyNames -cnotcontains $forbidden) "$Context falsely claims physical field '$forbidden'."
            }
        }
        default {
            throw "$Context has unsupported evidence status '$($License.evidence_status)'."
        }
    }
}

function Assert-Throws {
    param(
        [Parameter(Mandatory)]
        [scriptblock] $Action,

        [Parameter(Mandatory)]
        [string] $Pattern
    )

    try {
        & $Action
    }
    catch {
        if ($_.Exception.Message -notmatch $Pattern) {
            throw "Expected error matching '$Pattern', got '$($_.Exception.Message)'."
        }
        return
    }

    throw "Expected action to throw an error matching '$Pattern'."
}

function Test-Case {
    param(
        [Parameter(Mandatory)]
        [string] $Name,

        [Parameter(Mandatory)]
        [scriptblock] $Body
    )

    if (-not [string]::IsNullOrEmpty($env:M0_QT_TEST_CASE) -and $Name -cne $env:M0_QT_TEST_CASE) {
        return
    }

    try {
        & $Body
        $script:passed++
        Write-Output "[PASS] $Name"
    }
    catch {
        $script:failed++
        Write-Output "[FAIL] $Name`n$($_.Exception.Message)"
    }
}

function Assert-M0QtShellNonWindowsFailClosed {
    param([Parameter(Mandatory)][string] $CaseId)

    $missingRoot = Join-Path ([IO.Path]::GetTempPath()) ('music-player-m02-nonwindows-' + $CaseId + '-' + [Guid]::NewGuid().ToString('N'))
    Import-Module $modulePath -ErrorAction Stop
    Assert-Throws {
        Test-M0QtShellToolchain -RepositoryRoot $missingRoot -ExpectedContractSha256 $expectedContractSha256
    } 'M0QT020|Windows.*not.proven|platform.*not.proven'
    Assert-True (-not (Test-Path -LiteralPath $missingRoot)) `
        "The non-Windows fail-closed proof mutated root '$CaseId'."
}

Test-Case 'The Qt shell provisioning contract exists before any candidate scaffold' {
    Assert-True (Test-Path -LiteralPath $schemaPath -PathType Leaf) `
        'Expected closed Qt shell toolchain schema before provisioning validation.'
    Assert-True (Test-Path -LiteralPath $descriptorPath -PathType Leaf) `
        'Expected canonical Qt shell toolchain descriptor before provisioning validation.'
    Assert-True (Test-Path -LiteralPath $evidenceSchemaPath -PathType Leaf) `
        'Expected a closed synthetic-golden/live-receipt evidence schema.'
    Assert-True (Test-Path -LiteralPath $goldenPath -PathType Leaf) `
        'Expected canonical Qt shell toolchain golden before provisioning validation.'
    Assert-True (Test-Path -LiteralPath $validatorPath -PathType Leaf) `
        'Expected public Qt shell toolchain validator before provisioning validation.'
    Assert-True (Test-Path -LiteralPath $contractPath -PathType Leaf) `
        'Expected an independently anchored provisioning contract manifest.'
    Assert-True (Test-Path -LiteralPath $contractSidecarPath -PathType Leaf) `
        'Expected the provisioning contract consequence sidecar.'
    Assert-True (Test-Path -LiteralPath $modulePath -PathType Leaf) `
        'Expected the provisioning validator implementation module.'
    Assert-True (-not (Test-Path -LiteralPath $candidateShellPath)) `
        'Provisioning must not create the Qt candidate shell scaffold.'
}

Test-Case 'Provisioning preserves the descriptor-only candidate and creates no product scaffold' {
    Assert-Equal -Actual (Get-Sha256 -LiteralPath $candidateDescriptorPath) `
        -Expected $candidateDescriptorSha256 `
        -Message 'The frozen Qt candidate descriptor changed during provisioning.'
    $candidate = Read-JsonDocument -LiteralPath $candidateDescriptorPath
    Assert-Equal -Actual $candidate.status -Expected 'descriptor-only' `
        -Message 'Provisioning advanced the Qt candidate status.'
    Assert-True (-not (Test-Path -LiteralPath $candidateShellPath)) `
        'Provisioning created the forbidden candidate shell directory.'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $repositoryRoot 'CMakeLists.txt'))) `
        'Provisioning created a product CMake root.'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $repositoryRoot 'src'))) `
        'Provisioning created a product source root.'
    Assert-Equal -Actual (Get-Sha256 -LiteralPath (Join-Path $repositoryRoot 'experiments\m0\harness\manifest.v1.json')) `
        -Expected $m01HarnessManifestSha256 `
        -Message 'Provisioning changed the frozen M0.1 harness manifest.'
}

Test-Case 'Schema and canonical descriptor freeze exact ordered slots and preflight-only status' {
    $descriptorText = Get-Content -LiteralPath $descriptorPath -Raw
    $descriptor = $descriptorText | ConvertFrom-Json -Depth 100 -DateKind String
    Assert-True (Test-Json -Json $descriptorText -SchemaFile $schemaPath -ErrorAction SilentlyContinue) `
        'The canonical descriptor does not satisfy its closed schema.'

    $canonicalText = ($descriptor | ConvertTo-Json -Compress -Depth 100).Replace("`r`n", "`n").TrimEnd("`n") + "`n"
    Assert-Equal -Actual $descriptorText -Expected $canonicalText `
        -Message 'The descriptor is not canonical compact UTF-8 JSON with one LF.'
    Assert-SequenceEqual -Actual @($descriptor.qt.archives.id) -Expected $expectedArchiveIds `
        -Message 'Qt archive IDs or their canonical order drifted.'
    Assert-SequenceEqual -Actual @($descriptor.qt.archives.sha256) -Expected $expectedArchiveSha256 `
        -Message 'Qt archive SHA-256 inventory drifted.'
    Assert-SequenceEqual -Actual @($descriptor.qt.archives.sha1) -Expected $expectedArchiveSha1 `
        -Message 'Qt archive authoritative SHA-1 inventory drifted.'
    Assert-SequenceEqual -Actual @($descriptor.qt.archives.file_name) -Expected $expectedArchiveFileNames `
        -Message 'Qt archive filenames or their canonical order drifted.'
    Assert-SequenceEqual -Actual @($descriptor.qt.archives.size_bytes) -Expected $expectedArchiveSizes `
        -Message 'Qt archive sizes drifted.'
    Assert-SequenceEqual -Actual @($descriptor.qt.tools.id) -Expected $expectedQtToolIds `
        -Message 'Qt executable slots or their order drifted.'
    Assert-SequenceEqual -Actual @($descriptor.host.tools.id) -Expected $expectedHostToolIds `
        -Message 'Host executable slots or their order drifted.'
    Assert-SequenceEqual -Actual @($descriptor.qt.selected_artifacts.id) -Expected $expectedQtArtifactIds `
        -Message 'Qt artifact witness IDs or their canonical order drifted.'
    Assert-SequenceEqual -Actual @($descriptor.qt.selected_artifacts.relative_path) -Expected $expectedQtArtifactRelativePaths `
        -Message 'Qt artifact witness paths or their canonical order drifted.'
    Assert-SequenceEqual -Actual @($descriptor.qt.selected_artifacts.size_bytes) -Expected $expectedQtArtifactSizes `
        -Message 'Qt artifact witness sizes drifted.'
    Assert-SequenceEqual -Actual @($descriptor.qt.selected_artifacts.sha256) -Expected $expectedQtArtifactSha256 `
        -Message 'Qt artifact witness hashes drifted.'
    Assert-Equal -Actual $descriptor.validation.validation_mode -Expected 'offline_inputs_only' `
        -Message 'The descriptor overclaimed fully offline validation.'
    Assert-Equal -Actual $descriptor.validation.network_allowed -Expected $false `
        -Message 'The validator contract allowed network access.'
    Assert-Equal -Actual $descriptor.validation.network_enforcement -Expected 'not_enforced' `
        -Message 'The descriptor falsely claimed OS-level network enforcement.'
    Assert-Equal -Actual $descriptor.validation.network_observation -Expected 'not_measured' `
        -Message 'The descriptor falsely claimed measured network egress.'
    Assert-Equal -Actual $descriptor.scope.phase -Expected 'M0.2' -Message 'Wrong milestone scope.'
    Assert-Equal -Actual $descriptor.scope.mode -Expected 'preflight-only' -Message 'Provisioning overclaimed its mode.'
    Assert-Equal -Actual $descriptor.scope.candidate_status -Expected 'descriptor-only' -Message 'Provisioning overclaimed candidate progress.'
    foreach ($flag in @('candidate_decision', 'acceptance_ready', 'staging_approved', 'package_approved', 'manual_accessibility_validated')) {
        Assert-True (-not [bool]$descriptor.scope.$flag) "Provisioning overclaimed '$flag'."
    }

    $extra = Copy-JsonDocument -Value $descriptor
    $extra | Add-Member -NotePropertyName unexpected -NotePropertyValue $true
    Assert-True (-not (Test-Json -Json ($extra | ConvertTo-Json -Compress -Depth 100) -SchemaFile $schemaPath -ErrorAction SilentlyContinue)) `
        'The descriptor schema accepted an extra root property.'
}

Test-Case 'The isolated Qt stage manifest closes exact tool runtime and QML identities' {
    $descriptor = Read-JsonDocument -LiteralPath $descriptorPath
    $manifest = Read-JsonDocument -LiteralPath $stageManifestPath
    $manifestSchemaText = Get-Content -LiteralPath $stageManifestSchemaPath -Raw
    Assert-True (Test-Json -Json (Get-Content -LiteralPath $stageManifestPath -Raw) `
            -Schema $manifestSchemaText -ErrorAction SilentlyContinue) `
        'The isolated stage manifest is not valid against its anchored schema.'
    Assert-Equal -Actual (Get-Content -LiteralPath $stageManifestPath -Raw) `
        -Expected (($manifest | ConvertTo-Json -Compress -Depth 100) + "`n") `
        -Message 'The isolated stage manifest is not canonical compact JSON.'

    Assert-Equal -Actual @($manifest.entries).Count -Expected 162 `
        -Message 'The isolated stage does not have its exact 162-file inventory.'
    Assert-Equal -Actual ([long]$manifest.qml_closure.leaf_count) -Expected 119L `
        -Message 'The QML runtime closure leaf count drifted.'
    Assert-Equal -Actual ([long]$manifest.qml_closure.total_size_bytes) -Expected 1934682L `
        -Message 'The QML runtime closure byte count drifted.'
    Assert-Equal -Actual $manifest.qml_closure.canonical_tsv_sha256 `
        -Expected '97b67eab9a67c198583e454c4ed383b8f365262c262aaed445552c1f7edc0c44' `
        -Message 'The QML runtime closure fingerprint drifted.'
    Assert-Equal -Actual $manifest.qml_closure.ordering -Expected 'ordinal-ignore-case' `
        -Message 'The QML runtime closure ordering is ambiguous.'
    Assert-Equal -Actual $manifest.qml_closure.tooling_roots -Expected 'embedded-resources-not-staged' `
        -Message 'The stage ambiguously promotes QML tooling-root installation outputs.'
    Assert-Equal -Actual $manifest.qml_closure.qmllint_plugins.mode -Expected 'disabled-by-repeated-name' `
        -Message 'The stage does not require one qmllint disable option per plugin name.'
    Assert-SequenceEqual -Actual @($manifest.qml_closure.qmllint_plugins.names) `
        -Expected @('QtDesignStudio', 'Quick') `
        -Message 'The stage does not freeze the exact qmllint plugins excluded from the probe.'
    Assert-Equal -Actual ([long]$manifest.pe_closure.leaf_count) -Expected 40L `
        -Message 'The PE runtime closure leaf count drifted.'
    Assert-Equal -Actual ([long]$manifest.pe_closure.total_size_bytes) -Expected 57667264L `
        -Message 'The PE runtime closure byte count drifted.'
    Assert-Equal -Actual $manifest.pe_closure.canonical_tsv_sha256 `
        -Expected '4ec58c91c8e7c8a298a5696d24295ed584e21c2625a3ae9bf5b808a999150a8f' `
        -Message 'The PE runtime closure fingerprint drifted.'
    Assert-Equal -Actual $manifest.pe_closure.ordering -Expected 'ordinal-ignore-case' `
        -Message 'The PE runtime closure ordering is ambiguous.'
    Assert-Equal -Actual $manifest.pe_closure.derivation -Expected 'anchored-dumpbin-import-table-analysis' `
        -Message 'The PE import-closure derivation is ambiguous.'
    Assert-Equal -Actual ([long]$manifest.pe_closure.internal_import_edge_count) -Expected 115L `
        -Message 'The internal PE import edge count drifted.'
    Assert-Equal -Actual $manifest.pe_closure.internal_import_edges_tsv_sha256 `
        -Expected '7f6289cc5baea58996f2e3ab6b89ce6dc190d00bbad7571b46979fb91052a916' `
        -Message 'The internal PE import-edge fingerprint drifted.'
    Assert-Equal -Actual ([long]$manifest.pe_closure.missing_internal_qt_import_count) -Expected 0L `
        -Message 'The PE closure is missing a Qt import.'
    Assert-Equal -Actual ([long]$manifest.pe_closure.external_import_name_count) -Expected 54L `
        -Message 'The external PE import-name count drifted.'
    Assert-Equal -Actual $manifest.pe_closure.external_import_names_tsv_sha256 `
        -Expected '2c70f3b09a437ddb3409b87a36306f165987556530a6dd34e449cf71d320deee' `
        -Message 'The external PE import-name fingerprint does not use the declared ordinal-ignore-case ordering.'
    Assert-Equal -Actual $manifest.pe_closure.external_resolution_policy -Expected 'system32-or-api-set' `
        -Message 'The external PE resolution policy drifted.'
    Assert-Equal -Actual ([long]$manifest.pe_closure.api_set_alias_count) -Expected 16L `
        -Message 'The external PE API-set alias count drifted.'
    Assert-Equal -Actual ([long]$manifest.pe_closure.concrete_external_name_count) -Expected 38L `
        -Message 'The concrete external PE import-name count drifted.'
    Assert-Equal -Actual ([long]$manifest.pe_closure.concrete_external_system32_missing_count) -Expected 0L `
        -Message 'A concrete external PE import is absent from System32.'

    $paths = @($manifest.entries.stage_relative_path)
    $pathSet = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($path in $paths) {
        Assert-True ($path -cmatch '^[A-Za-z0-9._/-]+$' -and $path -cnotmatch '(^|/)\.\.(/|$)' -and
            -not $path.Contains('\')) "Unsafe stage path '$path'."
        Assert-True $pathSet.Add($path) "Duplicate or case-aliased stage path '$path'."
    }
    $sorted = [Collections.Generic.List[string]]::new()
    foreach ($path in $paths) { $sorted.Add($path) }
    $sorted.Sort([StringComparer]::OrdinalIgnoreCase)
    Assert-SequenceEqual -Actual $paths -Expected @($sorted) `
        -Message 'The isolated stage inventory is not ordered with its declared comparer.'
    foreach ($forbidden in @(
        'bin/MSVCP140.dll',
        'bin/MSVCP140_1.dll',
        'bin/VCRUNTIME140.dll',
        'bin/VCRUNTIME140_1.dll',
        'bin/icuuc.dll',
        'qml/builtins.qmltypes',
        'qml/jsroot.qmltypes'
    )) {
        Assert-True (-not $pathSet.Contains($forbidden)) "Forbidden ambient shadow '$forbidden' entered the stage."
    }
    Assert-Equal -Actual @($paths | Where-Object { $_ -like 'plugins/qmllint/*' }).Count -Expected 0 `
        -Message 'The stage included ambient qmllint plugins despite the explicit disable policy.'

    $toolById = @{}
    foreach ($item in @($descriptor.qt.tools)) { $toolById[[string]$item.id] = $item }
    $artifactById = @{}
    foreach ($item in @($descriptor.qt.selected_artifacts)) { $artifactById[[string]$item.id] = $item }
    $qmlRows = [Collections.Generic.List[string]]::new()
    $peRows = [Collections.Generic.List[string]]::new()
    $qmlBytes = 0L
    $peBytes = 0L
    foreach ($entry in @($manifest.entries)) {
        $source = switch -CaseSensitive ([string]$entry.source_kind) {
            'qt_tool' {
                Assert-True $toolById.ContainsKey([string]$entry.source_id) `
                    "Stage tool reference '$($entry.source_id)' is not in the descriptor."
                $toolById[[string]$entry.source_id]
            }
            'selected_artifact' {
                Assert-True $artifactById.ContainsKey([string]$entry.source_id) `
                    "Stage artifact reference '$($entry.source_id)' is not in the descriptor."
                $artifactById[[string]$entry.source_id]
            }
            'qt_file' { $entry }
            'inline_utf8' { $entry }
            default { throw "Unknown stage source kind '$($entry.source_kind)'." }
        }
        if ($entry.source_kind -cin @('qt_tool', 'selected_artifact')) {
            Assert-Equal -Actual $entry.stage_relative_path -Expected $source.relative_path `
                -Message "Stage reference '$($entry.source_id)' changed physical path."
        }
        if ($entry.source_kind -ceq 'qt_file') {
            Assert-Equal -Actual $entry.stage_relative_path -Expected $entry.source_relative_path `
                -Message "Stage Qt file '$($entry.stage_relative_path)' changed its source path."
        }
        if ($entry.source_kind -ceq 'inline_utf8') {
            $inlineBytes = [Convert]::FromBase64String([string]$entry.content_base64)
            Assert-Equal -Actual $entry.stage_relative_path -Expected 'bin/qt.conf' `
                -Message 'The stage has an unapproved inline file.'
            Assert-Equal -Actual ([long]$inlineBytes.Length) -Expected ([long]$entry.size_bytes) `
                -Message 'Inline qt.conf size is inconsistent.'
            Assert-Equal -Actual ([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($inlineBytes)).ToLowerInvariant()) `
                -Expected $entry.sha256 -Message 'Inline qt.conf hash is inconsistent.'
            Assert-Equal -Actual ([Text.UTF8Encoding]::new($false, $true).GetString($inlineBytes)) `
                -Expected "[Paths]`nPrefix=..`nBinaries=bin`nPlugins=plugins`nQmlImports=qml`n" `
                -Message 'Inline qt.conf does not close the staged Qt prefix.'
        }
        if ([string]$entry.stage_relative_path -cmatch '^qml/') {
            $qmlBytes += [long]$source.size_bytes
            $qmlRows.Add("$($entry.stage_relative_path)`t$([long]$source.size_bytes)`t$($source.sha256)`n")
        }
        if ($entry.source_kind -cin @('qt_tool', 'selected_artifact')) {
            $peBytes += [long]$source.size_bytes
            $peRows.Add("$($entry.stage_relative_path)`t$([long]$source.size_bytes)`t$($source.sha256)`n")
        }
    }
    Assert-Equal -Actual $qmlRows.Count -Expected 119 -Message 'The stage QML closure does not contain exactly 119 leaves.'
    Assert-Equal -Actual $qmlBytes -Expected 1934682L -Message 'The stage QML closure physical byte count drifted.'
    $qmlTsv = [Text.UTF8Encoding]::new($false).GetBytes(($qmlRows -join ''))
    Assert-Equal -Actual $qmlTsv.Length -Expected 13051 -Message 'The canonical QML closure TSV length drifted.'
    Assert-Equal -Actual ([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($qmlTsv)).ToLowerInvariant()) `
        -Expected $manifest.qml_closure.canonical_tsv_sha256 -Message 'The staged QML identities differ from the closure fingerprint.'
    Assert-Equal -Actual $peRows.Count -Expected 40 -Message 'The stage PE closure does not contain exactly 40 leaves.'
    Assert-Equal -Actual $peBytes -Expected 57667264L -Message 'The stage PE closure physical byte count drifted.'
    $peTsv = [Text.UTF8Encoding]::new($false).GetBytes(($peRows -join ''))
    Assert-Equal -Actual $peTsv.Length -Expected 4019 -Message 'The canonical PE closure TSV length drifted.'
    Assert-Equal -Actual ([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($peTsv)).ToLowerInvariant()) `
        -Expected $manifest.pe_closure.canonical_tsv_sha256 -Message 'The staged PE identities differ from the closure fingerprint.'

    Assert-Equal -Actual $descriptor.qt.stage_manifest.schema_sha256 -Expected (Get-Sha256 $stageManifestSchemaPath) `
        -Message 'Descriptor did not bind the stage-manifest schema.'
    Assert-Equal -Actual $descriptor.qt.stage_manifest.sha256 -Expected (Get-Sha256 $stageManifestPath) `
        -Message 'Descriptor did not bind the exact stage manifest.'
    Assert-Equal -Actual ([long]$descriptor.qt.stage_manifest.entry_count) -Expected 162L `
        -Message 'Descriptor stage inventory count drifted.'
    Assert-Equal -Actual ([long]$descriptor.qt.stage_manifest.stage_total_size_bytes) -Expected 59226130L `
        -Message 'Descriptor total staged byte count drifted.'
    Assert-Equal -Actual ([long]$descriptor.qt.stage_manifest.deployment_metadata_leaf_count) -Expected 14L `
        -Message 'Descriptor deployment metadata leaf count drifted.'
    Assert-Equal -Actual ([long]$descriptor.qt.stage_manifest.deployment_metadata_size_bytes) -Expected 8282L `
        -Message 'Descriptor deployment metadata byte count drifted.'
    Assert-Equal -Actual $descriptor.qt.stage_manifest.qmllint_plugin_disable_mode `
        -Expected 'repeated-name-options' -Message 'Descriptor qmllint disable mode drifted.'
    Assert-SequenceEqual -Actual @($descriptor.qt.stage_manifest.qmllint_disabled_plugins) `
        -Expected @('QtDesignStudio', 'Quick') -Message 'Descriptor qmllint disabled-plugin names drifted.'
    Assert-Equal -Actual $descriptor.qt.stage_manifest.pe_closure_tsv_sha256 `
        -Expected $manifest.pe_closure.canonical_tsv_sha256 -Message 'Descriptor PE closure fingerprint drifted.'
    Assert-Equal -Actual $descriptor.qt.stage_manifest.pe_external_import_names_tsv_sha256 `
        -Expected $manifest.pe_closure.external_import_names_tsv_sha256 `
        -Message 'Descriptor external PE import-name fingerprint drifted.'

    Import-Module $modulePath -ErrorAction Stop
    $module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
    $accepted = & $module {
        param($manifestDocument, $descriptorDocument)
        Assert-M0QtShellStageManifestDocument -ManifestDocument $manifestDocument `
            -DescriptorDocument $descriptorDocument
    } $manifest $descriptor
    Assert-Equal -Actual @($accepted.entries).Count -Expected 162 `
        -Message 'The runtime stage-manifest validator did not preserve the closed inventory.'

    $mutants = [Collections.Generic.List[object]]::new()
    $duplicate = Copy-JsonDocument -Value $manifest
    $duplicate.entries[1].stage_relative_path = $duplicate.entries[0].stage_relative_path
    $mutants.Add($duplicate)
    $missingMetadata = Copy-JsonDocument -Value $manifest
    $missingMetadata.entries = @($missingMetadata.entries | Where-Object stage_relative_path -ne 'modules/Core.json')
    $mutants.Add($missingMetadata)
    $wrongQtFileHash = Copy-JsonDocument -Value $manifest
    (@($wrongQtFileHash.entries | Where-Object stage_relative_path -eq 'mkspecs/qconfig.pri'))[0].sha256 = 'f' * 64
    $mutants.Add($wrongQtFileHash)
    $vacuousLint = Copy-JsonDocument -Value $manifest
    $vacuousLint.qml_closure.qmllint_plugins.names = @('all', 'Quick')
    $mutants.Add($vacuousLint)
    foreach ($mutant in $mutants) {
        Assert-Throws {
            & $module {
                param($manifestDocument, $descriptorDocument)
                Assert-M0QtShellStageManifestDocument -ManifestDocument $manifestDocument `
                    -DescriptorDocument $descriptorDocument
            } $mutant $descriptor
        } 'M0QT084.*stage|manifest|inventory|hash|qmllint|closure'
    }
}

Test-Case 'The isolated Qt stage is materialized from exact sources revalidated and removed by retained handles' {
    if (-not $IsWindows) {
        Assert-M0QtShellNonWindowsFailClosed -CaseId 'isolated-stage-materialization'
        return
    }

    Import-Module $modulePath -ErrorAction Stop
    $module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
    $descriptor = Read-JsonDocument -LiteralPath $descriptorPath
    $manifest = Read-JsonDocument -LiteralPath $stageManifestPath
    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) `
        ('music-player-m02-isolated-stage-' + [Guid]::NewGuid().ToString('N'))
    $stage = $null
    $extraPath = $null
    try {
        New-Item -ItemType Directory -Path $tempRoot | Out-Null
        $stage = & $module {
            param($root, $descriptorDocument, $manifestDocument, $parent)
            New-M0QtShellIsolatedStage -RepositoryRoot $root `
                -DescriptorDocument $descriptorDocument -ManifestDocument $manifestDocument `
                -StageParent $parent
        } $repositoryRoot $descriptor $manifest $tempRoot

        Assert-True ([IO.Directory]::Exists([string]$stage.root)) `
            'The materializer did not create its owned isolated stage.'
        Assert-Equal -Actual @($stage.files).Count -Expected 162 `
            -Message 'The materializer did not retain the exact stage leaves.'
        Assert-Equal -Actual @($stage.tool_paths.Keys).Count -Expected 4 `
            -Message 'The materializer did not expose exactly four staged Qt tools.'
        foreach ($toolId in $expectedQtToolIds) {
            Assert-True $stage.tool_paths.ContainsKey($toolId) `
                "The materializer omitted staged tool '$toolId'."
            Assert-True ([string]$stage.tool_paths[$toolId]).StartsWith(
                ([string]$stage.root + [IO.Path]::DirectorySeparatorChar),
                [StringComparison]::OrdinalIgnoreCase) `
                "Staged tool '$toolId' escaped the isolated root."
        }

        $accepted = & $module {
            param($isolatedStage)
            Assert-M0QtShellIsolatedStage -Stage $isolatedStage
        } $stage
        Assert-Equal -Actual $accepted -Expected $true `
            -Message 'The exact stage validator rejected the freshly materialized closure.'

        $stageValidationCalls = 0
        $versionProbe = & $module {
            param($isolatedStage, $temp, [ref]$calls)
            $trustedTool = @($isolatedStage.files | Where-Object {
                    ([string]$_.final_path).Equals(
                        [string]$isolatedStage.tool_paths.qmlformat,
                        [StringComparison]::OrdinalIgnoreCase)
                })
            if ($trustedTool.Count -ne 1) { throw 'The staged qmlformat handle is missing or ambiguous.' }
            $trustedDirectories = @($isolatedStage.parent_pins) + @($isolatedStage.directories)
            $assertStageCommand = Get-Command Assert-M0QtShellIsolatedStage -CommandType Function -ErrorAction Stop
            $validation = {
                & $assertStageCommand -Stage $isolatedStage | Out-Null
                $calls.Value++
                'callback-noise-must-not-escape'
            }.GetNewClosure()
            Invoke-M0QtShellBoundedProcess -FilePath $isolatedStage.tool_paths.qmlformat `
                -ArgumentList @('--version') -WorkingDirectory $isolatedStage.root `
                -TempRoot $temp -QtStageRoot $isolatedStage.root `
                -PinnedStateValidation $validation -TrustedExecutable $trustedTool[0] `
                -TrustedDirectories $trustedDirectories `
                -TimeoutSeconds 10
        } $stage $tempRoot ([ref]$stageValidationCalls)
        Assert-True ($versionProbe -is [pscustomobject]) `
            'Pinned-stage validation output leaked into the bounded-process result stream.'
        Assert-Equal -Actual $versionProbe.exit_code -Expected 0 `
            -Message 'The staged qmlformat executable did not run successfully under retained handles.'
        Assert-True (($versionProbe.stdout + $versionProbe.stderr) -match '6\.11\.2') `
            'The staged qmlformat version output drifted.'
        Assert-Equal -Actual $stageValidationCalls -Expected 3 `
            -Message 'The real staged executable was not guarded at all three process boundaries.'

        $capabilities = & $module {
            param($isolatedStage, $temp)
            $qmlformat = Get-M0QtShellIsolatedStageTool -Stage $isolatedStage -ToolId 'qmlformat'
            $qmllint = Get-M0QtShellIsolatedStageTool -Stage $isolatedStage -ToolId 'qmllint'
            [pscustomobject]@{
                format = Assert-M0QtShellQmlFormatCapability -FilePath $qmlformat.final_path `
                    -ArgumentPrefix @('--ignore-settings') -TimeoutSeconds 30 -TempRoot $temp `
                    -QtStage $isolatedStage -TrustedExecutable $qmlformat
                lint = Assert-M0QtShellQmlLintCapability -FilePath $qmllint.final_path `
                    -ArgumentPrefix @('--bare', '--ignore-settings',
                        '--disable-plugins', 'QtDesignStudio', '--disable-plugins', 'Quick',
                        '-I', (Join-Path ([string]$isolatedStage.root) 'qml')) `
                    -TimeoutSeconds 30 -TempRoot $temp -QtStage $isolatedStage `
                    -TrustedExecutable $qmllint
                qmltest = Assert-M0QtShellQmlTestCapability -QtStage $isolatedStage `
                    -TimeoutSeconds 30 -TempRoot $temp
                deployment = Assert-M0QtShellDeploymentCapability -QtStage $isolatedStage `
                    -TimeoutSeconds 30 -TempRoot $temp
            }
        } $stage $tempRoot
        Assert-Equal -Actual $capabilities.format.canonical_lf_round_trip -Expected $true `
            -Message 'Staged qmlformat did not prove canonical LF normalization.'
        Assert-Equal -Actual $capabilities.lint.invalid_fixture_rejected -Expected $true `
            -Message 'Staged qmllint did not reject the syntax-invalid fixture.'
        Assert-Equal -Actual $capabilities.qmltest.qt_test_passed -Expected $true `
            -Message 'Staged qmltestrunner did not prove its TAP smoke.'
        Assert-Equal -Actual $capabilities.qmltest.svg_image_loaded -Expected $true `
            -Message 'Staged qmltestrunner did not prove the SVG image plugin path.'
        Assert-Equal -Actual $capabilities.deployment.mapping_entry_count -Expected 14 `
            -Message 'Staged windeployqt did not prove the exact 14-line mapping.'

        $extraPath = Join-Path ([string]$stage.root) 'precreate-ambient-shadow.dll'
        $preCreateHookCalls = 0
        $preCreateValidationCalls = 0
        $suspendedBoundaryReached = 0
        Assert-Throws {
            & $module {
                param($isolatedStage, $temp, $extra,
                    [ref]$hookCalls, [ref]$validationCalls, [ref]$suspendedReached)
                $trustedTool = @(Get-M0QtShellIsolatedStageTool -Stage $isolatedStage -ToolId 'qmlformat')
                $trustedDirectories = @($isolatedStage.parent_pins) + @($isolatedStage.directories)
                $assertStageCommand = Get-Command Assert-M0QtShellIsolatedStage -CommandType Function -ErrorAction Stop
                $validation = {
                    $validationCalls.Value++
                    & $assertStageCommand -Stage $isolatedStage | Out-Null
                }.GetNewClosure()
                $script:M0QtShellProvisioningTestHook = [pscustomobject]@{
                    after_trusted_executable_opened_before_directory_pins = {
                        $hookCalls.Value++
                        [IO.File]::WriteAllBytes($extra, [byte[]](4, 5, 6))
                    }.GetNewClosure()
                    after_job_bound_suspended_before_resume = {
                        param([int] $SuspendedProcessId)
                        $suspendedReached.Value++
                    }.GetNewClosure()
                }
                try {
                    Invoke-M0QtShellBoundedProcess -FilePath $trustedTool[0].final_path `
                        -ArgumentList @('--version') -WorkingDirectory $isolatedStage.root `
                        -TempRoot $temp -QtStageRoot $isolatedStage.root `
                        -PinnedStateValidation $validation -TrustedExecutable $trustedTool[0] `
                        -TrustedDirectories $trustedDirectories -TimeoutSeconds 10
                }
                finally {
                    $script:M0QtShellProvisioningTestHook = $null
                }
            } $stage $tempRoot $extraPath ([ref]$preCreateHookCalls) `
                ([ref]$preCreateValidationCalls) ([ref]$suspendedBoundaryReached)
        } 'M0QT085.*extra|inventory|stage'
        Assert-Equal -Actual $preCreateHookCalls -Expected 1 `
            -Message 'The causal pre-CreateProcess stage mutation hook did not execute exactly once.'
        Assert-Equal -Actual $preCreateValidationCalls -Expected 1 `
            -Message 'The pre-CreateProcess stage mutation was not rejected by the first pinned-state validation.'
        Assert-Equal -Actual $suspendedBoundaryReached -Expected 0 `
            -Message 'CreateProcess was reached after the pre-launch stage inventory mutation.'
        Assert-True ([IO.File]::Exists($extraPath)) `
            'The pre-CreateProcess stage mutation witness is absent.'
        Remove-Item -LiteralPath $extraPath -Force
        $extraPath = $null

        $extraPath = Join-Path ([string]$stage.root) 'ambient-shadow.dll'
        $tamperValidationCalls = 0
        $tamperHookCalls = 0
        $suspendedProcessId = 0
        $resumeFixtureRoot = Join-Path $tempRoot 'suspended-resume-fixture'
        $resumeFixturePath = Join-Path $resumeFixtureRoot 'tst_resume_sentinel.qml'
        $resumeSentinelPath = Join-Path $resumeFixtureRoot 'resumed.tap'
        [IO.Directory]::CreateDirectory($resumeFixtureRoot) | Out-Null
        [IO.File]::WriteAllText($resumeFixturePath,
            "import QtQuick`nimport QtTest`nTestCase { name: `"ResumeSentinel`"; function test_ok() { verify(true) } }`n",
            [Text.UTF8Encoding]::new($false))
        Assert-Throws {
            & $module {
                param($isolatedStage, $temp, $extra, $fixture, $sentinel,
                    [ref]$validationCalls, [ref]$hookCalls, [ref]$processId)
                $trustedTool = @(Get-M0QtShellIsolatedStageTool -Stage $isolatedStage -ToolId 'qmltestrunner')
                $trustedDirectories = @($isolatedStage.parent_pins) + @($isolatedStage.directories)
                $assertStageCommand = Get-Command Assert-M0QtShellIsolatedStage -CommandType Function -ErrorAction Stop
                $validation = {
                    $validationCalls.Value++
                    & $assertStageCommand -Stage $isolatedStage | Out-Null
                }.GetNewClosure()
                $script:M0QtShellProvisioningTestHook = [pscustomobject]@{
                    after_job_bound_suspended_before_resume = {
                        param([int] $SuspendedProcessId)
                        $hookCalls.Value++
                        $processId.Value = $SuspendedProcessId
                        [IO.File]::WriteAllBytes($extra, [byte[]](1, 2, 3))
                        Start-Sleep -Milliseconds 500
                    }.GetNewClosure()
                }
                try {
                    Invoke-M0QtShellBoundedProcess -FilePath $trustedTool[0].final_path `
                        -ArgumentList @('-nocrashhandler', '-input', $fixture,
                            '-import', (Join-Path ([string]$isolatedStage.root) 'qml'),
                            '-plugins', (Join-Path ([string]$isolatedStage.root) 'plugins'),
                            '-o', ($sentinel + ',tap')) -WorkingDirectory $isolatedStage.root `
                        -TempRoot $temp -QtStageRoot $isolatedStage.root `
                        -PinnedStateValidation $validation -TrustedExecutable $trustedTool[0] `
                        -TrustedDirectories $trustedDirectories -TimeoutSeconds 10
                }
                finally {
                    $script:M0QtShellProvisioningTestHook = $null
                }
            } $stage $tempRoot $extraPath $resumeFixturePath $resumeSentinelPath `
                ([ref]$tamperValidationCalls) ([ref]$tamperHookCalls) ([ref]$suspendedProcessId)
        } 'M0QT085.*extra|inventory|stage'
        Assert-Equal -Actual $tamperValidationCalls -Expected 2 `
            -Message 'The suspended stage tamper was not rejected by the second validation before ResumeProcess.'
        Assert-Equal -Actual $tamperHookCalls -Expected 1 `
            -Message 'The causal suspended-stage tamper hook did not execute exactly once.'
        Assert-True ($suspendedProcessId -gt 0) `
            'The causal stage tamper test did not observe the suspended process identity.'
        Assert-True ($null -eq (Get-Process -Id $suspendedProcessId -ErrorAction SilentlyContinue)) `
            'The stage-tampered process survived rejection before ResumeProcess.'
        Assert-True (-not [IO.File]::Exists($resumeSentinelPath)) `
            'The suspended qmltestrunner produced TAP output before the stage mutation was rejected.'
        Assert-True ([IO.File]::Exists($extraPath)) `
            'The stage mutation was not present when the second boundary rejected it.'
        Remove-Item -LiteralPath $extraPath -Force
        $extraPath = $null
        Remove-Item -LiteralPath $resumeFixturePath -Force
        if ([IO.File]::Exists($resumeSentinelPath)) { Remove-Item -LiteralPath $resumeSentinelPath -Force }
        Remove-Item -LiteralPath $resumeFixtureRoot -Force
        $acceptedAfterTamperCleanup = & $module {
            param($isolatedStage)
            Assert-M0QtShellIsolatedStage -Stage $isolatedStage
        } $stage
        Assert-Equal -Actual $acceptedAfterTamperCleanup -Expected $true `
            -Message 'The exact stage did not recover after causal tamper cleanup.'

        & $module {
            param($isolatedStage)
            Close-M0QtShellIsolatedStage -Stage $isolatedStage
        } $stage
        Assert-True (-not [IO.Directory]::Exists([string]$stage.root)) `
            'The stage root remained after retained-handle cleanup.'
        $stage = $null
    }
    finally {
        if ($null -ne $extraPath -and [IO.File]::Exists($extraPath)) {
            Remove-Item -LiteralPath $extraPath -Force -ErrorAction SilentlyContinue
        }
        if ($null -ne $stage) {
            try {
                & $module {
                    param($isolatedStage)
                    Close-M0QtShellIsolatedStage -Stage $isolatedStage
                } $stage
            }
            catch { }
        }
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

Test-Case 'The descriptor schema rejects duplicate reordered incomplete and license-rebound closed slots' {
    $descriptor = Read-JsonDocument -LiteralPath $descriptorPath
    $mutants = [ordered]@{}

    $mutants.archive_duplicate = Copy-JsonDocument -Value $descriptor
    $mutants.archive_duplicate.qt.archives[1] = Copy-JsonDocument -Value $mutants.archive_duplicate.qt.archives[0]

    $mutants.archive_reordered = Copy-JsonDocument -Value $descriptor
    $firstArchive = Copy-JsonDocument -Value $mutants.archive_reordered.qt.archives[0]
    $mutants.archive_reordered.qt.archives[0] = Copy-JsonDocument -Value $mutants.archive_reordered.qt.archives[1]
    $mutants.archive_reordered.qt.archives[1] = $firstArchive

    $mutants.required_modules_without_widgets = Copy-JsonDocument -Value $descriptor
    $mutants.required_modules_without_widgets.qt.required_modules = @(
        $mutants.required_modules_without_widgets.qt.required_modules |
            Where-Object { $_ -cne 'Widgets' }
    )

    $mutants.required_modules_reordered = Copy-JsonDocument -Value $descriptor
    $firstModule = $mutants.required_modules_reordered.qt.required_modules[0]
    $mutants.required_modules_reordered.qt.required_modules[0] = `
        $mutants.required_modules_reordered.qt.required_modules[1]
    $mutants.required_modules_reordered.qt.required_modules[1] = $firstModule

    $mutants.extractor_members_reordered = Copy-JsonDocument -Value $descriptor
    $mutants.extractor_members_reordered.extractor.selected_members = @(
        'License.txt',
        'x64/7za.exe',
        'readme.txt'
    )

    $mutants.selected_artifact_omitted = Copy-JsonDocument -Value $descriptor
    $mutants.selected_artifact_omitted.qt.selected_artifacts = @(
        $mutants.selected_artifact_omitted.qt.selected_artifacts |
            Where-Object { $_.id -cne 'qtqml-workerscript-plugin' }
    )

    $mutants.selected_artifacts_reordered = Copy-JsonDocument -Value $descriptor
    $workerScriptQmldir = Copy-JsonDocument -Value $mutants.selected_artifacts_reordered.qt.selected_artifacts[24]
    $mutants.selected_artifacts_reordered.qt.selected_artifacts[24] = `
        Copy-JsonDocument -Value $mutants.selected_artifacts_reordered.qt.selected_artifacts[25]
    $mutants.selected_artifacts_reordered.qt.selected_artifacts[25] = $workerScriptQmldir

    $mutants.network_allowed = Copy-JsonDocument -Value $descriptor
    $mutants.network_allowed.validation.network_allowed = $true

    $mutants.qtbase_aggregate_expression_injected = Copy-JsonDocument -Value $descriptor
    $mutants.qtbase_aggregate_expression_injected.qt.archives[2].license | `
        Add-Member -NotePropertyName spdx_expression -NotePropertyValue 'LGPL-3.0-only'

    $mutants.d3d_license_relabelled_and_rebound = Copy-JsonDocument -Value $descriptor
    $reboundLicense = Copy-JsonDocument -Value $mutants.d3d_license_relabelled_and_rebound.qt.archives[4].license
    $reboundLicense.spdx_assertion = 'MIT'
    $mutants.d3d_license_relabelled_and_rebound.qt.archives[0].license = $reboundLicense

    $acceptedMutants = @(
        foreach ($entry in $mutants.GetEnumerator()) {
            $json = $entry.Value | ConvertTo-Json -Compress -Depth 100
            if (Test-Json -Json $json -SchemaFile $schemaPath -ErrorAction SilentlyContinue) {
                $entry.Key
            }
        }
    )
    Assert-Equal -Actual $acceptedMutants.Count -Expected 0 `
        -Message "The descriptor schema accepted closed-slot mutants: $($acceptedMutants -join ', ')."
}

Test-Case 'Every archive and external dependency freezes provenance hashes sizes and licenses' {
    $descriptor = Read-JsonDocument -LiteralPath $descriptorPath
    Assert-Equal -Actual $descriptor.qt.version -Expected '6.11.2' -Message 'Wrong Qt version.'
    Assert-Equal -Actual $descriptor.qt.edition -Expected 'Desktop' -Message 'Wrong Qt edition.'
    Assert-Equal -Actual $descriptor.qt.compiler -Expected 'MSVC2022' -Message 'Wrong Qt compiler ABI.'
    Assert-Equal -Actual $descriptor.qt.architecture -Expected 'x86_64' -Message 'Wrong Qt architecture.'
    Assert-Equal -Actual $descriptor.qt.abi -Expected 'msvc2022_64' -Message 'Wrong Qt ABI slot.'

    $qtArchiveBase = 'https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6112/qt6_6112_msvc2022_64/qt.qt6.6112.win64_msvc2022_64/'
    foreach ($archive in @($descriptor.qt.archives)) {
        Assert-Equal -Actual $archive.url -Expected ($qtArchiveBase + $archive.file_name) `
            "Qt archive '$($archive.id)' is not bound to its authoritative literal HTTPS URL."
        Assert-Equal -Actual $archive.sha1_url -Expected ($archive.url + '.sha1') `
            "Qt archive '$($archive.id)' is not bound to its authoritative SHA-1 sidecar URL."
        Assert-True ($archive.sha1 -cmatch '^[0-9a-f]{40}$') "Qt archive '$($archive.id)' lacks canonical SHA-1 metadata."
        Assert-True ($archive.sha256 -cmatch '^[0-9a-f]{64}$') "Qt archive '$($archive.id)' lacks canonical SHA-256 metadata."
        Assert-True ([long]$archive.size_bytes -gt 0) "Qt archive '$($archive.id)' lacks a positive exact size."
        Assert-LicenseEvidenceContract -License $archive.license -Context "Qt archive '$($archive.id)' license"
    }
    Assert-SequenceEqual -Actual @($descriptor.qt.archives.license.evidence_status) `
        -Expected @(
            'metadata_unverified_distribution_blocker',
            'metadata_unverified_distribution_blocker',
            'physical_sbom_noassertion_distribution_blocker',
            'physical_sbom_noassertion_distribution_blocker',
            'physical_sbom_noassertion_distribution_blocker',
            'physical_sbom_noassertion_distribution_blocker',
            'physical_sbom_noassertion_distribution_blocker'
        ) -Message 'Qt archive license evidence states drifted.'
    Assert-Equal -Actual $descriptor.qt.archives[0].license.blocker `
        -Expected 'component-specific-license-evidence-not-physically-verified' `
        -Message 'The D3D compiler distribution blocker drifted.'
    Assert-Equal -Actual $descriptor.qt.archives[1].license.blocker `
        -Expected 'mesa-component-license-evidence-not-physically-verified' `
        -Message 'The Mesa distribution blocker drifted.'
    foreach ($archive in @($descriptor.qt.archives | Select-Object -Skip 2)) {
        Assert-Equal -Actual $archive.license.spdx_assertion -Expected 'NOASSERTION' `
            -Message "Qt archive '$($archive.id)' falsely concluded an aggregate SPDX expression."
        Assert-Equal -Actual $archive.license.packaged -Expected $false `
            -Message "Qt archive '$($archive.id)' escaped its distribution blocker."
        Assert-True ($null -eq $archive.license.PSObject.Properties['spdx_expression']) `
            "Qt archive '$($archive.id)' promoted general terms into a component SPDX expression."
    }

    Assert-Equal -Actual $descriptor.catch2.version -Expected '3.15.3' -Message 'Wrong Catch2 version.'
    Assert-Equal -Actual $descriptor.catch2.commit_sha -Expected '8b08d4d79514f45f7e4ce2a607ac9c94e920d1bb' -Message 'Catch2 is not commit-pinned.'
    Assert-Equal -Actual $descriptor.catch2.sha256 -Expected 'b0299ae552918220a7a6e21e7de5b714777f4e8c883fb70c4bb23fe01df8c6e3' -Message 'Wrong Catch2 source hash.'
    Assert-Equal -Actual ([long]$descriptor.catch2.size_bytes) -Expected 1208344L -Message 'Wrong Catch2 source size.'
    Assert-Equal -Actual $descriptor.catch2.license.spdx_expression -Expected 'BSL-1.0' -Message 'Wrong Catch2 license.'
    Assert-True ([bool]$descriptor.catch2.offline_build_required) 'Catch2 must be an offline build input.'
    Assert-True (-not [bool]$descriptor.catch2.fetchcontent_network_allowed) 'Catch2 must forbid implicit FetchContent network access.'
    Assert-Equal -Actual $descriptor.catch2.url -Expected 'https://github.com/catchorg/Catch2/archive/refs/tags/v3.15.3.tar.gz' `
        -Message 'Catch2 source URL is not literal and immutable.'
    Assert-True ($descriptor.catch2.license.text_sha256 -cmatch '^[0-9a-f]{64}$') 'Catch2 license text is not hash-bound.'
    Assert-LicenseEvidenceContract -License $descriptor.catch2.license -Context 'Catch2 license'

    Assert-Equal -Actual $descriptor.nvda.version -Expected '2026.1.1' -Message 'Wrong NVDA version.'
    Assert-Equal -Actual $descriptor.nvda.sha256 -Expected '6e0289eb5a3aa076eb97ea99c5d5465cb48b5ecc6a3257dc3d811f881a1747c9' -Message 'Wrong NVDA installer hash.'
    Assert-Equal -Actual ([long]$descriptor.nvda.size_bytes) -Expected 62914952L -Message 'Wrong NVDA installer size.'
    Assert-Equal -Actual $descriptor.nvda.authenticode.thumbprint -Expected '4E9A772A99C1A3B713766DAC4B3545236CC2AE72' -Message 'Wrong NVDA signer thumbprint.'
    Assert-Equal -Actual $descriptor.nvda.license.spdx_expression -Expected 'GPL-2.0-or-later' -Message 'Wrong NVDA license.'
    Assert-Equal -Actual $descriptor.nvda.url -Expected 'https://download.nvaccess.org/releases/2026.1.1/nvda_2026.1.1.exe' `
        -Message 'NVDA installer URL is not literal and authoritative.'
    Assert-True ($descriptor.nvda.license.text_sha256 -cmatch '^[0-9a-f]{64}$') 'NVDA license text is not hash-bound.'
    Assert-LicenseEvidenceContract -License $descriptor.nvda.license -Context 'NVDA license'
    Assert-Equal -Actual $descriptor.nvda.license.scope `
        -Expected 'primary_program_with_special_exceptions_and_third_party_notices' `
        -Message 'NVDA license scope lost its exceptions and third-party notice boundary.'
    Assert-SequenceEqual -Actual @($descriptor.nvda.license.exceptions) `
        -Expected @('plugin-driver-exception', 'microsoft-distributable-code-exception') `
        -Message 'NVDA special exceptions drifted.'
    Assert-Equal -Actual $descriptor.nvda.license.third_party_notices_included -Expected $true `
        -Message 'NVDA copying.txt was not bound to its included third-party notices.'
    Assert-Equal -Actual $descriptor.nvda.manual_journey_validated -Expected $false `
        -Message 'An installed NVDA must not imply a completed manual journey.'

    Assert-Equal -Actual $descriptor.extractor.id -Expected '7zip' -Message 'The archive extractor is not explicit.'
    Assert-Equal -Actual $descriptor.extractor.version -Expected '26.02' -Message 'The archive extractor version drifted.'
    Assert-Equal -Actual $descriptor.extractor.archive.url -Expected 'https://github.com/ip7z/7zip/releases/download/26.02/7z2602-extra.7z' `
        -Message 'The extractor archive origin is not literal.'
    Assert-Equal -Actual ([long]$descriptor.extractor.archive.size_bytes) -Expected 1758916L `
        -Message 'The extractor archive size drifted.'
    Assert-Equal -Actual $descriptor.extractor.archive.sha256 -Expected '081df9e9311dfd9c9e0e98c1c80180b99bb51e4cb24156b5f3057fe3c259d70a' `
        -Message 'The extractor archive hash drifted.'
    Assert-Equal -Actual ([long]$descriptor.extractor.size_bytes) -Expected 1334784L `
        -Message 'The extractor executable size drifted.'
    Assert-Equal -Actual $descriptor.extractor.sha256 -Expected '35d4d69d7cd6cb44558f208c3b1334268013f9daf82d2dda848893a1c30c59c2' `
        -Message 'The archive extractor binary is not hash-pinned.'
    Assert-Equal -Actual $descriptor.extractor.license.spdx_expression `
        -Expected 'LGPL-2.1-or-later AND BSD-3-Clause AND BSD-2-Clause' `
        -Message 'The archive extractor license policy drifted.'
    Assert-Equal -Actual $descriptor.extractor.license.authoritative_url -Expected 'https://www.7-zip.org/license.txt' `
        -Message 'The extractor license source drifted.'
    Assert-Equal -Actual ([long]$descriptor.extractor.license.text_size_bytes) -Expected 5188L `
        -Message 'The extractor license text size drifted.'
    Assert-Equal -Actual $descriptor.extractor.license.text_sha256 -Expected '39d2187b942cc88818dd959e53c6b7dddd152097a42061467c4d56050168d646' `
        -Message 'The extractor license text hash drifted.'
    Assert-Equal -Actual $descriptor.extractor.license.vendor_component_class -Expected '7za-executable' `
        -Message 'The 7za.exe license was not bound to the vendor executable classification.'
    Assert-LicenseEvidenceContract -License $descriptor.extractor.license -Context '7za.exe license'
    Assert-SequenceEqual -Actual @($descriptor.extractor.selected_members) `
        -Expected @('x64/7za.exe', 'License.txt', 'readme.txt') `
        -Message 'The extractor selection includes an unapproved archive member.'
    Assert-SequenceEqual -Actual @($descriptor.extractor.excluded_components) `
        -Expected @('7z.dll', 'unrar-code', 'lzfse-code') `
        -Message 'The extractor exclusion boundary drifted.'
    Assert-SequenceEqual -Actual @($descriptor.extractor.selected_executable_embedded_components) `
        -Expected @('zstd-derived-bsd-3-clause-code', 'xxh64-derived-bsd-2-clause-code') `
        -Message 'The BSD-licensed code linked into 7za.exe was hidden or relabelled.'
    Assert-Equal -Actual $descriptor.extractor.selected_executable_contains_bsd_licensed_code -Expected $true `
        -Message 'The selected 7za.exe falsely claims to contain no BSD-licensed code.'
    Assert-Equal -Actual $descriptor.extractor.archive_other_members_promoted -Expected $false `
        -Message 'Unreviewed members of the 7-Zip Extra archive were promoted into the toolchain.'
    foreach ($flag in @('reject_parent_traversal', 'reject_absolute_paths', 'reject_ads', 'reject_reparse_points', 'closed_inventory_required')) {
        Assert-Equal -Actual $descriptor.extractor.policy.$flag -Expected $true `
            -Message "The extractor does not fail closed for '$flag'."
    }
}

Test-Case 'Every executable slot binds an exact provider path version architecture hash and capability' {
    $descriptor = Read-JsonDocument -LiteralPath $descriptorPath
    Assert-SequenceEqual -Actual @($descriptor.qt.tools.sha256) -Expected $expectedQtToolSha256 `
        -Message 'Qt tool hashes drifted.'
    Assert-SequenceEqual -Actual @($descriptor.qt.tools.size_bytes) -Expected $expectedQtToolSizes `
        -Message 'Qt tool sizes drifted.'
    Assert-SequenceEqual -Actual @($descriptor.qt.tools.license.component_id) -Expected $expectedQtToolLicenseComponentIds `
        -Message 'Qt tool SPDX component bindings drifted.'
    Assert-SequenceEqual -Actual @($descriptor.qt.tools.license.text_sha256) -Expected $expectedQtToolLicenseSha256 `
        -Message 'Qt tool SBOM hashes drifted.'
    Assert-SequenceEqual -Actual @($descriptor.qt.tools.license.text_size_bytes) -Expected $expectedQtToolLicenseSizes `
        -Message 'Qt tool SBOM sizes drifted.'
    Assert-SequenceEqual -Actual @($descriptor.qt.tools.license.local_relative_path) -Expected $expectedQtToolLicensePaths `
        -Message 'Qt tool SBOM paths drifted.'
    foreach ($tool in @($descriptor.qt.tools)) {
        Assert-Equal -Actual $tool.path_base -Expected 'qt_toolchain_root' -Message "Qt tool '$($tool.id)' uses an unapproved base."
        Assert-True ($tool.relative_path -cmatch '^bin/[A-Za-z0-9_.-]+\.exe$') "Qt tool '$($tool.id)' has a non-literal relative path."
        Assert-Equal -Actual $tool.version -Expected '6.11.2.0' -Message "Qt tool '$($tool.id)' version drifted."
        Assert-Equal -Actual $tool.architecture -Expected 'x86_64' -Message "Qt tool '$($tool.id)' architecture drifted."
        Assert-Equal -Actual $tool.provider -Expected 'The Qt Company' -Message "Qt tool '$($tool.id)' provider drifted."
        Assert-True (@($tool.required_capabilities).Count -gt 0) "Qt tool '$($tool.id)' has no behavioral capability contract."
        Assert-LicenseEvidenceContract -License $tool.license -Context "Qt tool '$($tool.id)' license"
    }

    Assert-SequenceEqual -Actual @($descriptor.host.tools.version) -Expected $expectedHostToolVersions `
        -Message 'Host tool versions drifted.'
    Assert-SequenceEqual -Actual @($descriptor.host.tools.sha256) -Expected $expectedHostToolSha256 `
        -Message 'Host tool hashes drifted.'
    Assert-SequenceEqual -Actual @($descriptor.host.tools.size_bytes) -Expected $expectedHostToolSizes `
        -Message 'Host tool sizes drifted.'
    Assert-SequenceEqual -Actual @($descriptor.host.tools.path_base) -Expected $expectedHostToolBases `
        -Message 'Host tool path bases drifted.'
    Assert-SequenceEqual -Actual @($descriptor.host.tools.relative_path) -Expected $expectedHostToolRelativePaths `
        -Message 'Host tool literal relative paths drifted.'
    foreach ($tool in @($descriptor.host.tools)) {
        Assert-True ($tool.path_base -cin @('program_files', 'vs2022_community')) "Host tool '$($tool.id)' uses an ambient or personal path base."
        Assert-True ($tool.relative_path -cmatch '^[A-Za-z0-9_. /-]+\.exe$') "Host tool '$($tool.id)' relative path is not closed."
        Assert-Equal -Actual $tool.architecture -Expected 'x86_64' -Message "Host tool '$($tool.id)' architecture drifted."
        Assert-True ($tool.provider.Length -gt 0) "Host tool '$($tool.id)' provider is missing."
        Assert-LicenseEvidenceContract -License $tool.license -Context "Host tool '$($tool.id)' license"
        Assert-True (@($tool.required_capabilities).Count -gt 0) "Host tool '$($tool.id)' has no exact-path capability probe."
    }
}

Test-Case 'Asset host-observation QML-capability and build-validation states remain precise' {
    $golden = Read-JsonDocument -LiteralPath $goldenPath
    $goldenText = Get-Content -LiteralPath $goldenPath -Raw
    Assert-True (Test-Json -Json $goldenText -SchemaFile $evidenceSchemaPath -ErrorAction SilentlyContinue) `
        'The synthetic golden does not satisfy its closed evidence schema.'
    $canonicalGoldenText = ($golden | ConvertTo-Json -Compress -Depth 100).Replace("`r`n", "`n").TrimEnd("`n") + "`n"
    Assert-Equal -Actual $goldenText -Expected $canonicalGoldenText `
        -Message 'The synthetic golden is not canonical compact UTF-8 JSON with one LF.'
    Assert-Equal -Actual $golden.artifact_role -Expected 'synthetic_golden' -Message 'The golden masquerades as live evidence.'
    Assert-Equal -Actual $golden.live_evidence -Expected $false -Message 'The golden masquerades as live evidence.'
    Assert-Equal -Actual $golden.validation_mode -Expected 'offline_inputs_only' `
        -Message 'The golden overclaimed fully offline execution.'
    Assert-Equal -Actual $golden.network_allowed -Expected $false -Message 'The golden allowed network access.'
    Assert-Equal -Actual $golden.network_enforcement -Expected 'not_enforced' `
        -Message 'The golden falsely claimed OS-level network enforcement.'
    Assert-Equal -Actual $golden.network_observation -Expected 'not_measured' `
        -Message 'The golden falsely claimed measured network egress.'
    Assert-Equal -Actual $golden.expected_states.assets_provisioned -Expected $true `
        -Message 'The expected asset-provisioning state drifted.'
    Assert-Equal -Actual $golden.expected_states.host_tools_observed -Expected $true `
        -Message 'Exact host-tool observation drifted.'
    Assert-Equal -Actual $golden.expected_states.qml_tool_capabilities_validated -Expected $true `
        -Message 'The QML tool capability state drifted.'
    Assert-Equal -Actual $golden.expected_states.preflight_scope_satisfied -Expected $true `
        -Message 'The narrow preflight-scope state drifted.'
    Assert-Equal -Actual $golden.expected_states.host_build_capability_validated -Expected $false `
        -Message 'Host build capability was falsely marked validated.'
    Assert-Equal -Actual $golden.expected_states.manual_validated -Expected $false -Message 'NVDA/manual accessibility was falsely marked complete.'
    foreach ($flag in @('candidate_accepted', 'candidate_build_validated', 'acceptance_ready', 'staging_approved', 'package_approved', 'qt_network_approved', 'accessibility_passed')) {
        Assert-Equal -Actual $golden.expected_states.$flag -Expected $false `
            -Message "The preflight golden overclaimed '$flag'."
    }
    Assert-Equal -Actual $golden.diagnostics.qt_network_staging_probe `
        -Expected 'diagnostic-no-go-final-bundle' `
        -Message 'The QtNetwork staging diagnostic was presented as approval.'
    Assert-SequenceEqual -Actual @($golden.expected_bindings.qt_artifacts.id) -Expected $expectedQtArtifactIds `
        -Message 'Golden Qt artifact witness IDs or their order drifted.'
    Assert-SequenceEqual -Actual @($golden.expected_bindings.qt_artifacts.relative_path) -Expected $expectedQtArtifactRelativePaths `
        -Message 'Golden Qt artifact witness paths or their order drifted.'
    Assert-SequenceEqual -Actual @($golden.expected_bindings.qt_artifacts.size_bytes) -Expected $expectedQtArtifactSizes `
        -Message 'Golden Qt artifact witness sizes drifted.'
    Assert-SequenceEqual -Actual @($golden.expected_bindings.qt_artifacts.sha256) -Expected $expectedQtArtifactSha256 `
        -Message 'Golden Qt artifact witness hashes drifted.'
}

Test-Case 'The synthetic golden is pure canonical and rejects impossible state projections' {
    Import-Module $modulePath -ErrorAction Stop
    $module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
    $first = & $module { New-M0QtShellSyntheticGolden }
    $second = & $module { New-M0QtShellSyntheticGolden }
    $goldenBytes = [IO.File]::ReadAllBytes($goldenPath)
    Assert-SequenceEqual -Actual @($first) -Expected @($second) -Message 'The synthetic golden builder is nondeterministic.'
    Assert-SequenceEqual -Actual @($first) -Expected @($goldenBytes) -Message 'The builder does not reproduce the versioned golden bytes.'

    $golden = Read-JsonDocument -LiteralPath $goldenPath
    $extra = Copy-JsonDocument -Value $golden
    $extra | Add-Member -NotePropertyName unexpected -NotePropertyValue $true
    Assert-True (-not (Test-Json -Json ($extra | ConvertTo-Json -Compress -Depth 100) -SchemaFile $evidenceSchemaPath -ErrorAction SilentlyContinue)) `
        'The evidence schema accepted an extra root field.'

    $impossible = Copy-JsonDocument -Value $golden
    $impossible.expected_states.assets_provisioned = $false
    $impossible.expected_states.host_tools_observed = $true
    Assert-True (-not (Test-Json -Json ($impossible | ConvertTo-Json -Compress -Depth 100) -SchemaFile $evidenceSchemaPath -ErrorAction SilentlyContinue)) `
        'The evidence schema accepted observed host tools without validated assets.'

    $manualFalseString = Copy-JsonDocument -Value $golden
    $manualFalseString.expected_states.manual_validated = 'false'
    Assert-True (-not (Test-Json -Json ($manualFalseString | ConvertTo-Json -Compress -Depth 100) -SchemaFile $evidenceSchemaPath -ErrorAction SilentlyContinue)) `
        'The evidence schema accepted a string in place of a boolean.'
}

Test-Case 'The evidence schema rejects duplicate reordered and incomplete physical bindings' {
    $golden = Read-JsonDocument -LiteralPath $goldenPath
    $mutants = [ordered]@{}

    $mutants.archive_duplicate = Copy-JsonDocument -Value $golden
    $mutants.archive_duplicate.expected_bindings.qt_archives[1] = `
        Copy-JsonDocument -Value $mutants.archive_duplicate.expected_bindings.qt_archives[0]

    $mutants.qt_tools_reordered = Copy-JsonDocument -Value $golden
    $firstTool = Copy-JsonDocument -Value $mutants.qt_tools_reordered.expected_bindings.qt_tools[0]
    $mutants.qt_tools_reordered.expected_bindings.qt_tools[0] = `
        Copy-JsonDocument -Value $mutants.qt_tools_reordered.expected_bindings.qt_tools[1]
    $mutants.qt_tools_reordered.expected_bindings.qt_tools[1] = $firstTool

    $mutants.qt_artifacts_reordered = Copy-JsonDocument -Value $golden
    $workerScriptQmldir = Copy-JsonDocument -Value $mutants.qt_artifacts_reordered.expected_bindings.qt_artifacts[24]
    $mutants.qt_artifacts_reordered.expected_bindings.qt_artifacts[24] = `
        Copy-JsonDocument -Value $mutants.qt_artifacts_reordered.expected_bindings.qt_artifacts[25]
    $mutants.qt_artifacts_reordered.expected_bindings.qt_artifacts[25] = $workerScriptQmldir

    $mutants.qt_artifact_omitted = Copy-JsonDocument -Value $golden
    $mutants.qt_artifact_omitted.expected_bindings.qt_artifacts = @(
        $mutants.qt_artifact_omitted.expected_bindings.qt_artifacts |
            Where-Object { $_.id -cne 'qttest-plugin' }
    )

    $mutants.stage_manifest_hash = Copy-JsonDocument -Value $golden
    $mutants.stage_manifest_hash.expected_bindings.stage_manifest.sha256 = 'f' * 64

    foreach ($index in 0..($golden.expected_bindings.qt_archives.Count - 1)) {
        if ($golden.expected_bindings.qt_archives[$index].license.evidence_status -cin @(
            'physical_verified',
            'physical_sbom_noassertion_distribution_blocker'
        )) {
            $mutant = Copy-JsonDocument -Value $golden
            $mutant.expected_bindings.qt_archives[$index].license.PSObject.Properties.Remove('text_size_bytes')
            $mutants["qt_archive_license_size_$index"] = $mutant
        }
    }
    foreach ($index in 0..($golden.expected_bindings.qt_tools.Count - 1)) {
        $mutant = Copy-JsonDocument -Value $golden
        $mutant.expected_bindings.qt_tools[$index].license.PSObject.Properties.Remove('text_size_bytes')
        $mutants["qt_tool_license_size_$index"] = $mutant
    }
    foreach ($bindingName in @('catch2', 'nvda', 'extractor')) {
        $mutant = Copy-JsonDocument -Value $golden
        $mutant.expected_bindings.$bindingName.license.PSObject.Properties.Remove('text_size_bytes')
        $mutants["$($bindingName)_license_size"] = $mutant
    }

    $acceptedMutants = @(
        foreach ($entry in $mutants.GetEnumerator()) {
            $json = $entry.Value | ConvertTo-Json -Compress -Depth 100
            if (Test-Json -Json $json -SchemaFile $evidenceSchemaPath -ErrorAction SilentlyContinue) {
                $entry.Key
            }
        }
    )
    Assert-Equal -Actual $acceptedMutants.Count -Expected 0 `
        -Message "The evidence schema accepted physical-binding mutants: $($acceptedMutants -join ', ')."
}

Test-Case 'The synthetic golden is byte-identical in clean processes cultures and working roots' {
    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('music-player-m02-golden-processes-' + [Guid]::NewGuid().ToString('N'))
    $pathSentinelRoot = Join-Path $tempRoot 'path-shadow'
    $pathSentinel = Join-Path $tempRoot 'ambient-tool-executed.txt'
    $oldPath = $env:PATH
    $pwshPath = (Get-Command pwsh -ErrorAction Stop).Source
    try {
        New-Item -ItemType Directory -Path $pathSentinelRoot | Out-Null
        foreach ($name in @('curl.cmd', 'git.cmd', 'cmake.cmd', 'qmlformat.cmd', 'qmllint.cmd', 'pwsh.cmd')) {
            $source = "@echo off`r`necho ambient>$pathSentinel`r`nexit /b 197`r`n"
            [IO.File]::WriteAllText((Join-Path $pathSentinelRoot $name), $source, [Text.ASCIIEncoding]::new())
        }
        $env:PATH = $pathSentinelRoot
        $outputs = @()
        foreach ($case in @(
            [pscustomobject]@{ culture = 'tr-TR'; root = (Join-Path $tempRoot 'root-a') },
            [pscustomobject]@{ culture = 'fr-FR'; root = (Join-Path $tempRoot 'root-b') }
        )) {
            New-Item -ItemType Directory -Path $case.root | Out-Null
            $escapedModule = $modulePath.Replace("'", "''")
            $escapedRoot = $case.root.Replace("'", "''")
            $childSource = @"
`$ErrorActionPreference = 'Stop'
[Globalization.CultureInfo]::CurrentCulture = [Globalization.CultureInfo]::GetCultureInfo('$($case.culture)')
[Globalization.CultureInfo]::CurrentUICulture = [Globalization.CultureInfo]::GetCultureInfo('$($case.culture)')
Set-Location -LiteralPath '$escapedRoot'
Import-Module -Name '$escapedModule' -ErrorAction Stop
`$module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
`$bytes = & `$module { New-M0QtShellSyntheticGolden }
[Console]::Out.Write([Convert]::ToBase64String([byte[]]`$bytes))
"@
            $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($childSource))
            $result = & $pwshPath -NoLogo -NoProfile -EncodedCommand $encoded 2>&1 | Out-String
            Assert-Equal -Actual $LASTEXITCODE -Expected 0 -Message "The clean golden child failed for $($case.culture)."
            $outputs += $result.Trim()
            Assert-Equal -Actual @(Get-ChildItem -LiteralPath $case.root -Force).Count -Expected 0 `
                -Message "The pure golden builder mutated working root '$($case.culture)'."
        }
        Assert-Equal -Actual $outputs[0] -Expected $outputs[1] `
            -Message 'The synthetic golden changed across processes, cultures, or roots.'
        Assert-SequenceEqual -Actual @([Convert]::FromBase64String($outputs[0])) `
            -Expected @([IO.File]::ReadAllBytes($goldenPath)) `
            -Message 'Clean children did not reproduce the versioned synthetic golden.'
        Assert-True (-not (Test-Path -LiteralPath $pathSentinel)) `
            'The pure golden builder executed an ambient tool from PATH.'
    }
    finally {
        $env:PATH = $oldPath
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

Test-Case 'The path-leak detector distinguishes rooted paths from URLs and exact normalization tokens' {
    foreach ($sample in @(
        'C:\Temp\private.bin',
        'D:/opt/private.bin',
        '/opt/private.bin',
        '\\server\share\private.bin',
        '\\?\C:\Temp\private.bin',
        '\\?\UNC\server\share\private.bin',
        '\\.\PhysicalDrive0',
        'file:///C:/private.bin'
    )) {
        Assert-True (Test-M0QtShellSensitivePathValue -Value $sample -AllowScriptNormalizationToken) `
            "The path-leak detector did not recognize '$sample'."
    }
    foreach ($sample in @('/', '/qml', '\', '\\', '\\?\', '\\?\UNC\', 'https://example.invalid/license')) {
        Assert-True (-not (Test-M0QtShellSensitivePathValue -Value $sample -AllowScriptNormalizationToken)) `
            "The path-leak detector rejected the allowed token '$sample'."
    }
}

Test-Case 'Versioned provisioning documents contain no personal or host-bound absolute paths' {
    foreach ($path in @(
        $schemaPath,
        $evidenceSchemaPath,
        $stageManifestSchemaPath,
        $descriptorPath,
        $goldenPath,
        $stageManifestPath,
        $contractPath
    )) {
        $document = Read-JsonDocument -LiteralPath $path
        $leaks = @(Get-JsonStringAtoms -Value $document | Where-Object {
                Test-M0QtShellSensitivePathValue -Value $_.value
            })
        $details = @($leaks | ForEach-Object { "$($_.pointer) [$($_.role)] = $($_.value)" }) -join '; '
        Assert-Equal -Actual $leaks.Count -Expected 0 `
            -Message "Versioned JSON '$([IO.Path]::GetFileName($path))' leaked a personal or absolute path: $details"
    }

    $sidecarText = Get-Content -LiteralPath $contractSidecarPath -Raw
    Assert-True ($sidecarText -cmatch '^[0-9a-f]{64}\n\z') `
        'The contract sidecar contains content other than its canonical hash.'

    foreach ($path in @($modulePath, $validatorPath)) {
        $tokens = $null
        $parseErrors = $null
        $ast = [Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$parseErrors)
        Assert-Equal -Actual $parseErrors.Count -Expected 0 `
            -Message "Cannot inspect path literals in '$([IO.Path]::GetFileName($path))'."
        $leaks = @($ast.FindAll({
                    param($node)
                    $node -is [Management.Automation.Language.StringConstantExpressionAst] -or
                    $node -is [Management.Automation.Language.ExpandableStringExpressionAst]
                }, $true) | Where-Object {
                Test-M0QtShellSensitivePathValue -Value $_.Value -AllowScriptNormalizationToken
            })
        $details = @($leaks | ForEach-Object { "line $($_.Extent.StartLineNumber): $($_.Extent.Text)" }) -join '; '
        Assert-Equal -Actual $leaks.Count -Expected 0 `
            -Message "Versioned script '$([IO.Path]::GetFileName($path))' leaked a path: $details"
    }
}

Test-Case 'Canonical JSON leaves are strict UTF-8 without BOM or CR and end in one LF' {
    $strictUtf8 = [Text.UTF8Encoding]::new($false, $true)
    foreach ($path in @(
        $schemaPath,
        $evidenceSchemaPath,
        $stageManifestSchemaPath,
        $descriptorPath,
        $goldenPath,
        $stageManifestPath,
        $contractPath
    )) {
        $bytes = [IO.File]::ReadAllBytes($path)
        Assert-True (-not ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)) `
            "Canonical leaf '$([IO.Path]::GetFileName($path))' contains a UTF-8 BOM."
        $text = $strictUtf8.GetString($bytes)
        Assert-True ($text -cnotmatch "`r") "Canonical leaf '$([IO.Path]::GetFileName($path))' contains CR bytes."
        Assert-True ($text.EndsWith("`n", [StringComparison]::Ordinal)) `
            "Canonical leaf '$([IO.Path]::GetFileName($path))' lacks its final LF."
        Assert-True (-not $text.EndsWith("`n`n", [StringComparison]::Ordinal)) `
            "Canonical leaf '$([IO.Path]::GetFileName($path))' has more than one terminal LF."
        $null = $text | ConvertFrom-Json -Depth 100 -DateKind String
    }
}

Test-Case 'The provisioning contract shape is closed canonical and validated before leaf use' {
    Import-Module $modulePath -ErrorAction Stop
    $module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
    $valid = [pscustomobject]@{
        files = @($expectedContractLeafPaths | ForEach-Object {
                [pscustomobject]@{ path = $_; sha256 = ('a' * 64); size_bytes = 1L }
            })
    }
    $accepted = & $module {
        param($document)
        Assert-M0QtShellContractDocument -ContractDocument $document
    } $valid
    Assert-SequenceEqual -Actual @($accepted.path) -Expected $expectedContractLeafPaths `
        -Message 'The pure contract validator changed the exact ordered inventory.'

    $mutants = [Collections.Generic.List[object]]::new()
    $extraRoot = Copy-JsonDocument -Value $valid
    $extraRoot | Add-Member -NotePropertyName extra -NotePropertyValue $true
    $mutants.Add($extraRoot)
    $extraLeafField = Copy-JsonDocument -Value $valid
    $extraLeafField.files[0] | Add-Member -NotePropertyName extra -NotePropertyValue $true
    $mutants.Add($extraLeafField)
    $missingLeafField = Copy-JsonDocument -Value $valid
    $missingLeafField.files[0].PSObject.Properties.Remove('sha256')
    $mutants.Add($missingLeafField)
    $uppercaseHash = Copy-JsonDocument -Value $valid
    $uppercaseHash.files[0].sha256 = 'A' * 64
    $mutants.Add($uppercaseHash)
    $negativeSize = Copy-JsonDocument -Value $valid
    $negativeSize.files[0].size_bytes = -1L
    $mutants.Add($negativeSize)
    $fractionalSize = Copy-JsonDocument -Value $valid
    $fractionalSize.files[0].size_bytes = 1.5
    $mutants.Add($fractionalSize)
    $wrongOrder = Copy-JsonDocument -Value $valid
    $swap = $wrongOrder.files[0]
    $wrongOrder.files[0] = $wrongOrder.files[1]
    $wrongOrder.files[1] = $swap
    $mutants.Add($wrongOrder)

    foreach ($mutant in $mutants) {
        Assert-Throws {
            & $module {
                param($document)
                Assert-M0QtShellContractDocument -ContractDocument $document
            } $mutant
        } 'M0QT011.*contract|shape|inventory|hash|size'
    }
}

Test-Case 'The independent contract anchor closes every executable provisioning leaf' {
    Assert-Equal -Actual (Get-Content -LiteralPath $contractSidecarPath -Raw) `
        -Expected ($expectedContractSha256 + "`n") `
        -Message 'The contract sidecar does not equal the external expected anchor byte-for-byte.'
    Assert-Equal -Actual (Get-Sha256 -LiteralPath $contractPath) -Expected $expectedContractSha256 `
        -Message 'The tracked provisioning contract does not match the external expected anchor.'

    Import-Module $modulePath -ErrorAction Stop
    $module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
    Assert-SequenceEqual -Actual @($module.ExportedCommands.Keys | Sort-Object) `
        -Expected @('Assert-M0QtShellReceipt', 'Test-M0QtShellToolchain') `
        -Message 'The provisioning module exposes an unapproved API.'

    $contract = & $module {
        param($root, $expected)
        Assert-M0QtShellProvisioningContract -RepositoryRoot $root -ExpectedContractSha256 $expected
    } $repositoryRoot $expectedContractSha256
    Assert-Equal -Actual $contract.contract_sha256 -Expected $expectedContractSha256 `
        -Message 'The contract did not preserve the external expected anchor.'

    $contractDocument = Read-JsonDocument -LiteralPath $contractPath
    Assert-SequenceEqual -Actual @($contractDocument.files.path) -Expected $expectedContractLeafPaths `
        -Message 'The provisioning contract inventory is not closed and literal.'
    foreach ($entry in @($contractDocument.files)) {
        $physicalPath = Join-Path $repositoryRoot ($entry.path -replace '/', '\')
        Assert-Equal -Actual (Get-Sha256 -LiteralPath $physicalPath) -Expected $entry.sha256 `
            -Message "Contract hash drifted for '$($entry.path)'."
        Assert-Equal -Actual (Get-Item -LiteralPath $physicalPath).Length -Expected ([long]$entry.size_bytes) `
            -Message "Contract size drifted for '$($entry.path)'."
    }
}

Test-Case 'A self-consistent cloned or reanchored contract cannot replace the expected root' {
    Import-Module $modulePath -ErrorAction Stop
    $module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
    $cloneRoot = Join-Path ([IO.Path]::GetTempPath()) ('music-player-m02-contract-clone-' + [Guid]::NewGuid().ToString('N'))
    try {
        $cloneContractRoot = Join-Path $cloneRoot 'experiments\m0\provisioning\qt-shell'
        New-Item -ItemType Directory -Path $cloneContractRoot -Force | Out-Null
        foreach ($source in @(Get-ChildItem -LiteralPath $contractRoot -Force)) {
            Copy-Item -LiteralPath $source.FullName -Destination $cloneContractRoot -Recurse -Force
        }
        $cloneDescriptorPath = Join-Path $cloneContractRoot 'qt-shell-toolchain.v1.json'
        $cloneDescriptor = Read-JsonDocument -LiteralPath $cloneDescriptorPath
        $cloneDescriptor.qt.archives[0].license.authoritative_url = 'https://example.invalid/reanchored-license'
        $cloneText = ($cloneDescriptor | ConvertTo-Json -Compress -Depth 100) + "`n"
        [IO.File]::WriteAllText($cloneDescriptorPath, $cloneText, [Text.UTF8Encoding]::new($false))

        $cloneContractPath = Join-Path $cloneContractRoot 'qt-shell-toolchain-contract.v1.json'
        $cloneContract = Read-JsonDocument -LiteralPath $cloneContractPath
        $descriptorEntry = @($cloneContract.files | Where-Object path -eq 'experiments/m0/provisioning/qt-shell/qt-shell-toolchain.v1.json')
        Assert-Equal -Actual $descriptorEntry.Count -Expected 1 -Message 'The clone descriptor slot is ambiguous.'
        $descriptorEntry[0].sha256 = Get-Sha256 -LiteralPath $cloneDescriptorPath
        $descriptorEntry[0].size_bytes = (Get-Item -LiteralPath $cloneDescriptorPath).Length
        [IO.File]::WriteAllText($cloneContractPath, (($cloneContract | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText("$cloneContractPath.sha256", ((Get-Sha256 -LiteralPath $cloneContractPath) + "`n"), [Text.UTF8Encoding]::new($false))

        Assert-Throws {
            & $module {
                param($root, $contractRootOverride, $expected)
                Assert-M0QtShellProvisioningContract -RepositoryRoot $root `
                    -ContractRoot $contractRootOverride -ExpectedContractSha256 $expected
            } $cloneRoot $cloneContractRoot $expectedContractSha256
        } 'contract|expected|root|hash'
    }
    finally {
        if (Test-Path -LiteralPath $cloneRoot) {
            Remove-Item -LiteralPath $cloneRoot -Recurse -Force
        }
    }
}

Test-Case 'Trusted-file validation rejects reparse points hardlinks and path aliases' {
    if (-not $IsWindows) {
        Assert-M0QtShellNonWindowsFailClosed -CaseId 'trusted-file'
        return
    }

    Import-Module $modulePath -ErrorAction Stop
    $module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('music-player-m02-file-trust-' + [Guid]::NewGuid().ToString('N'))
    try {
        New-Item -ItemType Directory -Path $tempRoot | Out-Null
        $leaf = Join-Path $tempRoot 'leaf.bin'
        [IO.File]::WriteAllBytes($leaf, [byte[]](1, 2, 3, 4))
        $hardlink = Join-Path $tempRoot 'leaf-hardlink.bin'
        New-Item -ItemType HardLink -Path $hardlink -Target $leaf | Out-Null
        Assert-Throws {
            & $module { param($path) Open-M0QtShellTrustedFile -LiteralPath $path } $leaf
        } 'link|single|identity'

        $outside = Join-Path $tempRoot 'outside'
        $contained = Join-Path $tempRoot 'contained'
        New-Item -ItemType Directory -Path $outside, $contained | Out-Null
        $outsideLeaf = Join-Path $outside 'outside.bin'
        [IO.File]::WriteAllBytes($outsideLeaf, [byte[]](5, 6, 7, 8))
        $junction = Join-Path $contained 'junction'
        New-Item -ItemType Junction -Path $junction -Target $outside | Out-Null
        Assert-Throws {
            & $module { param($path) Open-M0QtShellTrustedFile -LiteralPath $path } (Join-Path $junction 'outside.bin')
        } 'reparse|link|final'
        $adsLeaf = Join-Path $tempRoot 'ads-leaf.bin'
        [IO.File]::WriteAllBytes($adsLeaf, [byte[]](9, 10, 11, 12))
        [IO.File]::WriteAllText(($adsLeaf + ':stream'), 'ads', [Text.UTF8Encoding]::new($false))
        Assert-Throws {
            & $module { param($path) Open-M0QtShellTrustedFile -LiteralPath $path } ($adsLeaf + ':stream')
        } 'stream|ADS|path'
    }
    finally {
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

Test-Case 'Physical leaf and authenticity checks reject byte drift and unsigned NVDA substitutes' {
    if (-not $IsWindows) {
        Assert-M0QtShellNonWindowsFailClosed -CaseId 'physical-authenticity'
        return
    }

    Import-Module $modulePath -ErrorAction Stop
    $module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('music-player-m02-physical-leaf-' + [Guid]::NewGuid().ToString('N'))
    try {
        New-Item -ItemType Directory -Path $tempRoot | Out-Null
        $leaf = Join-Path $tempRoot 'leaf.bin'
        [IO.File]::WriteAllBytes($leaf, [byte[]](1, 2, 3, 4))
        $expectedHash = Get-Sha256 -LiteralPath $leaf
        $trusted = & $module {
            param($path, $hash)
            Open-M0QtShellTrustedFile -LiteralPath $path -ExpectedSha256 $hash -ExpectedSizeBytes 4
        } $leaf $expectedHash
        try {
            Assert-Equal -Actual $trusted.sha256 -Expected $expectedHash -Message 'Trusted-file hash projection drifted.'
        }
        finally {
            if ($trusted -and $trusted.PSObject.Properties['stream']) {
                $trusted.stream.Dispose()
            }
        }

        [IO.File]::WriteAllBytes($leaf, [byte[]](1, 2, 3, 5))
        Assert-Throws {
            & $module {
                param($path, $hash)
                Open-M0QtShellTrustedFile -LiteralPath $path -ExpectedSha256 $hash -ExpectedSizeBytes 4
            } $leaf $expectedHash
        } 'hash|bytes|drift'

        $unsignedExe = Join-Path $tempRoot 'nvda.exe'
        [IO.File]::WriteAllBytes($unsignedExe, [byte[]](0, 1, 2, 3))
        Assert-Throws {
            & $module {
                param($path)
                Assert-M0QtShellAuthenticode -LiteralPath $path `
                    -ExpectedSubject 'CN=NV Access Limited, O=NV Access Limited, L=Camp Mountain, S=Queensland, C=AU' `
                    -ExpectedThumbprint '4E9A772A99C1A3B713766DAC4B3545236CC2AE72'
            } $unsignedExe
        } 'signature|Authenticode|Valid'
    }
    finally {
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

Test-Case 'Archive member validation rejects traversal aliases and non-regular entries' {
    Import-Module $modulePath -ErrorAction Stop
    $module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
    $invalidInventories = @(
        [pscustomobject]@{ case_id = 'parent'; members = @([pscustomobject]@{ path = '../escape.exe'; kind = 'file' }) },
        [pscustomobject]@{ case_id = 'drive'; members = @([pscustomobject]@{ path = 'C:/escape.exe'; kind = 'file' }) },
        [pscustomobject]@{ case_id = 'rooted'; members = @([pscustomobject]@{ path = '/escape.exe'; kind = 'file' }) },
        [pscustomobject]@{ case_id = 'unc'; members = @([pscustomobject]@{ path = '//server/share/escape.exe'; kind = 'file' }) },
        [pscustomobject]@{ case_id = 'ads'; members = @([pscustomobject]@{ path = 'safe.exe:stream'; kind = 'file' }) },
        [pscustomobject]@{ case_id = 'symlink'; members = @([pscustomobject]@{ path = 'safe/link'; kind = 'symlink' }) },
        [pscustomobject]@{ case_id = 'case-alias'; members = @(
            [pscustomobject]@{ path = 'bin/tool.exe'; kind = 'file' },
            [pscustomobject]@{ path = 'BIN/TOOL.EXE'; kind = 'file' }
        ) }
    )
    foreach ($invalid in $invalidInventories) {
        Assert-Throws {
            & $module { param($members) Assert-M0QtShellArchiveMembers -Members $members } $invalid.members
        } 'archive|member|path|duplicate|link'
    }

    $valid = @(
        [pscustomobject]@{ path = 'x64/7za.exe'; kind = 'file' },
        [pscustomobject]@{ path = 'License.txt'; kind = 'file' }
    )
    & $module { param($members) Assert-M0QtShellArchiveMembers -Members $members } $valid
}

Test-Case 'Raw 7-Zip listings and extracted inventories fail closed on aliases links and extra leaves' {
    Import-Module $modulePath -ErrorAction Stop
    $module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
    $validListing = @'
Path = C:\staging\extractor.7z
Type = 7z

----------
Path = x64/7za.exe
Size = 1334784
Attributes = A -rw-r--r--

Path = License.txt
Size = 5188
Attributes = A -rw-r--r--
'@
    $members = & $module { param($text) ConvertFrom-M0QtShell7ZipSltListing -Text $text } $validListing
    Assert-SequenceEqual -Actual @($members.path) -Expected @('x64/7za.exe', 'License.txt') `
        -Message 'The raw 7-Zip listing parser did not preserve the closed member inventory.'
    & $module { param($parsed) Assert-M0QtShellArchiveMembers -Members $parsed } $members

    foreach ($badListing in @(
        ($validListing + "`nPath = ../escape.exe`nSize = 1`nAttributes = A -rw-r--r--`n"),
        ($validListing + "`nPath = X64/7ZA.EXE`nSize = 1`nAttributes = A -rw-r--r--`n"),
        ($validListing + "`nPath = safe-link`nSize = 0`nAttributes = A -rw-r--r--`nSymbolic Link = outside`n")
    )) {
        Assert-Throws {
            $parsed = & $module { param($text) ConvertFrom-M0QtShell7ZipSltListing -Text $text } $badListing
            & $module { param($items) Assert-M0QtShellArchiveMembers -Members $items } $parsed
        } 'archive|member|path|duplicate|link'
    }

    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('music-player-m02-extracted-inventory-' + [Guid]::NewGuid().ToString('N'))
    try {
        New-Item -ItemType Directory -Path (Join-Path $tempRoot 'x64') -Force | Out-Null
        [IO.File]::WriteAllBytes((Join-Path $tempRoot 'x64\7za.exe'), [byte[]](1, 2, 3))
        [IO.File]::WriteAllText((Join-Path $tempRoot 'License.txt'), 'license', [Text.UTF8Encoding]::new($false))
        & $module {
            param($root)
            Assert-M0QtShellExtractedInventory -Root $root -ExpectedRelativePaths @('x64/7za.exe', 'License.txt')
        } $tempRoot

        [IO.File]::WriteAllText((Join-Path $tempRoot 'extra.txt'), 'extra', [Text.UTF8Encoding]::new($false))
        Assert-Throws {
            & $module {
                param($root)
                Assert-M0QtShellExtractedInventory -Root $root -ExpectedRelativePaths @('x64/7za.exe', 'License.txt')
            } $tempRoot
        } 'inventory|extra|unexpected'
        Remove-Item -LiteralPath (Join-Path $tempRoot 'extra.txt') -Force

        if ($IsWindows) {
            $outside = Join-Path ([IO.Path]::GetTempPath()) ('music-player-m02-extracted-outside-' + [Guid]::NewGuid().ToString('N'))
            try {
                New-Item -ItemType Directory -Path $outside | Out-Null
                New-Item -ItemType Junction -Path (Join-Path $tempRoot 'redirected') -Target $outside | Out-Null
                Assert-Throws {
                    & $module {
                        param($root)
                        Assert-M0QtShellExtractedInventory -Root $root -ExpectedRelativePaths @('x64/7za.exe', 'License.txt')
                    } $tempRoot
                } 'reparse|link|inventory'
            }
            finally {
                if (Test-Path -LiteralPath (Join-Path $tempRoot 'redirected')) {
                    Remove-Item -LiteralPath (Join-Path $tempRoot 'redirected') -Force
                }
                if (Test-Path -LiteralPath $outside) {
                    Remove-Item -LiteralPath $outside -Recurse -Force
                }
            }
        }
    }
    finally {
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

Test-Case 'The bounded process helper returns only after its Job is empty on timeout output cap or root exit zero' {
    if (-not $IsWindows) {
        Assert-M0QtShellNonWindowsFailClosed -CaseId 'bounded-process'
        return
    }

    Import-Module $modulePath -ErrorAction Stop
    $module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('music-player-m02-timeout-' + [Guid]::NewGuid().ToString('N'))
    $childScript = Join-Path $tempRoot 'timeout-child.ps1'
    $grandchildPidPath = Join-Path $tempRoot 'grandchild.pid'
    $successfulRootScript = Join-Path $tempRoot 'successful-root-child.ps1'
    $sleeperScript = Join-Path $tempRoot 'persistent-sleeper.ps1'
    $successfulChildPidPath = Join-Path $tempRoot 'successful-root-child.pid'
    $successfulChildStartedPath = Join-Path $tempRoot 'successful-root-child.started'
    $jobBoundSuspendedHookPath = Join-Path $tempRoot 'after-job-bound-suspended-before-resume.hook'
    $abruptOwnerScript = Join-Path $tempRoot 'abrupt-owner.ps1'
    $abruptOwnerHookPath = Join-Path $tempRoot 'abrupt-owner-suspended-root.pid'
    $abruptChildPidPath = Join-Path $tempRoot 'abrupt-owner-child.pid'
    $abruptChildStartedPath = Join-Path $tempRoot 'abrupt-owner-child.started'
    $successfulChildPid = $null
    $abruptSuspendedRootPid = $null
    $abruptChildPid = $null
    $abruptOwnerProcess = $null
    try {
        New-Item -ItemType Directory -Path $tempRoot | Out-Null
        $childSource = @'
param([string] $GrandchildPidPath)
$grandchild = Start-Process -FilePath pwsh -ArgumentList @('-NoLogo','-NoProfile','-Command','Start-Sleep -Seconds 120') -PassThru -WindowStyle Hidden
[IO.File]::WriteAllText($GrandchildPidPath, [string]$grandchild.Id, [Text.UTF8Encoding]::new($false))
Start-Sleep -Seconds 120
'@
        [IO.File]::WriteAllText($childScript, $childSource, [Text.UTF8Encoding]::new($false))
        Assert-Throws {
            & $module {
                param($scriptPath, $pidPath)
                Invoke-M0QtShellBoundedProcess -FilePath (Get-Command pwsh -ErrorAction Stop).Source `
                    -ArgumentList @('-NoLogo', '-NoProfile', '-File', $scriptPath, '-GrandchildPidPath', $pidPath) `
                    -TimeoutSeconds 2 -MaximumCapturedBytes 4096
            } $childScript $grandchildPidPath
        } 'timeout'

        Assert-True (Test-Path -LiteralPath $grandchildPidPath -PathType Leaf) `
            'The timeout fixture never proved that its grandchild started.'
        $grandchildPid = [int][IO.File]::ReadAllText($grandchildPidPath)
        Assert-True (-not (Get-Process -Id $grandchildPid -ErrorAction SilentlyContinue)) `
            'The timeout helper returned before its owned Job became empty.'

        Assert-Throws {
            & $module {
                Invoke-M0QtShellBoundedProcess -FilePath (Get-Command pwsh -ErrorAction Stop).Source `
                    -ArgumentList @('-NoLogo', '-NoProfile', '-Command', 'for($i=0;$i-lt20000;$i++){[Console]::Out.Write("0123456789abcdef")}') `
                    -TimeoutSeconds 10 -MaximumCapturedBytes 4096
            }
        } 'captured|output|limit'
        Assert-Throws {
            & $module {
                Invoke-M0QtShellBoundedProcess -FilePath (Get-Command pwsh -ErrorAction Stop).Source `
                    -ArgumentList @('-NoLogo', '-NoProfile', '-Command', 'for($i=0;$i-lt20000;$i++){[Console]::Error.Write("0123456789abcdef")}') `
                    -TimeoutSeconds 10 -MaximumCapturedBytes 4096
            }
        } 'captured|output|limit'

        $stderrResult = & $module {
            Invoke-M0QtShellBoundedProcess -FilePath (Get-Command pwsh -ErrorAction Stop).Source `
                -ArgumentList @('-NoLogo', '-NoProfile', '-Command', '[Console]::Error.Write("bounded-error"); exit 7') `
                -TimeoutSeconds 10 -MaximumCapturedBytes 4096 -AllowNonZeroExit
        }
        Assert-Equal -Actual $stderrResult.exit_code -Expected 7 -Message 'The bounded runner lost the process exit code.'
        Assert-Equal -Actual $stderrResult.stdout -Expected '' -Message 'The bounded runner mixed stderr into stdout.'
        Assert-Equal -Actual $stderrResult.stderr -Expected 'bounded-error' -Message 'The bounded runner lost stderr-only output.'
        Assert-True ([Text.Encoding]::UTF8.GetByteCount($stderrResult.stderr) -le 4096) `
            'The bounded runner exceeded its output cap.'

        $sleeperSource = @'
param([Parameter(Mandatory)][string] $StartedPath)
[IO.File]::WriteAllText($StartedPath, 'started', [Text.UTF8Encoding]::new($false))
Start-Sleep -Seconds 120
'@
        [IO.File]::WriteAllText($sleeperScript, $sleeperSource, [Text.UTF8Encoding]::new($false))
        $successfulRootSource = @'
param(
    [Parameter(Mandatory)][string] $ChildPidPath,
    [Parameter(Mandatory)][string] $ChildStartedPath,
    [Parameter(Mandatory)][string] $PwshPath,
    [Parameter(Mandatory)][string] $SleeperScript
)
$child = Start-Process -FilePath $PwshPath `
    -ArgumentList @('-NoLogo', '-NoProfile', '-File', $SleeperScript, '-StartedPath', $ChildStartedPath) `
    -PassThru -WindowStyle Hidden
$deadline = [DateTime]::UtcNow.AddSeconds(5)
while (-not [IO.File]::Exists($ChildStartedPath) -and [DateTime]::UtcNow -lt $deadline) {
    Start-Sleep -Milliseconds 25
}
if (-not [IO.File]::Exists($ChildStartedPath)) { throw 'persistent child did not start' }
[IO.File]::WriteAllText($ChildPidPath, [string]$child.Id, [Text.UTF8Encoding]::new($false))
exit 0
'@
        [IO.File]::WriteAllText($successfulRootScript, $successfulRootSource, [Text.UTF8Encoding]::new($false))
        $pwshPath = (Get-Command pwsh -ErrorAction Stop).Source
        & $module {
            param($hookPath, $startedPath, $pidPath)
            $script:M0QtShellProvisioningTestHook = [pscustomobject]@{
                after_job_bound_suspended_before_resume = {
                    param([int] $SuspendedProcessId)
                    if ([IO.File]::Exists($startedPath) -or [IO.File]::Exists($pidPath)) {
                        throw 'The root process executed before its job-bound suspended launch was resumed.'
                    }
                    [IO.File]::WriteAllText($hookPath, [string]$SuspendedProcessId, [Text.UTF8Encoding]::new($false))
                    Start-Sleep -Seconds 2
                }.GetNewClosure()
            }
        } $jobBoundSuspendedHookPath $successfulChildStartedPath $successfulChildPidPath
        try {
            $successfulResult = & $module {
                param($pwsh, $rootScript, $pidPath, $startedPath, $sleepScript)
                Invoke-M0QtShellBoundedProcess -FilePath $pwsh `
                    -ArgumentList @(
                        '-NoLogo',
                        '-NoProfile',
                        '-File',
                        $rootScript,
                        '-ChildPidPath',
                        $pidPath,
                        '-ChildStartedPath',
                        $startedPath,
                        '-PwshPath',
                        $pwsh,
                        '-SleeperScript',
                        $sleepScript
                    ) -TimeoutSeconds 10 -MaximumCapturedBytes 4096
            } $pwshPath $successfulRootScript $successfulChildPidPath $successfulChildStartedPath $sleeperScript
        }
        finally {
            & $module { $script:M0QtShellProvisioningTestHook = $null }
        }
        Assert-True (Test-Path -LiteralPath $jobBoundSuspendedHookPath -PathType Leaf) `
            'The bounded runner did not expose the causal job-bound-suspended-before-resume hook.'
        Assert-Equal -Actual $successfulResult.exit_code -Expected 0 `
            -Message 'The successful root-process fixture did not exit zero.'
        Assert-True (Test-Path -LiteralPath $successfulChildPidPath -PathType Leaf) `
            'The successful root-process fixture never proved that its child started.'
        Assert-True (Test-Path -LiteralPath $successfulChildStartedPath -PathType Leaf) `
            'The persistent child never reached its start handshake.'
        $successfulChildPid = [int][IO.File]::ReadAllText($successfulChildPidPath)
        $successfulChildSurvived = $null -ne (Get-Process -Id $successfulChildPid -ErrorAction SilentlyContinue)
        if ($successfulChildSurvived) {
            Stop-Process -Id $successfulChildPid -Force -ErrorAction SilentlyContinue
            Wait-Process -Id $successfulChildPid -Timeout 5 -ErrorAction SilentlyContinue
        }
        Assert-True (-not $successfulChildSurvived) `
            'The bounded runner returned before the successful root Job became empty.'

        $abruptOwnerSource = @'
param(
    [Parameter(Mandatory)][string] $ModulePath,
    [Parameter(Mandatory)][string] $PwshPath,
    [Parameter(Mandatory)][string] $RootScript,
    [Parameter(Mandatory)][string] $ChildPidPath,
    [Parameter(Mandatory)][string] $ChildStartedPath,
    [Parameter(Mandatory)][string] $SleeperScript,
    [Parameter(Mandatory)][string] $HookPath
)
$ErrorActionPreference = 'Stop'
Import-Module $ModulePath -ErrorAction Stop
$module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
& $module {
    param($hookPath)
    $script:M0QtShellProvisioningTestHook = [pscustomobject]@{
        after_job_bound_suspended_before_resume = {
            param([int] $SuspendedProcessId)
            [IO.File]::WriteAllText($hookPath, [string]$SuspendedProcessId, [Text.UTF8Encoding]::new($false))
            Start-Sleep -Seconds 120
        }.GetNewClosure()
    }
} $HookPath
& $module {
    param($pwsh, $rootScript, $pidPath, $startedPath, $sleepScript)
    Invoke-M0QtShellBoundedProcess -FilePath $pwsh `
        -ArgumentList @(
            '-NoLogo', '-NoProfile', '-File', $rootScript,
            '-ChildPidPath', $pidPath,
            '-ChildStartedPath', $startedPath,
            '-PwshPath', $pwsh,
            '-SleeperScript', $sleepScript
        ) -TimeoutSeconds 30 -MaximumCapturedBytes 4096
} $PwshPath $RootScript $ChildPidPath $ChildStartedPath $SleeperScript
'@
        [IO.File]::WriteAllText($abruptOwnerScript, $abruptOwnerSource, [Text.UTF8Encoding]::new($false))
        $ownerStart = [Diagnostics.ProcessStartInfo]::new($pwshPath)
        $ownerStart.UseShellExecute = $false
        $ownerStart.CreateNoWindow = $true
        foreach ($argument in @(
            '-NoLogo', '-NoProfile', '-File', $abruptOwnerScript,
            '-ModulePath', $modulePath,
            '-PwshPath', $pwshPath,
            '-RootScript', $successfulRootScript,
            '-ChildPidPath', $abruptChildPidPath,
            '-ChildStartedPath', $abruptChildStartedPath,
            '-SleeperScript', $sleeperScript,
            '-HookPath', $abruptOwnerHookPath
        )) {
            $ownerStart.ArgumentList.Add([string]$argument)
        }
        $abruptOwnerProcess = [Diagnostics.Process]::Start($ownerStart)
        $hookDeadline = [DateTime]::UtcNow.AddSeconds(10)
        while (-not [IO.File]::Exists($abruptOwnerHookPath) -and [DateTime]::UtcNow -lt $hookDeadline) {
            Start-Sleep -Milliseconds 25
        }
        Assert-True (Test-Path -LiteralPath $abruptOwnerHookPath -PathType Leaf) `
            'The abrupt-owner fixture never reached the suspended-before-resume hook.'
        $abruptSuspendedRootPid = [int][IO.File]::ReadAllText($abruptOwnerHookPath)
        Assert-True ($null -ne (Get-Process -Id $abruptSuspendedRootPid -ErrorAction SilentlyContinue)) `
            'The hook did not identify a live suspended root process.'
        Assert-True (-not (Test-Path -LiteralPath $abruptChildStartedPath)) `
            'The suspended root executed before the abrupt owner was terminated.'
        Assert-True (-not (Test-Path -LiteralPath $abruptChildPidPath)) `
            'The suspended root spawned a child before resume.'
        $abruptOwnerProcess.Kill()
        $abruptOwnerProcess.WaitForExit(5000) | Out-Null
        Start-Sleep -Milliseconds 500
        $abruptRootSurvived = $null -ne (Get-Process -Id $abruptSuspendedRootPid -ErrorAction SilentlyContinue)
        if ($abruptRootSurvived) {
            Stop-Process -Id $abruptSuspendedRootPid -Force -ErrorAction SilentlyContinue
        }
        Assert-True (-not $abruptRootSurvived) `
            'An abrupt launcher-owner exit left its job-bound suspended root alive.'
        Assert-True (-not (Test-Path -LiteralPath $abruptChildStartedPath)) `
            'An abrupt launcher-owner exit allowed the suspended root to execute.'
    }
    finally {
        if ($module) {
            & $module { $script:M0QtShellProvisioningTestHook = $null }
        }
        if ($null -eq $successfulChildPid -and (Test-Path -LiteralPath $successfulChildPidPath -PathType Leaf)) {
            $successfulChildPid = [int][IO.File]::ReadAllText($successfulChildPidPath)
        }
        if ($null -ne $successfulChildPid) {
            Stop-Process -Id $successfulChildPid -Force -ErrorAction SilentlyContinue
        }
        if ($null -ne $abruptOwnerProcess -and -not $abruptOwnerProcess.HasExited) {
            try { $abruptOwnerProcess.Kill() } catch { }
        }
        if ($null -eq $abruptChildPid -and (Test-Path -LiteralPath $abruptChildPidPath -PathType Leaf)) {
            $abruptChildPid = [int][IO.File]::ReadAllText($abruptChildPidPath)
        }
        if ($null -ne $abruptChildPid) {
            Stop-Process -Id $abruptChildPid -Force -ErrorAction SilentlyContinue
        }
        if ($null -ne $abruptSuspendedRootPid) {
            Stop-Process -Id $abruptSuspendedRootPid -Force -ErrorAction SilentlyContinue
        }
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

Test-Case 'The native bounded child receives a closed environment exact PATH and explicit working directory' {
    if (-not $IsWindows) {
        Assert-M0QtShellNonWindowsFailClosed -CaseId 'bounded-process-environment'
        return
    }

    Import-Module $modulePath -ErrorAction Stop
    $module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('music-player-m02-process-environment-' + [Guid]::NewGuid().ToString('N'))
    $workingDirectory = Join-Path $tempRoot 'explicit-working-directory'
    $childPath = Join-Path $tempRoot 'environment-child.cmd'
    $cmdCopyPath = Join-Path $tempRoot 'cmd-single-link.exe'
    $sanitizedNames = @(
        'QML_IMPORT_PATH',
        'QML2_IMPORT_PATH',
        'QT_PLUGIN_PATH',
        'QT_QPA_PLATFORM_PLUGIN_PATH',
        'QT_QPA_PLATFORM',
        'QT_QUICK_CONTROLS_STYLE',
        'QML_DISABLE_DISK_CACHE',
        'QTDIR',
        'QT_SELECT',
        'HTTP_PROXY',
        'HTTPS_PROXY',
        'ALL_PROXY'
    )
    $pollutedNames = @($sanitizedNames) + @('PATH', 'PATHEXT', 'PSModulePath', 'NO_PROXY', 'COMSPEC', 'PROMPT')
    $oldValues = @{}
    try {
        New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
        New-Item -ItemType Directory -Path $workingDirectory -Force | Out-Null
        $windowsRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
        $system32 = Join-Path $windowsRoot 'System32'
        $systemCmdPath = Join-Path $system32 'cmd.exe'
        [IO.File]::Copy($systemCmdPath, $cmdCopyPath, $false)
        Assert-Equal -Actual (Get-Sha256 -LiteralPath $cmdCopyPath) `
            -Expected (Get-Sha256 -LiteralPath $systemCmdPath) `
            -Message 'The native environment dumper copy drifted from System32 cmd.exe.'
        $childSource = "@echo off`r`ncd`r`nset`r`n"
        [IO.File]::WriteAllText($childPath, $childSource, [Text.ASCIIEncoding]::new())
        foreach ($name in $pollutedNames) {
            $oldValues[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
            [Environment]::SetEnvironmentVariable($name, "polluted-$($name.ToLowerInvariant())", 'Process')
        }

        $result = & $module {
            param($cmdPath, $scriptPath, $workingRoot)
            Invoke-M0QtShellBoundedProcess -FilePath $cmdPath `
                -ArgumentList @('/D', '/C', $scriptPath) `
                -WorkingDirectory $workingRoot -TimeoutSeconds 10 -MaximumCapturedBytes 16384
        } $cmdCopyPath $childPath $workingDirectory
        $outputLines = @($result.stdout -split '\r?\n')
        Assert-True ($outputLines.Count -gt 1) 'The native environment dumper returned no environment block.'
        Assert-Equal -Actual $outputLines[0] -Expected ([IO.Path]::GetFullPath($workingDirectory)) `
            -Message 'The bounded runner ignored its explicit working directory.'
        $nativeEnvironment = @{}
        foreach ($line in @($outputLines | Select-Object -Skip 1)) {
            if ([string]::IsNullOrEmpty($line)) { continue }
            $separator = $line.IndexOf('=')
            if ($separator -le 0) { continue }
            $nativeEnvironment[$line.Substring(0, $separator)] = $line.Substring($separator + 1)
        }
        $expectedChildPath = @(
            [IO.Path]::GetDirectoryName($cmdCopyPath),
            $system32
        ) -join [IO.Path]::PathSeparator
        $expectedChildPathSegments = @($expectedChildPath -split [regex]::Escape([IO.Path]::PathSeparator))
        $ambientMachineAndUserPathSegments = @(
            [Environment]::GetEnvironmentVariable('PATH', [EnvironmentVariableTarget]::Machine)
            [Environment]::GetEnvironmentVariable('PATH', [EnvironmentVariableTarget]::User)
        ) -split [regex]::Escape([IO.Path]::PathSeparator) |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            Sort-Object -Unique
        $ambientPathInjectionWitnesses = @(
            $ambientMachineAndUserPathSegments |
                Where-Object { $_.TrimEnd('\') -notin @($expectedChildPathSegments | ForEach-Object { $_.TrimEnd('\') }) }
        )
        Assert-True ($ambientPathInjectionWitnesses.Count -gt 0) `
            'The host fixture did not expose a Machine/User PATH segment capable of proving inheritance rejection.'
        $childPathSegments = @($nativeEnvironment['PATH'] -split [regex]::Escape([IO.Path]::PathSeparator))
        $leakedAmbientPathSegments = @(
            $childPathSegments |
                Where-Object { $_.TrimEnd('\') -in @($ambientPathInjectionWitnesses | ForEach-Object { $_.TrimEnd('\') }) }
        )
        Assert-Equal -Actual $leakedAmbientPathSegments.Count -Expected 0 `
            -Message "The bounded runner inherited Machine/User PATH segments: $($leakedAmbientPathSegments -join ', ')."
        Assert-Equal -Actual $nativeEnvironment['PATH'] -Expected $expectedChildPath `
            -Message 'The bounded runner inherited or appended a Machine/User/Process PATH.'
        foreach ($name in $sanitizedNames) {
            Assert-True ([string]::IsNullOrEmpty([string]$nativeEnvironment[$name])) `
                "The bounded runner exposed ambient '$name' instead of removing or emptying it."
        }
        $allowedEnvironmentNames = @(
            'PATH',
            'SystemRoot',
            'WINDIR',
            'SystemDrive',
            'ProgramData',
            'TEMP',
            'TMP',
            'QML_IMPORT_PATH',
            'QML2_IMPORT_PATH',
            'QT_PLUGIN_PATH',
            'QT_QPA_PLATFORM_PLUGIN_PATH',
            'QT_QPA_PLATFORM',
            'QT_QUICK_CONTROLS_STYLE',
            'QML_DISABLE_DISK_CACHE',
            'QTDIR',
            'QT_SELECT',
            'HTTP_PROXY',
            'HTTPS_PROXY',
            'ALL_PROXY',
            'NO_PROXY',
            'COMSPEC',
            'PATHEXT',
            'PROMPT'
        )
        $ambientMachineAndUserNames = @(
            @([Environment]::GetEnvironmentVariables([EnvironmentVariableTarget]::Machine).Keys)
            @([Environment]::GetEnvironmentVariables([EnvironmentVariableTarget]::User).Keys)
        ) | Sort-Object -Unique
        $nativeEnvironmentNames = @($nativeEnvironment.Keys | Sort-Object)
        $inheritedNames = @(
            $nativeEnvironmentNames |
                Where-Object { $_ -in $ambientMachineAndUserNames -and $_ -notin $allowedEnvironmentNames }
        )
        Assert-Equal -Actual $inheritedNames.Count -Expected 0 `
            -Message "The bounded runner inherited Machine/User environment keys: $($inheritedNames -join ', ')."
        $unexpectedNames = @($nativeEnvironmentNames | Where-Object { $_ -notin $allowedEnvironmentNames })
        Assert-Equal -Actual $unexpectedNames.Count -Expected 0 `
            -Message "The native child received keys outside the closed environment: $($unexpectedNames -join ', ')."
        foreach ($forbiddenName in @('PSModulePath')) {
            Assert-True ($nativeEnvironmentNames -notcontains $forbiddenName) `
                "The native child inherited or synthesized forbidden '$forbiddenName'."
        }
        Assert-Equal -Actual $nativeEnvironment['COMSPEC'] -Expected $cmdCopyPath `
            -Message 'cmd.exe did not synthesize COMSPEC from the exact trusted executable path.'
        Assert-Equal -Actual $nativeEnvironment['PATHEXT'] -Expected '.COM;.EXE;.BAT;.CMD;.VBS;.JS;.WS;.MSC' `
            -Message 'cmd.exe synthesized an unexpected PATHEXT instead of its closed built-in value.'
        Assert-Equal -Actual $nativeEnvironment['PROMPT'] -Expected '$P$G' `
            -Message 'cmd.exe synthesized an unexpected prompt value.'
        Assert-Equal -Actual $nativeEnvironment['SystemRoot'] -Expected $windowsRoot `
            -Message 'The native child SystemRoot was not derived from the Windows known folder.'
        Assert-Equal -Actual $nativeEnvironment['WINDIR'] -Expected $windowsRoot `
            -Message 'The native child WINDIR was not derived from the Windows known folder.'
        Assert-Equal -Actual $nativeEnvironment['SystemDrive'] `
            -Expected ([IO.Path]::GetPathRoot($windowsRoot).TrimEnd([IO.Path]::DirectorySeparatorChar)) `
            -Message 'The native child SystemDrive was not derived from the Windows known folder.'
        Assert-Equal -Actual $nativeEnvironment['ProgramData'] `
            -Expected ([Environment]::GetFolderPath([Environment+SpecialFolder]::CommonApplicationData)) `
            -Message 'The native child ProgramData was not derived from its Windows known folder.'
        Assert-Equal -Actual $nativeEnvironment['NO_PROXY'] -Expected '*' `
            -Message 'The native child did not preserve the closed no-proxy value.'
    }
    finally {
        foreach ($name in $pollutedNames) {
            [Environment]::SetEnvironmentVariable($name, $oldValues[$name], 'Process')
        }
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

Test-Case 'The bounded runner rejects reserved aliases and malformed environment entries before launch' {
    if (-not $IsWindows) {
        Assert-M0QtShellNonWindowsFailClosed -CaseId 'bounded-process-environment-validation'
        return
    }

    Import-Module $modulePath -ErrorAction Stop
    $module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('music-player-m02-process-environment-validation-' + [Guid]::NewGuid().ToString('N'))
    $cmdCopyPath = Join-Path $tempRoot 'cmd-single-link.exe'
    $launchReached = $false
    try {
        New-Item -ItemType Directory -Path $tempRoot | Out-Null
        $systemCmdPath = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)) 'System32\cmd.exe'
        [IO.File]::Copy($systemCmdPath, $cmdCopyPath, $false)
        & $module {
            param([ref]$reached)
            $script:M0QtShellProvisioningTestHook = [pscustomobject]@{
                after_job_bound_suspended_before_resume = { $reached.Value = $true }.GetNewClosure()
            }
        } ([ref]$launchReached)

        $invalidEnvironments = @(
            @{ path = 'C:\untrusted' },
            @{ qml_import_path = 'C:\untrusted-qml' },
            @{ ('BAD' + [char]0 + 'NAME') = 'value' },
            @{ 'BAD=NAME' = 'value' },
            @{ SAFE_NAME = ('value' + [char]0 + 'suffix') }
        )
        foreach ($invalidEnvironment in $invalidEnvironments) {
            Assert-Throws {
                & $module {
                    param($exe, $environment, $temp)
                    Invoke-M0QtShellBoundedProcess -FilePath $exe -ArgumentList @('/D', '/C', 'exit 0') `
                        -Environment $environment -TempRoot $temp -TimeoutSeconds 5
                } $cmdCopyPath $invalidEnvironment $tempRoot
            } 'M0QT020.*environment|reserved|NUL|name'
            Assert-Equal -Actual $launchReached -Expected $false `
                -Message 'An invalid child environment reached suspended process creation.'
        }
        Assert-Throws {
            & $module {
                param($exe, $temp)
                Invoke-M0QtShellBoundedProcess -FilePath $exe `
                    -ArgumentList @('/D', '/C', ('exit 0' + [char]0 + ' & echo truncated')) `
                    -TempRoot $temp -TimeoutSeconds 5
            } $cmdCopyPath $tempRoot
        } 'M0QT020.*argument|NUL'
        Assert-Equal -Actual $launchReached -Expected $false `
            -Message 'A NUL-truncated process argument reached suspended process creation.'
    }
    finally {
        if ($module) { & $module { $script:M0QtShellProvisioningTestHook = $null } }
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

Test-Case 'The bounded runner revalidates pinned state before creation before resume and after process exit' {
    if (-not $IsWindows) {
        Assert-M0QtShellNonWindowsFailClosed -CaseId 'bounded-process-pinned-state-callback'
        return
    }

    Import-Module $modulePath -ErrorAction Stop
    $module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) `
        ('music-player-m02-pinned-state-callback-' + [Guid]::NewGuid().ToString('N'))
    $cmdCopyPath = Join-Path $tempRoot 'cmd-single-link.exe'
    $sentinel = Join-Path $tempRoot 'resumed.txt'
    try {
        New-Item -ItemType Directory -Path $tempRoot | Out-Null
        $systemCmdPath = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)) 'System32\cmd.exe'
        [IO.File]::Copy($systemCmdPath, $cmdCopyPath, $false)

        $validationCount = 0
        $result = & $module {
            param($exe, $temp, [ref]$count)
            $callback = { $count.Value++ }.GetNewClosure()
            Invoke-M0QtShellBoundedProcess -FilePath $exe -ArgumentList @('/D', '/C', 'exit 0') `
                -TempRoot $temp -TimeoutSeconds 5 -PinnedStateValidation $callback
        } $cmdCopyPath $tempRoot ([ref]$validationCount)
        Assert-Equal -Actual $result.exit_code -Expected 0 -Message 'The successful callback probe did not exit zero.'
        Assert-Equal -Actual $validationCount -Expected 3 `
            -Message 'Pinned state was not validated at all three required process boundaries.'

        foreach ($failureAt in @(1, 2, 3)) {
            if (Test-Path -LiteralPath $sentinel) { Remove-Item -LiteralPath $sentinel -Force }
            $validationCount = 0
            Assert-Throws {
                & $module {
                    param($exe, $temp, $outputSentinel, $failAt, [ref]$count)
                    $callback = {
                        $count.Value++
                        if ($count.Value -eq $failAt) {
                            throw [IO.InvalidDataException]::new("pinned-state-drift-$failAt")
                        }
                    }.GetNewClosure()
                    Invoke-M0QtShellBoundedProcess -FilePath $exe `
                        -ArgumentList @('/D', '/C', "echo resumed>$outputSentinel") `
                        -TempRoot $temp -TimeoutSeconds 5 -PinnedStateValidation $callback
                } $cmdCopyPath $tempRoot $sentinel $failureAt ([ref]$validationCount)
            } "pinned-state-drift-$failureAt"
            Assert-Equal -Actual $validationCount -Expected $failureAt `
                -Message "Pinned-state callback did not fail at boundary $failureAt."
            Assert-Equal -Actual (Test-Path -LiteralPath $sentinel) -Expected ($failureAt -eq 3) `
                -Message "Process resume behavior differed for callback failure boundary $failureAt."
        }
    }
    finally {
        if ($module) { & $module { $script:M0QtShellProvisioningTestHook = $null } }
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

Test-Case 'The bounded runner rejects an executable ancestor swap before process creation' {
    if (-not $IsWindows) {
        Assert-M0QtShellNonWindowsFailClosed -CaseId 'bounded-process-executable-ancestor'
        return
    }

    Import-Module $modulePath -ErrorAction Stop
    $module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('music-player-m02-process-executable-swap-' + [Guid]::NewGuid().ToString('N'))
    $originalParent = Join-Path $tempRoot 'original'
    $movedParent = Join-Path $tempRoot 'moved-original'
    $executableName = 'probe.exe'
    $executablePath = Join-Path $originalParent $executableName
    $launchReached = $false
    try {
        New-Item -ItemType Directory -Path $originalParent -Force | Out-Null
        $system32 = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)) 'System32'
        [IO.File]::Copy((Join-Path $system32 'cmd.exe'), $executablePath, $false)
        & $module {
            param($sourceParent, $destinationParent, $replacementExe, $replacementSource, [ref]$reached)
            $script:M0QtShellProvisioningTestHook = [pscustomobject]@{
                after_trusted_executable_opened_before_directory_pins = {
                    [IO.Directory]::Move($sourceParent, $destinationParent)
                    [IO.Directory]::CreateDirectory($sourceParent) | Out-Null
                    [IO.File]::Copy($replacementSource, $replacementExe, $false)
                }.GetNewClosure()
                after_job_bound_suspended_before_resume = { $reached.Value = $true }.GetNewClosure()
            }
        } $originalParent $movedParent $executablePath (Join-Path $system32 'where.exe') ([ref]$launchReached)

        Assert-Throws {
            & $module {
                param($exe, $temp)
                Invoke-M0QtShellBoundedProcess -FilePath $exe -ArgumentList @('/D', '/C', 'exit 0') `
                    -TempRoot $temp -TimeoutSeconds 5
            } $executablePath $tempRoot
        } 'M0QT021|final path|identity|changed|Access.*denied|Acesso negado'
        Assert-Equal -Actual $launchReached -Expected $false `
            -Message 'The replacement executable reached suspended process creation.'
    }
    finally {
        if ($module) { & $module { $script:M0QtShellProvisioningTestHook = $null } }
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

Test-Case 'QML capability probes reject ignored formatting and false lint exit codes through the bounded runner' {
    Import-Module $modulePath -ErrorAction Stop
    $module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('music-player-m02-qml-capability-' + [Guid]::NewGuid().ToString('N'))
    $fakePath = Join-Path $tempRoot 'fake-qml-tool.ps1'
    $checkSentinel = Join-Path $tempRoot 'unsupported-check-used.txt'
    try {
        New-Item -ItemType Directory -Path $tempRoot | Out-Null
        $fakeSource = @'
param(
    [Parameter(Mandatory)][string] $Mode,
    [Parameter(Mandatory)][string] $CheckSentinel,
    [Parameter(ValueFromRemainingArguments = $true)][string[]] $Remaining
)
if ($Remaining -ccontains '--check') {
    [IO.File]::WriteAllText($CheckSentinel, 'used', [Text.UTF8Encoding]::new($false))
}
switch ($Mode) {
    'echo-input' {
        $target = [string]$Remaining[-1]
        [Console]::Out.Write([IO.File]::ReadAllText($target))
        exit 0
    }
    'always-zero' { exit 0 }
    'always-one' { exit 1 }
    'hang' { Start-Sleep -Seconds 120; exit 0 }
    default { throw "unknown fake mode '$Mode'" }
}
'@
        [IO.File]::WriteAllText($fakePath, $fakeSource, [Text.UTF8Encoding]::new($false))
        $pwshPath = (Get-Command pwsh -ErrorAction Stop).Source

        Assert-Throws {
            & $module {
                param($filePath, $prefix)
                Assert-M0QtShellQmlFormatCapability -FilePath $filePath -ArgumentPrefix $prefix -TimeoutSeconds 2
            } $pwshPath @('-NoLogo', '-NoProfile', '-File', $fakePath, '-Mode', 'echo-input', '-CheckSentinel', $checkSentinel)
        } 'qmlformat|format|dirty|canonical'
        Assert-True (-not (Test-Path -LiteralPath $checkSentinel)) `
            'The qmlformat probe relied on the unsupported --check flag instead of formatting a copy and comparing bytes.'

        foreach ($mode in @('always-zero', 'always-one')) {
            Assert-Throws {
                & $module {
                    param($filePath, $prefix)
                    Assert-M0QtShellQmlLintCapability -FilePath $filePath -ArgumentPrefix $prefix -TimeoutSeconds 2
                } $pwshPath @('-NoLogo', '-NoProfile', '-File', $fakePath, '-Mode', $mode, '-CheckSentinel', $checkSentinel)
            } 'qmllint|valid|invalid|exit'
        }

        Assert-Throws {
            & $module {
                param($filePath, $prefix)
                Assert-M0QtShellQmlFormatCapability -FilePath $filePath -ArgumentPrefix $prefix -TimeoutSeconds 1
            } $pwshPath @('-NoLogo', '-NoProfile', '-File', $fakePath, '-Mode', 'hang', '-CheckSentinel', $checkSentinel)
        } 'timeout'
    }
    finally {
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

Test-Case 'The public validator derives live readiness from physical bytes and exact-path probes' {
    Import-Module $modulePath -ErrorAction Stop
    $module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
    $shadowRoot = Join-Path ([IO.Path]::GetTempPath()) ('music-player-m02-path-shadow-' + [Guid]::NewGuid().ToString('N'))
    $shadowSentinel = Join-Path $shadowRoot 'shadow-executed.txt'
    $shadowExecuted = $false
    $environmentOverrides = [ordered]@{
        PATH = $shadowRoot
        ProgramFiles = $shadowRoot
        SystemRoot = $shadowRoot
        WINDIR = $shadowRoot
        QML_IMPORT_PATH = (Join-Path $shadowRoot 'qml-import')
        QML2_IMPORT_PATH = (Join-Path $shadowRoot 'qml2-import')
        QT_PLUGIN_PATH = (Join-Path $shadowRoot 'qt-plugins')
        HTTP_PROXY = 'http://127.0.0.1:9'
        HTTPS_PROXY = 'http://127.0.0.1:9'
        ALL_PROXY = 'http://127.0.0.1:9'
    }
    $oldEnvironment = @{}
    try {
        New-Item -ItemType Directory -Path $shadowRoot | Out-Null
        $shadowSource = "@echo off`r`necho shadow>$shadowSentinel`r`nexit /b 0`r`n"
        [IO.File]::WriteAllText((Join-Path $shadowRoot 'qmlformat.cmd'), $shadowSource, [Text.ASCIIEncoding]::new())
        [IO.File]::WriteAllText((Join-Path $shadowRoot 'qmllint.cmd'), $shadowSource, [Text.ASCIIEncoding]::new())
        foreach ($name in $environmentOverrides.Keys) {
            $oldEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, [EnvironmentVariableTarget]::Process)
            [Environment]::SetEnvironmentVariable($name, [string]$environmentOverrides[$name], [EnvironmentVariableTarget]::Process)
        }
        $envelope = Test-M0QtShellToolchain -RepositoryRoot $repositoryRoot `
            -ExpectedContractSha256 $expectedContractSha256
        Assert-Equal -Actual ([Environment]::GetEnvironmentVariable('SystemRoot', 'Process')) `
            -Expected $shadowRoot -Message 'Authenticode validation did not restore hostile SystemRoot exactly.'
        Assert-Equal -Actual ([Environment]::GetEnvironmentVariable('WINDIR', 'Process')) `
            -Expected $shadowRoot -Message 'Authenticode validation did not restore hostile WINDIR exactly.'
    }
    finally {
        $shadowExecuted = Test-Path -LiteralPath $shadowSentinel
        foreach ($name in $environmentOverrides.Keys) {
            [Environment]::SetEnvironmentVariable($name, $oldEnvironment[$name], [EnvironmentVariableTarget]::Process)
        }
        if (Test-Path -LiteralPath $shadowRoot) {
            Remove-Item -LiteralPath $shadowRoot -Recurse -Force
        }
    }
    Assert-True (-not $shadowExecuted) 'A PATH-shadowed Qt tool was executed.'
    Assert-SequenceEqual -Actual @($envelope.PSObject.Properties.Name) -Expected @('receipt', 'receipt_sha256') `
        -Message 'The validator did not separate the formal receipt from its transport hash.'
    $receipt = $envelope.receipt
    $script:liveReceipt = $receipt

    Assert-Equal -Actual $receipt.artifact_role -Expected 'live_receipt' -Message 'The validator returned a synthetic oracle.'
    Assert-Equal -Actual $receipt.live_evidence -Expected $true -Message 'The validator did not identify physical evidence.'
    Assert-Equal -Actual $receipt.states.assets_provisioned -Expected $true -Message 'Physical asset validation did not pass.'
    Assert-Equal -Actual $receipt.states.host_tools_observed -Expected $true -Message 'Exact host-tool observation did not pass.'
    Assert-Equal -Actual $receipt.states.qml_tool_capabilities_validated -Expected $true `
        -Message 'QML tool behavior was not validated.'
    Assert-Equal -Actual $receipt.states.preflight_scope_satisfied -Expected $true `
        -Message 'The narrow tooling preflight scope was not satisfied.'
    Assert-Equal -Actual $receipt.states.host_build_capability_validated -Expected $false `
        -Message 'The receipt falsely claimed a host configure/build/test gate.'
    Assert-Equal -Actual $receipt.states.candidate_build_validated -Expected $false `
        -Message 'The receipt falsely claimed a candidate build.'
    Assert-Equal -Actual $receipt.states.manual_validated -Expected $false -Message 'The validator falsely passed the manual NVDA journey.'
    Assert-Equal -Actual $receipt.validation_mode -Expected 'offline_inputs_only' `
        -Message 'The receipt overclaimed fully offline execution.'
    Assert-Equal -Actual $receipt.network_allowed -Expected $false `
        -Message 'The validator contract allowed network access.'
    Assert-Equal -Actual $receipt.network_enforcement -Expected 'not_enforced' `
        -Message 'The receipt falsely claimed OS-level network enforcement.'
    Assert-Equal -Actual $receipt.network_observation -Expected 'not_measured' `
        -Message 'The receipt falsely claimed measured network egress.'
    Assert-Equal -Actual $receipt.capabilities.qmlformat.canonical_lf_round_trip -Expected $true -Message 'qmlformat behavior was not proved.'
    Assert-Equal -Actual $receipt.capabilities.qmllint.valid_fixture_accepted -Expected $true -Message 'qmllint did not accept the valid fixture.'
    Assert-Equal -Actual $receipt.capabilities.qmllint.invalid_fixture_rejected -Expected $true -Message 'qmllint did not reject the invalid fixture.'
    Assert-Equal -Actual $receipt.capabilities.deployment.probe_only -Expected $true -Message 'Deployment was overclaimed beyond a probe.'
    Assert-Equal -Actual $receipt.capabilities.deployment.staging_approved -Expected $false -Message 'The diagnostic staging probe was treated as approval.'
    Assert-Equal -Actual $receipt.capabilities.deployment.bundle_policy_status -Expected 'not-evaluated-by-tooling-preflight' `
        -Message 'The tooling validator presented a prior diagnostic bundle as current approval.'
    Assert-Equal -Actual $receipt.capabilities.nvda.authenticode_valid -Expected $true -Message 'NVDA authenticity was not proved physically.'
    Assert-Equal -Actual $receipt.capabilities.nvda.manual_journey_validated -Expected $false -Message 'NVDA installation was confused with manual validation.'
    Assert-Equal -Actual $receipt.contract_sha256 -Expected $expectedContractSha256 -Message 'The receipt is not bound to the external contract anchor.'
    Assert-Equal -Actual $receipt.descriptor_sha256 -Expected (Get-Sha256 -LiteralPath $descriptorPath) -Message 'The receipt is not bound to the descriptor bytes.'
    Assert-SequenceEqual -Actual @($receipt.observations.qt_archives.sha256) -Expected $expectedArchiveSha256 `
        -Message 'The receipt does not project physically checked Qt archive hashes.'
    Assert-SequenceEqual -Actual @($receipt.observations.qt_archives.size_bytes) -Expected $expectedArchiveSizes `
        -Message 'The receipt does not project physically checked Qt archive sizes.'
    Assert-SequenceEqual -Actual @($receipt.observations.qt_tools.sha256) -Expected $expectedQtToolSha256 `
        -Message 'The receipt does not project physically checked Qt tool hashes.'
    Assert-SequenceEqual -Actual @($receipt.observations.host_tools.sha256) -Expected $expectedHostToolSha256 `
        -Message 'The receipt does not project physically checked host tool hashes.'

    $receiptText = ($receipt | ConvertTo-Json -Compress -Depth 100) + "`n"
    Assert-True (Test-Json -Json $receiptText -SchemaFile $evidenceSchemaPath -ErrorAction SilentlyContinue) `
        'The live receipt does not satisfy its closed evidence schema.'
    $receiptBytes = [Text.UTF8Encoding]::new($false, $true).GetBytes($receiptText)
    $receiptSha256 = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($receiptBytes)).ToLowerInvariant()
    Assert-Equal -Actual $envelope.receipt_sha256 -Expected $receiptSha256 `
        -Message 'The transport hash does not bind the canonical formal receipt bytes.'
    $sensitivePathPattern = '(?i)(?<=[\[,:])"(?:[A-Z]:[\\/]|/(?!/)|\\\\)|Joao|file:///'
    Assert-True ('{"authoritative_url":"https://example.invalid/license"}' -cnotmatch $sensitivePathPattern) `
        'The path-leak detector confused an HTTPS URL with a Windows drive path.'
    Assert-True ('{"path":"C:\\Temp\\private.bin"}' -cmatch $sensitivePathPattern) `
        'The path-leak detector did not recognize a Windows absolute path.'
    Assert-True ('{"path":"/opt/private.bin"}' -cmatch $sensitivePathPattern) `
        'The path-leak detector did not recognize a Unix absolute path.'
    Assert-True ($receiptText -cnotmatch $sensitivePathPattern) `
        'The live receipt leaked an absolute or personal path.'
}

Test-Case 'The public validator requires an external anchor and never overwrites a receipt' {
    Import-Module $modulePath -ErrorAction Stop
    $module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
    $receiptRoot = Join-Path ([IO.Path]::GetTempPath()) ('music-player-m02-receipt-' + [Guid]::NewGuid().ToString('N'))
    $receiptPath = Join-Path $receiptRoot 'toolchain-receipt.v1.json'
    $missingParentReceiptPath = Join-Path $receiptRoot 'missing-parent\toolchain-receipt.v1.json'
    $probeSentinel = Join-Path $receiptRoot 'physical-probe-started.txt'
    try {
        New-Item -ItemType Directory -Path $receiptRoot | Out-Null
        Assert-Throws { Test-M0QtShellToolchain -RepositoryRoot $repositoryRoot } 'ExpectedContractSha256|mandatory'
        & $module {
            param($sentinel)
            $script:M0QtShellProvisioningTestHook = [pscustomobject]@{
                before_physical_probe = { [IO.File]::WriteAllText($sentinel, 'probe', [Text.UTF8Encoding]::new($false)) }.GetNewClosure()
            }
        } $probeSentinel
        Assert-Throws {
            Test-M0QtShellToolchain -RepositoryRoot $repositoryRoot `
                -ExpectedContractSha256 ('f' * 64)
        } 'contract|hash|expected'
        Assert-True (-not (Test-Path -LiteralPath $probeSentinel)) `
            'A wrong external anchor reached a physical tool probe.'
        & $module { $script:M0QtShellProvisioningTestHook = $null }

        & $module {
            param($sentinel)
            $script:M0QtShellProvisioningTestHook = [pscustomobject]@{
                before_physical_probe = { [IO.File]::WriteAllText($sentinel, 'probe', [Text.UTF8Encoding]::new($false)) }.GetNewClosure()
            }
        } $probeSentinel
        Assert-Throws {
            Test-M0QtShellToolchain -RepositoryRoot $repositoryRoot `
                -ExpectedContractSha256 $expectedContractSha256 -ReceiptPath $missingParentReceiptPath
        } 'receipt|parent|ancestor|missing'
        Assert-True (-not (Test-Path -LiteralPath $probeSentinel)) `
            'A ReceiptPath with a missing parent reached a physical tool probe.'
        Assert-True (-not (Test-Path -LiteralPath ([IO.Path]::GetDirectoryName($missingParentReceiptPath)))) `
            'Receipt validation created a missing parent directory.'
        & $module { $script:M0QtShellProvisioningTestHook = $null }

        $first = Test-M0QtShellToolchain -RepositoryRoot $repositoryRoot `
            -ExpectedContractSha256 $expectedContractSha256 -ReceiptPath $receiptPath
        $beforeHash = Get-Sha256 -LiteralPath $receiptPath
        & $module {
            param($sentinel)
            $script:M0QtShellProvisioningTestHook = [pscustomobject]@{
                before_physical_probe = { [IO.File]::WriteAllText($sentinel, 'probe', [Text.UTF8Encoding]::new($false)) }.GetNewClosure()
            }
        } $probeSentinel
        Assert-Throws {
            Test-M0QtShellToolchain -RepositoryRoot $repositoryRoot `
                -ExpectedContractSha256 $expectedContractSha256 -ReceiptPath $receiptPath
        } 'exist|overwrite|CreateNew'
        Assert-True (-not (Test-Path -LiteralPath $probeSentinel)) `
            'A no-overwrite conflict reached a physical tool probe.'
        Assert-Equal -Actual (Get-Sha256 -LiteralPath $receiptPath) -Expected $beforeHash `
            -Message 'A replay modified the committed receipt.'
        Assert-Equal -Actual $first.receipt_sha256 -Expected $beforeHash `
            -Message 'The returned receipt hash does not bind the committed bytes.'
        Assert-Equal -Actual @(Get-ChildItem -LiteralPath $receiptRoot -Force).Count -Expected 1 `
            -Message 'Anchor/conflict failures left a temp, sentinel, or extra receipt leaf.'
    }
    finally {
        if ($module) {
            & $module { $script:M0QtShellProvisioningTestHook = $null }
        }
        if (Test-Path -LiteralPath $receiptRoot) {
            Remove-Item -LiteralPath $receiptRoot -Recurse -Force
        }
    }
}

Test-Case 'The persisted receipt verifier requires an external byte-hash anchor' {
    Import-Module $modulePath -ErrorAction Stop
    $command = Get-Command Assert-M0QtShellReceipt -Module M0.QtShellProvisioning -ErrorAction Stop
    Assert-True ($null -ne $command.Parameters['ExpectedReceiptSha256']) `
        'The public persisted-receipt verifier has no independent receipt byte-hash anchor.'
    $parameterAttributes = @($command.Parameters['ExpectedReceiptSha256'].Attributes | Where-Object {
            $_ -is [Management.Automation.ParameterAttribute]
        })
    Assert-Equal -Actual $parameterAttributes.Count -Expected 1 `
        -Message 'The persisted receipt hash anchor has an ambiguous parameter contract.'
    Assert-True ([bool]$parameterAttributes[0].Mandatory) `
        'The persisted receipt byte-hash anchor is optional.'
    $patterns = @($command.Parameters['ExpectedReceiptSha256'].Attributes | Where-Object { $_ -is [Management.Automation.ValidatePatternAttribute] })
    Assert-Equal -Actual $patterns.Count -Expected 1 `
        -Message 'The persisted receipt hash anchor lacks one closed validation pattern.'
    Assert-Equal -Actual $patterns[0].RegexPattern -Expected '^[0-9a-f]{64}$' `
        -Message 'The persisted receipt hash anchor accepts a non-canonical SHA-256.'
}

Test-Case 'A published receipt is canonical schema-valid return-equivalent and semantically bound to the descriptor' {
    Import-Module $modulePath -ErrorAction Stop
    $module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
    $receiptRoot = Join-Path ([IO.Path]::GetTempPath()) ('music-player-m02-receipt-semantics-' + [Guid]::NewGuid().ToString('N'))
    $receiptPath = Join-Path $receiptRoot 'toolchain-receipt.v1.json'
    try {
        New-Item -ItemType Directory -Path $receiptRoot | Out-Null
        $returned = Test-M0QtShellToolchain -RepositoryRoot $repositoryRoot `
            -ExpectedContractSha256 $expectedContractSha256 -ReceiptPath $receiptPath
        Assert-True (Test-Path -LiteralPath $receiptPath -PathType Leaf) `
            'The validator did not publish the requested receipt.'

        $publishedBytes = [IO.File]::ReadAllBytes($receiptPath)
        Assert-True (-not ($publishedBytes.Length -ge 3 -and $publishedBytes[0] -eq 0xEF -and `
            $publishedBytes[1] -eq 0xBB -and $publishedBytes[2] -eq 0xBF)) `
            'The published receipt contains a UTF-8 BOM.'
        $publishedText = [Text.UTF8Encoding]::new($false, $true).GetString($publishedBytes)
        $published = $publishedText | ConvertFrom-Json -Depth 100 -DateKind String
        $canonicalPublishedText = ($published | ConvertTo-Json -Compress -Depth 100) + "`n"
        Assert-Equal -Actual $publishedText -Expected $canonicalPublishedText `
            -Message 'The published receipt is not canonical compact UTF-8 JSON with one LF.'
        Assert-True (Test-Json -Json $publishedText -SchemaFile $evidenceSchemaPath -ErrorAction SilentlyContinue) `
            'The published receipt does not satisfy the closed evidence schema.'

        Assert-SequenceEqual -Actual @($returned.PSObject.Properties.Name) -Expected @('receipt', 'receipt_sha256') `
            -Message 'The persisted receipt return did not use the closed transport envelope.'
        Assert-True ($null -ne $returned.PSObject.Properties['receipt_sha256']) `
            'The returned receipt envelope omitted the published byte hash.'
        Assert-Equal -Actual $returned.receipt_sha256 -Expected (Get-Sha256 -LiteralPath $receiptPath) `
            -Message 'The returned receipt hash does not bind the published bytes.'
        $returnedDocumentText = ($returned.receipt | ConvertTo-Json -Compress -Depth 100) + "`n"
        Assert-Equal -Actual $returnedDocumentText -Expected $publishedText `
            -Message 'The returned formal receipt differs from the published document.'

        $receiptSha256 = Get-Sha256 -LiteralPath $receiptPath
        $wrongReceiptSha256 = '0' + $receiptSha256.Substring(1)
        if ($wrongReceiptSha256 -ceq $receiptSha256) {
            $wrongReceiptSha256 = '1' + $receiptSha256.Substring(1)
        }
        $wrongOutput = & pwsh -NoLogo -NoProfile -NonInteractive -File $validatorPath `
            -RepositoryRoot $repositoryRoot -ExpectedContractSha256 $expectedContractSha256 `
            -ReceiptPath $receiptPath -ExpectedReceiptSha256 $wrongReceiptSha256 2>&1 | Out-String
        $wrongExitCode = $LASTEXITCODE
        Assert-True ($wrongExitCode -ne 0) `
            'The public runner accepted a receipt whose external byte-hash anchor was wrong.'
        Assert-True ($wrongOutput -match '(?s)M0QT021.*trusted file hash drifted') `
            'The public runner rejected the wrong receipt hash for an unrelated reason.'
        Assert-Equal -Actual (Get-Sha256 -LiteralPath $receiptPath) -Expected $receiptSha256 `
            -Message 'Wrong-hash verification mutated the persisted receipt.'

        $validOutput = & pwsh -NoLogo -NoProfile -NonInteractive -File $validatorPath `
            -RepositoryRoot $repositoryRoot -ExpectedContractSha256 $expectedContractSha256 `
            -ReceiptPath $receiptPath -ExpectedReceiptSha256 $receiptSha256 2>&1 | Out-String
        $validExitCode = $LASTEXITCODE
        Assert-Equal -Actual $validExitCode -Expected 0 `
            -Message "The public VerifyReceipt runner failed:`n$validOutput"
        $verified = $validOutput.Trim() | ConvertFrom-Json -Depth 100 -DateKind String
        Assert-SequenceEqual -Actual @($verified.PSObject.Properties.Name) -Expected @('receipt', 'receipt_sha256') `
            -Message 'The public VerifyReceipt runner did not return the closed transport envelope.'
        Assert-Equal -Actual $verified.receipt_sha256 -Expected $receiptSha256 `
            -Message 'The public VerifyReceipt runner lost the external physical receipt hash.'
        Assert-Equal -Actual (($verified.receipt | ConvertTo-Json -Compress -Depth 100) + "`n") -Expected $publishedText `
            -Message 'The public VerifyReceipt runner changed the formal receipt.'

        $semanticMutants = [ordered]@{}
        $semanticMutants.contract_hash = Copy-JsonDocument -Value $published
        $semanticMutants.contract_hash.contract_sha256 = 'f' * 64

        foreach ($entry in $semanticMutants.GetEnumerator()) {
            $mutantText = ($entry.Value | ConvertTo-Json -Compress -Depth 100) + "`n"
            Assert-True (Test-Json -Json $mutantText -SchemaFile $evidenceSchemaPath -ErrorAction SilentlyContinue) `
                "The '$($entry.Key)' receipt mutation was not schema-valid, so it cannot prove semantic crosslink rejection."
            $mutantPath = Join-Path $receiptRoot ("semantic-$($entry.Key).json")
            [IO.File]::WriteAllText($mutantPath, $mutantText, [Text.UTF8Encoding]::new($false))
            Assert-Throws {
                Assert-M0QtShellReceipt -RepositoryRoot $repositoryRoot `
                    -ExpectedContractSha256 $expectedContractSha256 `
                    -ExpectedReceiptSha256 (Get-Sha256 -LiteralPath $mutantPath) -ReceiptPath $mutantPath
            } 'receipt|semantic|binding|descriptor|contract'
        }

        $schemaMutants = [ordered]@{}
        $schemaMutants.archive_id = Copy-JsonDocument -Value $published
        $schemaMutants.archive_id.observations.qt_archives[0].id = 'schema-invalid-wrong-id'
        $schemaMutants.tool_version = Copy-JsonDocument -Value $published
        $schemaMutants.tool_version.observations.qt_tools[0].version = '6.11.2.1'
        $schemaMutants.physical_binding = Copy-JsonDocument -Value $published
        $schemaMutants.physical_binding.observations.host_tools[0].sha256 = 'e' * 64
        $schemaMutants.receipt_hash_in_evidence_document = Copy-JsonDocument -Value $published
        $schemaMutants.receipt_hash_in_evidence_document | `
            Add-Member -NotePropertyName receipt_sha256 -NotePropertyValue ('d' * 64)

        foreach ($entry in $schemaMutants.GetEnumerator()) {
            $mutantText = ($entry.Value | ConvertTo-Json -Compress -Depth 100) + "`n"
            Assert-True (-not (Test-Json -Json $mutantText -SchemaFile $evidenceSchemaPath -ErrorAction SilentlyContinue)) `
                "The closed receipt schema accepted the '$($entry.Key)' physical-binding mutation."
            $mutantPath = Join-Path $receiptRoot ("schema-$($entry.Key).json")
            [IO.File]::WriteAllText($mutantPath, $mutantText, [Text.UTF8Encoding]::new($false))
            Assert-Throws {
                Assert-M0QtShellReceipt -RepositoryRoot $repositoryRoot `
                    -ExpectedContractSha256 $expectedContractSha256 `
                    -ExpectedReceiptSha256 (Get-Sha256 -LiteralPath $mutantPath) -ReceiptPath $mutantPath
            } 'receipt|schema|canonical|binding'
        }
    }
    finally {
        if (Test-Path -LiteralPath $receiptRoot) {
            Remove-Item -LiteralPath $receiptRoot -Recurse -Force
        }
    }
}

Test-Case 'The public runner rejects a preloaded provisioning module before any physical probe' {
    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('music-player-m02-clean-runner-' + [Guid]::NewGuid().ToString('N'))
    $childPath = Join-Path $tempRoot 'preload-child.ps1'
    try {
        New-Item -ItemType Directory -Path $tempRoot | Out-Null
        $escapedRunner = $validatorPath.Replace("'", "''")
        $escapedRoot = $repositoryRoot.Replace("'", "''")
        $childSource = @"
`$ErrorActionPreference = 'Stop'
`$fake = New-Module -Name 'M0.QtShellProvisioning' -ScriptBlock { function Test-M0QtShellToolchain { throw 'fake accepted' }; Export-ModuleMember Test-M0QtShellToolchain }
Import-Module `$fake
& '$escapedRunner' -RepositoryRoot '$escapedRoot' -ExpectedContractSha256 '$expectedContractSha256'
"@
        [IO.File]::WriteAllText($childPath, $childSource, [Text.UTF8Encoding]::new($false))
        $output = & pwsh -NoLogo -NoProfile -File $childPath 2>&1 | Out-String
        $exitCode = $LASTEXITCODE
        Assert-True ($exitCode -ne 0) 'The public runner accepted a preloaded provisioning module.'
        Assert-True ($output -match 'M0QT040|clean PowerShell|preloaded') `
            'The public runner failed for an unrelated reason instead of the clean-process gate.'
    }
    finally {
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

Test-Case 'The public runner rejects a preloaded native trust type before resolving repository paths' {
    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('music-player-m02-clean-native-' + [Guid]::NewGuid().ToString('N'))
    $childPath = Join-Path $tempRoot 'preload-native-child.ps1'
    $missingRepository = Join-Path $tempRoot 'must-stay-missing'
    try {
        New-Item -ItemType Directory -Path $tempRoot | Out-Null
        $escapedRunner = $validatorPath.Replace("'", "''")
        $escapedRoot = $missingRepository.Replace("'", "''")
        $childSource = @"
`$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition 'namespace M0QtShellNativeV1 { public static class FileIdentity { } }'
& '$escapedRunner' -RepositoryRoot '$escapedRoot' -ExpectedContractSha256 '$expectedContractSha256'
"@
        [IO.File]::WriteAllText($childPath, $childSource, [Text.UTF8Encoding]::new($false))
        $output = & pwsh -NoLogo -NoProfile -File $childPath 2>&1 | Out-String
        Assert-True ($LASTEXITCODE -ne 0) 'The public runner accepted a preloaded native trust type.'
        Assert-True ($output -match '(?s)(M0QT040.*native|preloaded.*FileIdentity|clean PowerShell)') `
            'The native preload failed for an unrelated reason.'
        Assert-True (-not (Test-Path -LiteralPath $missingRepository)) `
            'The native preload gate resolved or mutated the repository root.'
    }
    finally {
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

Test-Case 'Direct module import rejects a preloaded native namespace before any function or physical probe' {
    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('music-player-m02-direct-native-preload-' + [Guid]::NewGuid().ToString('N'))
    $childPath = Join-Path $tempRoot 'direct-native-preload-child.ps1'
    $importSideEffect = Join-Path $tempRoot 'import-continued.txt'
    $probeRoot = Join-Path $tempRoot 'must-stay-missing'
    $receiptPath = Join-Path $tempRoot 'must-stay-missing-receipt.json'
    try {
        New-Item -ItemType Directory -Path $tempRoot | Out-Null
        $escapedModule = $modulePath.Replace("'", "''")
        $escapedImportSideEffect = $importSideEffect.Replace("'", "''")
        $escapedProbeRoot = $probeRoot.Replace("'", "''")
        $escapedReceipt = $receiptPath.Replace("'", "''")
        $childSource = @"
`$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
namespace M0QtShellNativeV1 {
    public static class FileIdentity { }
    public static class OtherPreloadedTrustType { }
}
'@
Import-Module '$escapedModule' -Force -ErrorAction Stop
[IO.File]::WriteAllText('$escapedImportSideEffect', 'import-continued', [Text.UTF8Encoding]::new(`$false))
Test-M0QtShellToolchain -RepositoryRoot '$escapedProbeRoot' -ExpectedContractSha256 '$expectedContractSha256' -ReceiptPath '$escapedReceipt'
"@
        [IO.File]::WriteAllText($childPath, $childSource, [Text.UTF8Encoding]::new($false))
        $output = & pwsh -NoLogo -NoProfile -File $childPath 2>&1 | Out-String
        Assert-True ($LASTEXITCODE -ne 0) 'Direct Import-Module accepted a preloaded native provisioning namespace.'
        Assert-True ($output -match 'M0QT040') `
            'Direct Import-Module failed without the causal M0QT040 native-preload diagnostic.'
        Assert-True (-not (Test-Path -LiteralPath $importSideEffect)) `
            'Code after direct Import-Module ran despite the native-preload rejection.'
        Assert-True (-not (Test-Path -LiteralPath $probeRoot)) `
            'Direct module preload rejection reached a repository probe or mutation.'
        Assert-True (-not (Test-Path -LiteralPath $receiptPath)) `
            'Direct module preload rejection published a receipt.'
    }
    finally {
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

Test-Case 'Direct module import rejects a case-folded native namespace before Add-Type resolution' {
    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('music-player-m02-direct-native-casefold-' + [Guid]::NewGuid().ToString('N'))
    $childPath = Join-Path $tempRoot 'direct-native-casefold-child.ps1'
    $importSideEffect = Join-Path $tempRoot 'import-continued.txt'
    try {
        New-Item -ItemType Directory -Path $tempRoot | Out-Null
        $escapedModule = $modulePath.Replace("'", "''")
        $escapedImportSideEffect = $importSideEffect.Replace("'", "''")
        $childSource = @"
`$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition 'namespace m0qtshellnativev1 { public static class FileIdentity { public static string Marker() { return "fake-lowercase"; } } }'
Import-Module '$escapedModule' -Force -ErrorAction Stop
[IO.File]::WriteAllText('$escapedImportSideEffect', 'import-continued', [Text.UTF8Encoding]::new(`$false))
"@
        [IO.File]::WriteAllText($childPath, $childSource, [Text.UTF8Encoding]::new($false))
        $output = & pwsh -NoLogo -NoProfile -File $childPath 2>&1 | Out-String
        Assert-True ($LASTEXITCODE -ne 0) 'Direct Import-Module accepted a case-folded native provisioning namespace.'
        Assert-True ($output -match 'M0QT040') `
            'The case-folded direct preload failed without the causal M0QT040 diagnostic.'
        Assert-True (-not (Test-Path -LiteralPath $importSideEffect)) `
            'Code after Import-Module ran despite the case-folded native-preload rejection.'
    }
    finally {
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

Test-Case 'The public runner validates its anchored module bytes before import and succeeds cleanly' {
    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('music-player-m02-runner-bootstrap-' + [Guid]::NewGuid().ToString('N'))
    $cloneRoot = Join-Path $tempRoot 'clone'
    $sideEffectSentinel = Join-Path $tempRoot 'tampered-module-imported.txt'
    try {
        $cloneContractRoot = Join-Path $cloneRoot 'experiments\m0\provisioning\qt-shell'
        New-Item -ItemType Directory -Path $cloneContractRoot -Force | Out-Null
        foreach ($source in @(Get-ChildItem -LiteralPath $contractRoot -File -Force)) {
            Copy-Item -LiteralPath $source.FullName -Destination $cloneContractRoot -Force
        }

        $cloneModulePath = Join-Path $cloneContractRoot 'M0.QtShellProvisioning.psm1'
        $sideEffectSource = "[IO.File]::WriteAllText('$($sideEffectSentinel.Replace("'", "''"))', 'imported', [Text.UTF8Encoding]::new(`$false))`n"
        $originalModuleSource = [IO.File]::ReadAllText($cloneModulePath)
        [IO.File]::WriteAllText($cloneModulePath, ($sideEffectSource + $originalModuleSource), [Text.UTF8Encoding]::new($false))

        $cloneContractPath = Join-Path $cloneContractRoot 'qt-shell-toolchain-contract.v1.json'
        $cloneContract = Read-JsonDocument -LiteralPath $cloneContractPath
        $moduleEntry = @($cloneContract.files | Where-Object path -eq 'experiments/m0/provisioning/qt-shell/M0.QtShellProvisioning.psm1')
        Assert-Equal -Actual $moduleEntry.Count -Expected 1 -Message 'The clone module slot is ambiguous.'
        $moduleEntry[0].sha256 = Get-Sha256 -LiteralPath $cloneModulePath
        $moduleEntry[0].size_bytes = (Get-Item -LiteralPath $cloneModulePath).Length
        [IO.File]::WriteAllText($cloneContractPath, (($cloneContract | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText("$cloneContractPath.sha256", ((Get-Sha256 -LiteralPath $cloneContractPath) + "`n"), [Text.UTF8Encoding]::new($false))

        $cloneRunner = Join-Path $cloneContractRoot 'Test-M0QtShellToolchain.ps1'
        $cloneOutput = & pwsh -NoLogo -NoProfile -File $cloneRunner `
            -RepositoryRoot $cloneRoot -ExpectedContractSha256 $expectedContractSha256 2>&1 | Out-String
        Assert-True ($LASTEXITCODE -ne 0) 'The clone runner accepted a reanchored tampered module.'
        Assert-True (-not (Test-Path -LiteralPath $sideEffectSentinel)) `
            'The public runner imported untrusted module bytes before checking the external contract anchor.'
        Assert-True ($cloneOutput -match 'contract|expected|hash|M0QT') `
            'The clone runner failed for an unrelated reason.'

        $cleanOutput = & pwsh -NoLogo -NoProfile -File $validatorPath `
            -RepositoryRoot $repositoryRoot -ExpectedContractSha256 $expectedContractSha256 2>&1 | Out-String
        Assert-Equal -Actual $LASTEXITCODE -Expected 0 -Message 'The clean public runner failed physical validation.'
        Assert-True ($cleanOutput -match '"artifact_role":"live_receipt"') `
            'The clean public runner did not emit the canonical live receipt JSON.'
    }
    finally {
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

Test-Case 'Receipt publication is contained atomic exception-safe and pins its parent directory identity' {
    if (-not $IsWindows) {
        Assert-M0QtShellNonWindowsFailClosed -CaseId 'receipt-publication'
        return
    }

    Import-Module $modulePath -ErrorAction Stop
    $module = Get-Module -Name M0.QtShellProvisioning -ErrorAction Stop
    $tempRoot = Join-Path ([IO.Path]::GetTempPath()) ('music-player-m02-receipt-atomic-' + [Guid]::NewGuid().ToString('N'))
    try {
        $safeRoot = Join-Path $tempRoot 'safe'
        $outsideRoot = Join-Path $tempRoot 'outside'
        New-Item -ItemType Directory -Path $safeRoot, $outsideRoot -Force | Out-Null
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes("{`"receipt`":true}`n")
        $crashPath = Join-Path $safeRoot 'exception.json'
        Assert-Throws {
            & $module {
                param($path, $payload)
                Publish-M0QtShellReceipt -LiteralPath $path -Bytes $payload -AfterTempFlush { throw 'synthetic-exception' }
            } $crashPath $bytes
        } 'synthetic-exception'
        Assert-True (-not (Test-Path -LiteralPath $crashPath)) 'An exception exposed a partial final receipt.'
        Assert-Equal -Actual @(Get-ChildItem -LiteralPath $safeRoot -Force).Count -Expected 0 `
            -Message 'An exception left a stale receipt temp file.'

        $pinnedParent = Join-Path $tempRoot 'pinned-parent'
        $movedParent = Join-Path $tempRoot 'moved-parent'
        New-Item -ItemType Directory -Path $pinnedParent | Out-Null
        $pinnedReceipt = Join-Path $pinnedParent 'receipt.json'
        $swapWasBlocked = $false
        & $module {
            param($path, $payload, $parent, $moved, [ref] $blocked)
            Publish-M0QtShellReceipt -LiteralPath $path -Bytes $payload -AfterTempFlush {
                try {
                    [IO.Directory]::Move($parent, $moved)
                }
                catch [IO.IOException] {
                    $blocked.Value = $true
                }
            }.GetNewClosure()
        } $pinnedReceipt $bytes $pinnedParent $movedParent ([ref]$swapWasBlocked)
        Assert-True $swapWasBlocked 'The retained parent handle did not block a concurrent directory swap.'
        Assert-True (Test-Path -LiteralPath $pinnedReceipt -PathType Leaf) `
            'The receipt was not published below the retained parent identity.'
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $movedParent 'receipt.json'))) `
            'The receipt followed a swapped parent outside its retained directory identity.'

        $identityAncestor = Join-Path $tempRoot 'identity-ancestor'
        $identityParent = Join-Path $identityAncestor 'parent'
        $movedAncestor = Join-Path $tempRoot 'identity-ancestor-moved'
        $outsideSentinel = Join-Path $outsideRoot 'rollback-sentinel.json'
        New-Item -ItemType Directory -Path $identityParent | Out-Null
        [IO.File]::WriteAllText($outsideSentinel, 'outside-sentinel', [Text.UTF8Encoding]::new($false))
        $identityReceipt = Join-Path $identityParent 'receipt.json'
        $swapState = [pscustomobject]@{ moved = $false; blocked = $false }
        Assert-Throws {
            & $module {
                param($path, $payload, $ancestor, $moved, $parent, $state)
                Publish-M0QtShellReceipt -LiteralPath $path -Bytes $payload -AfterAtomicMove {
                    try {
                        [IO.Directory]::Move($ancestor, $moved)
                        $state.moved = $true
                        [IO.Directory]::CreateDirectory($parent) | Out-Null
                        [IO.File]::WriteAllText($path, 'replacement-sentinel', [Text.UTF8Encoding]::new($false))
                    }
                    catch [IO.IOException] {
                        $state.blocked = $true
                    }
                    throw 'synthetic-post-move-exception'
                }.GetNewClosure()
            } $identityReceipt $bytes $identityAncestor $movedAncestor $identityParent $swapState
        } 'synthetic-post-move-exception'
        Assert-True ($swapState.moved -xor $swapState.blocked) `
            'The ancestor swap probe neither moved nor observed the retained-identity block.'
        Assert-Equal -Actual ([IO.File]::ReadAllText($outsideSentinel)) -Expected 'outside-sentinel' `
            -Message 'Failure cleanup removed an unrelated homonymous leaf outside the retained identity.'
        if ($swapState.moved) {
            Assert-Equal -Actual ([IO.File]::ReadAllText($identityReceipt)) -Expected 'replacement-sentinel' `
                -Message 'Failure cleanup followed the old pathname and removed its replacement identity.'
            Assert-True (-not (Test-Path -LiteralPath (Join-Path (Join-Path $movedAncestor 'parent') 'receipt.json'))) `
                'Failure cleanup did not remove the originally published receipt identity.'
        }
        else {
            Assert-True (-not (Test-Path -LiteralPath $identityReceipt)) `
                'Failure cleanup left the originally published receipt identity after the swap was blocked.'
        }

        $junction = Join-Path $safeRoot 'redirected'
        New-Item -ItemType Junction -Path $junction -Target $outsideRoot | Out-Null
        $redirectedPath = Join-Path $junction 'receipt.json'
        Assert-Throws {
            & $module { param($path, $payload) Publish-M0QtShellReceipt -LiteralPath $path -Bytes $payload } $redirectedPath $bytes
        } 'reparse|final|contained|parent'
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $outsideRoot 'receipt.json'))) `
            'Receipt publication escaped through a reparse parent.'
    }
    finally {
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

Write-Output "Tests: $script:passed passed, $script:failed failed."
if ($script:failed -ne 0) {
    exit 1
}
