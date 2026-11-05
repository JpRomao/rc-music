import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { sendYoutubeAudioRequest } from '../../extensions/chromium/youtube/native-client.mjs';

const requestId = '01234567-89ab-4cde-8fab-0123456789ab';
const request = () => ({ schemaVersion: 1, type: 'youtube.audio.request', requestId, items: [{ videoId: 'aB_0123-xYz', title: 'Title', artist: '', lyricsPage: '', fetchLyrics: true }] });
const accepted = () => ({ schemaVersion: 1, type: 'youtube.audio.response', requestId, status: 'accepted', jobIds: ['yt-job-1'] });
const vectors = JSON.parse(await readFile(new URL('./youtube-protocol-vectors.json', import.meta.url), 'utf8'));
function event() { const listeners = new Set(); return { addListener: f => listeners.add(f), removeListener: f => listeners.delete(f), emit: (...args) => [...listeners].forEach(f => f(...args)), get size() { return listeners.size; } }; }
function harness() {
  const calls = [], hosts = [], timers = new Map();
  let nextTimer = 0;
  const port = { onMessage: event(), onDisconnect: event(), disconnected: 0, postMessage: raw => calls.push(structuredClone(raw)), disconnect() { this.disconnected++; } };
  const chrome = { runtime: { lastError: undefined, connectNative: host => { hosts.push(host); return port; } } };
  const options = { setTimer: callback => { timers.set(++nextTimer, callback); return nextTimer; }, clearTimer: id => timers.delete(id) };
  return { chrome, port, calls, hosts, options, timers };
}
for (const vector of vectors.responses) {
  test(`shared Qt/Node response vector: ${vector.name}`, async () => {
    const h = harness(), raw = request();
    raw.items = Array.from({ length: vector.itemCount }, (_, index) => ({ ...raw.items[0], videoId: String(index).padStart(11, '0') }));
    const pending = sendYoutubeAudioRequest(h.chrome, raw, h.options);
    h.port.onMessage.emit(vector.payload);
    if (vector.valid && vector.payload.status === 'accepted') assert.deepEqual((await pending).jobIds, vector.payload.jobIds);
    else await assert.rejects(pending, { code: vector.valid ? 'rejected' : 'invalid_response' });
  });
}
test('YouTube bridge is separate and acknowledges admission rather than completion', async () => {
  const h = harness(), pending = sendYoutubeAudioRequest(h.chrome, request(), h.options);
  pending.catch(() => {}); // Keep an expected Red assertion from leaking a rejected promise.
  assert.deepEqual(h.hosts, ['com.rcmusic.youtube']);
  assert.deepEqual(h.calls, [request()]);
  h.port.onMessage.emit(accepted());
  assert.deepEqual(await pending, { status: 'accepted', requestId, jobIds: ['yt-job-1'] });
  assert.equal(h.port.disconnected, 1);
  assert.equal(h.timers.size + h.port.onMessage.size + h.port.onDisconnect.size, 0);
});
test('bad requests never reach either native bridge', async () => {
  const h = harness();
  await assert.rejects(sendYoutubeAudioRequest(h.chrome, { ...request(), command: 'run' }, h.options), { code: 'invalid_request' });
  assert.equal(h.hosts.length + h.calls.length, 0);
});
test('missing bridge and disconnect are explicit uncertainty and do not expose native details', async () => {
  const h = harness();
  h.chrome.runtime.connectNative = () => { throw new Error('C:/private/path token'); };
  await assert.rejects(sendYoutubeAudioRequest(h.chrome, request(), h.options), { code: 'bridge_unavailable', message: 'bridge_unavailable' });
  const h2 = harness(), pending = sendYoutubeAudioRequest(h2.chrome, request(), h2.options);
  h2.chrome.runtime.lastError = { message: 'private native details' };
  h2.port.onDisconnect.emit();
  await assert.rejects(pending, { code: 'bridge_unavailable', message: 'bridge_unavailable' });
  assert.equal(h2.calls.length, 1);
  assert.equal(h2.timers.size, 0);
});
test('timeout closes listeners and does not resend after a late response', async () => {
  const h = harness(), pending = sendYoutubeAudioRequest(h.chrome, request(), h.options);
  for (const callback of [...h.timers.values()]) callback();
  await assert.rejects(pending, { code: 'bridge_timeout' });
  h.port.onMessage.emit(accepted());
  assert.equal(h.calls.length, 1);
  assert.equal(h.port.onMessage.size + h.port.onDisconnect.size + h.timers.size, 0);
});
test('mismatched, main-protocol, duplicated or oversized ACKs never report acceptance', async () => {
  for (const raw of [null, {}, { ...accepted(), type: 'download.response' }, { ...accepted(), requestId: 'other' },
    { ...accepted(), status: 'completed' }, { ...accepted(), jobIds: ['one', 'two'] }, { ...accepted(), jobIds: ['a'.repeat(81)] },
    { ...accepted(), extra: 'field' }, { ...accepted(), jobIds: ['bad/path'] }]) {
    const h = harness(), pending = sendYoutubeAudioRequest(h.chrome, request(), h.options);
    h.port.onMessage.emit(raw);
    await assert.rejects(pending, { code: 'invalid_response' });
  }
});
test('known server rejection is not an accepted download', async () => {
  const h = harness(), pending = sendYoutubeAudioRequest(h.chrome, request(), h.options);
  h.port.onMessage.emit({ schemaVersion: 1, type: 'youtube.audio.response', requestId, status: 'rejected', code: 'not_available' });
  await assert.rejects(pending, { code: 'rejected' });
});
