import test from 'node:test';
import assert from 'node:assert/strict';
import { createPopupController } from '../../extensions/chromium/main/popup-controller.mjs';
import { mountPopup } from '../../extensions/chromium/main/popup.mjs';
import { normalizeSnapshot, buildDownloadRequest } from '../../extensions/chromium/main/policy.mjs';
import * as policy from '../../extensions/chromium/main/policy.mjs';
import { sendDownloadRequest } from '../../extensions/chromium/main/native-client.mjs';

const pageUrl = 'https://example.com/listen?private=page#track';
const media = (index) => ({
  url: `https://media.example.com/${index}.mp3?private=media`,
  title: `Faixa ${index}`,
  kind: 'audio',
  sourceId: 'c1:d1',
  origin: 'https://media.example.com',
});
const snapshot = (count = 2) => ({
  type: 'snapshot', pageUrl, candidates: Array.from({ length: count }, (_, i) => {
    const { url, title, kind, sourceId } = media(i);
    return { url, title, kind, sourceId };
  }), truncated: false,
});
const deferred = () => {
  let resolve;
  let reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
};
const flush = async () => { for (let i = 0; i < 20; i += 1) await Promise.resolve(); };

function event() {
  const listeners = new Set();
  return {
    addListener(listener) { listeners.add(listener); },
    removeListener(listener) { listeners.delete(listener); },
    emit(...args) { for (const listener of [...listeners]) listener(...args); },
    get size() { return listeners.size; },
  };
}

function harness(overrides = {}) {
  const ports = [];
  const injections = [];
  const sends = [];
  const states = [];
  const timers = new Map();
  let nextTimer = 0;
  let freshSnapshot = snapshot();
  const currentTab = { id: 7, windowId: 3, url: pageUrl, active: true };
  const chrome = {
    tabs: {
      query: async () => [{ ...currentTab }],
      get: async () => ({ ...currentTab }),
      onUpdated: event(), onRemoved: event(), onActivated: event(),
      connect(tabId, options) {
        const onMessage = event();
        const emit = onMessage.emit;
        onMessage.emit = (raw) => { freshSnapshot = structuredClone(raw); emit(raw); };
        const port = { tabId, options, onMessage, onDisconnect: event(), closed: 0,
          disconnect() { this.closed += 1; this.onDisconnect.emit(); } };
        ports.push(port);
        return port;
      },
    },
    scripting: {
      async executeScript(options) {
        injections.push(options);
        const result = options.func?.toString().includes('__rcMusicDetectionV2')
          ? structuredClone(freshSnapshot) : options.func ? currentTab.url : undefined;
        return [{ frameId: 0, documentId: 'document-1', result }];
      },
    },
  };
  const options = {
    chrome,
    normalizeSnapshot(raw) {
      if (raw.type !== 'snapshot' || !Array.isArray(raw.candidates)) throw Object.assign(new Error('secret'), { code: 'invalid_snapshot' });
      return structuredClone({ pageUrl: raw.pageUrl,
        candidates: raw.candidates.map((item) => ({ ...item, origin: new URL(item.url).origin })), truncated: raw.truncated });
    },
    buildDownloadRequest(data, selected, requestId) {
      return { schemaVersion: 1, type: 'download.request', requestId,
        items: data.candidates.filter((item) => selected.includes(item.url)).map(({ url, title, kind }) => ({ url, title, kind })) };
    },
    isSelectionCurrent: (...args) => policy.isSelectionCurrent(...args),
    async sendDownloadRequest(_chrome, request) {
      sends.push(structuredClone(request));
      return { status: 'accepted', requestId: request.requestId, jobIds: request.items.map((_, i) => `job-${i}`) };
    },
    newRequestId: () => '11111111-1111-4111-8111-111111111111',
    onState: (state) => states.push(state),
    setTimer(callback) { const id = ++nextTimer; timers.set(id, callback); return id; },
    clearTimer(id) { timers.delete(id); },
    ...overrides,
  };
  const controller = createPopupController(options);
  return {
    controller, chrome, ports, injections, sends, states, timers, currentTab,
    get freshSnapshot() { return freshSnapshot; },
    set freshSnapshot(value) { freshSnapshot = structuredClone(value); },
    async ready(count = 2) {
      await controller.refresh();
      ports.at(-1).onMessage.emit(snapshot(count));
      await flush();
    },
    expire() { for (const [id, callback] of [...timers]) { timers.delete(id); callback(); } },
  };
}

