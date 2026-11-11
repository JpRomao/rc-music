import test from 'node:test';
import assert from 'node:assert/strict';
import { createYoutubePopupController } from '../../extensions/chromium/youtube/popup-controller.mjs';
import { mountYoutubePopup } from '../../extensions/chromium/youtube/popup.mjs';

const requestId = '01234567-89ab-4cde-8fab-0123456789ab';
function event() { const listeners = new Set(); return { addListener: f => listeners.add(f), removeListener: f => listeners.delete(f), emit: (...args) => [...listeners].forEach(f => f(...args)), get size() { return listeners.size; } }; }
function harness({ autoAccept = false, onState = () => {} } = {}) {
  const tab = { id: 7, windowId: 3, active: true, status: 'complete', url: 'https://www.youtube.com/watch?v=aB_0123-xYz', title: '<b>曲名</b> - YouTube' };
  const window = { id: 3, focused: true }, sends = [], states = [];
  const chrome = {
    tabs: { query: async () => [{ ...tab }], get: async () => ({ ...tab }), onUpdated: event(), onRemoved: event(), onActivated: event() },
    windows: { getLastFocused: async () => ({ ...window }), onFocusChanged: event() },
  };
  const ack = { status: 'accepted', requestId, jobIds: ['job-1'] };
  let finish, uuidCalls = 0;
  const sendRequest = async (_, request) => {
    sends.push(structuredClone(request));
    return autoAccept ? ack : new Promise(resolve => { finish = resolve; });
  };
  const controller = createYoutubePopupController({ chrome, newRequestId: () => { uuidCalls++; return requestId; }, sendRequest,
    onState: state => { states.push(state); onState(state); } });
  return { tab, window, sends, states, chrome, controller, get uuidCalls() { return uuidCalls; }, accept: () => finish(ack) };
}
const flush = async () => { for (let i = 0; i < 8; i++) await Promise.resolve(); };
test('opening or refreshing reads only the current tab and never submits a request', async () => {
  const h = harness();
  assert.equal(h.sends.length, 0);
  await h.controller.refresh();
  const state = h.controller.getState();
  assert.equal(state.status, 'ready');
  assert.equal(state.candidate.videoId, 'aB_0123-xYz');
  assert.equal(state.title, '<b>曲名</b>');
  assert.equal(state.fetchLyrics, true);
  assert.equal(state.selected, false);
  assert.equal(state.canSend, false);
  assert.equal(h.sends.length, 0);
});
test('only explicit selection and submit send a detached typed request with editable metadata', async () => {
  const h = harness(); await h.controller.refresh();
  h.controller.setSelected(true);
  h.controller.setFields({ title: 'Corrected 曲名', artist: 'Artist', lyricsPage: 'https://www.letras.mus.br/artist/song/', fetchLyrics: true });
  assert.equal(h.controller.getState().canSend, true);
  const pending = h.controller.send(); await flush();
  assert.deepEqual(h.sends, [{ schemaVersion: 1, type: 'youtube.audio.request', requestId, items: [{ videoId: 'aB_0123-xYz', title: 'Corrected 曲名', artist: 'Artist', lyricsPage: 'https://www.letras.mus.br/artist/song/', fetchLyrics: true }] }]);
  assert.equal(h.controller.getState().canSend, false);
  h.accept(); assert.equal(await pending, true);
  assert.equal(h.controller.getState().status, 'accepted');
  assert.match(h.controller.getState().message, /Pedido aceito/u);
  assert.doesNotMatch(h.controller.getState().message, /download concluído/iu);
});
test('unchecked selection and invalid metadata cannot invoke a native bridge', async () => {
  const h = harness(); await h.controller.refresh();
  assert.equal(await h.controller.send(), false);
  h.controller.setSelected(true); h.controller.setFields({ lyricsPage: 'https://evil.test/a' });
  assert.equal(h.controller.getState().canSend, false);
  assert.equal(await h.controller.send(), false);
  assert.equal(h.sends.length, 0);
});
test('navigation revokes the selected video and refuses a stale submit', async () => {
  const h = harness(); await h.controller.refresh(); h.controller.setSelected(true);
  h.tab.url = 'https://www.youtube.com/watch?v=zzzzzzzzzzz';
  assert.equal(await h.controller.send(), false);
  assert.equal(h.sends.length, 0);
  assert.equal(h.controller.getState().selected, false);
  assert.equal(h.controller.getState().status, 'stale');
});
test('browser navigation and closing the tab invalidate selection without extra inspection', async () => {
  for (const action of ['update', 'remove', 'activate']) {
    const h = harness(); await h.controller.refresh(); h.controller.setSelected(true);
    if (action === 'update') h.chrome.tabs.onUpdated.emit(7, { url: 'https://example.com' });
    if (action === 'remove') h.chrome.tabs.onRemoved.emit(7);
    if (action === 'activate') h.chrome.tabs.onActivated.emit({ tabId: 8, windowId: 3 });
    assert.equal(h.controller.getState().status, 'stale');
    assert.equal(h.controller.getState().canSend, false);
    assert.equal(h.sends.length, 0);
    h.controller.dispose();
    assert.equal(h.chrome.tabs.onUpdated.size + h.chrome.tabs.onRemoved.size + h.chrome.tabs.onActivated.size, 0);
    assert.equal(h.chrome.windows.onFocusChanged.size, 0);
  }
});
test('a late admission after navigation cannot rearm a stale popup or resubmit', async () => {
  const h = harness(); await h.controller.refresh(); h.controller.setSelected(true);
  const pending = h.controller.send(); await flush();
  h.chrome.tabs.onRemoved.emit(7); h.accept();
  assert.equal(await pending, false);
  assert.equal(h.controller.getState().status, 'stale');
  assert.equal(h.sends.length, 1);
});
test('fresh send requires the original active tab, focused window and stable committed URL', async t => {
  for (const [label, change] of [
    ['another active tab', h => { h.chrome.tabs.query = async () => [{ ...h.tab, id: 8 }]; }],
    ['inactive result', h => { h.tab.active = false; }],
    ['another focused window', h => { h.window.id = 4; }],
    ['no focused browser window', h => { h.window.focused = false; }],
    ['same video in a changed page URL', h => { h.tab.url += '&t=12'; }],
    ['loading at the same URL', h => { h.tab.status = 'loading'; }],
    ['navigation not yet committed', h => { h.tab.pendingUrl = 'https://www.youtube.com/watch?v=zzzzzzzzzzz'; }],
  ]) {
    await t.test(label, async () => {
      const h = harness({ autoAccept: true });
      assert.equal(await h.controller.refresh(), true);
      h.controller.setSelected(true);
      change(h);
      assert.equal(await h.controller.send(), false);
      assert.equal(h.uuidCalls, 0);
      assert.equal(h.sends.length, 0);
      assert.equal(h.controller.getState().status, 'stale');
      assert.equal(h.controller.getState().canSend, false);
    });
  }
});
test('a tab or window switch during refresh cannot restore the old candidate', async t => {
  for (const action of ['activation', 'navigation', 'window focus']) {
    await t.test(action, async () => {
      const h = harness();
      const oldTab = { ...h.tab };
      let resolveQuery;
      h.chrome.tabs.query = () => new Promise(resolve => { resolveQuery = resolve; });
      const pending = h.controller.refresh();
      await flush();
      if (action === 'activation') h.chrome.tabs.onActivated.emit({ tabId: 8, windowId: 3 });
      if (action === 'navigation') h.chrome.tabs.onUpdated.emit(7, { status: 'loading' }, h.tab);
      if (action === 'window focus') h.chrome.windows.onFocusChanged.emit(4);
      resolveQuery([oldTab]);
      assert.equal(await pending, false);
      assert.equal(h.controller.getState().status, 'stale');
      assert.equal(h.controller.getState().candidate, null);
      assert.equal(h.sends.length, 0);
    });
  }
});
test('refresh cannot retarget from the popup window to another focused window', async () => {
  const h = harness({ autoAccept: true });
  h.window.id = 4;
  h.chrome.tabs.query = async ({ currentWindow }) => [currentWindow ? { ...h.tab } : { ...h.tab, id: 8, windowId: 4 }];
  assert.equal(await h.controller.refresh(), false);
  assert.equal(h.controller.getState().candidate, null);
  assert.equal(h.sends.length, 0);
});
test('background-window activation keeps consent but loss of window focus revokes it', async () => {
  const h = harness();
  assert.equal(await h.controller.refresh(), true);
  h.controller.setSelected(true);
  h.chrome.tabs.onActivated.emit({ tabId: 8, windowId: 4 });
  assert.equal(h.controller.getState().canSend, true);
  h.chrome.windows.onFocusChanged.emit(-1);
  assert.equal(h.controller.getState().status, 'stale');
  assert.equal(await h.controller.send(), false);
  assert.equal(h.uuidCalls, 0);
});
test('navigation announced synchronously before native handoff does not post', async () => {
  const h = harness({ autoAccept: true, onState: state => {
    if (state.status === 'sending') h.chrome.tabs.onUpdated.emit(7, { status: 'loading' }, h.tab);
  } });
  assert.equal(await h.controller.refresh(), true);
  h.controller.setSelected(true);
  assert.equal(await h.controller.send(), false);
  assert.equal(h.sends.length, 0);
  assert.equal(h.controller.getState().status, 'stale');
  assert.doesNotMatch(h.controller.getState().message, /Confira a fila/u);
});
test('missing component and protocol errors remain actionable without pretending download success', async () => {
  const h = harness();
  const controller = createYoutubePopupController({ chrome: h.chrome, newRequestId: () => requestId,
    sendRequest: async () => { throw Object.assign(new Error('private details'), { code: 'bridge_unavailable' }); } });
  await controller.refresh(); controller.setSelected(true);
  assert.equal(await controller.send(), false);
  assert.equal(controller.getState().status, 'error');
  assert.match(controller.getState().message, /componente YouTube no player/u);
  assert.doesNotMatch(controller.getState().message, /private details/u);
});
test('unsupported pages and empty active tabs never become downloadable', async () => {
  const h = harness(); h.tab.url = 'https://example.com/video';
  await h.controller.refresh();
  assert.equal(h.controller.getState().status, 'error');
  assert.equal(h.controller.getState().candidate, null);
  assert.equal(h.controller.getState().canSend, false);
});
test('mounted popup renders tab text literally and sends only after a real selected click', async () => {
  const h = harness();
  const elements = new Map();
  for (const id of ['refresh', 'status', 'selection', 'selected', 'video-title', 'title', 'artist', 'lyrics-page', 'fetch-lyrics', 'send']) {
    const listeners = new Map();
    elements.set(id, { textContent: '', value: '', checked: false, hidden: true, disabled: false,
      addEventListener: (name, callback) => listeners.set(name, callback), removeEventListener: name => listeners.delete(name),
      emit: name => listeners.get(name)?.({ preventDefault() {} }), get listenerCount() { return listeners.size; } });
  }
  const document = { getElementById: id => elements.get(id) };
  const view = mountYoutubePopup({ document, chrome: h.chrome, newRequestId: () => requestId,
    sendRequest: async (_, request) => { h.sends.push(request); return { status: 'accepted', requestId, jobIds: ['job-1'] }; } });
  await flush();
  assert.equal(elements.get('video-title').textContent, '<b>曲名</b>');
  assert.equal(elements.get('selection').hidden, false);
  assert.equal(elements.get('send').disabled, true);
  assert.equal(h.sends.length, 0);
  elements.get('selected').checked = true; elements.get('selected').emit('change');
  elements.get('title').value = '<img src=x> 曲名'; elements.get('title').emit('input');
  elements.get('artist').value = 'Artist'; elements.get('artist').emit('input');
  assert.equal(elements.get('send').disabled, false);
  elements.get('send').emit('click'); await flush();
  assert.equal(h.sends.length, 1);
  assert.equal(h.sends[0].items[0].title, '<img src=x> 曲名');
  assert.match(elements.get('status').textContent, /Pedido aceito/u);
  view.dispose();
  assert.equal([...elements.values()].reduce((count, node) => count + node.listenerCount, 0), 0);
});
