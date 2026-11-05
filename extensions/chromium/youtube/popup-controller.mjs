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
    if (disposed || !state.candidate) return;
    const uncertain = state.status === 'sending';
    epoch++;
    publish({ status: 'stale', candidate: null, selected: false,
      message: uncertain ? 'A página mudou após o início do envio. Confira a fila no app antes de reenviar.'
        : 'O vídeo mudou ou foi fechado. Atualize e selecione o vídeo novamente.' });
  };
  const onUpdated = (id, changes) => { if (id === state.candidate?.tabId && (changes.url !== undefined || changes.status === 'loading')) invalidate(); };
  const onRemoved = id => { if (id === state.candidate?.tabId) invalidate(); };
  const onActivated = info => { if (state.candidate && info.tabId !== state.candidate.tabId) invalidate(); };
  chrome.tabs.onUpdated.addListener(onUpdated);
  chrome.tabs.onRemoved.addListener(onRemoved);
  chrome.tabs.onActivated.addListener(onActivated);
  async function refresh() {
    if (disposed || ['sending', 'checking'].includes(state.status)) return false;
    const token = ++epoch;
    publish({ status: 'loading', candidate: null, selected: false, title: '', artist: '', lyricsPage: '', fetchLyrics: true,
      message: 'Lendo o vídeo da aba atual…' });
    try {
      const tabs = await chrome.tabs.query({ active: true, currentWindow: true });
      if (!live(token)) return false;
      if (!Array.isArray(tabs) || tabs.length !== 1) throw new Error('unsupported_page');
      const candidate = videoFromTab(tabs[0]);
      publish({ status: 'ready', candidate, title: candidate.title, message: 'Selecione o vídeo e confira os dados antes de baixar.' });
      return true;
    } catch {
      if (live(token)) publish({ status: 'error', candidate: null, selected: false,
        message: 'Abra uma página de vídeo do YouTube compatível e clique em Atualizar vídeo.' });
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
    if (!canSend()) return false;
    const token = epoch, candidate = structuredClone(state.candidate), metadata = fields();
    publish({ status: 'checking', message: 'Conferindo o vídeo selecionado…' });
    try {
      const current = videoFromTab(await chrome.tabs.get(candidate.tabId));
      if (!live(token)) return false;
      if (current.tabId !== candidate.tabId || current.videoId !== candidate.videoId) { invalidate(); return false; }
      const request = buildYoutubeAudioRequest(candidate, metadata, newRequestId());
      if (!live(token) || state.status !== 'checking') return false;
      publish({ status: 'sending', message: 'Enviando pedido ao componente YouTube…' });
      const result = await sendRequest(chrome, request);
      if (!live(token)) return false;
      if (!result || result.status !== 'accepted' || result.requestId !== request.requestId
        || !Array.isArray(result.jobIds) || result.jobIds.length !== 1) throw Object.assign(new Error('invalid_response'), { code: 'invalid_response' });
      publish({ status: 'accepted', selected: false, message: 'Pedido aceito pelo app. Acompanhe o progresso e a conclusão no RC Music.' });
      return true;
    } catch (error) {
      if (!live(token)) return false;
      if (state.status === 'checking') { invalidate(); return false; }
      const code = Object.hasOwn(messages, error?.code) ? error.code : 'invalid_response';
      publish({ status: 'error', selected: false, message: error?.serverCode === 'not_available' ? messages.bridge_unavailable : messages[code] });
      return false;
    }
  }
  function dispose() {
    if (disposed) return;
    disposed = true; epoch++;
    chrome.tabs.onUpdated.removeListener(onUpdated);
    chrome.tabs.onRemoved.removeListener(onRemoved);
    chrome.tabs.onActivated.removeListener(onActivated);
  }
  return { getState, refresh, setSelected, setFields, send, dispose };
}
