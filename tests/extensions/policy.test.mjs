import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import * as policy from '../../extensions/chromium/main/policy.mjs';

const { normalizeSnapshot, buildDownloadRequest, validateDownloadRequest } = policy;

const requestId = '01234567-89ab-4cde-8fab-0123456789ab';
const pageUrl = 'https://page.example.test/list?q=private#part';
const audio = { url: 'https://media.example.test/song.mp3?token=secret#time', title: 'Canção', kind: 'audio' };
const video = { url: 'https://media.example.test/video.mp4', title: 'Vídeo', kind: 'video' };
const snapshot = (candidates = [audio, video]) => ({ type: 'snapshot', pageUrl,
  candidates: candidates.map((item, index) => ({ sourceId: `c1:d${index + 1}`, ...item })), truncated: false });
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
  assert.equal(result.candidates[0].sourceId, 'c1:d1');
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

test('snapshot source identities preserve both canonical positive safe-integer components', () => {
  for (const sourceId of ['c1:d1', 'c37:d902', 'c9007199254740991:d9007199254740991']) {
    const result = normalizeSnapshot(snapshot([{ ...video, sourceId }]));
    assert.equal(result.candidates[0].sourceId, sourceId);
    assert.equal(result.candidates[0].origin, 'https://media.example.test');
  }
});

test('snapshot source identities reject missing, non-ASCII, noncanonical and unsafe values', () => {
  const missing = snapshot([video]);
  delete missing.candidates[0].sourceId;
  throwsCode(() => normalizeSnapshot(missing), 'invalid_snapshot');
  for (const sourceId of [undefined, null, 1, {}, '', 'c0:d1', 'c1:d0', 'c01:d1', 'c1:d01',
    'c-1:d1', 'c1:d-1', 'c+1:d1', 'c1:d+1', 'c1.0:d1', 'c1:d1.0', 'c1e1:d1', 'c1:d1e1',
    'C1:d1', 'c1:D1', ' c1:d1', 'c1:d1 ', 'c1:d1\n', 'c１:d1', 'c1:d١',
    'c9007199254740992:d1', 'c1:d9007199254740992', 'c999999999999999999999:d1', 'c1:d1\0']) {
    throwsCode(() => normalizeSnapshot(snapshot([{ ...video, sourceId }])), 'invalid_snapshot');
  }
  throwsCode(() => normalizeSnapshot(snapshot([{ ...video, extra: 'not part of a candidate' }])), 'invalid_snapshot');
});

test('request construction revalidates source identities but keeps the native v1 item shape', () => {
  const clean = normalizeSnapshot(snapshot([{ ...video, sourceId: 'c7:d9' }]));
  const request = buildDownloadRequest(clean, [video.url], requestId);
  assert.deepEqual(request.items, [video]);
  assert.equal(clean.candidates[0].sourceId, 'c7:d9');
  assert.equal(Object.hasOwn(request.items[0], 'sourceId'), false);
  throwsCode(() => validateDownloadRequest({ ...request, items: [{ ...video, sourceId: 'c7:d9' }] }), 'invalid_request');
  for (const sourceId of [undefined, 'c0:d1', 'c1:d9007199254740992']) {
    throwsCode(() => buildDownloadRequest({ ...clean, candidates: [{ ...clean.candidates[0], sourceId }] },
      [video.url], requestId), 'invalid_snapshot');
  }
});

test('current selection accepts raw and normalized snapshots without losing source identity', () => {
  const raw = snapshot();
  const clean = normalizeSnapshot(raw);
  for (const before of [raw, clean]) {
    for (const fresh of [structuredClone(raw), normalizeSnapshot(raw)]) {
      assert.equal(policy.isSelectionCurrent(before, fresh, [video.url]), true);
      assert.equal(policy.isSelectionCurrent(before, fresh, clean.candidates.map(item => item.url)), true);
    }
  }
});

