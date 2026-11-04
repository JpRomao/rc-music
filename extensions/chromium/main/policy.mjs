const MAX_URL = 8192;
const MAX_TITLE = 200;
const MAX_CANDIDATES = 100;
const MAX_BATCH = 20;
const blockedDomains = [
  'youtube.com', 'youtu.be', 'youtube-nocookie.com', 'googlevideo.com',
  'youtube.googleapis.com', 'youtubei.googleapis.com', 'youtubekids.com',
  'localhost', 'local', 'internal', 'lan', 'home', 'corp', 'home.arpa', 'in-addr.arpa', 'ip6.arpa',
];
const controls = /[\u0000-\u0020\u007f-\u009f\\]/u;
const titleControls = /[\u0000-\u001f\u007f-\u009f\u202a-\u202e\u2066-\u2069]/gu;
const uuid = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i;

function wellFormedText(value) {
  // Iteration preserves surrogate pairs; older supported Chromium lacks String.toWellFormed.
  let result = '';
  for (const character of value) {
    const code = character.charCodeAt(0);
    result += character.length === 1 && code >= 0xd800 && code <= 0xdfff ? '\uFFFD' : character;
  }
  return result;
}

export function protocolError(code) {
  return Object.assign(new Error(code), { code });
}

function exactKeys(value, keys) {
  return value !== null && typeof value === 'object' && !Array.isArray(value)
    && Object.keys(value).length === keys.length && keys.every(key => Object.hasOwn(value, key));
}

function withinBytes(value, maxBytes) {
  try {
    return new TextEncoder().encode(JSON.stringify(value)).byteLength <= maxBytes;
  } catch {
    return false;
  }
}

function mediaUrl(raw, preserveFragment = false) {
  if (typeof raw !== 'string' || raw.length > MAX_URL || controls.test(raw) || !/^https?:\/\//i.test(raw)
    || /%(?![0-9a-f]{2})/i.test(raw) || wellFormedText(raw) !== raw) return null;
  try {
    const url = new URL(raw);
    const host = url.hostname.toLowerCase().replace(/\.$/, '');
    if (url.username || url.password || url.port || host.startsWith('[')
      || /^\d+\.\d+\.\d+\.\d+$/.test(host) || !host.includes('.') || host.length > 253
      || !host.split('.').every(label => label.length <= 63 && /^[a-z0-9](?:[a-z0-9-]*[a-z0-9])?$/i.test(label))
      || blockedDomains.some(domain => host === domain || host.endsWith(`.${domain}`))) return null;
    if (!preserveFragment) url.hash = '';
    return url.href.length <= MAX_URL ? url : null;
  } catch {
    return null;
  }
}

function cleanTitle(title, kind) {
  return wellFormedText(title).replace(titleControls, '').trim().slice(0, MAX_TITLE).replace(/[\uD800-\uDBFF]$/u, '')
    || (kind === 'audio' ? 'Áudio' : 'Vídeo');
}

function validSourceId(value) {
  if (typeof value !== 'string') return false;
  const parts = /^c([1-9][0-9]{0,15}):d([1-9][0-9]{0,15})$/.exec(value);
  return parts !== null && Number.isSafeInteger(Number(parts[1])) && Number.isSafeInteger(Number(parts[2]));
}

export function normalizeSnapshot(raw) {
  if (!exactKeys(raw, ['type', 'pageUrl', 'candidates', 'truncated']) || raw.type !== 'snapshot'
    || !Array.isArray(raw.candidates) || typeof raw.truncated !== 'boolean' || !withinBytes(raw, 1024 * 1024)) {
    throw protocolError('invalid_snapshot');
  }
  const page = mediaUrl(raw.pageUrl, true);
  if (!page) throw protocolError('unsupported_page');
  const candidates = [];
  const seen = new Set();
  for (const item of raw.candidates.slice(0, MAX_CANDIDATES)) {
    if (!exactKeys(item, ['url', 'title', 'kind', 'sourceId']) || typeof item.title !== 'string'
      || (item.kind !== 'audio' && item.kind !== 'video') || !validSourceId(item.sourceId)) {
      throw protocolError('invalid_snapshot');
    }
    const url = mediaUrl(item.url);
    if (!url || seen.has(url.href)) continue;
    seen.add(url.href);
    candidates.push({ url: url.href, title: cleanTitle(item.title, item.kind), kind: item.kind,
      sourceId: item.sourceId, origin: url.origin });
  }
  return { pageUrl: page.href, candidates, truncated: raw.truncated || raw.candidates.length > MAX_CANDIDATES };
}

