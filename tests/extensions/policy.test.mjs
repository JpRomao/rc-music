import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { normalizeSnapshot, buildDownloadRequest, validateDownloadRequest } from '../../extensions/chromium/main/policy.mjs';

const requestId = '01234567-89ab-4cde-8fab-0123456789ab';
const pageUrl = 'https://page.example.test/list?q=private#part';
const audio = { url: 'https://media.example.test/song.mp3?token=secret#time', title: 'Canção', kind: 'audio' };
const video = { url: 'https://media.example.test/video.mp4', title: 'Vídeo', kind: 'video' };
const snapshot = (candidates = [audio, video]) => ({ type: 'snapshot', pageUrl, candidates, truncated: false });
const throwsCode = (fn, code) => assert.throws(fn, error => error.code === code);

for (const vector of JSON.parse(readFileSync(new URL('./native-protocol-vectors.json', import.meta.url), 'utf8')).cases) {
  test(`shared native protocol: ${vector.name}`, () => {
    if (vector.accepted) assert.deepEqual(validateDownloadRequest(vector.request), vector.request);
    else throwsCode(() => validateDownloadRequest(vector.request), 'invalid_request');
  });
}

test('normalizes media URLs without fragment, keeps signed query and document identity, deduplicates', () => {
  const result = normalizeSnapshot(snapshot([audio, { ...audio, url: audio.url.replace('#time', '#other') }, video]));
  assert.equal(result.pageUrl, pageUrl);
  assert.equal(result.candidates.length, 2);
  assert.equal(result.candidates[0].url, 'https://media.example.test/song.mp3?token=secret');
  assert.equal(result.candidates[0].origin, 'https://media.example.test');
});

test('builds only explicitly selected URLs in displayed order without page URL or arbitrary fields', () => {
  const clean = normalizeSnapshot(snapshot());
  const request = buildDownloadRequest(clean, [clean.candidates[1].url], requestId);
  assert.deepEqual(request, { schemaVersion: 1, type: 'download.request', requestId, items: [video] });
  assert.deepEqual(validateDownloadRequest(request), request);
});

test('does not turn discovery, missing selection, duplicate selection or unknown URL into a request', () => {
  const clean = normalizeSnapshot(snapshot());
  for (const selected of [[], [video.url, video.url], ['https://other.example.test/file.mp3'], null]) {
    throwsCode(() => buildDownloadRequest(clean, selected, requestId), 'invalid_selection');
  }
});

test('rejects dangerous, local, opaque, credentialed and nonstandard-port candidates without rejecting valid media', () => {
  const bad = [
    'file:///C:/secret.mp3', 'data:audio/mp3;base64,AA==', 'blob:https://page.example.test/id',
    'javascript:alert(1)', '//remote.example.test/file.mp3', 'https:\\evil.example.test/a.mp3',
    'https://user:password@media.example.test/song.mp3', 'https://media.example.test:8443/a.mp3',
    'http://localhost/a.mp3', 'http://x.localhost/a.mp3', 'http://host.local/a.mp3', 'http://host/a.mp3',
    'http://127.0.0.1/a.mp3', 'http://127.1/a.mp3', 'http://2130706433/a.mp3', 'http://0x7f000001/a.mp3',
    'http://[::1]/a.mp3', 'http://[::ffff:127.0.0.1]/a.mp3', 'http://169.254.169.254/a.mp3',
    'http://192.168.1.1/a.mp3', 'https://8.8.8.8/a.mp3', 'http://router.home.arpa/a.mp3',
    'https://media.example.test/a\n.mp3', 'https://media.example.test/a\0.mp3',
  ];
  for (const url of bad) {
    assert.deepEqual(normalizeSnapshot(snapshot([{ ...audio, url }, video])).candidates.map(item => item.url), [video.url], url);
    throwsCode(() => normalizeSnapshot({ ...snapshot(), pageUrl: url }), 'unsupported_page');
  }
});

test('blocks YouTube pages/media and domain aliases, not suffix-lookalike unrelated sites', () => {
  for (const host of ['youtube.com', 'www.youtube.com', 'youtu.be', 'youtube-nocookie.com', 'r1.googlevideo.com',
    'YOUTUBE.COM.', '%79outube.com', 'youtubei.googleapis.com', 'youtube.googleapis.com', 'youtubekids.com']) {
    const url = `https://${host}/watch.mp4`;
    assert.equal(normalizeSnapshot(snapshot([{ ...video, url }])).candidates.length, 0);
    throwsCode(() => normalizeSnapshot({ ...snapshot(), pageUrl: url }), 'unsupported_page');
  }
  assert.equal(normalizeSnapshot(snapshot([{ ...video, url: 'https://notyoutube.com/video.mp4' }])).candidates.length, 1);
});