test('detection opens only the top isolated document and never sends implicitly', async () => {
  const h = harness();
  await h.ready();
  assert.deepEqual(h.injections[0], { target: { tabId: 7, frameIds: [0] }, world: 'ISOLATED', files: ['detector.js'] });
  assert.deepEqual(h.ports[0].options, { documentId: 'document-1', name: 'rc-music-detection-v2' });
  assert.equal(h.controller.getState().status, 'ready');
  assert.equal(h.controller.getState().pageOrigin, 'https://example.com');
  assert.deepEqual(h.controller.getState().selectedUrls, []);
  h.controller.setSelected(media(0).url, true);
  assert.equal(h.sends.length, 0);
  await h.controller.submit();
  assert.equal(h.sends.length, 0, 'selection without authorization cannot send');
  h.controller.dispose();
});

test('explicit authorized submit sends exactly the selection and reports accepted, not completed', async () => {
  const h = harness();
  await h.ready(3);
  h.controller.setSelected(media(2).url, true);
  h.controller.setSelected(media(0).url, true);
  h.controller.setAuthorized(true);
  await h.controller.submit();
  assert.deepEqual(h.sends[0].items, [media(0), media(2)].map(({ url, title, kind }) => ({ url, title, kind })));
  assert.equal(h.sends.length, 1);
  const state = h.controller.getState();
  assert.equal(state.status, 'accepted');
  assert.equal(state.acceptedCount, 2);
  assert.match(state.message, /aceit|app/i);
  assert.doesNotMatch(state.message, /conclu[ií]d/i);
  assert.equal(state.authorized, false);
  assert.deepEqual(state.selectedUrls, []);
  await h.controller.submit();
  assert.equal(h.sends.length, 1);
  h.controller.dispose();
});

test('a changed selected source revokes consent even when its URL and label remain the same', async () => {
  const h = harness();
  await h.ready();
  h.controller.setSelected(media(0).url, true);
  h.controller.setAuthorized(true);
  const next = snapshot();
  next.candidates[0].sourceId = 'c1:d2';
  h.ports[0].onMessage.emit(next);
  await flush();
  assert.equal(h.controller.getState().authorized, false);
  assert.equal(h.controller.getState().canSend, false);
  await h.controller.submit();
  assert.equal(h.sends.length, 0);
  h.controller.dispose();
});

test('submit scans selected sources again before native transmission without relying on stream updates', async () => {
  for (const change of [
    (raw) => { raw.candidates[0].sourceId = 'c1:d9'; },
    (raw) => { raw.candidates[0].sourceId = 'c2:d1'; },
    (raw) => { raw.candidates[0].title = 'Replaced title'; },
    (raw) => { raw.candidates[0].kind = 'video'; },
    (raw) => { raw.candidates.shift(); },
  ]) {
    const h = harness();
    await h.ready();
    h.controller.setSelected(media(0).url, true);
    h.controller.setAuthorized(true);
    change(h.freshSnapshot);
    await h.controller.submit();
    assert.equal(h.sends.length, 0);
    assert.equal(h.controller.getState().status, 'error');
    assert.equal(h.controller.getState().authorized, false);
    assert.match(h.controller.getState().message, /fonte|sele[cç][aã]o|Atualizar/i);
    h.controller.dispose();
  }
});

