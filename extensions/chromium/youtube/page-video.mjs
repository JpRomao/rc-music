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

// Serialized by chrome.scripting in the isolated world. Only consume explicit
// recording metadata tied to this video; general video authors are uploaders.
export function collectYoutubeMusicMetadata(document = globalThis.document) {
  try {
    const videoId = raw => {
      if (typeof raw !== 'string' || raw.length > 8192 || /[\s\u0000-\u001f\u007f-\u009f\\]/u.test(raw)) return '';
      const matched = /^https:\/\/([^/?#]+)([^?#]*)/iu.exec(raw);
      if (!matched) return '';
      let url;
      try { url = new URL(raw); } catch { return ''; }
      if (matched[1].includes('@') || url.port) return '';
      const path = matched[2];
      let id;
      if (['youtube.com', 'www.youtube.com', 'm.youtube.com'].includes(url.hostname)) {
        if (path === '/watch' && url.searchParams.getAll('v').length === 1) id = url.searchParams.get('v');
        else id = /^\/(?:shorts|embed)\/([a-zA-Z0-9_-]{11})\/?$/u.exec(path)?.[1];
      } else if (['youtu.be', 'www.youtu.be'].includes(url.hostname)) {
        id = /^\/([a-zA-Z0-9_-]{11})\/?$/u.exec(path)?.[1];
      }
      return /^[a-zA-Z0-9_-]{11}$/u.test(id ?? '') ? id : '';
    };
    const pageUrl = document?.location?.href;
    const selectedVideo = videoId(pageUrl);
    if (!selectedVideo) return null;
    const empty = { pageUrl };
    const type = (entry, name) => [entry?.['@type']].flat().some(value => value === name
      || value === 'https://schema.org/' + name || value === 'http://schema.org/' + name);
    const text = value => typeof value === 'string' && value.length <= 2048 ? value.trim() : '';
    const attached = (...urls) => urls.some(value => videoId(value) === selectedVideo);
    const entries = [];
    const recording = entry => {
      const performers = [entry.byArtist].flat().filter(value => value !== undefined && value !== null);
      const artist = performers.length === 1 ? text(performers[0]?.name) : '';
      entries.push({ title: text(entry.name), artist });
    };

    const scripts = document.querySelectorAll('script[type="application/ld+json"]');
    if (scripts.length > 8) return empty;
    let remaining = 131072;
    for (const script of scripts) {
      const content = script.textContent ?? '';
      if (content.length > 65536 || (remaining -= content.length) < 0) return empty;
      let parsed;
      try { parsed = JSON.parse(content); } catch { continue; }
      const root = [parsed].flat();
      if (root.length > 32) return empty;
      for (const value of root) {
        const graph = Array.isArray(value?.['@graph']) ? value['@graph'] : [];
        if (graph.length > 32) return empty;
        for (const entry of [value, ...graph]) {
          if (type(entry, 'MusicRecording') && attached(entry.url, entry['@id'], entry.mainEntityOfPage,
            entry.mainEntityOfPage?.['@id'])) recording(entry);
          if (!type(entry, 'VideoObject') || !attached(entry.url, entry.embedUrl, entry['@id'])) continue;
          const audio = [entry.audio].flat().filter(value => type(value, 'MusicRecording'));
          if (audio.length > 1) return empty;
          if (audio.length === 1 && (!audio[0].url || videoId(audio[0].url) === selectedVideo)) recording(audio[0]);
        }
      }
    }

    const tags = document.querySelectorAll('meta[property="og:type"], meta[property="og:title"], meta[property="og:url"], meta[property="music:musician"]');
    if (tags.length > 16) return empty;
    const values = property => [...new Set([...tags].filter(tag => tag.getAttribute('property') === property)
      .map(tag => text(tag.getAttribute('content'))).filter(Boolean))];
    const types = values('og:type'), urls = values('og:url'), titles = values('og:title');
    if (types.length === 1 && types[0] === 'music.song' && urls.length === 1 && videoId(urls[0]) === selectedVideo) {
      if (titles.length > 1) return empty;
      const musicians = values('music:musician');
      const artist = musicians.length === 1 && !/^(?:[a-z][a-z0-9+.-]*:|\/)/iu.test(musicians[0]) ? musicians[0] : '';
      entries.push({ title: titles[0] ?? '', artist });
    }

    const [names, artists] = ['title', 'artist'].map(key => [...new Set(entries.map(entry => entry[key]).filter(Boolean))]);
    if (names.length > 1 || artists.length > 1) return empty;
    // A title and a performer supplied by separate, incomplete records may
    // describe different songs in the same video.
    if (names.length && artists.length && !entries.some(entry => entry.title && entry.artist)) return empty;
    return { pageUrl, ...(names.length ? { title: names[0] } : {}), ...(artists.length ? { artist: artists[0] } : {}) };
  } catch {
    return null;
  }
}
