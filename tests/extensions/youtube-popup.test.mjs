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
    windows: { getCurrent: async () => ({ ...window }), getLastFocused: async () => ({ ...window }), onFocusChanged: event() },
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
const flush = () => new Promise(resolve => setImmediate(resolve));
function selectWithArtist(controller) {
  controller.setFields({ artist: 'Test Artist' });
  controller.setSelected(true);
}
test('a focused action popup uses its owning window even when the browser reports that window unfocused', async t => {
  for (const letras of [false, true]) await t.test(letras ? 'Letras' : 'YouTube', async () => {
    const h = letras ? letrasHarness() : harness(); h.controller.dispose();
    h.window.focused = false;
    h.chrome.windows.getLastFocused = async () => ({ id: 99, focused: false });
    const requests = [];
    const controller = createYoutubePopupController({ chrome: h.chrome, hasPopupFocus: () => true,
      newRequestId: () => requestId, async sendRequest(_chrome, request) {
        requests.push(structuredClone(request));
        return { status: 'accepted', requestId, jobIds: ['job-1'] };
      } });
    assert.equal(await controller.refresh(), true);
    assert.equal(controller.getState().candidate.tabId, 7);
    assert.equal(controller.getState().selected, false);
    assert.equal(requests.length, 0);
    selectWithArtist(controller);
    assert.equal(await controller.send(), true);
    assert.equal(requests.length, 1);
    controller.dispose();
  });
});
test('popup focus does not authorize a different window or survive blur during checking', async t => {
  for (const reason of ['different owner', 'blur at focus guard', 'blur during final source read']) await t.test(reason, async () => {
    const h = letrasHarness(); h.controller.dispose();
    let focused = true, calls = 0;
    const controller = createYoutubePopupController({ chrome: h.chrome, hasPopupFocus: () => focused,
      newRequestId: () => requestId, async sendRequest() { calls++; return { status: 'accepted', requestId, jobIds: ['job-1'] }; } });
    assert.equal(await controller.refresh(), true);
    selectWithArtist(controller);
    if (reason === 'different owner') h.chrome.windows.getCurrent = async () => ({ id: 9, focused: false });
    if (reason === 'blur at focus guard') focused = false;
    if (reason === 'blur during final source read') {
      const execute = h.chrome.scripting.executeScript;
      let reads = 0;
      h.chrome.scripting.executeScript = async options => {
        const result = await execute(options);
        if (++reads === 2) focused = false;
        return result;
      };
    }
    assert.equal(await controller.send(), false);
    assert.equal(calls, 0);
    assert.equal(controller.getState().status, 'stale');
    controller.dispose();
  });
});
function letrasHarness(options = {}) {
  const h = harness(options);
  h.tab.url = 'https://www.letras.mus.br/bleach/1222774/';
  h.tab.title = 'Asterisk - Bleach - LETRAS.MUS.BR';
  h.snapshot = { pageUrl: h.tab.url, videoId: 'wW9TwZdWpjw', title: 'Asterisk' };
  h.documentId = 'letras-document-1';
  h.injections = [];
  h.chrome.scripting = { executeScript: async options => {
    h.injections.push(options);
    return [{ frameId: 0, documentId: h.documentId, result: structuredClone(h.snapshot) }];
  } };
  return h;
}
test('Letras offers its declared video and requests manual confirmation when an artist is missing', async () => {
  const h = letrasHarness({ autoAccept: true });
  assert.equal(await h.controller.refresh(), true);
  const state = h.controller.getState();
  assert.equal(state.candidate.videoId, 'wW9TwZdWpjw');
  assert.equal(state.candidate.url, 'https://www.youtube.com/watch?v=wW9TwZdWpjw');
  assert.equal(state.title, 'Asterisk');
  assert.equal(state.lyricsPage, 'https://www.letras.mus.br/bleach/1222774/');
  assert.equal(state.artist, '');
  assert.equal(state.canSend, false);
  assert.equal(h.sends.length, 0);
  h.controller.setSelected(true);
  h.controller.setFields({ fetchLyrics: false });
  assert.equal(h.controller.getState().canSend, false);
  assert.equal(await h.controller.send(), false);
  h.controller.setFields({ artist: 'Orange Range' });
  assert.equal(await h.controller.send(), true);
  assert.deepEqual(h.sends, [{ schemaVersion: 1, type: 'youtube.audio.request', requestId,
    items: [{ videoId: 'wW9TwZdWpjw', videoUrl: 'https://www.youtube.com/watch?v=wW9TwZdWpjw', title: 'Asterisk', artist: 'Orange Range',
      lyricsPage: 'https://www.letras.mus.br/bleach/1222774/', fetchLyrics: false }] }]);
  assert.equal(h.injections.length, 3);
  assert.equal(h.injections[0].world, 'ISOLATED');
  assert.deepEqual(h.injections[0].target, { tabId: 7, frameIds: [0] });
  assert.deepEqual(h.injections[1].target, { tabId: 7, documentIds: ['letras-document-1'] });
  assert.deepEqual(h.injections[2].target, { tabId: 7, documentIds: ['letras-document-1'] });
});
test('Letras with no loaded video explains how to refresh without sending a request', async () => {
  const h = letrasHarness();
  h.tab.url = 'https://www.letras.mus.br/bleach/453185/';
  h.snapshot = null;
  assert.equal(await h.controller.refresh(), false);
  assert.match(h.controller.getState().message, /Play.*Atualizar vídeo/u);
  assert.equal(h.sends.length, 0);

  h.snapshot = { pageUrl: h.tab.url, videoId: 'DW6toOrQkkU', title: 'Number One', artist: 'Bleach' };
  assert.equal(await h.controller.refresh(), true);
  assert.equal(h.controller.getState().title, 'Number One');
  assert.equal(h.controller.getState().artist, 'Bleach');
  assert.equal(h.controller.getState().canSend, false);
  assert.equal(h.sends.length, 0);
});
test('Letras same-URL document or video replacement revokes selected consent before native handoff', async t => {
  for (const [name, change] of [
    ['document', h => { h.documentId = 'letras-document-2'; }],
    ['video', h => { h.snapshot.videoId = 'aB_0123-xYz'; }],
    ['page origin', h => { h.snapshot.pageUrl = 'https://other.test/bleach/1222774/'; }],
    ['missing source', h => { h.snapshot = null; }],
  ]) await t.test(name, async () => {
    const h = letrasHarness({ autoAccept: true });
    assert.equal(await h.controller.refresh(), true);
    selectWithArtist(h.controller); change(h);
    assert.equal(await h.controller.send(), false);
    assert.equal(h.sends.length, 0);
    assert.equal(h.uuidCalls, 0);
    assert.equal(h.controller.getState().status, 'stale');
  });
});
test('Letras rejects unavailable scripting, unidentified documents and subframe-only replies', async t => {
  for (const reply of [[], [{ frameId: 0, result: {} }], [{ frameId: 9, documentId: 'subframe', result: {} }]]) {
    await t.test(JSON.stringify(reply), async () => {
      const h = letrasHarness(); h.chrome.scripting.executeScript = async () => reply;
      assert.equal(await h.controller.refresh(), false);
      assert.equal(h.controller.getState().candidate, null);
      assert.equal(h.sends.length, 0);
    });
  }
  const h = letrasHarness();
  h.chrome.scripting.executeScript = async () => { throw new Error('Cannot access contents of the page'); };
  assert.equal(await h.controller.refresh(), false);
  assert.equal(h.controller.getState().candidate, null);
  assert.doesNotMatch(h.controller.getState().message, /Cannot access/u);
});
test('Letras late snapshot after focus or URL change cannot restore consent', async t => {
  for (const action of ['focus', 'silent navigation']) await t.test(action, async () => {
    const h = letrasHarness();
    const execute = h.chrome.scripting.executeScript;
    h.chrome.scripting.executeScript = async options => {
      const result = await execute(options);
      if (action === 'focus') h.chrome.windows.onFocusChanged.emit(4);
      else h.tab.url = 'https://www.letras.mus.br/other/song/';
      return result;
    };
    assert.equal(await h.controller.refresh(), false);
    assert.equal(h.controller.getState().candidate, null);
    assert.equal(h.sends.length, 0);
  });
});
test('Letras retry preserves the admitted identity and rejects a replaced embedded video', async () => {
  const h = letrasHarness(); h.controller.dispose();
  const requests = [];
  const controller = createYoutubePopupController({ chrome: h.chrome, newRequestId: () => requestId,
    async sendRequest(_chrome, request) {
      requests.push(structuredClone(request));
      throw Object.assign(new Error('Uncertain'), { code: 'bridge_timeout' });
    } });
  assert.equal(await controller.refresh(), true);
  selectWithArtist(controller);
  assert.equal(await controller.send(), false);
  assert.equal(controller.getState().canRetry, true);
  assert.equal(await controller.retry(), false);
  assert.deepEqual(requests[1], requests[0]);
  h.snapshot.videoId = 'aB_0123-xYz';
  assert.equal(await controller.retry(), false);
  assert.equal(requests.length, 2);
  assert.equal(controller.getState().status, 'stale');
  controller.dispose();
});
test('Letras source replacement during the final asynchronous guards prevents send and retry', async t => {
  for (const phase of ['tab guard', 'focus guard']) {
    for (const retry of [false, true]) await t.test(`${phase}, retry=${retry}`, async () => {
      const h = letrasHarness(); h.controller.dispose();
      const requests = [];
      const controller = createYoutubePopupController({ chrome: h.chrome, newRequestId: () => requestId,
        async sendRequest(_chrome, request) {
          requests.push(structuredClone(request));
          throw Object.assign(new Error('Uncertain'), { code: 'bridge_timeout' });
        } });
      assert.equal(await controller.refresh(), true);
      selectWithArtist(controller);
      if (retry) {
        assert.equal(await controller.send(), false);
        assert.equal(controller.getState().canRetry, true);
      }
      const before = requests.length;
      if (phase === 'tab guard') h.chrome.tabs.get = async () => {
        h.snapshot.videoId = 'aB_0123-xYz';
        return { ...h.tab };
      };
      else h.chrome.windows.getCurrent = async () => {
        h.snapshot.videoId = 'aB_0123-xYz';
        return { ...h.window };
      };
      assert.equal(await (retry ? controller.retry() : controller.send()), false);
      assert.equal(requests.length, before);
      assert.equal(controller.getState().status, 'stale');
      assert.equal(controller.getState().canRetry, false);
      controller.dispose();
    });
  }
});
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
test('YouTube music credits fill separate fields and the selected request sends the canonical URL', async () => {
  const h = harness({ autoAccept: true });
  h.tab.url += '&list=ignored&t=18';
  const reads = [];
  h.chrome.scripting = { executeScript: async options => {
    reads.push(options);
    return [{ frameId: 0, documentId: 'youtube-document',
      result: { pageUrl: h.tab.url, title: 'Asterisk', artist: 'Orange Range' } }];
  } };
  assert.equal(await h.controller.refresh(), true);
  const state = h.controller.getState();
  assert.equal(state.title, 'Asterisk');
  assert.equal(state.artist, 'Orange Range');
  assert.equal(state.fileName, '');
  assert.equal(reads.length, 1);
  assert.deepEqual(reads[0].target, { tabId: 7, frameIds: [0] });
  assert.equal(reads[0].world, 'ISOLATED');
  h.controller.setSelected(true);
  h.controller.setFields({ artist: 'Confirmed artist', fileName: 'Custom name' });
  assert.equal(await h.controller.send(), true);
  assert.deepEqual(h.sends[0].items[0], {
    videoId: 'aB_0123-xYz', videoUrl: 'https://www.youtube.com/watch?v=aB_0123-xYz',
    title: 'Asterisk', artist: 'Confirmed artist', lyricsPage: '', fetchLyrics: true, fileName: 'Custom name',
  });
  assert.equal(reads.length, 1);
});
test('a mismatched or unavailable YouTube metadata read leaves the fields editable', async t => {
  for (const cause of ['Different page', 'Subframe', 'Blocked']) await t.test(cause, async () => {
    const h = harness({ autoAccept: true });
    h.chrome.scripting = { executeScript: async () => {
      if (cause === 'Blocked') throw new Error('User denied permission');
      return [{ frameId: cause === 'Subframe' ? 1 : 0, documentId: 'youtube-document',
        result: { pageUrl: cause === 'Different page' ? 'https://youtube.com/watch?v=zzzzzzzzzzz' : h.tab.url,
          title: 'Wrong title', artist: 'Wrong artist' } }];
    } };
    assert.equal(await h.controller.refresh(), true);
    h.controller.setSelected(true);
    assert.equal(h.controller.getState().title, '<b>曲名</b>');
    assert.equal(h.controller.getState().artist, '');
    assert.equal(h.controller.getState().canSend, false);
    h.controller.setFields({ title: 'Confirmed title', artist: 'Confirmed artist' });
    assert.equal(await h.controller.send(), true);
  });
});
test('a YouTube metadata read cannot bind a silently replaced active page', async () => {
  const h = harness({ autoAccept: true });
  h.chrome.scripting = { executeScript: async () => {
    const pageUrl = h.tab.url;
    h.tab.url = 'https://www.youtube.com/watch?v=zzzzzzzzzzz';
    return [{ frameId: 0, result: { pageUrl, title: 'Old track', artist: 'Old artist' } }];
  } };
  assert.equal(await h.controller.refresh(), false);
  assert.equal(h.controller.getState().candidate, null);
  assert.equal(await h.controller.send(), false);
  assert.equal(h.sends.length, 0);
});
test('only explicit selection and submit send a detached typed request with editable metadata', async () => {
  const h = harness(); await h.controller.refresh();
  h.controller.setSelected(true);
  h.controller.setFields({ title: 'Corrected 曲名', artist: 'Artist', lyricsPage: 'https://www.letras.mus.br/artist/song/', fetchLyrics: true });
  assert.equal(h.controller.getState().canSend, true);
  const pending = h.controller.send(); await flush();
  assert.deepEqual(h.sends, [{ schemaVersion: 1, type: 'youtube.audio.request', requestId, items: [{ videoId: 'aB_0123-xYz',
    videoUrl: 'https://www.youtube.com/watch?v=aB_0123-xYz', title: 'Corrected 曲名', artist: 'Artist',
    lyricsPage: 'https://www.letras.mus.br/artist/song/', fetchLyrics: true }] }]);
  assert.equal(h.controller.getState().canSend, false);
  h.accept(); assert.equal(await pending, true);
  assert.equal(h.controller.getState().status, 'accepted');
  assert.match(h.controller.getState().message, /Pedido aceito/u);
  assert.doesNotMatch(h.controller.getState().message, /download concluído/iu);
});
test('unchecked selection and invalid metadata cannot invoke a native bridge', async () => {
  const h = harness(); await h.controller.refresh();
  assert.equal(await h.controller.send(), false);
  selectWithArtist(h.controller); h.controller.setFields({ lyricsPage: 'https://evil.test/a' });
  assert.equal(h.controller.getState().canSend, false);
  assert.equal(await h.controller.send(), false);
  assert.equal(h.sends.length, 0);
});
test('navigation revokes the selected video and refuses a stale submit', async () => {
  const h = harness(); await h.controller.refresh(); selectWithArtist(h.controller);
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
  const h = harness(); await h.controller.refresh(); selectWithArtist(h.controller);
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
      selectWithArtist(h.controller);
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
  selectWithArtist(h.controller);
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
  selectWithArtist(h.controller);
  assert.equal(await h.controller.send(), false);
  assert.equal(h.sends.length, 0);
  assert.equal(h.controller.getState().status, 'stale');
  assert.doesNotMatch(h.controller.getState().message, /Confira a fila/u);
});
test('missing component and protocol errors remain actionable without pretending download success', async () => {
  const h = harness();
  const controller = createYoutubePopupController({ chrome: h.chrome, newRequestId: () => requestId,
    sendRequest: async () => { throw Object.assign(new Error('private details'), { code: 'bridge_unavailable' }); } });
  await controller.refresh(); selectWithArtist(controller);
  assert.equal(await controller.send(), false);
  assert.equal(controller.getState().status, 'error');
  assert.match(controller.getState().message, /componente YouTube no player/u);
  assert.doesNotMatch(controller.getState().message, /private details/u);
});
test('explicit retry repeats the unchanged YouTube request and metadata after uncertainty', async () => {
  const h = harness();
  const requests = [];
  let ids = 0;
  const controller = createYoutubePopupController({ chrome: h.chrome,
    newRequestId() { ids += 1; return requestId; },
    async sendRequest(_chrome, request) {
      requests.push(structuredClone(request));
      if (requests.length === 1) {
        request.items[0].artist = 'Mutation after the handoff';
        throw Object.assign(new Error('No receipt'), { code: 'invalid_response' });
      }
      return { status: 'accepted', requestId, jobIds: ['same-job'] };
    },
  });
  await controller.refresh();
  controller.setSelected(true);
  controller.setFields({ title: '曲名', artist: 'Artist', lyricsPage: 'https://www.letras.mus.br/artist/song/' });
  assert.equal(await controller.send(), false);
  assert.equal(controller.getState().canRetry, true);
  assert.equal(await controller.send(), false);
  assert.equal(requests.length, 1);
  assert.equal(await controller.retry(), true);
  assert.equal(requests.length, 2);
  assert.equal(JSON.stringify(requests[1]), JSON.stringify(requests[0]));
  assert.equal(ids, 1);
  assert.equal(controller.getState().status, 'accepted');
  assert.equal(controller.getState().canRetry, false);
  controller.dispose();
});
test('YouTube context and metadata changes revoke an uncertain request', async t => {
  for (const [name, change] of [
    ['unannounced navigation', (h) => { h.tab.url += '&t=12'; }],
    ['focus loss', (h) => h.chrome.windows.onFocusChanged.emit(4)],
    ['metadata edit', (_h, controller) => controller.setFields({ artist: 'Other artist' })],
    ['selection change', (_h, controller) => controller.setSelected(false)],
  ]) {
    await t.test(name, async () => {
      const h = harness();
      let calls = 0;
      const controller = createYoutubePopupController({ chrome: h.chrome, newRequestId: () => requestId,
        async sendRequest() {
          calls += 1;
          throw Object.assign(new Error('Lost receipt'), { code: 'bridge_timeout' });
        },
      });
      await controller.refresh();
      selectWithArtist(controller);
      assert.equal(await controller.send(), false);
      assert.equal(controller.getState().canRetry, true);
      change(h, controller);
      assert.equal(await controller.retry(), false);
      assert.equal(calls, 1);
      assert.equal(controller.getState().canRetry, false);
      controller.dispose();
    });
  }
});
test('unsupported pages and empty active tabs never become downloadable', async () => {
  const h = harness(); h.tab.url = 'https://example.com/video';
  await h.controller.refresh();
  assert.equal(h.controller.getState().status, 'error');
  assert.equal(h.controller.getState().candidate, null);
  assert.equal(h.controller.getState().canSend, false);
});
test('inspection failures identify the browser boundary without exposing raw page or API data', async t => {
  for (const [code, change] of [
    ['window_query', h => { h.chrome.windows.getCurrent = async () => { throw new Error('private-api-detail'); }; }],
    ['window_focus', h => { h.window.focused = false; }],
    ['active_tab_count', h => { h.chrome.tabs.query = async () => []; }],
    ['active_tab_window', h => { h.tab.windowId = 9; }],
    ['active_tab_state', h => { h.tab.status = 'loading'; }],
    ['active_tab_url', h => { delete h.tab.url; }],
    ['page_inspection', h => { h.tab.url = 'https://www.letras.mus.br/bleach/1222774/';
      h.chrome.scripting = { executeScript: async () => { throw new Error('private-api-detail'); } }; }],
  ]) await t.test(code, async () => {
    const h = harness(); change(h);
    assert.equal(await h.controller.refresh(), false);
    const state = h.controller.getState();
    assert.equal(state.status, 'error');
    assert.match(state.message, new RegExp(`\\[contexto: ${code}\\]`, 'u'));
    assert.doesNotMatch(state.message, /private-api-detail|https:\/\//u);
    assert.equal(state.candidate, null);
    assert.equal(h.sends.length, 0);
    h.controller.dispose();
  });
});
test('mounted popup renders tab text literally and sends only after a real selected click', async () => {
  const h = harness();
  h.window.focused = false;
  const elements = new Map();
  for (const id of ['refresh', 'status', 'selection', 'selected', 'video-title', 'video-url', 'title', 'artist', 'file-name', 'lyrics-page', 'fetch-lyrics', 'send']) {
    const listeners = new Map();
    elements.set(id, { textContent: '', value: '', checked: false, hidden: true, disabled: false,
      addEventListener: (name, callback) => listeners.set(name, callback), removeEventListener: name => listeners.delete(name),
      emit: name => listeners.get(name)?.({ preventDefault() {} }), get listenerCount() { return listeners.size; } });
  }
  let popupFocused = true;
  const popupListeners = new Map();
  const document = { hasFocus: () => popupFocused, getElementById: id => elements.get(id), defaultView: {
    addEventListener: (name, callback) => popupListeners.set(name, callback),
    removeEventListener: name => popupListeners.delete(name),
  } };
  const view = mountYoutubePopup({ document, chrome: h.chrome, newRequestId: () => requestId,
    sendRequest: async (_, request) => {
      h.sends.push(request);
      if (h.sends.length === 1) throw Object.assign(new Error('No receipt'), { code: 'bridge_timeout' });
      return { status: 'accepted', requestId, jobIds: ['job-1'] };
    } });
  await flush();
  assert.equal(elements.get('video-title').textContent, '<b>曲名</b>');
  assert.equal(elements.get('video-url').value, 'https://www.youtube.com/watch?v=aB_0123-xYz');
  assert.equal(elements.get('selection').hidden, false);
  assert.equal(elements.get('send').disabled, true);
  assert.equal(h.sends.length, 0);
  elements.get('selected').checked = true; elements.get('selected').emit('change');
  popupFocused = false; popupListeners.get('blur')();
  assert.equal(elements.get('send').disabled, true);
  assert.equal(elements.get('selected').checked, false);
  assert.equal(h.sends.length, 0);
  popupFocused = true; elements.get('refresh').emit('click'); await flush();
  elements.get('selected').checked = true; elements.get('selected').emit('change');
  elements.get('title').value = '<img src=x> 曲名'; elements.get('title').emit('input');
  elements.get('artist').value = 'Artist'; elements.get('artist').emit('input');
  assert.equal(elements.get('file-name').placeholder, '<img src=x> 曲名 - Artist');
  elements.get('file-name').value = '  Minha faixa.MP3  '; elements.get('file-name').emit('input');
  assert.equal(elements.get('send').disabled, false);
  elements.get('send').emit('click'); await flush();
  assert.equal(h.sends.length, 1);
  assert.equal(h.sends[0].items[0].title, '<img src=x> 曲名');
  assert.equal(h.sends[0].items[0].fileName, 'Minha faixa.MP3');
  assert.equal(elements.get('send').disabled, false);
  assert.equal(elements.get('send').textContent, 'Repetir o mesmo pedido');
  elements.get('send').emit('click'); await flush();
  assert.deepEqual(h.sends, [h.sends[0], h.sends[0]]);
  assert.match(elements.get('status').textContent, /Pedido aceito/u);
  view.dispose();
  assert.equal([...elements.values()].reduce((count, node) => count + node.listenerCount, 0), 0);
  assert.equal(popupListeners.size, 0);
});