test('fresh inspection ignores changes to unselected candidates and is bound to the exact top document', async () => {
  const h = harness();
  await h.ready();
  h.controller.setSelected(media(0).url, true);
  h.controller.setAuthorized(true);
  h.freshSnapshot.candidates[1].sourceId = 'c1:d9';
  await h.controller.submit();
  assert.equal(h.sends.length, 1);
  const fresh = h.injections.filter((injection) => injection.func?.toString().includes('__rcMusicDetectionV2'));
  assert.equal(fresh.length, 1);
  assert.deepEqual(fresh[0].target, { tabId: 7, documentIds: ['document-1'] });
  assert.equal(fresh[0].world, 'ISOLATED');
  assert.deepEqual(Object.keys(h.sends[0].items[0]).sort(), ['kind', 'title', 'url']);
  h.controller.dispose();
});

test('fresh probe cannot substitute another frame, document, page or an inactive detector', async () => {
  for (const result of [
    [{ frameId: 1, documentId: 'document-1', result: snapshot() }],
    [{ frameId: 0, documentId: 'document-2', result: snapshot() }],
    [{ frameId: 0, documentId: 'document-1', result: { ...snapshot(), pageUrl: `${pageUrl}changed` } }],
    [{ frameId: 0, documentId: 'document-1', result: null }],
    [],
  ]) {
    const h = harness();
    await h.ready();
    const execute = h.chrome.scripting.executeScript;
    h.chrome.scripting.executeScript = (options) => options.func?.toString().includes('__rcMusicDetectionV2')
      ? Promise.resolve(result) : execute(options);
    h.controller.setSelected(media(0).url, true);
    h.controller.setAuthorized(true);
    await h.controller.submit();
    assert.equal(h.sends.length, 0);
    assert.equal(h.controller.getState().authorized, false);
    assert.ok(['error', 'disconnected'].includes(h.controller.getState().status));
    h.controller.dispose();
  }
});

test('a navigation after the fresh scan is rechecked before handing off a native request', async () => {
  const h = harness();
  await h.ready();
  const execute = h.chrome.scripting.executeScript;
  h.chrome.scripting.executeScript = async (options) => {
    const result = await execute(options);
    if (options.func?.toString().includes('__rcMusicDetectionV2')) h.currentTab.url = 'https://example.com/changed';
    return result;
  };
  h.controller.setSelected(media(0).url, true);
  h.controller.setAuthorized(true);
  await h.controller.submit();
  assert.equal(h.sends.length, 0);
  assert.equal(h.controller.getState().status, 'disconnected');
  h.controller.dispose();
});

test('duplicate selected URLs in the fresh result are rejected before normalization can hide them', async () => {
  const h = harness({ normalizeSnapshot, buildDownloadRequest });
  await h.ready();
  h.controller.setSelected(media(0).url, true);
  h.controller.setAuthorized(true);
  h.freshSnapshot.candidates.push({ ...h.freshSnapshot.candidates[0] });
  await h.controller.submit();
  assert.equal(h.sends.length, 0);
  assert.equal(h.controller.getState().status, 'error');
  h.controller.dispose();
});

test('a selected-source update observed during the final page check cancels the unsent captured batch', async () => {
  for (const selectedChanges of [true, false]) {
    const h = harness();
    await h.ready();
    const pending = deferred();
    let checks = 0;
    h.chrome.tabs.get = async () => {
      checks += 1;
      return checks === 2 ? pending.promise : { ...h.currentTab };
    };
    h.controller.setSelected(media(0).url, true);
    h.controller.setAuthorized(true);
    const sending = h.controller.submit();
    await flush();
    assert.equal(checks, 2);
    assert.equal(h.sends.length, 0);
    const next = snapshot();
    next.candidates[selectedChanges ? 0 : 1].sourceId = 'c1:d9';
    h.ports[0].onMessage.emit(next);
    pending.resolve({ ...h.currentTab });
    await sending;
    assert.equal(h.sends.length, selectedChanges ? 0 : 1);
    assert.equal(h.controller.getState().status, selectedChanges ? 'error' : 'accepted');
    h.controller.dispose();
  }
});

