const MAX_SELECTION = 20;
const messages = {
  navigation: 'A página mudou ou foi fechada. Clique em Atualizar para fazer uma nova seleção.',
  invalid_snapshot: 'Não foi possível validar a lista de mídia. Clique em Atualizar para tentar novamente.',
  unsupported_page: 'Esta página não é compatível com a extensão principal. YouTube pertence à extensão separada.',
  inspection_timeout: 'A página não respondeu a tempo. Clique em Atualizar para tentar novamente.',
  bridge_unavailable: 'A ponte local está indisponível ou desconectou. O recebimento não foi confirmado: confira a fila no app antes de reenviar.',
  bridge_timeout: 'Não foi possível confirmar o recebimento. Confira a fila no app antes de tentar enviar novamente.',
  invalid_response: 'O app não confirmou uma resposta válida. Confira a fila no app antes de reenviar.',
  rejected: 'O app recusou o pedido. Verifique a integração e faça uma nova seleção.',
  invalid_selection: 'A seleção deixou de ser válida. Clique em Atualizar e escolha os itens novamente.',
  source_changed: 'Uma fonte selecionada mudou. Clique em Atualizar e revise a seleção antes de autorizar novamente.',
  invalid_request: 'Não foi possível validar o pedido. Clique em Atualizar e faça uma nova seleção.',
};
const messageFor = (code, fallback) => Object.hasOwn(messages, code) ? messages[code] : fallback;

