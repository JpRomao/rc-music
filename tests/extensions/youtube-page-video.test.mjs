import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import * as collectors from '../../extensions/chromium/youtube/page-video.mjs';
const { collectLetrasYoutubeVideo, collectYoutubeMusicMetadata } = collectors;

const collectorSource = '(' + collectLetrasYoutubeVideo.toString() + ')()';
const pageUrl = 'https://www.letras.mus.br/bleach/1222774/';
// Minimal public data shape observed in the maintainer's chosen page, not its lyrics or full HTML.
const descriptor = `_omq.push(['ui/player', {"Artist":"Bleach","DNS":"bleach","ID":1222774,"Name":"＊〜アスタリスク〜 (Asterisk)","URL":"1222774","YoutubeID":"wW9TwZdWpjw"}]);`;
function documentFixture({ url = pageUrl, scripts = [descriptor], frames = [], player = true } = {}) {
  const root = { querySelectorAll: selector => { assert.equal(selector, 'iframe'); return frames.map(src => ({ getAttribute: name => name === 'src' ? src : null })); } };
  return { location: { href: url }, baseURI: url, title: 'Asterisk - Bleach - LETRAS.MUS.BR',
    scripts: scripts.map(textContent => ({ src: '', type: '', textContent })),
    getElementById: id => { assert.equal(id, 'player'); return player ? root : null; } };
}
const expected = { pageUrl, videoId: 'wW9TwZdWpjw', title: '＊〜アスタリスク〜 (Asterisk)', artist: 'Bleach' };
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

const numberOnePage = 'https://www.letras.mus.br/bleach/453185/';
const numberOneDescriptor = '_omq.push(["ui/player", {"Artist":"Bleach","DNS":"bleach","ID":453185,"Name":"Number One","URL":"453185","YoutubeID":""}]);';
const numberOneVideo = 'DW6toOrQkkU';
function letrasProxy({ id = numberOneVideo, paramsId = id, host = 'https://www.letras.mus.br' } = {}) {
  const url = new URL('https://solr.sscdn.co/iframe_proxy_player_letras_endpoint.html');
  url.search = new URLSearchParams({ v: '1', host, api_player: 'Iframe',
    player_vars: JSON.stringify({ autoplay: 1, type: 'Iframe', videoId: paramsId }), video_id: id });
  return url.href;
}
function numberOneFixture(frames, scripts = [numberOneDescriptor]) {
  return documentFixture({ url: numberOnePage, scripts, frames });
}
test('Number One is recognized when Letras loads its proxy iframe with the video ID', () => {
  const beforePlayback = numberOneFixture(['about:blank']);
  assert.equal(collectLetrasYoutubeVideo(beforePlayback), null);
  const afterPlayback = numberOneFixture(['about:blank', letrasProxy()]);
  const context = { document: afterPlayback, URL, TextEncoder };
  assert.deepEqual(JSON.parse(JSON.stringify(vm.runInNewContext(collectorSource, context))),
    { pageUrl: numberOnePage, videoId: numberOneVideo, title: 'Number One', artist: 'Bleach' });
});
test('Letras proxy requires its exact endpoint, one consistent video ID and the current page origin', () => {
  for (const src of [
    letrasProxy().replace('solr.sscdn.co', 'solr.sscdn.co.evil.test'),
    letrasProxy().replace('solr.sscdn.co', 'user@solr.sscdn.co'),
    letrasProxy().replace('solr.sscdn.co', 'solr.sscdn.co:444'),
    letrasProxy().replace('/iframe_proxy_player_letras_endpoint.html', '/other.html'),
    letrasProxy({ host: 'https://letras.mus.br' }),
    letrasProxy({ paramsId: 'aB_0123-xYz' }),
    letrasProxy({ id: 'invalid' }),
    letrasProxy() + '&video_id=DW6toOrQkkU',
    letrasProxy() + '&player_vars=%7B%7D',
  ]) assert.equal(collectLetrasYoutubeVideo(numberOneFixture([src])), null, src);
  assert.equal(collectLetrasYoutubeVideo(numberOneFixture([letrasProxy()], [])), null);
  assert.equal(collectLetrasYoutubeVideo(numberOneFixture([letrasProxy()], [numberOneDescriptor.replace('"453185"', '"other"')])), null);
});
test('Letras rejects conflicting direct and proxy videos instead of choosing one', () => {
  assert.equal(collectLetrasYoutubeVideo(numberOneFixture([letrasProxy(), 'https://www.youtube.com/embed/aB_0123-xYz'])), null);
  const staticOther = numberOneDescriptor.replace('"YoutubeID":""', '"YoutubeID":"aB_0123-xYz"');
  assert.equal(collectLetrasYoutubeVideo(numberOneFixture([letrasProxy()], [staticOther])), null);
});