test('timeout or disposal during a fresh scan makes its late result inert', async () => {
  for (const end of [(h) => h.expire(), (h) => h.controller.dispose()]) {
    const h = harness();
    await h.ready();
    const pending = deferred();
    const execute = h.chrome.scripting.executeScript;
    h.chrome.scripting.executeScript = (options) => options.func?.toString().includes('__rcMusicDetectionV2')
      ? pending.promise : execute(options);
    h.controller.setSelected(media(0).url, true);
    h.controller.setAuthorized(true);
    const sending = h.controller.submit();
    await flush();
    assert.equal(h.sends.length, 0);
    end(h);
    const count = h.states.length;
    pending.resolve([{ frameId: 0, documentId: 'document-1', result: snapshot() }]);
    await sending;
    assert.equal(h.states.length, count);
    assert.equal(h.sends.length, 0);
    assert.equal(h.timers.size, 0);
    h.controller.dispose();
  }
});

test('selection is limited to twenty known unique candidates', async () => {
  const h = harness();
  await h.ready(21);
  for (let i = 0; i < 21; i += 1) h.controller.setSelected(media(i).url, true);
  h.controller.setSelected(media(0).url, true);
  h.controller.setSelected('https://attacker.example.com/not-in-list.mp3', true);
  assert.equal(h.controller.getState().selectedUrls.length, 20);
  assert.match(h.controller.getState().message, /20/);
  h.controller.setSelected(media(0).url, false);
  h.controller.setSelected(media(20).url, true);
  assert.equal(h.controller.getState().selectedUrls.length, 20);
  assert.equal(h.controller.getState().selectedUrls.includes(media(20).url), true);
  h.controller.dispose();
});

test('new candidates are never implicitly selected and removed candidates revoke authorization', async () => {
  const h = harness();
  await h.ready();
  h.controller.setSelected(media(0).url, true);
  h.controller.setAuthorized(true);
  h.ports[0].onMessage.emit(snapshot(3));
  await flush();
  assert.deepEqual(h.controller.getState().selectedUrls, [media(0).url]);
  h.ports[0].onMessage.emit({ ...snapshot(), candidates: [media(1)] });
  await flush();
  assert.deepEqual(h.controller.getState().selectedUrls, []);
  assert.equal(h.controller.getState().authorized, false);
  assert.equal(h.sends.length, 0);
  h.controller.dispose();
});

test('empty and truncated detection have explicit non-success states', async () => {
  const h = harness();
  await h.ready(0);
  assert.equal(h.controller.getState().status, 'empty');
  assert.match(h.controller.getState().message, /nenhum|nenhuma/i);
  h.ports[0].onMessage.emit({ ...snapshot(), truncated: true });
  await flush();
  assert.equal(h.controller.getState().truncated, true);
  h.controller.dispose();
});

test('untrusted snapshots are normalized before presentation and errors are redacted', async () => {
  const h = harness({ normalizeSnapshot(raw) {
    if (!raw.candidates.length) return { pageUrl: raw.pageUrl, candidates: [], truncated: false };
    throw Object.assign(new Error('https://secret.example/?token=secret'), { code: 'invalid_snapshot' });
  } });
  await h.ready();
  assert.equal(h.controller.getState().status, 'error');
  assert.equal(h.controller.getState().candidates.length, 0);
  assert.doesNotMatch(h.controller.getState().message, /secret|token/);
  assert.equal(h.sends.length, 0);
  h.controller.dispose();
});

test('unsupported page is rejected before detector injection', async () => {
  const h = harness({ normalizeSnapshot() { throw Object.assign(new Error('secret'), { code: 'unsupported_page' }); } });
  await h.controller.refresh();
  assert.equal(h.controller.getState().status, 'error');
  assert.match(h.controller.getState().message, /compatível/);
  assert.equal(h.injections.length, 0);
  assert.equal(h.ports.length, 0);
  h.controller.dispose();
});

