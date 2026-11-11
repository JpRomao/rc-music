import { videoFromTab, buildYoutubeAudioRequest } from './policy.mjs';
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
export function createYoutubePopupController({ chrome, newRequestId, sendRequest = sendYoutubeAudioRequest, onState = () => {} }) {
  let disposed = false, epoch = 0;
  let binding = null, refreshWindowId = null, dispatched = false;
  let state = { status: 'idle', candidate: null, selected: false, title: '', artist: '', lyricsPage: '',
    fetchLyrics: true, message: 'Abra um vídeo do YouTube e clique em Atualizar vídeo.' };
  const fields = () => ({ title: state.title, artist: state.artist, lyricsPage: state.lyricsPage, fetchLyrics: state.fetchLyrics });
  const canSend = () => {
    if (disposed || state.status !== 'ready' || !state.candidate || !state.selected) return false;
    try { buildYoutubeAudioRequest(state.candidate, fields(), previewId); return true; } catch { return false; }
  };
  const getState = () => structuredClone({ ...state, canSend: canSend() });
  const publish = changes => {
    if (disposed) return;
    state = { ...state, ...changes };
    onState(getState());
  };
  const live = token => !disposed && epoch === token;
  const invalidate = () => {
    if (disposed || (!binding && state.status !== 'loading')) return;
    const uncertain = dispatched;
    epoch++;
    binding = null;
    refreshWindowId = null;
    dispatched = false;
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

  async function activeVideo(windowId) {
    const tabs = await chrome.tabs.query({ active: true, currentWindow: true });
    if (!Array.isArray(tabs) || tabs.length !== 1) throw new Error('unsupported_page');
    const tab = tabs[0];
    if (tab?.active !== true || tab.windowId !== windowId || tab.discarded === true || (tab.pendingUrl ?? '') !== ''
      || (tab.status !== undefined && tab.status !== 'complete')) throw new Error('unsupported_page');
    const candidate = videoFromTab(tab);
    // The original page is only used to recheck the selection. It never enters the view or native request.
    return { candidate, context: { tabId: candidate.tabId, windowId, pageUrl: tab.url } };
  }

  async function refresh() {
    if (disposed || ['sending', 'checking'].includes(state.status)) return false;
    const token = ++epoch;
    binding = null;
    refreshWindowId = null;
    publish({ status: 'loading', candidate: null, selected: false, title: '', artist: '', lyricsPage: '', fetchLyrics: true,
      message: 'Lendo o vídeo da aba atual…' });
    if (!live(token)) return false;
    try {
      const windowId = await focusedWindowId();
      if (!live(token)) return false;
      refreshWindowId = windowId;
      const { candidate, context } = await activeVideo(windowId);
      if (!live(token)) return false;
      binding = context;
      refreshWindowId = null;
      publish({ status: 'ready', candidate, title: candidate.title, message: 'Selecione o vídeo e confira os dados antes de baixar.' });
      return live(token);
    } catch {
      if (live(token)) {
        binding = null;
        refreshWindowId = null;
        publish({ status: 'error', candidate: null, selected: false,
          message: 'Abra uma página de vídeo do YouTube compatível e clique em Atualizar vídeo.' });
      }
      return false;
    }
  }
  function setSelected(selected) {
    if (state.status === 'ready' && typeof selected === 'boolean') publish({ selected });
  }
  function setFields(changes) {
    if (disposed || state.status !== 'ready' || !changes || typeof changes !== 'object') return;
    const edited = {};
    for (const key of ['title', 'artist', 'lyricsPage']) if (typeof changes[key] === 'string') edited[key] = changes[key];
    if (typeof changes.fetchLyrics === 'boolean') edited.fetchLyrics = changes.fetchLyrics;
    publish(edited);
  }
  async function send() {
    if (!binding || !canSend()) return false;
    const token = epoch, expected = binding, candidate = structuredClone(state.candidate), metadata = fields();
    publish({ status: 'checking', message: 'Conferindo o vídeo selecionado…' });
    if (!live(token)) return false;
    try {
      const current = await activeVideo(expected.windowId);
      if (!live(token)) return false;
      if (current.context.tabId !== expected.tabId || current.context.pageUrl !== expected.pageUrl) { invalidate(); return false; }
      const windowId = await focusedWindowId();
      if (!live(token)) return false;
      if (windowId !== expected.windowId) { invalidate(); return false; }
      const request = buildYoutubeAudioRequest(candidate, metadata, newRequestId());
      if (!live(token) || state.status !== 'checking') return false;
      publish({ status: 'sending', message: 'Enviando pedido ao componente YouTube…' });
      if (!live(token) || state.status !== 'sending') return false;
      dispatched = true;
      const result = await sendRequest(chrome, request);
      if (!live(token)) return false;
      if (!result || result.status !== 'accepted' || result.requestId !== request.requestId
        || !Array.isArray(result.jobIds) || result.jobIds.length !== 1) throw Object.assign(new Error('invalid_response'), { code: 'invalid_response' });
      dispatched = false;
      publish({ status: 'accepted', selected: false, message: 'Pedido aceito pelo app. Acompanhe o progresso e a conclusão no RC Music.' });
      return true;
    } catch (error) {
      if (!live(token)) return false;
      if (state.status === 'checking') { invalidate(); return false; }
      const code = Object.hasOwn(messages, error?.code) ? error.code : 'invalid_response';
      dispatched = false;
      publish({ status: 'error', selected: false, message: error?.serverCode === 'not_available' ? messages.bridge_unavailable : messages[code] });
      return false;
    }
  }
  function dispose() {
    if (disposed) return;
    disposed = true; epoch++;
    binding = null;
    refreshWindowId = null;
    chrome.tabs.onUpdated.removeListener(onUpdated);
    chrome.tabs.onRemoved.removeListener(onRemoved);
    chrome.tabs.onActivated.removeListener(onActivated);
    chrome.windows.onFocusChanged.removeListener(onFocusChanged);
  }
  return { getState, refresh, setSelected, setFields, send, dispose };
}
