import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash, createPublicKey } from 'node:crypto';
import { readFileSync } from 'node:fs';

const identities = [
  ['main', 'kggjfjbjhklkbpelddffdjelbiomoolc', 'a66959197aba1f4b3355394b18eceeb25dd21767a74a5d2fd6e8dc8a711dd953'],
  ['youtube', 'lhcdmfamadilnahlckpalijljfohjgkc', 'b723c50c038bd07b2af0b89b95e796a22a30d1fc00d790f98b218ae24a676724'],
];

for (const [channel, expectedId, expectedHash] of identities) {
  test(`${channel} has a pinned public identity independent of the extension directory`, () => {
    const manifest = JSON.parse(readFileSync(new URL(`../../extensions/chromium/${channel}/manifest.json`, import.meta.url), 'utf8'));
    assert.equal(typeof manifest.key, 'string');
    const der = Buffer.from(manifest.key, 'base64');
    assert.equal(der.toString('base64'), manifest.key);
    const publicKey = createPublicKey({ key: der, format: 'der', type: 'spki' });
    assert.equal(publicKey.type, 'public');
    assert.equal(publicKey.asymmetricKeyType, 'rsa');
    assert.equal(publicKey.asymmetricKeyDetails.modulusLength, 2048);
    assert.deepEqual(publicKey.export({ format: 'der', type: 'spki' }), der);
    // Expected values were calculated independently with Python hashlib.
    const hash = createHash('sha256').update(der).digest('hex');
    assert.equal(hash, expectedHash);
    const id = hash.slice(0, 32).replace(/[0-9a-f]/gu, nibble => String.fromCharCode(97 + Number.parseInt(nibble, 16)));
    assert.equal(id, expectedId);
  });
}

test('the two extension channels retain distinct identities', () => {
  assert.notEqual(identities[0][1], identities[1][1]);
});