test('changing selected candidate metadata requires renewed authorization', async () => {
  const h = harness();
  await h.ready();
  h.controller.setSelected(media(0).url, true);
  h.controller.setAuthorized(true);
  const next = snapshot();
  next.candidates[0].title = 'Título novo';
  h.ports[0].onMessage.emit(next);
  await flush();
  assert.equal(h.controller.getState().authorized, false);
  assert.deepEqual(h.controller.getState().selectedUrls, [media(0).url]);
  h.controller.dispose();
});

test('navigation on the same origin invalidates selection and disconnects detection', async () => {
  const h = harness();
  await h.ready();
  h.controller.setSelected(media(0).url, true);
  h.controller.setAuthorized(true);
  h.chrome.tabs.onUpdated.emit(7, { url: 'https://example.com/other' }, h.currentTab);
  assert.equal(h.controller.getState().status, 'disconnected');
  assert.deepEqual(h.controller.getState().selectedUrls, []);
  assert.equal(h.ports[0].closed, 1);
  await h.controller.submit();
  assert.equal(h.sends.length, 0);
  h.controller.dispose();
});

test('tab closure, activation elsewhere, detector disconnect and loading invalidate the session', async () => {
  for (const action of [
    (h) => h.chrome.tabs.onRemoved.emit(7),
    (h) => h.chrome.tabs.onActivated.emit({ tabId: 8, windowId: 3 }),
    (h) => h.ports[0].onDisconnect.emit(),
    (h) => h.chrome.tabs.onUpdated.emit(7, { status: 'loading' }, h.currentTab),
  ]) {
    const h = harness();
    await h.ready();
    action(h);
    assert.equal(h.controller.getState().status, 'disconnected');
    assert.equal(h.controller.getState().candidates.length, 0);
    h.controller.dispose();
  }
});

test('snapshot URL drift invalidates before rendering media from the new document', async () => {
  const h = harness();
  await h.ready();
  h.ports[0].onMessage.emit({ ...snapshot(), pageUrl: 'https://example.com/listen#new' });
  await flush();
  assert.equal(h.controller.getState().status, 'disconnected');
  assert.equal(h.sends.length, 0);
  h.controller.dispose();
});

test('send revalidates current tab URL even when no navigation notification arrived', async () => {
  const h = harness();
  await h.ready();
  h.controller.setSelected(media(0).url, true);
  h.controller.setAuthorized(true);
  h.currentTab.url = 'https://example.com/changed';
  await h.controller.submit();
  assert.equal(h.sends.length, 0);
  assert.equal(h.controller.getState().status, 'disconnected');
  h.controller.dispose();
});

test('send revalidates exact document identity and its live URL', async () => {
  for (const result of [
    [{ frameId: 0, documentId: 'document-2', result: pageUrl }],
    [{ frameId: 0, documentId: 'document-1', result: 'https://example.com/changed' }],
    [],
  ]) {
    const h = harness();
    await h.ready();
    h.chrome.scripting.executeScript = async () => result;
    h.controller.setSelected(media(0).url, true);
    h.controller.setAuthorized(true);
    await h.controller.submit();
    assert.equal(h.sends.length, 0);
    assert.equal(h.controller.getState().status, 'disconnected');
    h.controller.dispose();
  }
});

test('pending submit freezes captured batch, authorization and double clicks', async () => {
  const pending = deferred();
  const sent = [];
  const h = harness({ sendDownloadRequest(_chrome, request) { sent.push(request); return pending.promise; } });
  await h.ready();
  h.controller.setSelected(media(0).url, true);
  h.controller.setAuthorized(true);
  const submit = h.controller.submit();
  await flush();
  assert.equal(h.controller.getState().status, 'sending');
  h.controller.setSelected(media(1).url, true);
  h.controller.setAuthorized(false);
  h.ports[0].onMessage.emit(snapshot(3));
  await h.controller.submit();
  await h.controller.refresh();
  assert.equal(sent.length, 1);
  assert.equal(h.ports.length, 1);
  assert.deepEqual(sent[0].items, [{ url: media(0).url, title: media(0).title, kind: 'audio' }]);
  pending.resolve({ status: 'accepted', requestId: sent[0].requestId, jobIds: ['job-0'] });
  await submit;
  h.controller.dispose();
});

