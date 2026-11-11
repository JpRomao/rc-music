import { createPopupController } from './popup-controller.mjs';
import { normalizeSnapshot, buildDownloadRequest, isSelectionCurrent } from './policy.mjs';
import { sendDownloadRequest } from './native-client.mjs';

export function mountPopup(document, controller) {
  const byId = (id) => document.getElementById(id);
  const elements = {
    status: byId('status'), page: byId('page-origin'), list: byId('media-list'),
    count: byId('selection-count'), truncated: byId('truncated'),
    authorize: byId('authorize'), refresh: byId('refresh'), send: byId('send'),
  };
  const rows = new Map();
  const listeners = new Set();
  let nextId = 0;
  let disposed = false;

  function listen(target, name, callback) {
    target.addEventListener(name, callback);
    const remove = () => { target.removeEventListener(name, callback); listeners.delete(remove); };
    listeners.add(remove);
    return remove;
  }

  function createRow(candidate) {
    const row = document.createElement('li');
    const label = document.createElement('label');
    const checkbox = document.createElement('input');
    const text = document.createElement('span');
    const title = document.createElement('span');
    const metadata = document.createElement('span');
    row.className = 'media-row';
    label.className = 'media-choice';
    text.className = 'media-text';
    title.className = 'media-title';
    metadata.className = 'media-origin';
    metadata.id = `media-origin-${++nextId}`;
    checkbox.type = 'checkbox';
    checkbox.setAttribute('aria-describedby', metadata.id);
    text.append(title, metadata);
    label.append(checkbox, text);
    row.append(label);
    const remove = listen(checkbox, 'change', () => {
      if (!disposed) controller.setSelected(candidate.url, checkbox.checked);
    });
    return { row, checkbox, title, metadata, remove };
  }

  function render(state) {
    if (disposed) return;
    const busy = state.status === 'loading' || state.status === 'sending';
    const interactive = state.status === 'ready';
    elements.status.textContent = state.message;
    elements.status.setAttribute('data-state', state.status);
    elements.page.textContent = state.pageOrigin || 'Apenas a página aberta';
    elements.count.textContent = `${state.selectedUrls.length} de 20 selecionados`;
    elements.truncated.hidden = !state.truncated;
    elements.authorize.checked = state.authorized;
    elements.authorize.disabled = !interactive || state.checkingSelection || state.selectedUrls.length === 0;
    elements.refresh.disabled = busy;
    elements.send.disabled = !state.canSend || busy;
    elements.send.textContent = state.status === 'sending' ? 'Enviando…' : 'Baixar selecionados';
    elements.list.setAttribute('aria-busy', String(state.status === 'loading'));
    for (const [url, record] of rows) {
      if (!state.candidates.some((candidate) => candidate.url === url)) {
        record.remove();
        rows.delete(url);
      }
    }
    const ordered = state.candidates.map((candidate) => {
      if (!rows.has(candidate.url)) rows.set(candidate.url, createRow(candidate));
      const record = rows.get(candidate.url);
      record.title.textContent = candidate.title;
      record.metadata.textContent = `${candidate.kind === 'video' ? 'Vídeo → MP3' : 'Áudio'} · ${candidate.origin}`;
      record.checkbox.setAttribute('aria-label', candidate.title);
      record.checkbox.checked = state.selectedUrls.includes(candidate.url);
      record.checkbox.disabled = !interactive || (!record.checkbox.checked && state.selectedUrls.length >= 20);
      return record.row;
    });
    // Keep existing checkbox nodes attached on selection-only updates, preserving keyboard focus.
    if (elements.list.children.length !== ordered.length
      || ordered.some((row, index) => elements.list.children[index] !== row)) {
      const focused = document.activeElement;
      elements.list.replaceChildren(...ordered);
      if (focused !== document.activeElement
        && [...rows.values()].some(({ checkbox }) => checkbox === focused && !checkbox.disabled)) {
        focused.focus();
      }
    }
    elements.list.hidden = ordered.length === 0;
  }

  listen(elements.authorize, 'change', () => controller.setAuthorized(elements.authorize.checked));
  listen(elements.refresh, 'click', () => { void controller.refresh(); });
  listen(elements.send, 'click', () => { void controller.submit(); });
  render(controller.getState());

  function dispose() {
    if (disposed) return;
    disposed = true;
    for (const remove of [...listeners]) remove();
    rows.clear();
  }

  return { render, dispose };
}

if (globalThis.document && globalThis.chrome?.tabs) {
  let view;
  const controller = createPopupController({
    chrome: globalThis.chrome, normalizeSnapshot, buildDownloadRequest, isSelectionCurrent, sendDownloadRequest,
    newRequestId: () => crypto.randomUUID(), onState: (state) => view?.render(state),
  });
  view = mountPopup(globalThis.document, controller);
  const dispose = () => { view.dispose(); controller.dispose(); };
  globalThis.addEventListener('pagehide', dispose, { once: true });
  globalThis.addEventListener('unload', dispose, { once: true });
  void controller.refresh();
}
