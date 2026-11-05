#Requires -Version 7.0
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../RcMusicChromiumIdentity.psm1') -Force
$state = @{ Passed = 0; Failed = 0 }
# Public-only fixed vectors. Root independently calculated SHA256/IDs with
# Python hashlib; tests never read personal key storage or generate any key.
$mainPublic = 'MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAqX9/7jyIe6knu4ZGOK+J3L7DrO16UrFM7AjYtt3tBD7ROepgbUEnrkg1RFLKiUcuAZIIF3qfvLdwZiY3ZebHReloePi/ZhmwKNoey4JeBnlZpZhrFdzI+WBFWTr92Ams1x6LwU6ugdEwfr5uZxUX/hEYUqqcdblF06YntRwu8rPr8tmsGl9s+rZw6L5arTz3Mn1Pw4Y1Wt4q+NRlWYso14doQZ8s/OHuJFRY7kc8MhpIN05ayPlcv2VQIe+xWdm67nR8nWlbsr08c0bQNrgAqCmmnCrWUPW3IXvbcb+4rnL4xiyYnXnxLRPJMpz3fNkpIu7aziR1869t9Hiz2hJ5cQIDAQAB'
$youtubePublic = 'MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAsJmJ6eCY4G5chs/I8sD1xnEmue2mffsMr7Ya5gymCGYNpufwaECbeRMh3cAz0xjy7VB0+m25bVWrWAll9xJdPqPahx7aJ0on4T2666fSrzrhGm2Rq5RxVUFcHAmV+GZF1W+1evt1TO3EuNT8WW9+74Ysph6cbo2nm7JEwurhsZ4uBb8vtEQyg1AvE/osIL6qUEW0SRwXabhVL1H911jDHM+tocePeX4BsFfoZMRTaAO94MlWFPniUskPmSQ0o80q+9HP9h5ovZidDfEkB2Ifq+sMFKPJOhJUAHPZhD7HmTFymwfJlQg6XOIQNYUOe6Xm1Vl7gdRfjl4W9XM08yu5tQIDAQAB'
function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Assert-Equal($Actual, $Expected) {
    if ($Actual -cne $Expected) { throw "Expected '$Expected', found '$Actual'." }
}
function Assert-Rejected([scriptblock] $Body) {
    try { & $Body | Out-Null } catch { return }
    throw 'Expected rejection.'
}
function Bytes([string] $Text) { return ,([Text.UTF8Encoding]::new($false, $true).GetBytes($Text)) }
function With-Key($Value) {
    return Bytes (ConvertTo-Json -InputObject ([ordered]@{ manifest_version = 3; name = 'Owned fixture 日本'; key = $Value }) -Depth 5 -Compress)
}
function Identity([byte[]] $Value) { return Get-RcMusicChromiumManifestIdentity -ManifestBytes $Value }

