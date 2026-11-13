import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { videoFromTab, validateYoutubeAudioRequest, buildYoutubeAudioRequest } from '../../extensions/chromium/youtube/policy.mjs';

const requestId = '01234567-89ab-4cde-8fab-0123456789ab';
const item = () => ({ videoId: 'aB_0123-xYz', title: '曲のテスト', artist: 'Artist', lyricsPage: 'https://www.letras.mus.br/artist/song/', fetchLyrics: true });
const request = () => ({ schemaVersion: 1, type: 'youtube.audio.request', requestId, items: [item()] });
const vectors = JSON.parse(await readFile(new URL('./youtube-protocol-vectors.json', import.meta.url), 'utf8'));

for (const vector of vectors.requests) {
  test(`shared Qt/Node request vector: ${vector.name}`, () => {
    if (vector.valid) assert.deepEqual(validateYoutubeAudioRequest(vector.payload), vector.payload);
    else assert.throws(() => validateYoutubeAudioRequest(vector.payload));
  });
}

test('YouTube pages identify one exact video and remove tracking from the canonical identity', () => {
  for (const url of ['https://www.youtube.com/watch?v=aB_0123-xYz&list=private&t=25', 'https://youtube.com/shorts/aB_0123-xYz',
    'https://m.youtube.com/embed/aB_0123-xYz', 'https://youtu.be/aB_0123-xYz?si=private']) {
    const candidate = videoFromTab({ id: 7, url, title: '曲のテスト - YouTube' });
    assert.deepEqual(candidate, { tabId: 7, videoId: 'aB_0123-xYz', url: 'https://www.youtube.com/watch?v=aB_0123-xYz', title: '曲のテスト' });
  }
});
test('YouTube lookalikes, credentials, encoded paths, ambiguous IDs and other pages are refused', () => {
  for (const url of ['https://youtube.com.evil.test/watch?v=aB_0123-xYz', 'https://evilyoutube.com/watch?v=aB_0123-xYz',
    'https://user@youtube.com/watch?v=aB_0123-xYz', 'http://youtube.com/watch?v=aB_0123-xYz',
    'https://www.youtube.com:444/watch?v=aB_0123-xYz', 'https://youtube.com/watch?v=aB_0123-xYz&v=zzzzzzzzzzz',
    'https://youtube.com/watch?v=too-short', 'https://youtu.be/aB_0123-xYz/extra', 'https://youtube.com/shorts/%61B_0123-xYz',
    'https://youtube.com/playlist?list=secret', 'https://example.com/aB_0123-xYz', 'https://youtube.com\\@evil.test/watch?v=aB_0123-xYz']) {
    assert.throws(() => videoFromTab({ id: 7, url, title: 'Video' }), { code: 'unsupported_page' }, url);
  }
});
test('request contains only the selected video identity and editable metadata, never tool or media URLs', () => {
  const candidate = videoFromTab({ id: 7, url: 'https://www.youtube.com/watch?v=aB_0123-xYz', title: 'Video' });
  const fields = { title: '日本語 <b>literal</b>', artist: '', lyricsPage: '', fetchLyrics: false };
  const result = buildYoutubeAudioRequest(candidate, fields, requestId);
  assert.deepEqual(result, { schemaVersion: 1, type: 'youtube.audio.request', requestId, items: [{ videoId: candidate.videoId, ...fields }] });
  assert.deepEqual(validateYoutubeAudioRequest(result), result);
  fields.title = 'Changed';
  assert.equal(result.items[0].title, '日本語 <b>literal</b>');
});
test('valid requests preserve all metadata and are detached from their caller', () => {
  const raw = request();
  const copied = validateYoutubeAudioRequest(raw);
  assert.deepEqual(copied, raw);
  copied.items[0].title = 'Changed';
  assert.equal(raw.items[0].title, '曲のテスト');
});
test('strict request schema rejects extra fields, invalid UUIDs, coercions and duplicate videos', () => {
  const invalid = [null, {}, { ...request(), type: 'download.request' }, { ...request(), schemaVersion: 2 },
    { ...request(), command: 'run' }, { ...request(), requestId: 'not-a-uuid' }, { ...request(), items: [] },
    { ...request(), items: [item(), item()] }];
  for (const change of [{ videoId: 'x' }, { videoId: 'abc/defghij' }, { url: 'https://evil.test/a.mp3' }, { title: '' },
    { title: ' leading' }, { title: 'trailing ' }, { title: 'A\nB' }, { title: 'A\u202eB' }, { artist: 4 },
    { artist: 'bad\u0000text' }, { fetchLyrics: 'true' }, { title: '\ud800' }]) {
    invalid.push({ ...request(), items: [{ ...item(), ...change }] });
  }
  for (const raw of invalid) assert.throws(() => validateYoutubeAudioRequest(raw));
});
test('metadata byte limits preserve Unicode without allowing oversized messages', () => {
  assert.doesNotThrow(() => validateYoutubeAudioRequest({ ...request(), items: [{ ...item(), title: 'a'.repeat(512), artist: 'b'.repeat(512) }] }));
  for (const change of [{ title: 'a'.repeat(513) }, { artist: '日'.repeat(171) }]) {
    assert.throws(() => validateYoutubeAudioRequest({ ...request(), items: [{ ...item(), ...change }] }));
  }
  assert.doesNotThrow(() => validateYoutubeAudioRequest({ ...request(), items: Array.from({ length: 20 }, (_, i) => ({ ...item(), videoId: String(i).padStart(11, '0') })) }));
  assert.throws(() => validateYoutubeAudioRequest({ ...request(), items: Array.from({ length: 21 }, (_, i) => ({ ...item(), videoId: String(i).padStart(11, '0') })) }));
  assert.throws(() => validateYoutubeAudioRequest({ ...request(), items: Array.from({ length: 20 }, (_, i) => ({ ...item(),
    videoId: String(i).padStart(11, '0'), title: '"'.repeat(512), artist: '"'.repeat(512), lyricsPage: `https://www.letras.mus.br/${'a'.repeat(1900)}/` })) }));
});
test('lyrics page is optional and otherwise bound to exact Letras HTTPS pages', () => {
  for (const lyricsPage of ['', 'https://letras.mus.br/artist/song/', 'https://WWW.LETRAS.MUS.BR:443/artist/song']) {
    assert.doesNotThrow(() => validateYoutubeAudioRequest({ ...request(), items: [{ ...item(), lyricsPage }] }));
  }
  for (const lyricsPage of ['http://www.letras.mus.br/a/b/', 'https://www.letras.mus.br.evil.test/a/b/',
    'https://user:pass@www.letras.mus.br/a/b/', 'https://www.letras.mus.br:444/a/b/', 'https://www.letras.mus.br/a/b/?key=private',
    'https://www.letras.mus.br/a/b/#part', 'https://www.letras.mus.br/a/../b/', 'https://www.letras.mus.br/a/%2e%2e/b/',
    'https://www.letras.mus.br/a/%2fb/', 'https://www.letras.mus.br/a/%5cb/', 'https://www.letras.mus.br/a//b/',
    'https://www.letras.mus.br/a/%zz/', 'https://www.letras.mus.br/a/\u0000b/', 'https://www.letras.mus.br/a/\\b/',
    'https://@letras.mus.br/a/b/', 'https://www.letras.mus.br/a/b/?', 'https://www.letras.mus.br/a/b/#',
    'https://www.letras.mus.br/a/%252e%252e/']) {
    assert.throws(() => validateYoutubeAudioRequest({ ...request(), items: [{ ...item(), lyricsPage }] }), undefined, lyricsPage);
  }
});

test('optional output name is independent of track metadata and omitted when blank', () => {
  const candidate = { tabId: 7, videoId: 'aB_0123-xYz' };
  const fields = { title: 'Number One', artist: 'Bleach', lyricsPage: '', fetchLyrics: false };
  assert.deepEqual(buildYoutubeAudioRequest(candidate, { ...fields, fileName: '  Faixa 日本.MP3  ' }, requestId).items,
    [{ videoId: candidate.videoId, ...fields, fileName: 'Faixa 日本.MP3' }]);
  assert.deepEqual(buildYoutubeAudioRequest(candidate, { ...fields, fileName: '  ' }, requestId).items,
    [{ videoId: candidate.videoId, ...fields }]);
  for (const fileName of [null, '', ' padded', 'a\nb', 'x\u202ey', '日'.repeat(171)]) {
    const raw = request();
    raw.items[0].fileName = fileName;
    assert.throws(() => validateYoutubeAudioRequest(raw));
  }
});