const youtubePage = 'https://www.youtube.com/watch?v=aB_0123-xYz&list=PLfixture';
const youtubeObject = 'https://www.youtube.com/watch?v=aB_0123-xYz';
const recording = { '@type': 'MusicRecording', url: youtubeObject, name: '音楽の題', byArtist: { name: 'Recorded Artist' } };
function youtubeFixture({ url = youtubePage, scripts = [], properties = {} } = {}) {
  return { location: { href: url }, querySelectorAll(selector) {
    if (selector === 'script[type="application/ld+json"]')
      return scripts.map(data => ({ textContent: typeof data === 'string' ? data : JSON.stringify(data) }));
    assert.equal(selector, 'meta[property="og:type"], meta[property="og:title"], meta[property="og:url"], meta[property="music:musician"]');
    return Object.entries(properties).flatMap(([property, content]) => [content].flat().map(value => ({
      getAttribute: name => name === 'property' ? property : name === 'content' ? value : null,
    })));
  } };
}
test('YouTube isolated collector reads only a MusicRecording attached to the selected video', () => {
  const document = youtubeFixture({ scripts: [{ '@context': 'https://schema.org', '@graph': [
    { '@type': 'VideoObject', url: youtubeObject, name: 'Video title', author: { name: 'Uploader' } },
    recording,
  ] }] });
  const parsed = vm.runInNewContext(`(${collectYoutubeMusicMetadata.toString()})()`, { document, URL });
  assert.deepEqual(JSON.parse(JSON.stringify(parsed)), { pageUrl: youtubePage, title: '音楽の題', artist: 'Recorded Artist' });
  assert.deepEqual(collectYoutubeMusicMetadata(youtubeFixture({ scripts: [{ '@type': 'VideoObject', embedUrl: 'https://www.youtube.com/embed/aB_0123-xYz',
    audio: { '@type': 'https://schema.org/MusicRecording', byArtist: { name: 'Performer' } } }] })), { pageUrl: youtubePage, artist: 'Performer' });
});
test('YouTube ignores uploaded-by data, mismatched recordings, and competing songs', () => {
  const upload = { '@type': 'VideoObject', url: youtubeObject, name: 'Video title', author: { name: 'Uploader' } };
  for (const scripts of [[upload, { ...recording, url: 'https://www.youtube.com/watch?v=wW9TwZdWpjw' }],
    [upload, { ...recording, url: undefined }], [recording, { ...recording, name: 'Different track' }],
    [{ ...recording, byArtist: undefined }, { ...recording, name: undefined }],
    [{ ...upload, audio: [{ ...recording, url: undefined }, { ...recording, url: undefined, name: 'Other song' }] }],
    ['{"@type":"MusicRecording","byArtist":(globalThis.executed = true)}', upload],
    Array(9).fill(recording), ['x'.repeat(65537), recording]]) {
    assert.deepEqual(collectYoutubeMusicMetadata(youtubeFixture({ scripts })), { pageUrl: youtubePage });
  }
});
test('YouTube reads song-scoped Open Graph title without using a profile URL as the artist', () => {
  const properties = { 'og:type': 'music.song', 'og:url': youtubeObject, 'og:title': 'Song', 'music:musician': 'https://example.test/artists/a' };
  assert.deepEqual(collectYoutubeMusicMetadata(youtubeFixture({ properties })), { pageUrl: youtubePage, title: 'Song' });
  assert.deepEqual(collectYoutubeMusicMetadata(youtubeFixture({ properties: { ...properties, 'music:musician': 'Explicit Artist' } })),
    { pageUrl: youtubePage, title: 'Song', artist: 'Explicit Artist' });
  assert.deepEqual(collectYoutubeMusicMetadata(youtubeFixture({ properties: { ...properties, 'og:type': 'video.other', 'music:musician': 'Uploader' } })),
    { pageUrl: youtubePage });
  assert.deepEqual(collectYoutubeMusicMetadata(youtubeFixture({ scripts: [recording], properties })), { pageUrl: youtubePage });
});
test('YouTube collector validates video URL before reading any structured metadata', () => {
  for (const url of ['http://www.youtube.com/watch?v=aB_0123-xYz', 'https://www.youtube.com.evil.test/watch?v=aB_0123-xYz',
    'https://user@www.youtube.com/watch?v=aB_0123-xYz', 'https://www.youtube.com/feed/subscriptions',
    'https://www.youtube.com/watch?v=aB_0123-xYz&v=wW9TwZdWpjw']) {
    const document = { location: { href: url }, querySelectorAll() { assert.fail('read unsupported page'); } };
    assert.equal(collectYoutubeMusicMetadata(document), null);
  }
});