test('navigation during pre-send verification makes its late success inert', async () => {
  const h = harness();
  await h.ready();
  const pending = deferred();
  h.chrome.tabs.get = () => pending.promise;
  h.controller.setSelected(media(0).url, true);
  h.controller.setAuthorized(true);
  const submit = h.controller.submit();
  h.chrome.tabs.onUpdated.emit(7, { status: 'loading' }, h.currentTab);
  pending.resolve({ ...h.currentTab });
  await submit;
  assert.equal(h.sends.length, 0);
  assert.equal(h.controller.getState().status, 'disconnected');
  h.controller.dispose();
});

test('a pending snapshot verification blocks stale submit and re-enables only the reconciled selection', async () => {
  const h = harness();
  await h.ready();
  h.controller.setSelected(media(0).url, true);
  h.controller.setAuthorized(true);
  const pending = deferred();
  h.chrome.tabs.get = () => pending.promise;
  h.ports[0].onMessage.emit(snapshot(3));
  assert.equal(h.controller.getState().canSend, false);
  assert.equal(h.states.at(-1).canSend, false);
  await h.controller.submit();
  assert.equal(h.sends.length, 0);
  pending.resolve({ ...h.currentTab });
  await flush();
  assert.equal(h.controller.getState().canSend, true);
  assert.equal(h.states.at(-1).canSend, true);
  const removed = deferred();
  h.chrome.tabs.get = () => removed.promise;
  h.ports[0].onMessage.emit({ ...snapshot(), candidates: [media(1)] });
  await h.controller.submit();
  assert.equal(h.sends.length, 0);
  removed.resolve({ ...h.currentTab });
  await flush();
  assert.equal(h.controller.getState().canSend, false);
  assert.equal(h.controller.getState().authorized, false);
  h.controller.dispose();
});

test('detector disconnect consumes Chromium lastError without exposing its text', async () => {
  const h = harness();
  let reads = 0;
  h.chrome.runtime = { get lastError() { reads += 1; return { message: 'secret' }; } };
  await h.ready();
  h.ports[0].onDisconnect.emit();
  assert.equal(reads, 1);
  assert.doesNotMatch(h.controller.getState().message, /secret/);
  h.controller.dispose();
});

