import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import { collectLetrasYoutubeVideo } from '../../extensions/chromium/youtube/page-video.mjs';

const pageUrl = 'https://www.letras.mus.br/bleach/1222774/';
// Minimal public data shape observed in the maintainer's chosen page, not its lyrics or full HTML.
const descriptor = `_omq.push(['ui/player', {"Artist":"Bleach","DNS":"bleach","ID":1222774,"Name":"＊〜アスタリスク〜 (Asterisk)","URL":"1222774","YoutubeID":"wW9TwZdWpjw"}]);`;
function documentFixture({ url = pageUrl, scripts = [descriptor], frames = [], player = true } = {}) {
  const root = { querySelectorAll: selector => { assert.equal(selector, 'iframe'); return frames.map(src => ({ getAttribute: name => name === 'src' ? src : null })); } };
  return { location: { href: url }, baseURI: url, title: 'Asterisk - Bleach - LETRAS.MUS.BR',
    scripts: scripts.map(textContent => ({ src: '', type: '', textContent })),
    getElementById: id => { assert.equal(id, 'player'); return player ? root : null; } };
}
const expected = { pageUrl, videoId: 'wW9TwZdWpjw', title: '＊〜アスタリスク〜 (Asterisk)' };
test('Letras public player descriptor yields only its bounded source identity and title', () => {
  assert.deepEqual(collectLetrasYoutubeVideo(documentFixture()), expected);
});
test('serialized isolated-world collector needs no closure, extension API or page script execution', () => {
  const context = { document: documentFixture(), URL, TextEncoder };
  assert.deepEqual(JSON.parse(JSON.stringify(vm.runInNewContext(`(${collectLetrasYoutubeVideo.toString()})()`, context))), expected);
});
test('Letras player iframe supplies an identity without inspecting its cross-origin document', () => {
  const doc = documentFixture({ scripts: [], frames: ['https://www.youtube-nocookie.com/embed/wW9TwZdWpjw?enablejsapi=1'] });
  assert.deepEqual(collectLetrasYoutubeVideo(doc), { pageUrl, videoId: 'wW9TwZdWpjw', title: doc.title });
  assert.deepEqual(collectLetrasYoutubeVideo(documentFixture({ frames: ['https://www.youtube.com/embed/wW9TwZdWpjw'] })), expected);
});
test('Letras rejects another page descriptor or divergent video identities instead of choosing the first', () => {
  for (const doc of [
    documentFixture({ scripts: [descriptor.replace('"DNS":"bleach"', '"DNS":"other"')] }),
    documentFixture({ scripts: [descriptor.replace('"URL":"1222774"', '"URL":"other"')] }),
    documentFixture({ scripts: [descriptor, descriptor.replace('wW9TwZdWpjw', 'aB_0123-xYz')] }),
    documentFixture({ frames: ['https://www.youtube.com/embed/aB_0123-xYz'] }),
    documentFixture({ scripts: [], frames: ['https://www.youtube.com/embed/aB_0123-xYz', 'https://www.youtube.com/embed/wW9TwZdWpjw'] }),
  ]) assert.equal(collectLetrasYoutubeVideo(doc), null);
});
test('Letras never executes JavaScript pretending to be player data', () => {
  const doc = documentFixture({ scripts: [`_omq.push(['ui/player', (() => { globalThis.compromised = true; return {"YoutubeID":"wW9TwZdWpjw"}; })()]);`] });
  const context = { document: doc, URL, TextEncoder, compromised: false };
  assert.equal(vm.runInNewContext(`(${collectLetrasYoutubeVideo.toString()})()`, context), null);
  assert.equal(context.compromised, false);
});
test('Letras rejects malformed descriptors, invalid IDs and missing player instead of guessing', () => {
  for (const doc of [
    documentFixture({ scripts: [descriptor.replace('"YoutubeID":"wW9TwZdWpjw"', '"YoutubeID":null')] }),
    documentFixture({ scripts: [descriptor.replace('wW9TwZdWpjw', 'invalid')] }),
    documentFixture({ scripts: [descriptor.replace('"Name"', 'Name')] }),
    documentFixture({ scripts: [descriptor + 'fetch("https://other.test/")'] }),
    documentFixture({ player: false }), documentFixture({ scripts: [] }),
  ]) assert.equal(collectLetrasYoutubeVideo(doc), null);
});
test('Letras collector enforces host, scheme and canonical song path before reading page data', () => {
  for (const url of ['https://www.letras.mus.br.evil.test/bleach/1222774/', 'http://www.letras.mus.br/bleach/1222774/',
    'https://user@www.letras.mus.br/bleach/1222774/', 'https://www.letras.mus.br:444/bleach/1222774/',
    'https://www.letras.mus.br/bleach/1222774/?tracking=x', 'https://www.letras.mus.br/bleach/1222774/#player',
    'https://www.letras.mus.br/other/../bleach/1222774/', 'https://www.letras.mus.br/bleach%2fother/1222774/',
    'https://www.letras.mus.br/', 'not-a-url']) {
    const doc = documentFixture({ url });
    Object.defineProperty(doc, 'scripts', { get() { assert.fail('unsupported origin/path read page scripts'); } });
    assert.equal(collectLetrasYoutubeVideo(doc), null, url);
  }
});
test('Letras read limits reject excess scripts, text bytes and player frames', () => {
  const text = ' '.repeat(65537);
  for (const doc of [documentFixture({ scripts: [...Array(64).fill(''), descriptor] }),
    documentFixture({ scripts: [text, descriptor] }), documentFixture({ scripts: ['曲'.repeat(24000), descriptor] }),
    documentFixture({ scripts: [...Array(17).fill(' '.repeat(64000)), descriptor] }),
    documentFixture({ frames: Array(9).fill('https://www.youtube.com/embed/wW9TwZdWpjw') }),
  ]) assert.equal(collectLetrasYoutubeVideo(doc), null);
});
test('Letras iframe discovery rejects lookalike and credentialed hosts without following links', () => {
  for (const src of ['https://youtube.com.evil.test/embed/wW9TwZdWpjw', 'http://www.youtube.com/embed/wW9TwZdWpjw',
    'https://user@www.youtube.com/embed/wW9TwZdWpjw', 'https://www.youtube.com:444/embed/wW9TwZdWpjw',
    'https://www.youtube.com/embed/invalid', 'javascript:alert(1)']) {
    assert.equal(collectLetrasYoutubeVideo(documentFixture({ scripts: [], frames: [src] })), null);
  }
});
