import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, existsSync } from 'node:fs';

const root = new URL('../../extensions/chromium/main/', import.meta.url);
const manifest = JSON.parse(readFileSync(new URL('manifest.json', root), 'utf8'));

test('MV3 main extension requests only temporary detection and explicit native messaging', () => {
  assert.equal(manifest.manifest_version, 3);
  assert.equal(manifest.minimum_chrome_version, '106');
  assert.deepEqual([...manifest.permissions].sort(), ['activeTab', 'nativeMessaging', 'scripting'].sort());
  for (const field of ['host_permissions', 'optional_host_permissions', 'content_scripts', 'background',
    'externally_connectable', 'web_accessible_resources', 'update_url', 'key']) {
    assert.equal(Object.hasOwn(manifest, field), false, field);
  }
  assert.equal(manifest.action.default_popup, 'popup.html');
  assert.match(manifest.version, /^\d+\.\d+\.\d+$/);
});

test('popup CSP blocks network, remote scripts, frames and embedding', () => {
  const directives = manifest.content_security_policy.extension_pages.split(';').map(s => s.trim());
  for (const rule of ["default-src 'self'", "script-src 'self'", "object-src 'none'", "connect-src 'none'",
    "frame-src 'none'", "base-uri 'none'", "form-action 'none'"]) assert.ok(directives.includes(rule), rule);
  assert.ok(!directives.some(rule => /unsafe-|https?:|\*/.test(rule)));
});

test('extension resources are local and present, with no script text evaluation or remote display assets', () => {
  const names = ['popup.html', 'popup.css', 'popup.mjs', 'popup-controller.mjs', 'policy.mjs', 'native-client.mjs', 'detector.js'];
  for (const name of names) assert.equal(existsSync(new URL(name, root)), true, name);
  for (const name of ['popup.html', 'popup.css', 'popup.mjs']) {
    const source = readFileSync(new URL(name, root), 'utf8');
    assert.doesNotMatch(source, /\b(?:eval|fetch|XMLHttpRequest)\s*\(|\binnerHTML\s*=|<(?:iframe|object|embed)\b|(?:src|href)\s*=\s*["']https?:/i);
  }
  const html = readFileSync(new URL('popup.html', root), 'utf8');
  assert.match(html, /<html\b[^>]*lang=["']pt-BR["']/i);
  assert.match(html, /<script\b[^>]*type=["']module["'][^>]*src=["']popup.mjs["']/i);
});
