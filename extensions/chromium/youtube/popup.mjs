import { createYoutubePopupController } from './popup-controller.mjs';

export function mountYoutubePopup({ document, chrome, newRequestId = () => crypto.randomUUID(), sendRequest }) {
  const nodes = {};
  for (const id of ['refresh', 'status', 'selection', 'selected', 'video-title', 'title', 'artist', 'lyrics-page', 'fetch-lyrics', 'send']) {
    nodes[id] = document.getElementById(id);
    if (!nodes[id]) throw new Error('Missing popup control');
  }
  const listeners = [];
  let disposed = false;
  function render(state) {
    if (disposed) return;
    const editable = state.status === 'ready';
    nodes.status.textContent = state.message;
    nodes.selection.hidden = state.candidate === null;
    nodes['video-title'].textContent = state.candidate?.title || '';
    nodes.selected.checked = state.selected;
    nodes.selected.disabled = !editable;
    for (const [id, value] of [['title', state.title], ['artist', state.artist], ['lyrics-page', state.lyricsPage]]) {
      if (nodes[id].value !== value) nodes[id].value = value;
      nodes[id].disabled = !editable;
    }
    nodes['fetch-lyrics'].checked = state.fetchLyrics;
    nodes['fetch-lyrics'].disabled = !editable;
    nodes.send.textContent = state.canRetry ? 'Repetir o mesmo pedido' : state.fetchLyrics ? 'Baixar MP3 + letra' : 'Baixar MP3';
    nodes.send.disabled = !state.canSend && !state.canRetry;
    nodes.refresh.disabled = ['loading', 'checking', 'sending'].includes(state.status);
  }
  const controller = createYoutubePopupController({ chrome, newRequestId, sendRequest, onState: render });
  function listen(node, name, callback) {
    node.addEventListener(name, callback);
    listeners.push([node, name, callback]);
  }
  const edit = () => controller.setFields({ title: nodes.title.value, artist: nodes.artist.value,
    lyricsPage: nodes['lyrics-page'].value, fetchLyrics: nodes['fetch-lyrics'].checked });
  listen(nodes.refresh, 'click', () => { void controller.refresh(); });
  listen(nodes.selected, 'change', () => controller.setSelected(nodes.selected.checked));
  for (const id of ['title', 'artist', 'lyrics-page']) listen(nodes[id], 'input', edit);
  listen(nodes['fetch-lyrics'], 'change', edit);
  listen(nodes.send, 'click', () => {
    if (controller.getState().canRetry) void controller.retry();
    else void controller.send();
  });
  render(controller.getState());
  void controller.refresh();
  return {
    dispose() {
      if (disposed) return;
      disposed = true;
      for (const [node, name, callback] of listeners) node.removeEventListener(name, callback);
      controller.dispose();
    },
  };
}

if (globalThis.document && globalThis.chrome?.tabs) {
  const view = mountYoutubePopup({ document: globalThis.document, chrome: globalThis.chrome });
  globalThis.addEventListener('pagehide', () => view.dispose(), { once: true });
  globalThis.addEventListener('unload', () => view.dispose(), { once: true });
}
