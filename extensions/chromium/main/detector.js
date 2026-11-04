(() => {
  "use strict";

  // This property exists only in the extension's ISOLATED world. It exposes
  // bounded, fresh data while an authorized popup owns a detection session.
  const sentinel = "__rcMusicDetectionV2";
  if (globalThis[sentinel]) return;
  const maxCandidates = 100;
  const maxElements = 2000;
  const maxRoots = 64;
  const maxDocuments = 32;
  const maxUrlLength = 8192;
  const maxTitleLength = 200;
  const maxSnapshotBytes = 1024 * 1024;
  const encoder = new TextEncoder();
  let generation = 0;
  let current = null;

  function documentUrl(owner, snapshot) {
    try {
      const value = owner.location?.href ?? owner.URL;
      if (typeof value !== "string" || value.length === 0) return null;
      if (value.length > maxUrlLength) { snapshot.truncated = true; return null; }
      return value;
    } catch {
      return null;
    }
  }

  function collect(session) {
    const snapshot = { type: "snapshot", pageUrl: session.pageUrl, candidates: [], truncated: false };
    const seenUrls = new Set();
    const seenRoots = new Set();
    const documents = new Map();
    const roots = [];
    const observed = [];
    let inspected = 0;
    let byteLength = encoder.encode(JSON.stringify(snapshot)).length;

    function enqueue(root, owner) {
      if (!root || seenRoots.has(root)) return;
      if (seenRoots.size >= maxRoots) { snapshot.truncated = true; return; }
      if (!documents.has(owner)) {
        if (documents.size >= maxDocuments) { snapshot.truncated = true; return; }
        const href = documentUrl(owner, snapshot);
        if (href === null) return;
        let baseURI;
        try { baseURI = owner.baseURI; } catch { return; }
        if (typeof baseURI !== "string" || baseURI.length === 0) return;
        if (baseURI.length > maxUrlLength) { snapshot.truncated = true; return; }
        let identity = session.identities.get(owner);
        if (!identity || identity.href !== href) {
          if (session.nextDocument >= Number.MAX_SAFE_INTEGER) { snapshot.truncated = true; return; }
          identity = { href, id: ++session.nextDocument };
          session.identities.set(owner, identity);
        }
        documents.set(owner, { sourceId: "c" + session.generation + ":d" + identity.id, baseURI });
      }
      seenRoots.add(root);
      roots.push({ root, owner, ...documents.get(owner) });
    }

    function normalizeUrl(value, context) {
      if (typeof value !== "string" || value.length === 0) return null;
      if (value.length > maxUrlLength) { snapshot.truncated = true; return null; }
      if (/[\u0000-\u001f\u007f]/u.test(value)) return null;
      try {
        const url = new URL(value, context.baseURI);
        if (!["https:", "http:"].includes(url.protocol) || url.username || url.password) return null;
        url.hash = "";
        if (url.href.length > maxUrlLength) { snapshot.truncated = true; return null; }
        return url;
      } catch {
        return null;
      }
    }

    function add(value, kind, node, context, directLink = false) {
      const url = normalizeUrl(value, context);
      if (!url) return;
      if (directLink) {
        const extension = /\.(mp3|mp4|m4a)$/iu.exec(url.pathname)?.[1].toLowerCase();
        if (!extension) return;
        kind = extension === "mp4" ? "video" : "audio";
      }
      if (seenUrls.has(url.href)) return;
      if (snapshot.candidates.length >= maxCandidates) { snapshot.truncated = true; return; }
      const rawTitle = node.getAttribute("title") || node.getAttribute("aria-label") || context.owner.title || "";
      if (rawTitle.length > maxTitleLength) snapshot.truncated = true;
      const title = rawTitle.slice(0, maxTitleLength).replace(/[\uD800-\uDBFF]$/u, "")
        .replace(/[\u0000-\u001f\u007f]/gu, " ").trim();
      const candidate = { url: url.href, title, kind, sourceId: context.sourceId };
      const additionalBytes = encoder.encode(JSON.stringify(candidate)).length + (snapshot.candidates.length ? 1 : 0);
      if (byteLength + additionalBytes > maxSnapshotBytes) { snapshot.truncated = true; return; }
      snapshot.candidates.push(candidate);
      seenUrls.add(url.href);
      byteLength += additionalBytes;
    }

    enqueue(document, document);
    for (let index = 0; index < roots.length; index += 1) {
      const context = roots[index];
      const walker = context.owner.createTreeWalker(context.root, NodeFilter.SHOW_ELEMENT);
      observed.push(context.root);
      let node;
      while ((node = walker.nextNode())) {
        if (inspected >= maxElements) { snapshot.truncated = true; break; }
        inspected += 1;
        const tag = node.tagName.toLowerCase();
        if (tag === "audio" || tag === "video") {
          add(node.currentSrc, tag, node, context);
          add(node.getAttribute("src"), tag, node, context);
        } else if (tag === "source") {
          const parent = node.parentElement;
          const parentTag = parent?.tagName.toLowerCase();
          if (parentTag === "audio" || parentTag === "video") add(node.getAttribute("src"), parentTag, parent, context);
        } else if (tag === "a") {
          add(node.getAttribute("href"), "audio", node, context, true);
        }
        // Closed roots are not exposed through shadowRoot. contentDocument is
        // used only when the browser permits access; no allFrames/MAIN injection.
        if (node.shadowRoot) enqueue(node.shadowRoot, context.owner);
        if (tag === "iframe" || tag === "frame") {
          try {
            const child = node.contentDocument;
            if (child) enqueue(child, child);
          } catch { /* Cross-origin or sandbox-inaccessible document. */ }
        }
      }
      if (inspected >= maxElements && index + 1 < roots.length) {
        snapshot.truncated = true;
        break;
      }
    }
    session.synchronize(observed);
    return snapshot;
  }

  Object.defineProperty(globalThis, sentinel, { value: Object.freeze({
    snapshot() { return current?.snapshot() ?? null; },
  }) });

  chrome.runtime.onConnect.addListener((port) => {
    if (port.name !== "rc-music-detection-v2" || port.sender?.id !== chrome.runtime.id
      || port.sender.url !== chrome.runtime.getURL("popup.html") || "tab" in port.sender) return;

    current?.stop();
    if (generation >= Number.MAX_SAFE_INTEGER) {
      try { port.disconnect(); } catch { /* Already disconnected. */ }
      return;
    }
    const pageUrl = location.href;
    const session = {
      generation: ++generation, pageUrl, identities: new WeakMap(), nextDocument: 0,
      snapshot: freshSnapshot, stop, synchronize,
    };
    let active = true;
    let pendingScan = null;
    let periodicTimer = null;
    let lastSnapshot = null;
    const observations = new Map();

    function retire(root, observation) {
      observation.disconnect();
      root.removeEventListener("loadedmetadata", scheduleScan, true);
      root.removeEventListener("durationchange", scheduleScan, true);
      root.removeEventListener("load", scheduleScan, true);
    }

    function synchronize(roots) {
      const retained = new Set(roots);
      for (const [root, observation] of observations) {
        if (!retained.has(root)) { retire(root, observation); observations.delete(root); }
      }
      for (const root of roots) {
        if (observations.has(root)) continue;
        const observation = new MutationObserver(scheduleScan);
        // Track before setup so failure releases partially acquired resources.
        observations.set(root, observation);
        observation.observe(root, {
          subtree: true, childList: true, attributes: true, characterData: true,
          attributeFilter: ["src", "href", "title", "aria-label"],
        });
        root.addEventListener("loadedmetadata", scheduleScan, true);
        root.addEventListener("durationchange", scheduleScan, true);
        root.addEventListener("load", scheduleScan, true);
      }
    }

    function stop(disconnect = true) {
      if (!active) return;
      active = false;
      clearTimeout(pendingScan);
      clearInterval(periodicTimer);
      for (const [root, observation] of observations) retire(root, observation);
      observations.clear();
      window.removeEventListener("popstate", checkNavigation);
      window.removeEventListener("hashchange", checkNavigation);
      window.removeEventListener("pagehide", close);
      port.onDisconnect.removeListener(onDisconnect);
      if (current === session) current = null;
      lastSnapshot = null;
      if (disconnect) {
        try { port.disconnect(); } catch { /* Already disconnected. */ }
      }
    }

    function close() { stop(); }
    function onDisconnect() { stop(false); }
    function checkNavigation() {
      if (active && location.href !== pageUrl) stop();
      return active;
    }

    function freshSnapshot() {
      if (!checkNavigation()) return null;
      try {
        const snapshot = collect(session);
        return checkNavigation() ? snapshot : null;
      } catch {
        stop();
        return null;
      }
    }

    function scan() {
      if (pendingScan !== null) clearTimeout(pendingScan);
      pendingScan = null;
      const snapshot = freshSnapshot();
      if (!snapshot) return;
      try {
        const serialized = JSON.stringify(snapshot);
        if (serialized !== lastSnapshot) {
          port.postMessage(snapshot);
          lastSnapshot = serialized;
        }
      } catch { stop(); }
    }

    function scheduleScan() {
      if (!checkNavigation()) return;
      // Preserve the first deadline even under continuous DOM mutations.
      if (pendingScan === null) pendingScan = setTimeout(scan, 200);
    }

    current = session;
    port.onDisconnect.addListener(onDisconnect);
    if (typeof pageUrl !== "string" || pageUrl.length > maxUrlLength) { stop(); return; }
    window.addEventListener("popstate", checkNavigation);
    window.addEventListener("hashchange", checkNavigation);
    window.addEventListener("pagehide", close);
    // Bounded periodic discovery sees late attachShadow and child navigation
    // without patching page APIs or retaining observation while popup is closed.
    periodicTimer = setInterval(() => {
      // An already scheduled mutation scan is due within 200 ms. Do not reset
      // its first deadline or insert an intermediate snapshot before it.
      if (pendingScan === null) scan();
    }, 500);
    scan();
  });
})();
