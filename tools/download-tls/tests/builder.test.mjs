import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import test from 'node:test';

const repo = path.resolve(import.meta.dirname, '../../..');
const builder = path.join(repo, 'tools/download-tls/New-RcMusicRestrictedTlsBackend.ps1');
const source = path.join(repo, 'q/96bvah3m0t0t71m990olv5gpvs/source/qtbase-patched');
const sdk = path.join(repo, 'q/f75eb9bea8280844c6d1a2b3f4/sdk');
fs.mkdirSync(path.join(repo, 'out/product/download-tls'), { recursive: true });
const testRoot = fs.mkdtempSync(path.join(repo, 'out/product/download-tls/builder-test-'));
const digest = (file) => createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const invoke = (...args) => spawnSync('pwsh', ['-NoProfile', '-File', builder, '-PrepareOnly', ...args], {
  cwd: repo, encoding: 'utf8', timeout: 30000,
});
const detail = (result) => result.stdout + result.stderr;

test.after(() => {
  assert.ok(testRoot.startsWith(path.join(repo, 'out/product/download-tls/builder-test-')));
  fs.rmSync(testRoot, { recursive: true });
});

test('prepares a private backend from exact inputs and preserves the original source and SDK', () => {
  const output = path.join(testRoot, 'prepared');
  const originalFile = path.join(source, 'src/plugins/tls/schannel/qtls_schannel.cpp');
  const originalDigest = digest(originalFile);
  const originalDll = digest(path.join(sdk, 'plugins/tls/qschannelbackend.dll'));
  const result = invoke('-OutputDirectory', output);
  assert.equal(result.status, 0, detail(result));
  const receipt = JSON.parse(fs.readFileSync(path.join(output, 'prepared.json'), 'utf8'));
  assert.equal(receipt.backend, 'rcmusic-schannel-offline');
  assert.equal(receipt.qt_version, '6.11.2');
  assert.equal(receipt.compiled, false);
  assert.equal(receipt.revocation_policy, 'cached-status-soft-fail-unknown');
  assert.equal(receipt.inputs.length, 24);
  assert.equal(digest(originalFile), originalDigest);
  assert.equal(digest(path.join(sdk, 'plugins/tls/qschannelbackend.dll')), originalDll);
  const cpp = fs.readFileSync(path.join(output, 'source/schannel/qtls_schannel.cpp'), 'utf8');
  assert.match(cpp, /return QStringLiteral\("rcmusic-schannel-offline"\);/);
  assert.match(cpp, /CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL/);
  assert.match(cpp, /CERT_CHAIN_REVOCATION_CHECK_CACHE_ONLY/);
  assert.match(cpp, /CERT_CHAIN_DISABLE_AIA/);
  assert.match(cpp, /SCH_CRED_CACHE_ONLY_URL_RETRIEVAL_ON_CREATE/);
  assert.match(cpp, /SCH_CRED_REVOCATION_CHECK_CACHE_ONLY/);
  assert.equal((cpp.match(/rcMusicTlsInputFits\(intermediateBuffer\.size\(\), missingData\)/g) ?? []).length, 3);
  assert.equal((cpp.match(/readToBuffer\(intermediateBuffer, plainSocket\) < 0/g) ?? []).length, 2);
  assert.match(cpp, /if \(bytesRead < 0\)/);
  assert.match(cpp, /if \(!rcMusicTlsInputFits\(buffer\.size\(\), toRead\)\)/);
  assert.match(cpp, /CERT_TRUST_IS_REVOKED/);
  assert.match(cpp, /HostNameMismatch/);
  assert.doesNotMatch(cpp, /@future\(maybe\): Do something with this/);
  for (const record of receipt.outputs) {
    assert.equal(digest(path.join(output, record.path)), record.sha256);
  }
});

test('rejects a changed source before writing the destination', () => {
  const changed = path.join(testRoot, 'changed-source');
  fs.mkdirSync(path.join(changed, 'src/plugins/tls/schannel'), { recursive: true });
  fs.writeFileSync(path.join(changed, 'src/plugins/tls/schannel/qtls_schannel.cpp'), 'modified source');
  const output = path.join(testRoot, 'changed-source-output');
  const result = invoke('-SourceRoot', changed, '-OutputDirectory', output);
  assert.notEqual(result.status, 0);
  assert.match(detail(result), /SourceHashMismatch/);
  assert.equal(fs.existsSync(output), false);
});

test('rejects an unexpected SDK before copying source', () => {
  const changed = path.join(testRoot, 'changed-sdk');
  fs.mkdirSync(path.join(changed, 'bin'), { recursive: true });
  fs.writeFileSync(path.join(changed, 'bin/Qt6Core.dll'), 'unexpected SDK');
  const output = path.join(testRoot, 'changed-sdk-output');
  const result = invoke('-QtPrefix', changed, '-OutputDirectory', output);
  assert.notEqual(result.status, 0);
  assert.match(detail(result), /SdkHashMismatch/);
  assert.equal(fs.existsSync(output), false);
});

test('rejects destinations outside the build area and the build area itself', () => {
  for (const output of [repo, path.join(repo, 'out/product/download-tls'), path.join(repo, 'tools/download-tls/unwanted')]) {
    const result = invoke('-OutputDirectory', output);
    assert.notEqual(result.status, 0);
    assert.match(detail(result), /OutputOutsideBuildArea/);
  }
  assert.equal(fs.existsSync(path.join(repo, 'tools/download-tls/unwanted')), false);
});

test('refuses an existing destination without changing caller files', () => {
  const output = path.join(testRoot, 'existing');
  fs.mkdirSync(output);
  fs.writeFileSync(path.join(output, 'keep.txt'), 'caller owned');
  const result = invoke('-OutputDirectory', output);
  assert.notEqual(result.status, 0);
  assert.match(detail(result), /OutputAlreadyExists/);
  assert.equal(fs.readFileSync(path.join(output, 'keep.txt'), 'utf8'), 'caller owned');
});