function rawSnapshot(snapshot) {
  if (snapshot !== null && typeof snapshot === 'object' && Object.hasOwn(snapshot, 'type')) return snapshot;
  if (!exactKeys(snapshot, ['pageUrl', 'candidates', 'truncated']) || !Array.isArray(snapshot.candidates)) {
    throw protocolError('invalid_snapshot');
  }
  return { type: 'snapshot', pageUrl: snapshot.pageUrl, truncated: snapshot.truncated,
    candidates: Array.from(snapshot.candidates, item => {
      if (!exactKeys(item, ['url', 'title', 'kind', 'sourceId', 'origin'])) throw protocolError('invalid_snapshot');
      const { url, title, kind, sourceId } = item;
      return { url, title, kind, sourceId };
    }) };
}

function validSelection(selectedUrls) {
  if (!Array.isArray(selectedUrls) || selectedUrls.length === 0 || selectedUrls.length > MAX_BATCH) return false;
  for (const url of selectedUrls) {
    if (typeof url !== 'string') return false;
  }
  return new Set(selectedUrls).size === selectedUrls.length;
}

function uniqueSelectedUrls(raw, selectedUrls) {
  const counts = new Map(selectedUrls.map(url => [url, 0]));
  for (const item of raw.candidates) {
    const url = mediaUrl(item?.url);
    if (url && counts.has(url.href)) counts.set(url.href, counts.get(url.href) + 1);
  }
  return selectedUrls.every(url => counts.get(url) === 1);
}

export function isSelectionCurrent(snapshot, fresh, selectedUrls) {
  try {
    if (!validSelection(selectedUrls) || snapshot?.pageUrl !== fresh?.pageUrl) return false;
    const previousRaw = rawSnapshot(snapshot);
    const currentRaw = rawSnapshot(fresh);
    const previous = normalizeSnapshot(previousRaw);
    const current = normalizeSnapshot(currentRaw);
    if (previous.pageUrl !== current.pageUrl || !uniqueSelectedUrls(previousRaw, selectedUrls)
      || !uniqueSelectedUrls(currentRaw, selectedUrls)) return false;
    return selectedUrls.every(url => {
      const before = previous.candidates.find(item => item.url === url);
      const after = current.candidates.find(item => item.url === url);
      return before !== undefined && after !== undefined
        && ['url', 'title', 'kind', 'sourceId'].every(field => before[field] === after[field]);
    });
  } catch {
    return false;
  }
}

export function buildDownloadRequest(snapshot, selectedUrls, requestId) {
  if (!validSelection(selectedUrls) || !Array.isArray(snapshot?.candidates)) {
    throw protocolError('invalid_selection');
  }
  const selected = new Set(selectedUrls);
  const clean = normalizeSnapshot(rawSnapshot(snapshot));
  const items = clean.candidates.filter(item => selected.has(item.url)).map(({ url, title, kind }) => ({ url, title, kind }));
  if (items.length !== selected.size) throw protocolError('invalid_selection');
  return validateDownloadRequest({ schemaVersion: 1, type: 'download.request', requestId, items });
}

export function validateDownloadRequest(raw) {
  if (!exactKeys(raw, ['schemaVersion', 'type', 'requestId', 'items']) || raw.schemaVersion !== 1
    || raw.type !== 'download.request' || typeof raw.requestId !== 'string' || !uuid.test(raw.requestId)
    || !Array.isArray(raw.items) || raw.items.length === 0 || raw.items.length > MAX_BATCH
    || !withinBytes(raw, 256 * 1024)) throw protocolError('invalid_request');
  const seen = new Set();
  const items = Array.from(raw.items, item => {
    if (!exactKeys(item, ['url', 'title', 'kind']) || typeof item.title !== 'string'
      || !item.title || item.title.length > MAX_TITLE || (item.kind !== 'audio' && item.kind !== 'video')) {
      throw protocolError('invalid_request');
    }
    const url = mediaUrl(item.url);
    if (!url || url.href !== item.url || cleanTitle(item.title, item.kind) !== item.title || seen.has(item.url)) {
      throw protocolError('invalid_request');
    }
    seen.add(item.url);
    return { url: item.url, title: item.title, kind: item.kind };
  });
  return { schemaVersion: 1, type: 'download.request', requestId: raw.requestId, items };
}
