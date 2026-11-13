// Serialized by chrome.scripting; keep all dependencies inside the function.
export function collectLetrasYoutubeVideo(document = globalThis.document) {
  try {
    const pageUrl = document?.location?.href;
    if (typeof pageUrl !== 'string' || pageUrl.length > 2048 || /[\s\\]/u.test(pageUrl)) return null;
    const page = new URL(pageUrl);
    if (page.protocol !== 'https:' || !['letras.mus.br', 'www.letras.mus.br'].includes(page.hostname)
      || page.username || page.password || page.port || page.search || page.hash || page.href !== pageUrl) return null;
    const segments = page.pathname.slice(1).replace(/\/$/u, '').split('/');
    if (segments.length !== 2 || segments.some(segment => !segment || /%(?:2f|5c|25)/iu.test(segment)
      || !/^[\p{L}\p{N}_.~-]+$/u.test(decodeURIComponent(segment)) || ['.', '..'].includes(decodeURIComponent(segment)))) return null;
    const player = document.getElementById('player');
    if (!player) return null;
    const ids = new Set(), encoder = new TextEncoder();
    const validId = /^[a-zA-Z0-9_-]{11}$/u;
    let title = '', artist = '', bytes = 0;
    const scripts = document.scripts;
    if (!scripts || scripts.length > 64) return null;
    for (const script of scripts) {
      if (script.src) continue;
      const text = script.textContent ?? '';
      if (text.length > 65536) return null;
      const size = encoder.encode(text).byteLength;
      if (size > 65536 || (bytes += size) > 1048576) return null;
      if (!/^\s*_omq\.push\(\[\s*(['"])ui\/player\1\s*,/u.test(text)) continue;
      const match = /^\s*_omq\.push\(\[\s*(['"])ui\/player\1\s*,\s*(\{[\s\S]*\})\s*\]\);\s*$/u.exec(text);
      if (!match || match[2].length > 16384) return null;
      const data = JSON.parse(match[2]);
      if (data === null || typeof data !== 'object' || Array.isArray(data)
        || data.DNS !== decodeURIComponent(segments[0]) || data.URL !== decodeURIComponent(segments[1])
        || typeof data.YoutubeID !== 'string' || (data.YoutubeID !== '' && !validId.test(data.YoutubeID))
        || typeof data.Name !== 'string' || data.Name.length > 2048 || !data.Name.trim()) return null;
      if (data.YoutubeID) ids.add(data.YoutubeID);
      title ||= data.Name;
      if (typeof data.Artist === 'string' && data.Artist.length <= 2048) artist ||= data.Artist;
    }
    const frames = player.querySelectorAll('iframe');
    if (frames.length > 8) return null;
    for (const frame of frames) {
      const source = frame.getAttribute('src');
      if (typeof source !== 'string' || source.length > 8192 || /[\s\\]/u.test(source)) continue;
      let url;
      try { url = new URL(source, pageUrl); } catch { continue; }
      if (url.protocol !== 'https:' || url.username || url.password || url.port) continue;
      if (['youtube.com', 'www.youtube.com', 'youtube-nocookie.com', 'www.youtube-nocookie.com'].includes(url.hostname)) {
        const id = /^\/embed\/([a-zA-Z0-9_-]{11})\/?$/u.exec(url.pathname)?.[1];
        if (id) ids.add(id);
        continue;
      }
      // Letras can leave YoutubeID empty and load this iframe only when its player starts.
      // The proxy's URL carries the video ID, so its cross-origin contents are unnecessary.
      if (!title || url.origin !== 'https://solr.sscdn.co'
        || url.pathname !== '/iframe_proxy_player_letras_endpoint.html' || url.hash) continue;
      const parameter = name => {
        const values = url.searchParams.getAll(name);
        return values.length === 1 ? values[0] : null;
      };
      const id = parameter('video_id'), encodedVars = parameter('player_vars');
      if (parameter('host') !== page.origin || parameter('v') !== '1'
        || parameter('api_player') !== 'Iframe' || id === null || !validId.test(id) || encodedVars === null) return null;
      const vars = JSON.parse(encodedVars);
      if (vars?.type !== 'Iframe' || vars.videoId !== id) return null;
      ids.add(id);
    }
    if (ids.size !== 1) return null;
    return { pageUrl, videoId: [...ids][0], title: title || String(document.title ?? '').slice(0, 2048),
      ...(artist ? { artist } : {}) };
  } catch {
    return null;
  }
}