Test-Case 'valid keyless manifest objects keep identity pending and preserve their bytes' {
    foreach ($json in @('{}', '{"name":"RC Music 日本","manifest_version":3}', '{"nested":{"key":"not a root key"}}', '{"KEY":"case-sensitive field"}')) {
        $inputBytes = Bytes $json
        $before = [Convert]::ToHexString($inputBytes)
        Assert-Equal (Identity $inputBytes) $null
        Assert-Equal ([Convert]::ToHexString($inputBytes)) $before
    }
}
Test-Case 'the pinned Main public SPKI has the independently calculated Chromium ID and full hash' {
    $result = Identity (With-Key $mainPublic)
    Assert-Equal $result.extension_id 'kggjfjbjhklkbpelddffdjelbiomoolc'
    Assert-Equal $result.origin 'chrome-extension://kggjfjbjhklkbpelddffdjelbiomoolc/'
    Assert-Equal $result.public_key_sha256 'a66959197aba1f4b3355394b18eceeb25dd21767a74a5d2fd6e8dc8a711dd953'
    Assert-Equal (@($result.PSObject.Properties.Name | Sort-Object) -join '|') 'extension_id|origin|public_key_sha256'
}
Test-Case 'the distinct Youtube public SPKI stays separate from the Main identity' {
    $result = Identity (With-Key $youtubePublic)
    Assert-Equal $result.extension_id 'lhcdmfamadilnahlckpalijljfohjgkc'
    Assert-Equal $result.origin 'chrome-extension://lhcdmfamadilnahlckpalijljfohjgkc/'
    Assert-Equal $result.public_key_sha256 'b723c50c038bd07b2af0b89b95e796a22a30d1fc00d790f98b218ae24a676724'
}
Test-Case 'JSON formatting metadata paths and property order cannot change a keyed identity' {
    $inputBytes = Bytes ('{ "path":"C:/different installed version", "key":"' + $mainPublic + '","name":"changed" }')
    $before = [Convert]::ToHexString($inputBytes)
    $result = Identity $inputBytes
    Assert-Equal $result.extension_id 'kggjfjbjhklkbpelddffdjelbiomoolc'
    Assert-Equal ([Convert]::ToHexString($inputBytes)) $before
}
Test-Case 'present keys must be strings and cannot fall back to identity pending' {
    foreach ($value in @($null, $false, $true, 3, @(), @('key'), @{ value = $mainPublic })) {
        Assert-Rejected { Identity (With-Key $value) }
    }
}
Test-Case 'Base64 keys reject emptiness whitespace PEM wrappers invalid alphabets and extra padding' {
    foreach ($value in @('', ' ', ($mainPublic + "`n"), ("`t" + $mainPublic), $mainPublic.Insert(40, ' '),
            ("-----BEGIN PUBLIC KEY-----`n" + $mainPublic + "`n-----END PUBLIC KEY-----"),
            ('-----BEGIN PRIVATE KEY-----'), ($mainPublic + '='), ($mainPublic + '!'), $mainPublic.Replace('+', '-'))) {
        Assert-Rejected { Identity (With-Key $value) }
    }
}
Test-Case 'only fully consumed canonical SPKI is accepted rather than raw RSA PKCS1 or arbitrary DER' {
    $der = [Convert]::FromBase64String($mainPublic)
    # This fixed RSA2048 vector has the 24-byte SPKI wrapper before PKCS1.
    $pkcs1 = [byte[]]$der[24..($der.Length - 1)]
    foreach ($value in @([Convert]::ToBase64String($pkcs1),
            [Convert]::ToBase64String([byte[]]($der + [byte]0)), 'MAMCAQE=', 'AA==')) {
        Assert-Rejected { Identity (With-Key $value) }
    }
}
Test-Case 'weak RSA and non-RSA public SPKI are refused without private key operations' {
    # Chromium PDF public RSA1024 key: public data, no associated secret here.
    $weak = 'MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDN6hM0rsDYGbzQPQfOygqlRtQgKUXMfnSjhIBL7LnReAVBEd7ZmKtyN2qmSasMl4HZpMhVe2rPWVVwBDl6iyNE/Kok6E6v6V3vCLGsOpQAuuNVye/3QxzIldzG/jQAdWZiyXReRVapOhZtLjGfywCvlWq7Sl/e3sbc0vWybSDI2QIDAQAB'
    # P-256 public generator point in a fixed EC SPKI; this performs no generation.
    $ec = [Convert]::FromHexString('3059301306072A8648CE3D020106082A8648CE3D030107034200046B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C2964FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5')
    foreach ($value in @($weak, [Convert]::ToBase64String($ec))) { Assert-Rejected { Identity (With-Key $value) } }
}
Test-Case 'duplicate JSON names are rejected in root nested objects and objects inside arrays' {
    foreach ($json in @(
        ('{"key":"' + $mainPublic + '","key":"' + $mainPublic + '"}'),
        ('{"key":"' + $mainPublic + '","ke\u0079":"' + $mainPublic + '"}'),
        '{"name":"first","name":"second"}',
        '{"nested":{"x":1,"x":2}}', '{"nested":[{"x":1,"x":2}]}')) {
        Assert-Rejected { Identity (Bytes $json) }
    }
}
Test-Case 'comments trailing commas trailing documents and non-object roots are rejected' {
    foreach ($json in @('/* comment */ {}', '{"x":1,}', '{} {}', '{"x":NaN}', '[]', 'null', 'false', '1', '"text"', '')) {
        Assert-Rejected { Identity (Bytes $json) }
    }
}
Test-Case 'manifest decoding rejects malformed UTF8 before parsing JSON' {
    foreach ($bytes in @([byte[]]@(0xff), [byte[]]@(0x7b, 0x22, 0x78, 0x22, 0x3a, 0x22, 0xc0, 0xaf, 0x22, 0x7d),
            [byte[]]@(0x7b, 0x22, 0x78, 0x22, 0x3a, 0x22, 0xed, 0xa0, 0x80, 0x22, 0x7d))) {
        Assert-Rejected { Identity $bytes }
    }
}
Test-Case 'the 64KiB byte cap is inclusive and rejects larger valid UTF8 JSON' {
    $prefix = '{"note":"'; $suffix = '"}'
    $overhead = [Text.Encoding]::UTF8.GetByteCount($prefix + $suffix)
    $exact = Bytes ($prefix + ('a' * (65536 - $overhead)) + $suffix)
    Assert-Equal $exact.Length 65536
    Assert-Equal (Identity $exact) $null
    $large = Bytes ($prefix + ('a' * (65537 - $overhead)) + $suffix)
    Assert-Rejected { Identity $large }
    Assert-Rejected { Identity (With-Key ('A' * 60000)) }
}
Test-Case 'depth sixteen is accepted and deeper nested manifests are rejected even without key' {
    $exact = '{}'
    for ($index = 0; $index -lt 15; $index++) { $exact = '{"child":' + $exact + '}' }
    Assert-Equal (Identity (Bytes $exact)) $null
    Assert-Rejected { Identity (Bytes ('{"child":' + $exact + '}')) }
}
Write-Host "Passed: $($state.Passed); Failed: $($state.Failed)"
if ($state.Failed -gt 0) { exit 1 }