test('bridge errors are safe, clear the batch and never retry automatically', async () => {
  for (const code of ['bridge_unavailable', 'bridge_timeout', 'invalid_response', 'rejected']) {
    let calls = 0;
    const h = harness({ async sendDownloadRequest() { calls += 1; throw Object.assign(new Error('C:/private/secret https://example.com/?token=hidden'), { code }); } });
    await h.ready();
    h.controller.setSelected(media(0).url, true);
    h.controller.setAuthorized(true);
    await h.controller.submit();
    assert.ok(['error', 'disconnected'].includes(h.controller.getState().status));
    assert.doesNotMatch(h.controller.getState().message, /secret|hidden|token|C:\//);
    if (code === 'bridge_unavailable') assert.match(h.controller.getState().message, /fila no app/i);
    assert.deepEqual(h.controller.getState().selectedUrls, []);
    await h.controller.submit();
    assert.equal(calls, 1);
    h.controller.dispose();
  }
});

test('initial inspection timeout is bounded and late completion cannot reconnect', async () => {
  const h = harness();
  const pending = deferred();
  h.chrome.tabs.query = () => pending.promise;
  const refresh = h.controller.refresh();
  h.expire();
  pending.resolve([{ ...h.currentTab }]);
  await refresh;
  assert.equal(h.controller.getState().status, 'error');
  assert.equal(h.ports.length, 0);
  h.controller.dispose();
});

test('missing detector snapshot times out and a new refresh can recover', async () => {
  const h = harness();
  await h.controller.refresh();
  h.expire();
  assert.equal(h.controller.getState().status, 'error');
  assert.equal(h.ports[0].closed, 1);
  await h.ready();
  assert.equal(h.controller.getState().status, 'ready');
  h.controller.dispose();
});

test('controller bounds a pending send and ignores a success after timeout', async () => {
  const pending = deferred();
  let calls = 0;
  const h = harness({ sendDownloadRequest() { calls += 1; return pending.promise; } });
  await h.ready();
  h.controller.setSelected(media(0).url, true);
  h.controller.setAuthorized(true);
  const sending = h.controller.submit();
  await flush();
  h.expire();
  assert.equal(h.controller.getState().status, 'error');
  assert.match(h.controller.getState().message, /fila no app/);
  const count = h.states.length;
  pending.resolve({ status: 'accepted', requestId: '11111111-1111-4111-8111-111111111111', jobIds: ['job'] });
  await sending;
  assert.equal(h.states.length, count);
  assert.equal(calls, 1);
  h.controller.dispose();
});

test('production policy and native client compose with the controller over fake Chromium ports', async () => {
  const nativeRequests = [];
  const nativePort = { onMessage: event(), onDisconnect: event(), closed: 0,
    disconnect() { this.closed += 1; },
    postMessage(request) {
      nativeRequests.push(request);
      this.onMessage.emit({ schemaVersion: 1, type: 'download.response', requestId: request.requestId,
        status: 'accepted', jobIds: ['job-1'] });
    },
  };
  const h = harness({ normalizeSnapshot, buildDownloadRequest,
    sendDownloadRequest: (chrome, request) => sendDownloadRequest(chrome, request, { setTimer: () => 1, clearTimer: () => {} }),
  });
  h.chrome.runtime = { connectNative(name) { assert.equal(name, 'com.rcmusic.media'); return nativePort; } };
  await h.controller.refresh();
  const raw = snapshot(2);
  raw.candidates = raw.candidates.map(({ url, title, kind, sourceId }) => ({ url, title, kind, sourceId }));
  h.ports[0].onMessage.emit(raw);
  await flush();
  assert.equal(h.controller.getState().status, 'ready');
  h.controller.setSelected(media(1).url, true);
  h.controller.setAuthorized(true);
  await h.controller.submit();
  assert.equal(h.controller.getState().status, 'accepted');
  assert.deepEqual(nativeRequests[0].items, [{ url: media(1).url, title: media(1).title, kind: 'audio' }]);
  assert.equal(Object.hasOwn(nativeRequests[0], 'pageUrl'), false);
  assert.equal(nativePort.closed, 1);
  assert.equal(nativePort.onMessage.size, 0);
  h.controller.dispose();
});

test('dispose removes listeners/timers and makes late native responses inert', async () => {
  const pending = deferred();
  const h = harness({ sendDownloadRequest: () => pending.promise });
  await h.ready();
  h.controller.setSelected(media(0).url, true);
  h.controller.setAuthorized(true);
  const submit = h.controller.submit();
  await flush();
  h.controller.dispose();
  const count = h.states.length;
  pending.resolve({ status: 'accepted', requestId: '11111111-1111-4111-8111-111111111111', jobIds: ['job'] });
  await submit;
  assert.equal(h.states.length, count);
  assert.equal(h.chrome.tabs.onUpdated.size, 0);
  assert.equal(h.chrome.tabs.onRemoved.size, 0);
  assert.equal(h.chrome.tabs.onActivated.size, 0);
  assert.equal(h.ports[0].onMessage.size, 0);
  assert.equal(h.ports[0].onDisconnect.size, 0);
  assert.equal(h.timers.size, 0);
});

function fakeDocument() {
  class Element {
    constructor(tagName) {
      this.tagName = tagName.toUpperCase(); this.children = []; this.attributes = {}; this.listeners = new Map();
      this.textContent = ''; this.hidden = false; this.disabled = false; this.checked = false;
    }
    set innerHTML(_) { throw new Error('Remote HTML must never be interpreted'); }
    append(...children) { this.children.push(...children); }
    replaceChildren(...children) { this.children = [...children]; }
    setAttribute(key, value) { this.attributes[key] = String(value); }
    addEventListener(name, listener) { if (!this.listeners.has(name)) this.listeners.set(name, new Set()); this.listeners.get(name).add(listener); }
    removeEventListener(name, listener) { this.listeners.get(name)?.delete(listener); }
    emit(name) { for (const listener of [...(this.listeners.get(name) ?? [])]) listener({ target: this }); }
  }
  const ids = Object.fromEntries(['status', 'page-origin', 'media-list', 'selection-count', 'truncated', 'authorize', 'refresh', 'send'].map((id) => [id, new Element(id === 'authorize' ? 'input' : 'div')]));
  return { ids, getElementById: (id) => ids[id], createElement: (name) => new Element(name) };
}

const descendants = (node) => [node, ...node.children.flatMap(descendants)];

test('DOM renders hostile titles as text, hides query URLs and has named native checkboxes', () => {
  const document = fakeDocument();
  const item = { ...media(0), title: '<img src=x onerror=alert(1)>' };
  const state = { status: 'ready', candidates: [item], selectedUrls: [], authorized: false, canSend: false,
    pageOrigin: 'https://example.com', truncated: false, message: 'Selecione a mídia.' };
  const controller = { getState: () => state, setSelected() {}, setAuthorized() {}, submit() {}, refresh() {} };
  const view = mountPopup(document, controller);
  const nodes = descendants(document.ids['media-list']);
  assert.ok(nodes.some((node) => node.textContent === item.title));
  assert.equal(nodes.filter((node) => node.tagName === 'IMG').length, 0);
  assert.doesNotMatch(nodes.map((node) => `${node.textContent} ${JSON.stringify(node.attributes)}`).join(' '), /private=media/);
  const checkbox = nodes.find((node) => node.tagName === 'INPUT');
  assert.equal(checkbox.type, 'checkbox');
  assert.equal(checkbox.attributes['aria-label'], item.title);
  assert.ok(checkbox.attributes['aria-describedby']);
  assert.equal(document.ids.send.disabled, true);
  view.dispose();
});

test('DOM selection and consent only issue explicit controller actions, with stable focus nodes', () => {
  const document = fakeDocument();
  const actions = [];
  const state = { status: 'ready', candidates: [media(0)], selectedUrls: [], authorized: false, canSend: false,
    pageOrigin: 'https://example.com', truncated: false, message: '' };
  const controller = { getState: () => state,
    setSelected: (...args) => actions.push(['select', ...args]),
    setAuthorized: (...args) => actions.push(['authorize', ...args]),
    submit: () => actions.push(['send']), refresh: () => actions.push(['refresh']) };
  const view = mountPopup(document, controller);
  const checkbox = descendants(document.ids['media-list']).find((node) => node.tagName === 'INPUT');
  checkbox.checked = true;
  checkbox.emit('change');
  assert.deepEqual(actions, [['select', media(0).url, true]]);
  view.render({ ...state, selectedUrls: [media(0).url] });
  assert.equal(descendants(document.ids['media-list']).find((node) => node.tagName === 'INPUT'), checkbox);
  document.ids.authorize.checked = true;
  document.ids.authorize.emit('change');
  assert.deepEqual(actions.at(-1), ['authorize', true]);
  document.ids.send.emit('click');
  assert.deepEqual(actions.at(-1), ['send']);
  document.ids.refresh.emit('click');
  assert.deepEqual(actions.at(-1), ['refresh']);
  view.render({ ...state, status: 'sending', selectedUrls: [media(0).url], authorized: true });
  assert.equal(checkbox.disabled, true);
  assert.equal(document.ids.refresh.disabled, true);
  assert.equal(document.ids.authorize.disabled, true);
  const actionCount = actions.length;
  view.dispose();
  document.ids.send.emit('click');
  checkbox.emit('change');
  assert.equal(actions.length, actionCount);
});