test('current selection ignores additions, removals and changes to unselected candidates', () => {
  const before = snapshot();
  for (const candidates of [
    [{ ...video, sourceId: 'c1:d2' }],
    [{ ...audio, title: 'Changed elsewhere', sourceId: 'c8:d3' }, { ...video, sourceId: 'c1:d2' }],
    [{ ...video, sourceId: 'c1:d2' }, { ...audio, url: 'https://media.example.test/new.mp3', sourceId: 'c9:d1' }],
  ]) {
    assert.equal(policy.isSelectionCurrent(before, snapshot(candidates), [video.url]), true);
  }
});

test('current selection rejects selected URL, title, kind or source identity changes', () => {
  const before = snapshot();
  for (const update of [
    { url: 'https://media.example.test/replacement.mp4' }, { title: 'Replacement' }, { kind: 'audio' },
    { sourceId: 'c2:d2' }, { sourceId: 'c1:d3' }, { sourceId: 'c0:d2' },
  ]) {
    const fresh = snapshot([audio, { ...video, sourceId: 'c1:d2', ...update }]);
    assert.equal(policy.isSelectionCurrent(before, fresh, [video.url]), false);
    assert.equal(policy.isSelectionCurrent(normalizeSnapshot(before), fresh, [video.url]), false);
  }
});

test('current selection requires the exact page URL and each selected URL to remain present', () => {
  const before = snapshot();
  for (const changedPage of ['https://page.example.test/other', pageUrl + '-new', pageUrl.replace('private', 'other')]) {
    assert.equal(policy.isSelectionCurrent(before, { ...snapshot(), pageUrl: changedPage }, [video.url]), false);
  }
  assert.equal(policy.isSelectionCurrent(before, snapshot([audio]), [video.url]), false);
  assert.equal(policy.isSelectionCurrent(snapshot([audio]), before, [video.url]), false);
});

test('current selection rejects duplicates in either snapshot before URL deduplication can hide them', () => {
  const clean = normalizeSnapshot(snapshot());
  const duplicated = snapshot([audio, video, { ...video, sourceId: 'c2:d1' }]);
  const normalizedDuplicate = { ...clean, candidates: [...clean.candidates, { ...clean.candidates[1] }] };
  for (const duplicate of [duplicated, normalizedDuplicate,
    snapshot([audio, video, { ...video, url: video.url + '#another', sourceId: 'c2:d1' }])]) {
    assert.equal(policy.isSelectionCurrent(duplicate, clean, [video.url]), false);
    assert.equal(policy.isSelectionCurrent(clean, duplicate, [video.url]), false);
  }
});

test('current selection fails closed for malformed, empty, duplicate or oversized selections', () => {
  const clean = normalizeSnapshot(snapshot());
  for (const selected of [undefined, null, {}, video.url, [], [video.url, video.url], [1], [null],
    new Array(1), [video.url, ,], Array.from({ length: 21 }, (_, index) => `https://media.example.test/${index}.mp3`)]) {
    assert.equal(policy.isSelectionCurrent(clean, clean, selected), false);
  }
  assert.equal(policy.isSelectionCurrent(clean, clean, ['https://media.example.test/absent.mp3']), false);
});

test('current selection returns false for invalid snapshots instead of surfacing protocol errors', () => {
  const clean = normalizeSnapshot(snapshot());
  for (const invalid of [null, [], {}, { ...snapshot(), truncated: 'false' },
    { ...snapshot(), pageUrl: 'file:///C:/page.html' }, { ...snapshot(), extra: 'unexpected' },
    { ...snapshot(), candidates: [{ ...video, sourceId: 'c1:d0' }] },
    { ...clean, candidates: [{ ...clean.candidates[1], sourceId: undefined }] }]) {
    assert.equal(policy.isSelectionCurrent(clean, invalid, [video.url]), false);
    assert.equal(policy.isSelectionCurrent(invalid, clean, [video.url]), false);
  }
});