test('bounds and sanitizes title, rejects oversized URL, marks truncated candidate lists', () => {
  const long = { ...audio, title: '\0\u202E' + 'a'.repeat(220) };
  const result = normalizeSnapshot(snapshot([long, { ...video, url: 'https://media.example.test/' + 'x'.repeat(8192) }]));
  assert.equal(result.candidates[0].title, 'a'.repeat(200));
  assert.equal(result.candidates.length, 1);
  const many = Array.from({ length: 101 }, (_, i) => ({ ...audio, url: `https://media.example.test/${i}.mp3` }));
  const limited = normalizeSnapshot(snapshot(many));
  assert.equal(limited.candidates.length, 100);
  assert.equal(limited.truncated, true);
});

test('malformed snapshots fail closed without leaking untrusted values in errors', () => {
  for (const raw of [null, [], {}, { ...snapshot(), type: 'download.request' }, { ...snapshot(), candidates: null },
    { ...snapshot(), truncated: 'false' }, { ...snapshot(), command: 'run' }, { ...snapshot(), candidates: ['secret'] },
    { ...snapshot(), extra: 's'.repeat(1024 * 1024) }]) {
    throwsCode(() => normalizeSnapshot(raw), 'invalid_snapshot');
  }
  throwsCode(() => normalizeSnapshot(snapshot([{ ...audio, kind: 'command' }])), 'invalid_snapshot');
});

test('batch limit and exact UTF-8 quota are enforced without silently dropping selection', () => {
  const items = Array.from({ length: 21 }, (_, i) => ({ ...audio, url: `https://media.example.test/${i}.mp3` }));
  const clean = normalizeSnapshot(snapshot(items));
  assert.equal(buildDownloadRequest(clean, clean.candidates.slice(0, 20).map(i => i.url), requestId).items.length, 20);
  throwsCode(() => buildDownloadRequest(clean, clean.candidates.map(i => i.url), requestId), 'invalid_selection');
  const expanded = Array.from({ length: 20 }, (_, i) => ({ ...audio, url: `https://media.example.test/${i}?q=${'é'.repeat(7900)}` }));
  throwsCode(() => validateDownloadRequest({ schemaVersion: 1, type: 'download.request', requestId, items: expanded }), 'invalid_request');
});

test('native request validation rejects extra fields, noncanonical URLs, invalid IDs and untrusted object mutations', () => {
  const request = buildDownloadRequest(normalizeSnapshot(snapshot()), [video.url], requestId);
  for (const bad of [{ ...request, command: 'exec' }, { ...request, schemaVersion: 2 }, { ...request, requestId: '../file' },
    { ...request, items: [{ ...video, headers: { cookie: 'private' } }] }, { ...request, items: [video, video] },
    { ...request, items: [{ ...audio }] }, { ...request, items: [{ ...video, url: 'https://youtu.be/a.mp4' }] }]) {
    throwsCode(() => validateDownloadRequest(bad), 'invalid_request');
  }
  const copy = validateDownloadRequest(request);
  request.items[0].url = 'https://youtu.be/a.mp4';
  assert.equal(copy.items[0].url, video.url);
});

test('sparse native item arrays are rejected before serialization can turn holes into null', () => {
  for (const items of [new Array(1), [video, ,]]) {
    throwsCode(() => validateDownloadRequest({ schemaVersion: 1, type: 'download.request', requestId, items }), 'invalid_request');
  }
});

test('rejects malformed percent escapes and lone surrogates before URL parsing repairs them', () => {
  for (const suffix of ['%', '%0', '%zz', '\uD800', '\uDC00']) {
    const item = { ...video, url: `https://media.example.test/a${suffix}.mp4` };
    assert.equal(normalizeSnapshot(snapshot([item])).candidates.length, 0, suffix);
    throwsCode(() => validateDownloadRequest({ schemaVersion: 1, type: 'download.request', requestId, items: [item] }), 'invalid_request');
  }
});

test('preserves legal percent encodings and signed URL punctuation exactly', () => {
  const url = 'https://media.example.test./%7E%41%2f.mp3?q=[x]&sig=%2b';
  const clean = normalizeSnapshot(snapshot([{ ...audio, url }]));
  assert.equal(clean.candidates[0].url, url);
  assert.equal(buildDownloadRequest(clean, [url], requestId).items[0].url, url);
});

test('title sanitization repairs lone surrogates and never truncates half an emoji', () => {
  for (const [title, expected] of [['x\uD800y\uDC00', 'x\uFFFDy\uFFFD'], ['a'.repeat(199) + '🎵', 'a'.repeat(199)],
    ['a'.repeat(198) + '🎵x', 'a'.repeat(198) + '🎵']]) {
    const clean = normalizeSnapshot(snapshot([{ ...video, title }]));
    assert.equal(clean.candidates[0].title, expected);
    assert.equal(buildDownloadRequest(clean, [video.url], requestId).items[0].title, expected);
  }
  throwsCode(() => validateDownloadRequest({ schemaVersion: 1, type: 'download.request', requestId,
    items: [{ ...video, title: 'bad\uD800' }] }), 'invalid_request');
});