export function createPopupController({
  chrome, normalizeSnapshot, buildDownloadRequest, isSelectionCurrent, sendDownloadRequest, newRequestId,
  onState = () => {}, setTimer = setTimeout, clearTimer = clearTimeout, timeoutMs = 5000,
}) {
  let disposed = false;
  let epoch = 0;
  let binding = null;
  let port = null;
  let portMessage = null;
  let portDisconnect = null;
  let timer = null;
  let currentSnapshot = null;
  let pendingSnapshot = null;
  let checkingSnapshot = false;
  let submission = null;
  let state = {
    status: 'idle', candidates: [], selectedUrls: [], authorized: false,
    truncated: false, pageOrigin: '', acceptedCount: 0,
    message: 'Abra uma página de áudio ou vídeo e clique em Atualizar.',
  };

  function getState() {
    const checkingSelection = checkingSnapshot || pendingSnapshot !== null;
    return structuredClone({ ...state, checkingSelection,
      canSend: state.status === 'ready' && !checkingSelection
        && state.authorized && state.selectedUrls.length > 0,
    });
  }

  function publish(changes) {
    if (disposed) return;
    state = { ...state, ...changes };
    onState(getState());
  }

  function clearDeadline() {
    if (timer !== null) clearTimer(timer);
    timer = null;
  }

  function detach() {
    clearDeadline();
    const previous = port;
    port = null;
    if (previous) {
      previous.onMessage.removeListener(portMessage);
      previous.onDisconnect.removeListener(portDisconnect);
      try { previous.disconnect(); } catch { /* Already closed by Chromium. */ }
    }
    binding = null;
    currentSnapshot = null;
    pendingSnapshot = null;
    checkingSnapshot = false;
    submission = null;
  }

  function finish(status, message, { acceptedCount = 0, retainList = false } = {}) {
    if (disposed) return;
    epoch += 1;
    detach();
    publish({ status, message, selectedUrls: [], authorized: false, acceptedCount,
      ...(retainList ? {} : { candidates: [], pageOrigin: '', truncated: false }),
    });
  }

  function invalidate() {
    const message = state.status === 'sending'
      ? `${messages.navigation} Se o envio já começou, confira a fila no app antes de reenviar.`
      : messages.navigation;
    finish('disconnected', message);
  }

  const live = (token) => !disposed && token === epoch;
  const staleError = () => Object.assign(new Error('Session changed'), { code: 'navigation' });

  function armDeadline(token, code) {
    clearDeadline();
    timer = setTimer(() => {
      if (live(token)) finish('error', messages[code]);
    }, timeoutMs);
  }

  async function verifyCurrent(token) {
    const expected = binding;
    if (!live(token) || !expected) throw staleError();
    const [tab, window] = await Promise.all([
      chrome.tabs.get(expected.tabId), chrome.windows.getLastFocused(),
    ]);
    if (!live(token) || tab.id !== expected.tabId || tab.windowId !== expected.windowId
      || tab.url !== expected.pageUrl || tab.active !== true || tab.status === 'loading' || tab.pendingUrl
      || window?.id !== expected.windowId || window.focused !== true) {
      throw staleError();
    }
    const result = await chrome.scripting.executeScript({
      target: { tabId: expected.tabId, documentIds: [expected.documentId] },
      world: 'ISOLATED', func: () => location.href,
    });
    if (!live(token) || result.length !== 1 || result[0].frameId !== 0
      || result[0].documentId !== expected.documentId || result[0].result !== expected.pageUrl) {
      throw staleError();
    }
  }

  async function verifySelectedSources(token, snapshot, selected) {
    const expected = binding;
    if (!live(token) || !expected) throw staleError();
    const result = await chrome.scripting.executeScript({
      target: { tabId: expected.tabId, documentIds: [expected.documentId] },
      world: 'ISOLATED', func: () => globalThis.__rcMusicDetectionV2?.snapshot() ?? null,
    });
    if (!live(token) || result.length !== 1 || result[0].frameId !== 0
      || result[0].documentId !== expected.documentId) throw staleError();
    if (result[0].result === null) throw Object.assign(new Error('Inactive detection'), { code: 'source_changed' });
    const fresh = normalizeSnapshot(result[0].result);
    if (fresh.pageUrl !== expected.pageUrl) throw staleError();
    // Compare the raw result too: normalization deliberately deduplicates URLs.
    if (!isSelectionCurrent(snapshot, result[0].result, selected)) {
      throw Object.assign(new Error('Selected source changed'), { code: 'source_changed' });
    }
  }

  async function consumeSnapshots(token) {
    if (checkingSnapshot || !pendingSnapshot || !live(token)) return;
    checkingSnapshot = true;
    publish({});
    if (!live(token)) return;
    armDeadline(token, 'inspection_timeout');
    try {
      while (pendingSnapshot && live(token) && state.status !== 'sending') {
        const next = pendingSnapshot;
        pendingSnapshot = null;
        await verifyCurrent(token);
        if (!live(token) || state.status === 'sending') return;
        if (pendingSnapshot) continue;
        const candidates = next.candidates;
        const selectedUrls = state.selectedUrls.filter((url) => candidates.some((item) => item.url === url));
        const selectionChanged = selectedUrls.length !== state.selectedUrls.length || selectedUrls.some((url) => {
          const previous = state.candidates.find((item) => item.url === url);
          const candidate = candidates.find((item) => item.url === url);
          return previous.title !== candidate.title || previous.kind !== candidate.kind || previous.sourceId !== candidate.sourceId;
        });
        currentSnapshot = next;
        publish({ candidates, selectedUrls, truncated: next.truncated,
          authorized: selectionChanged ? false : state.authorized,
          pageOrigin: new URL(next.pageUrl).origin, status: candidates.length ? 'ready' : 'empty',
          message: candidates.length
            ? 'Selecione os itens que deseja enviar ao RC Music.'
            : 'Nenhuma mídia compatível encontrada. Inicie a mídia na página e tente Atualizar.',
        });
      }
    } catch {
      if (live(token)) invalidate();
    } finally {
      if (live(token)) {
        checkingSnapshot = false;
        if (state.status !== 'sending') clearDeadline();
        publish({});
      }
    }
  }

  function receive(raw, token) {
    if (!live(token)) return;
    try {
      const next = normalizeSnapshot(raw);
      if (next.pageUrl !== binding?.pageUrl) { invalidate(); return; }
      if (state.status === 'sending') {
        if (submission && !submission.sent && !isSelectionCurrent(submission.snapshot, raw, submission.selected)) {
          finish('error', messages.source_changed);
        }
        return;
      }
      pendingSnapshot = next;
      // Rendering can skip intermediate snapshots. Revocation cannot skip an
      // observed loss, replacement or ambiguous selected source.
      if (state.authorized && currentSnapshot && !isSelectionCurrent(currentSnapshot, raw, state.selectedUrls)) {
        publish({ authorized: false });
      }
      void consumeSnapshots(token);
    } catch (error) {
      finish('error', messageFor(error?.code, messages.invalid_snapshot));
    }
  }

  async function refresh() {
    if (disposed || state.status === 'sending' || state.status === 'loading') return;
    epoch += 1;
    detach();
    const token = epoch;
    publish({ status: 'loading', candidates: [], selectedUrls: [], authorized: false,
      truncated: false, pageOrigin: '', acceptedCount: 0, message: 'Procurando áudio e vídeo nesta página…' });
    armDeadline(token, 'inspection_timeout');
    try {
      const tabs = await chrome.tabs.query({ active: true, currentWindow: true });
      if (!live(token)) return;
      const tab = tabs[0];
      if (tabs.length !== 1 || !Number.isInteger(tab?.id) || !Number.isInteger(tab.windowId)
        || typeof tab.url !== 'string') throw staleError();
      normalizeSnapshot({ type: 'snapshot', pageUrl: tab.url, candidates: [], truncated: false });
      binding = { tabId: tab.id, windowId: tab.windowId, pageUrl: tab.url, documentId: null };
      const result = await chrome.scripting.executeScript({
        target: { tabId: tab.id, frameIds: [0] }, world: 'ISOLATED', files: ['media-collector.js', 'detector.js'],
      });
      if (!live(token)) return;
      if (result.length !== 1 || result[0].frameId !== 0 || typeof result[0].documentId !== 'string' || !result[0].documentId) throw staleError();
      binding.documentId = result[0].documentId;
      await verifyCurrent(token);
      if (!live(token)) return;
      port = chrome.tabs.connect(tab.id, { documentId: binding.documentId, name: 'rc-music-detection-v2' });
      portMessage = (raw) => receive(raw, token);
      portDisconnect = () => {
        void chrome.runtime?.lastError;
        if (live(token)) invalidate();
      };
      port.onMessage.addListener(portMessage);
      port.onDisconnect.addListener(portDisconnect);
    } catch (error) {
      if (live(token)) finish('error', messageFor(error?.code, 'Não foi possível inspecionar esta página. Abra uma página HTTP(S) compatível e clique em Atualizar.'));
    }
  }

  function setSelected(url, selected) {
    if (disposed || state.status !== 'ready' || typeof selected !== 'boolean'
      || !state.candidates.some((item) => item.url === url)) return;
    const selection = new Set(state.selectedUrls);
    if (selected && !selection.has(url) && selection.size >= MAX_SELECTION) {
      publish({ message: 'Selecione até 20 itens por envio.' });
      return;
    }
    const before = selection.size;
    if (selected) selection.add(url); else selection.delete(url);
    if (selection.size !== before) publish({ selectedUrls: [...selection], authorized: false,
      message: 'Confirme o uso autorizado para enviar a seleção.' });
  }

  function setAuthorized(authorized) {
    if (disposed || state.status !== 'ready' || typeof authorized !== 'boolean'
      || (authorized && (checkingSnapshot || pendingSnapshot))) return;
    publish({ authorized });
  }

  async function submit() {
    if (disposed || !getState().canSend || !currentSnapshot) return;
    const token = epoch;
    const snapshot = structuredClone(currentSnapshot);
    const selected = [...state.selectedUrls];
    const attempt = { snapshot, selected, sent: false };
    submission = attempt;
    publish({ status: 'sending', message: 'Validando a página e enviando a seleção…' });
    armDeadline(token, 'bridge_timeout');
    try {
      const request = buildDownloadRequest(snapshot, selected, newRequestId());
      await verifyCurrent(token);
      if (!live(token)) return;
      await verifySelectedSources(token, snapshot, selected);
      if (!live(token)) return;
      await verifyCurrent(token);
      if (!live(token)) return;
      armDeadline(token, 'bridge_timeout');
      attempt.sent = true;
      const response = await sendDownloadRequest(chrome, request, { timeoutMs });
      if (!live(token)) return;
      finish('accepted', 'Pedidos aceitos pelo RC Music. Acompanhe os downloads e a conversão no app.', {
        acceptedCount: response.jobIds.length, retainList: true,
      });
    } catch (error) {
      if (!live(token)) return;
      if (error?.code === 'navigation' || (!attempt.sent && !Object.hasOwn(messages, error?.code))) invalidate();
      else finish(error?.code === 'bridge_unavailable' ? 'disconnected' : 'error', messageFor(error?.code, messages.invalid_response));
    }
  }

  const onUpdated = (tabId, change) => {
    if (binding?.tabId === tabId && (change.status === 'loading'
      || (typeof change.url === 'string' && change.url !== binding.pageUrl))) invalidate();
  };
  const onRemoved = (tabId) => { if (binding?.tabId === tabId) invalidate(); };
  const onActivated = ({ tabId, windowId }) => {
    if (binding && windowId === binding.windowId && tabId !== binding.tabId) invalidate();
  };
  const onFocusChanged = (windowId) => {
    if (binding && windowId !== binding.windowId) invalidate();
  };
  chrome.tabs.onUpdated.addListener(onUpdated);
  chrome.tabs.onRemoved.addListener(onRemoved);
  chrome.tabs.onActivated.addListener(onActivated);
  chrome.windows.onFocusChanged.addListener(onFocusChanged);

  function dispose() {
    if (disposed) return;
    disposed = true;
    epoch += 1;
    detach();
    chrome.tabs.onUpdated.removeListener(onUpdated);
    chrome.tabs.onRemoved.removeListener(onRemoved);
    chrome.tabs.onActivated.removeListener(onActivated);
    chrome.windows.onFocusChanged.removeListener(onFocusChanged);
  }

  return { refresh, submit, setSelected, setAuthorized, dispose, getState };
}
