import { protocolError, validateYoutubeAudioRequest, YOUTUBE_NATIVE_HOST } from './policy.mjs';

const rejectionCodes = new Set(['unsupported_source', 'invalid_request', 'busy', 'not_available', 'limit_exceeded']);
function response(raw, request) {
  if (!raw || typeof raw !== 'object' || Array.isArray(raw) || Object.keys(raw).length !== 5
    || raw.schemaVersion !== 1 || raw.type !== 'youtube.audio.response' || raw.requestId !== request.requestId) {
    throw protocolError('invalid_response');
  }
  if (raw.status === 'rejected' && rejectionCodes.has(raw.code)) {
    throw Object.assign(protocolError('rejected'), { serverCode: raw.code });
  }
  if (raw.status !== 'accepted' || !Array.isArray(raw.jobIds) || raw.jobIds.length !== request.items.length
    || new Set(raw.jobIds).size !== raw.jobIds.length
    || raw.jobIds.some(id => typeof id !== 'string' || !/^[a-zA-Z0-9_-]{1,80}$/u.test(id))) throw protocolError('invalid_response');
  return { status: 'accepted', requestId: request.requestId, jobIds: [...raw.jobIds] };
}
export async function sendYoutubeAudioRequest(chrome, rawRequest, {
  timeoutMs = 5000, setTimer = setTimeout, clearTimer = clearTimeout,
} = {}) {
  const request = validateYoutubeAudioRequest(rawRequest);
  return new Promise((resolve, reject) => {
    let port, timer, settled = false;
    const finish = (error, result) => {
      if (settled) return;
      settled = true;
      if (timer !== undefined) clearTimer(timer);
      if (port) {
        port.onMessage.removeListener(onMessage);
        port.onDisconnect.removeListener(onDisconnect);
        try { port.disconnect(); } catch { /* The browser already closed this channel. */ }
      }
      if (error) reject(error);
      else resolve(result);
    };
    const onMessage = raw => {
      try { finish(null, response(raw, request)); }
      catch (error) {
        finish(error.code === 'rejected' ? Object.assign(protocolError('rejected'), { serverCode: error.serverCode })
          : protocolError('invalid_response'));
      }
    };
    const onDisconnect = () => {
      void chrome.runtime.lastError; // Acknowledge Chrome without displaying paths or native details.
      finish(protocolError('bridge_unavailable'));
    };
    try {
      port = chrome.runtime.connectNative(YOUTUBE_NATIVE_HOST);
      port.onMessage.addListener(onMessage);
      port.onDisconnect.addListener(onDisconnect);
      timer = setTimer(() => finish(protocolError('bridge_timeout')), timeoutMs);
      if (settled) { clearTimer(timer); return; }
      port.postMessage(request);
    } catch {
      finish(protocolError('bridge_unavailable'));
    }
  });
}
