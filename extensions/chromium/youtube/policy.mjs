export const YOUTUBE_NATIVE_HOST = 'com.rcmusic.youtube';
export function protocolError(code) { return Object.assign(new Error(code), { code }); }

const MAX_ITEMS = 20;
const MAX_MESSAGE_BYTES = 64 * 1024;
const uuid = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i;
const videoId = /^[a-zA-Z0-9_-]{11}$/;
const metadataControls = /[\u0000-\u001f\u007f-\u009f\u2028\u2029\u202a-\u202e\u2066-\u2069]/u;
const urlControls = /[\s\u0000-\u0020\u007f-\u009f\\]/u;
const encoder = new TextEncoder();

function scalarText(value) {
  if (typeof value !== 'string') return false;
  for (const character of value) {
    const unit = character.charCodeAt(0);
    if (character.length === 1 && unit >= 0xd800 && unit <= 0xdfff) return false;
  }
  return true;
}
function exactKeys(value, keys) {
  return value !== null && typeof value === 'object' && !Array.isArray(value)
    && Object.keys(value).length === keys.length && keys.every(key => Object.hasOwn(value, key));
}
function validMetadata(value, optional = false) {
  return typeof value === 'string' && value.length <= 512 && scalarText(value) && encoder.encode(value).byteLength <= 512 && value.trim() === value
    && (optional || value.length > 0) && !metadataControls.test(value);
}
function parsedHttps(raw, maximumBytes) {
  if (typeof raw !== 'string' || raw.length > maximumBytes || !scalarText(raw) || encoder.encode(raw).byteLength > maximumBytes || urlControls.test(raw)
    || !/^https:\/\//i.test(raw) || /%(?![a-f0-9]{2})/i.test(raw)) return null;
  const authority = /^https:\/\/([^/?#]*)/i.exec(raw)?.[1];
  if (!authority || authority.includes('@')) return null;
  try {
    const url = new URL(raw);
    return url.username || url.password || url.port ? null : url;
  } catch {
    return null;
  }
}
function validLyricsPage(raw) {
  if (raw === '') return true;
  const url = parsedHttps(raw, 2048);
  if (!url || !['letras.mus.br', 'www.letras.mus.br'].includes(url.hostname.toLowerCase())
    || raw.includes('?') || raw.includes('#')) return false;
  const path = /^https:\/\/[^/?#]+(\/[^?#]*)$/i.exec(raw)?.[1];
  if (!path || path === '/' || /%(?:2f|5c|25)/i.test(path)) return false;
  const segments = path.slice(1).split('/');
  if (segments.at(-1) === '') segments.pop();
  return segments.length > 0 && segments.every(segment => {
    if (!segment) return false;
    try {
      const decoded = decodeURIComponent(segment);
      return decoded !== '.' && decoded !== '..' && /^[\p{L}\p{N}_.~-]+$/u.test(decoded);
    } catch {
      return false;
    }
  });
}
function safeMetadata(raw, fallback = 'Vídeo YouTube') {
  let title = typeof raw === 'string' ? raw.slice(0, 2048) : '';
  title = [...title].filter(character => scalarText(character) && !metadataControls.test(character)).join('').trim();
  let result = '';
  for (const character of title) {
    if (encoder.encode(result + character).byteLength > 512) break;
    result += character;
  }
  return result.trim() || fallback;
}
export function videoFromTab(tab) {
  if (!tab || !Number.isSafeInteger(tab.id) || tab.id < 0) throw protocolError('unsupported_page');
  const url = parsedHttps(tab.url, 8192);
  if (!url) throw protocolError('unsupported_page');
  const host = url.hostname.toLowerCase();
  // Read the original path so URL normalization cannot disguise dot/encoded paths.
  const path = /^https:\/\/[^/?#]+([^?#]*)/i.exec(tab.url)?.[1] || '/';
  let id;
  if (['youtube.com', 'www.youtube.com', 'm.youtube.com'].includes(host)) {
    if (path === '/watch' && url.searchParams.getAll('v').length === 1) id = url.searchParams.get('v');
    else id = /^\/(?:shorts|embed)\/([a-zA-Z0-9_-]{11})\/?$/u.exec(path)?.[1];
  } else if (['youtu.be', 'www.youtu.be'].includes(host)) {
    id = /^\/([a-zA-Z0-9_-]{11})\/?$/u.exec(path)?.[1];
  }
  if (!id || !videoId.test(id)) throw protocolError('unsupported_page');
  const tabTitle = typeof tab.title === 'string' ? tab.title.slice(0, 2048).replace(/ - YouTube$/u, '') : '';
  return { tabId: tab.id, videoId: id, url: `https://www.youtube.com/watch?v=${id}`, title: safeMetadata(tabTitle) };
}
export function isLetrasVideoPage(raw) {
  const url = parsedHttps(raw, 2048);
  return Boolean(url && validLyricsPage(raw) && url.pathname.slice(1).replace(/\/$/u, '').split('/').length === 2);
}
export function videoFromLetrasPage(tab, snapshot) {
  const keys = ['pageUrl', 'videoId', 'title'];
  if (snapshot && Object.hasOwn(snapshot, 'artist')) keys.push('artist');
  if (!tab || !Number.isSafeInteger(tab.id) || tab.id < 0 || !isLetrasVideoPage(tab.url)
    || !exactKeys(snapshot, keys) || snapshot.pageUrl !== tab.url
    || typeof snapshot.videoId !== 'string' || !videoId.test(snapshot.videoId)
    || typeof snapshot.title !== 'string' || snapshot.title.length > 2048
    || (keys.includes('artist') && (typeof snapshot.artist !== 'string' || snapshot.artist.length > 2048)))
    throw protocolError('unsupported_page');
  return { tabId: tab.id, videoId: snapshot.videoId, url: `https://www.youtube.com/watch?v=${snapshot.videoId}`,
    title: safeMetadata(snapshot.title), artist: safeMetadata(snapshot.artist, ''), lyricsPage: tab.url };
}
export function validateYoutubeAudioRequest(raw) {
  if (!exactKeys(raw, ['schemaVersion', 'type', 'requestId', 'items']) || raw.schemaVersion !== 1
    || raw.type !== 'youtube.audio.request' || typeof raw.requestId !== 'string' || !uuid.test(raw.requestId)
    || !Array.isArray(raw.items) || raw.items.length === 0) throw protocolError('invalid_request');
  if (raw.items.length > MAX_ITEMS) throw protocolError('limit_exceeded');
  const seen = new Set();
  const items = raw.items.map(item => {
    const keys = ['videoId', 'title', 'artist', 'lyricsPage', 'fetchLyrics'];
    if (item && Object.hasOwn(item, 'fileName')) keys.push('fileName');
    if (!exactKeys(item, keys)
      || typeof item.videoId !== 'string' || !videoId.test(item.videoId) || seen.has(item.videoId)
      || !validMetadata(item.title) || !validMetadata(item.artist, true)
      || typeof item.lyricsPage !== 'string' || !validLyricsPage(item.lyricsPage)
      || typeof item.fetchLyrics !== 'boolean' || (keys.includes('fileName') && !validMetadata(item.fileName)))
      throw protocolError('invalid_request');
    seen.add(item.videoId);
    return { videoId: item.videoId, title: item.title, artist: item.artist, lyricsPage: item.lyricsPage,
      fetchLyrics: item.fetchLyrics, ...(keys.includes('fileName') ? { fileName: item.fileName } : {}) };
  });
  const result = { schemaVersion: 1, type: 'youtube.audio.request', requestId: raw.requestId, items };
  if (encoder.encode(JSON.stringify(result)).byteLength > MAX_MESSAGE_BYTES) throw protocolError('limit_exceeded');
  return result;
}
export function buildYoutubeAudioRequest(candidate, fields, requestId) {
  if (!candidate || !Number.isSafeInteger(candidate.tabId) || candidate.tabId < 0) throw protocolError('invalid_request');
  const fileName = typeof fields.fileName === 'string' ? fields.fileName.trim() : fields.fileName;
  return validateYoutubeAudioRequest({ schemaVersion: 1, type: 'youtube.audio.request', requestId,
    items: [{ videoId: candidate.videoId, title: fields.title, artist: fields.artist, lyricsPage: fields.lyricsPage,
      fetchLyrics: fields.fetchLyrics, ...(fileName === undefined || fileName === '' ? {} : { fileName }) }] });
}
