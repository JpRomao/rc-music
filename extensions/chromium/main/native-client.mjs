import { protocolError, validateDownloadRequest } from './policy.mjs';

const HOST_NAME = 'com.rcmusic.media';
const rejectionCodes = new Set(['unsupported_source', 'invalid_request', 'busy', 'not_available', 'limit_exceeded']);

function parseResponse(raw, request) {
  if (!raw || typeof raw !== 'object' || Array.isArray(raw)
    || raw.schemaVersion !== 1 || raw.type !== 'download.response' || raw.requestId !== request.requestId
    || Object.keys(raw).length !== 5) throw protocolError('invalid_response');
  if (raw.status === 'rejected' && rejectionCodes.has(raw.code)) throw protocolError('rejected');
  if (raw.status !== 'accepted' || !Array.isArray(raw.jobIds) || raw.jobIds.length !== request.items.length
    || new Set(raw.jobIds).size !== raw.jobIds.length
    || raw.jobIds.some(id => typeof id !== 'string' || !/^[a-zA-Z0-9_-]{1,80}$/.test(id))) {
    throw protocolError('invalid_response');
  }
  return { status: 'accepted', requestId: request.requestId, jobIds: [...raw.jobIds] };
}

export async function sendDownloadRequest(chrome, rawRequest, {
  timeoutMs = 5000, setTimer = setTimeout, clearTimer = clearTimeout,
} = {}) {
  const request = validateDownloadRequest(rawRequest);
  return new Promise((resolve, reject) => {
    let port;
    let timer;
    let settled = false;
    const finish = (error, result) => {
      if (settled) return;
      settled = true;
      if (timer !== undefined) clearTimer(timer);
      if (port) {
        port.onMessage.removeListener(onMessage);
        port.onDisconnect.removeListener(onDisconnect);
        try { port.disconnect(); } catch { /* Already closed by browser. */ }
      }
      if (error) reject(error);
      else resolve(result);
    };
    const onMessage = raw => {
      try { finish(null, parseResponse(raw, request)); }
      catch (error) { finish(protocolError(error.code === 'rejected' ? 'rejected' : 'invalid_response')); }
    };
    const onDisconnect = () => {
      // Read lastError to acknowledge Chrome's callback error without logging sensitive details.
      void chrome.runtime.lastError;
      finish(protocolError('bridge_unavailable'));
    };
    try {
      port = chrome.runtime.connectNative(HOST_NAME);
      port.onMessage.addListener(onMessage);
      port.onDisconnect.addListener(onDisconnect);
      timer = setTimer(() => finish(protocolError('bridge_timeout')), timeoutMs);
      port.postMessage(request);
    } catch {
      finish(protocolError('bridge_unavailable'));
    }
  });
}
