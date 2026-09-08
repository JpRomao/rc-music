import test from 'node:test';
import assert from 'node:assert/strict';
import { sendDownloadRequest } from '../../extensions/chromium/main/native-client.mjs';

const requestId = '01234567-89ab-4cde-8fab-0123456789ab';
const request = () => ({ schemaVersion: 1, type: 'download.request', requestId,
  items: [{ url: 'https://media.example.test/a.mp3', title: 'Áudio', kind: 'audio' }] });
const accepted = () => ({ schemaVersion: 1, type: 'download.response', requestId, status: 'accepted', jobIds: ['job-1'] });
function event() {
  const handlers = new Set();
  return { addListener: f => handlers.add(f), removeListener: f => handlers.delete(f),
    emit: value => [...handlers].forEach(f => f(value)), get size() { return handlers.size; } };
}
function harness() {
  const calls = [];
  const timers = new Map();
  let timerId = 0;
  const port = { onMessage: event(), onDisconnect: event(), disconnected: 0,
    postMessage: message => calls.push(structuredClone(message)), disconnect() { this.disconnected++; } };
  const hosts = [];
  const chrome = { runtime: { connectNative: host => { hosts.push(host); return port; }, lastError: undefined } };
  const options = { setTimer: f => { timers.set(++timerId, f); return timerId; }, clearTimer: id => timers.delete(id) };
  return { chrome, port, calls, hosts, options, timers, expire: () => [...timers.values()].forEach(f => f()) };
}

test('validates then sends exactly once to fixed native host and accepts correlated ACK, not completion', async () => {
  const h = harness();
  const pending = sendDownloadRequest(h.chrome, request(), h.options);
  assert.deepEqual(h.hosts, ['com.rcmusic.media']);
  assert.deepEqual(h.calls, [request()]);
  h.port.onMessage.emit(accepted());
  assert.deepEqual(await pending, { status: 'accepted', requestId, jobIds: ['job-1'] });
  assert.equal(h.port.disconnected, 1);
  assert.equal(h.port.onMessage.size + h.port.onDisconnect.size + h.timers.size, 0);
});

test('invalid request never connects or transmits', async () => {
  const h = harness();
  await assert.rejects(sendDownloadRequest(h.chrome, { ...request(), command: 'run' }, h.options), { code: 'invalid_request' });
  assert.equal(h.hosts.length + h.calls.length, 0);
});

test('unavailable host and disconnect report safe error without echoing native paths or URLs', async () => {
  const h = harness();
  h.chrome.runtime.connectNative = () => { throw new Error('C:/secret/path https://private/token'); };
  await assert.rejects(sendDownloadRequest(h.chrome, request(), h.options), { code: 'bridge_unavailable', message: 'bridge_unavailable' });
  const h2 = harness();
  const pending = sendDownloadRequest(h2.chrome, request(), h2.options);
  h2.chrome.runtime.lastError = { message: 'secret paths' };
  h2.port.onDisconnect.emit();
  await assert.rejects(pending, { code: 'bridge_unavailable', message: 'bridge_unavailable' });
  assert.equal(h2.calls.length, 1);
  assert.equal(h2.timers.size + h2.port.onMessage.size + h2.port.onDisconnect.size, 0);
});

test('timeout cleans channel and ignores late replies without retry', async () => {
  const h = harness();
  const pending = sendDownloadRequest(h.chrome, request(), h.options);
  h.expire();
  await assert.rejects(pending, { code: 'bridge_timeout' });
  h.port.onMessage.emit(accepted());
  h.port.onDisconnect.emit();
  assert.equal(h.calls.length, 1);
  assert.equal(h.port.disconnected, 1);
  assert.equal(h.timers.size + h.port.onMessage.size + h.port.onDisconnect.size, 0);
});

test('rejects mismatched, oversized, unknown and malformed responses', async () => {
  const badReplies = [null, {}, { ...accepted(), requestId: 'ffffffff-ffff-4fff-8fff-ffffffffffff' },
    { ...accepted(), schemaVersion: 2 }, { ...accepted(), status: 'completed' },
    { ...accepted(), jobIds: [] }, { ...accepted(), jobIds: ['one', 'two'] },
    { ...accepted(), jobIds: ['../../file'] }, { ...accepted(), command: 'execute' },
    { ...accepted(), jobIds: ['s'.repeat(1024 * 1024)] }];
  for (const response of badReplies) {
    const h = harness();
    const pending = sendDownloadRequest(h.chrome, request(), h.options);
    h.port.onMessage.emit(response);
    await assert.rejects(pending, { code: 'invalid_response' });
    assert.equal(h.calls.length, 1);
  }
});

test('typed native rejection does not claim acceptance and ignores free text', async () => {
  const h = harness();
  const pending = sendDownloadRequest(h.chrome, request(), h.options);
  h.port.onMessage.emit({ schemaVersion: 1, type: 'download.response', requestId, status: 'rejected', code: 'unsupported_source' });
  await assert.rejects(pending, { code: 'rejected', message: 'rejected' });
});

test('send failure closes channel, cleans callbacks and does not resend', async () => {
  const h = harness();
  h.port.postMessage = () => { throw new Error('raw sensitive text'); };
  await assert.rejects(sendDownloadRequest(h.chrome, request(), h.options), { code: 'bridge_unavailable' });
  assert.equal(h.hosts.length, 1);
  assert.equal(h.port.disconnected, 1);
  assert.equal(h.timers.size + h.port.onMessage.size + h.port.onDisconnect.size, 0);
});
