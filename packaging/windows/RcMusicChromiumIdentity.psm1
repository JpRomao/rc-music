#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-ChromiumIdentityJson([Text.Json.JsonElement] $Element) {
    if ($Element.ValueKind -eq [Text.Json.JsonValueKind]::Object) {
        $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($property in $Element.EnumerateObject()) {
            if (-not $names.Add($property.Name)) { throw 'Duplicate Chromium manifest JSON property.' }
            Assert-ChromiumIdentityJson $property.Value
        }
    } elseif ($Element.ValueKind -eq [Text.Json.JsonValueKind]::Array) {
        foreach ($item in $Element.EnumerateArray()) { Assert-ChromiumIdentityJson $item }
    }
}

# Chromium itself hashes decoded key bytes. Requiring canonical RSA SPKI with
# 2048..4096 bits is this packager's stricter policy, not publisher attestation.
# No key is generated, signed, installed or registered by this module.
function Get-RcMusicChromiumManifestIdentity {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [AllowEmptyCollection()] [byte[]] $ManifestBytes)
    if ($ManifestBytes.Length -lt 1 -or $ManifestBytes.Length -gt 65536) {
        throw 'Chromium manifest JSON must contain between 1 and 65536 bytes.'
    }
    $text = [Text.UTF8Encoding]::new($false, $true).GetString($ManifestBytes)
    $options = [Text.Json.JsonDocumentOptions]::new()
    $options.MaxDepth = 16
    $options.AllowTrailingCommas = $false
    $options.CommentHandling = [Text.Json.JsonCommentHandling]::Disallow
    $document = [Text.Json.JsonDocument]::Parse($text, $options)
    try {
        $root = $document.RootElement
        if ($root.ValueKind -ne [Text.Json.JsonValueKind]::Object) {
            throw 'A Chromium manifest must be a JSON object.'
        }
        Assert-ChromiumIdentityJson $root
        $key = $null
        foreach ($property in $root.EnumerateObject()) {
            if ($property.Name -ceq 'key') { $key = $property.Value; break }
        }
        if ($null -eq $key) { return $null }
        if ($key.ValueKind -ne [Text.Json.JsonValueKind]::String) {
            throw 'A present Chromium manifest key must be a public-key string.'
        }
        $encoded = $key.GetString()
        if ($encoded.Length -eq 0 -or $encoded -cmatch '[^A-Za-z0-9+/=]') {
            throw 'The Chromium public key must be canonical Base64 without whitespace or PEM wrappers.'
        }
        $der = [Convert]::FromBase64String($encoded)
        if ([Convert]::ToBase64String($der) -cne $encoded) {
            throw 'The Chromium public key Base64 representation is not canonical.'
        }
        $rsa = [Security.Cryptography.RSA]::Create()
        try {
            [int] $consumed = 0
            $rsa.ImportSubjectPublicKeyInfo($der, [ref]$consumed)
            if ($consumed -ne $der.Length -or $rsa.KeySize -lt 2048 -or $rsa.KeySize -gt 4096 -or
                [Convert]::ToBase64String($rsa.ExportSubjectPublicKeyInfo()) -cne $encoded) {
                throw 'The Chromium key must be a fully consumed canonical RSA SPKI public key of 2048 to 4096 bits.'
            }
        } finally { $rsa.Dispose() }

        $hash = [Security.Cryptography.SHA256]::HashData($der)
        $id = [Text.StringBuilder]::new(32)
        for ($index = 0; $index -lt 16; $index++) {
            $null = $id.Append([char](97 + ($hash[$index] -shr 4)))
            $null = $id.Append([char](97 + ($hash[$index] -band 15)))
        }
        $extensionId = $id.ToString()
        return [pscustomobject]@{
            extension_id = $extensionId; origin = 'chrome-extension://' + $extensionId + '/'
            public_key_sha256 = [Convert]::ToHexString($hash).ToLowerInvariant()
        }
    } finally { $document.Dispose() }
}

Export-ModuleMember -Function Get-RcMusicChromiumManifestIdentity
