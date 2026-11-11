import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

const root = new URL('../../extensions/chromium/youtube/', import.meta.url);
test('YouTube extension is optional and permission-isolated from the main package', async () => {
  const manifest = JSON.parse(await readFile(new URL('manifest.json', root), 'utf8'));
  assert.equal(manifest.manifest_version, 3);
  assert.deepEqual(manifest.permissions, ['activeTab', 'nativeMessaging']);
  assert.equal(manifest.host_permissions, undefined);
  assert.equal(manifest.background, undefined);
  assert.equal(manifest.content_scripts, undefined);
  assert.match(manifest.content_security_policy.extension_pages, /connect-src 'none'/u);
});
test('popup uses external scripts, labelled controls, plaintext DOM and an honest admission message', async () => {
  const html = await readFile(new URL('popup.html', root), 'utf8');
  const script = await readFile(new URL('popup.mjs', root), 'utf8');
  assert.match(html, /lang="pt-BR"/u);
  for (const id of ['selected', 'title', 'artist', 'lyrics-page', 'fetch-lyrics', 'send', 'status']) assert.match(html, new RegExp(`id="${id}"`, 'u'));
  assert.match(html, /type="module" src="popup.mjs"/u);
  assert.match(html, /Requer os opcionais Ferramentas de download e Preparar extensão YouTube do RC Music/u);
  assert.match(html, /Pedido aceito confirma a entrada na fila\. Acompanhe o progresso e a conclusão no painel Downloads do RC Music/u);
  assert.doesNotMatch(script, /innerHTML|eval\(|new Function|fetch\(/u);
});
