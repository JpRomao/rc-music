import { videoFromTab, isLetrasVideoPage, videoFromLetrasPage, buildYoutubeAudioRequest } from './policy.mjs';
import { collectLetrasYoutubeVideo } from './page-video.mjs';
import { sendYoutubeAudioRequest } from './native-client.mjs';

const previewId = '00000000-0000-4000-8000-000000000000';
const messages = {
  bridge_unavailable: 'Requer o componente YouTube no player. Não foi possível confirmar o recebimento; confira a fila no app antes de reenviar.',
  bridge_timeout: 'O componente YouTube não confirmou a tempo. Confira a fila no app antes de reenviar.',
  invalid_response: 'A resposta do componente YouTube não pôde ser validada. Confira a fila no app antes de reenviar.',
  rejected: 'O componente YouTube recusou o pedido. Verifique a integração e atualize a seleção.',
  invalid_request: 'Confira o título, o artista e a página do Letras antes de enviar.',
  limit_exceeded: 'Os dados do pedido excedem o limite. Reduza o título, o artista ou a página informada.',
};
const uncertainReply = new Set(['bridge_unavailable', 'bridge_timeout', 'invalid_response']);
export function createYoutubePopupController({ chrome, newRequestId, sendRequest = sendYoutubeAudioRequest, onState = () => {} }) {
  let disposed = false, epoch = 0;
  let binding = null, refreshWindowId = null, dispatched = false;
  let retryableRequest = null;
  let state = { status: 'idle', candidate: null, selected: false, title: '', artist: '', lyricsPage: '',
    fetchLyrics: true, message: 'Abra um vídeo do YouTube ou uma música no Letras e clique em Atualizar vídeo.' };
  const fields = () => ({ title: state.title, artist: state.artist, lyricsPage: state.lyricsPage, fetchLyrics: state.fetchLyrics });
  const canSend = () => {
    if (disposed || state.status !== 'ready' || !state.candidate || !state.selected) return false;
    try { buildYoutubeAudioRequest(state.candidate, fields(), previewId); return true; } catch { return false; }
  };
  const canRetry = () => !disposed && state.status === 'error' && retryableRequest !== null;
  const getState = () => structuredClone({ ...state, canSend: canSend(), canRetry: canRetry() });
  const publish = changes => {
    if (disposed) return;
    state = { ...state, ...changes };
    onState(getState());
  };
  const live = token => !disposed && epoch === token;
  const invalidate = () => {
    if (disposed || (!binding && state.status !== 'loading')) return;
    const uncertain = dispatched || retryableRequest !== null;
    epoch++;
    binding = null;
    refreshWindowId = null;
    dispatched = false;
    retryableRequest = null;
    publish({ status: 'stale', candidate: null, selected: false,
      message: uncertain ? 'A página mudou após o início do envio. Confira a fila no app antes de reenviar.'
        : 'O vídeo mudou ou foi fechado. Atualize e selecione o vídeo novamente.' });
  };
  const onUpdated = (tabId, changes, tab) => {
    if (changes.url === undefined && changes.status !== 'loading' && changes.status !== 'unloaded' && changes.discarded !== true) return;
    if (tabId === binding?.tabId || (refreshWindowId !== null && tab?.windowId === refreshWindowId && tab.active === true)) invalidate();
  };
  const onRemoved = (tabId, info) => {
    if (tabId === binding?.tabId || (refreshWindowId !== null && info?.windowId === refreshWindowId)) invalidate();
  };
  const onActivated = ({ tabId, windowId }) => {
    if ((binding && windowId === binding.windowId && tabId !== binding.tabId) || windowId === refreshWindowId) invalidate();
  };
  const onFocusChanged = windowId => {
    if ((binding && windowId !== binding.windowId) || (state.status === 'loading' && windowId !== refreshWindowId)) invalidate();
  };
  chrome.tabs.onUpdated.addListener(onUpdated);
  chrome.tabs.onRemoved.addListener(onRemoved);
  chrome.tabs.onActivated.addListener(onActivated);
  chrome.windows.onFocusChanged.addListener(onFocusChanged);

  async function focusedWindowId() {
    const window = await chrome.windows.getLastFocused();
    if (window?.focused !== true || !Number.isSafeInteger(window.id) || window.id < 0) throw new Error('unsupported_page');
    return window.id;
  }

  async function readLetrasSource(tab, documentId) {
    const results = await chrome.scripting.executeScript({ world: 'ISOLATED',
      target: documentId ? { tabId: tab.id, documentIds: [documentId] } : { tabId: tab.id, frameIds: [0] },
      func: collectLetrasYoutubeVideo });
    if (!Array.isArray(results) || results.length !== 1 || results[0]?.frameId !== 0
      || typeof results[0].documentId !== 'string' || !/^[a-zA-Z0-9_-]{1,128}$/u.test(results[0].documentId)
      || (documentId && results[0].documentId !== documentId)) throw new Error('unsupported_page');
    return { candidate: videoFromLetrasPage(tab, results[0].result), documentId: results[0].documentId };
  }

  async function activeVideo(windowId, documentId) {
    const tabs = await chrome.tabs.query({ active: true, currentWindow: true });
    if (!Array.isArray(tabs) || tabs.length !== 1) throw new Error('unsupported_page');
    const tab = tabs[0];
    if (tab?.active !== true || tab.windowId !== windowId || tab.discarded === true || (tab.pendingUrl ?? '') !== ''
      || (tab.status !== undefined && tab.status !== 'complete')) throw new Error('unsupported_page');
    if (!isLetrasVideoPage(tab.url)) {
      const candidate = videoFromTab(tab);
      return { candidate, context: { tabId: candidate.tabId, windowId, pageUrl: tab.url } };
    }
    const source = await readLetrasSource(tab, documentId);
    const current = await chrome.tabs.get(tab.id);
    if (current?.id !== tab.id || current.active !== true || current.windowId !== windowId || current.url !== tab.url
      || current.discarded === true || (current.pendingUrl ?? '') !== ''
      || (current.status !== undefined && current.status !== 'complete')) throw new Error('unsupported_page');
    return { candidate: source.candidate, context: { tabId: tab.id, windowId, pageUrl: tab.url, documentId: source.documentId } };
  }

  async function refresh() {
    if (disposed || ['sending', 'checking'].includes(state.status)) return false;
    const token = ++epoch;
    binding = null;
    refreshWindowId = null;
    retryableRequest = null;
    publish({ status: 'loading', candidate: null, selected: false, title: '', artist: '', lyricsPage: '', fetchLyrics: true,
      message: 'Lendo o vídeo da aba atual…' });
    if (!live(token)) return false;
    try {
      const windowId = await focusedWindowId();
      if (!live(token)) return false;
      refreshWindowId = windowId;
      const { candidate, context } = await activeVideo(windowId);
      if (!live(token)) return false;
      if (await focusedWindowId() !== windowId) throw new Error('unsupported_page');
      if (!live(token)) return false;
      binding = context;
      refreshWindowId = null;
      publish({ status: 'ready', candidate, title: candidate.title, lyricsPage: candidate.lyricsPage ?? '',
        message: 'Selecione o vídeo e confira os dados antes de baixar.' });
      return live(token);
    } catch {
      if (live(token)) {
        binding = null;
        refreshWindowId = null;
        publish({ status: 'error', candidate: null, selected: false,
          message: 'Abra um vídeo do YouTube ou uma página Letras com vídeo reconhecido e clique em Atualizar vídeo.' });
      }
      return false;
    }
  }
  function setSelected(selected) {
    if (canRetry() && typeof selected === 'boolean') { invalidate(); return; }
    if (state.status === 'ready' && typeof selected === 'boolean') publish({ selected });
  }
  function setFields(changes) {
    if (disposed || (state.status !== 'ready' && !canRetry()) || !changes || typeof changes !== 'object') return;
    const edited = {};
    for (const key of ['title', 'artist', 'lyricsPage']) if (typeof changes[key] === 'string') edited[key] = changes[key];
    if (typeof changes.fetchLyrics === 'boolean') edited.fetchLyrics = changes.fetchLyrics;
    if (canRetry()) {
      if (Object.keys(edited).some(key => edited[key] !== state[key])) invalidate();
      return;
    }
    publish(edited);
  }
  async function transmit(retainedRequest = null) {
    const token = epoch, expected = binding, candidate = structuredClone(state.candidate), metadata = fields();
    let request = retainedRequest;
    publish({ status: 'checking', message: 'Conferindo o vídeo selecionado…' });
    if (!live(token)) return false;
    try {
      const current = await activeVideo(expected.windowId, expected.documentId);
      if (!live(token)) return false;
      if (current.context.tabId !== expected.tabId || current.context.pageUrl !== expected.pageUrl
        || current.context.documentId !== expected.documentId || current.candidate.videoId !== candidate.videoId) { invalidate(); return false; }
      const windowId = await focusedWindowId();
      if (!live(token)) return false;
      if (windowId !== expected.windowId) { invalidate(); return false; }
      if (expected.documentId !== undefined) {
        // Read the source after all asynchronous tab/focus guards. Do not await
        // another context check between this snapshot and the native handoff.
        const fresh = await readLetrasSource({ id: expected.tabId, url: expected.pageUrl }, expected.documentId);
        if (!live(token)) return false;
        if (fresh.candidate.videoId !== candidate.videoId) { invalidate(); return false; }
      }
      request ??= buildYoutubeAudioRequest(candidate, metadata, newRequestId());
      if (!live(token) || state.status !== 'checking') return false;
      publish({ status: 'sending', message: 'Enviando pedido ao componente YouTube…' });
      if (!live(token) || state.status !== 'sending') return false;
      dispatched = true;
      const result = await sendRequest(chrome, structuredClone(request));
      if (!live(token)) return false;
      if (!result || result.status !== 'accepted' || result.requestId !== request.requestId
        || !Array.isArray(result.jobIds) || result.jobIds.length !== 1) throw Object.assign(new Error('invalid_response'), { code: 'invalid_response' });
      dispatched = false;
      retryableRequest = null;
      publish({ status: 'accepted', selected: false, message: 'Pedido aceito pelo app. Acompanhe o progresso e a conclusão no RC Music.' });
      return true;
    } catch (error) {
      if (!live(token)) return false;
      if (state.status === 'checking') { invalidate(); return false; }
      const code = Object.hasOwn(messages, error?.code) ? error.code : 'invalid_response';
      retryableRequest = dispatched && uncertainReply.has(code) ? request : null;
      dispatched = false;
      const message = error?.serverCode === 'not_available' ? messages.bridge_unavailable : messages[code];
      publish({ status: 'error', selected: false, message: retryableRequest
        ? message + ' Você pode repetir o mesmo pedido neste popup.' : message });
      return false;
    }
  }
  async function send() {
    return binding && canSend() ? transmit() : false;
  }
  async function retry() {
    return binding && canRetry() ? transmit(retryableRequest) : false;
  }
  function dispose() {
    if (disposed) return;
    disposed = true; epoch++;
    binding = null;
    refreshWindowId = null;
    retryableRequest = null;
    chrome.tabs.onUpdated.removeListener(onUpdated);
    chrome.tabs.onRemoved.removeListener(onRemoved);
    chrome.tabs.onActivated.removeListener(onActivated);
    chrome.windows.onFocusChanged.removeListener(onFocusChanged);
  }
  return { getState, refresh, setSelected, setFields, send, retry, dispose };
}
