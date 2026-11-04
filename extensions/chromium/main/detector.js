(() => {
  "use strict";

  // This property exists only in the extension's ISOLATED world. It exposes
  // bounded, fresh data while an authorized popup owns a detection session.
  const sentinel = "__rcMusicDetectionV2";
  if (globalThis[sentinel]) return;
  const maxUrlLength = 8192;
  if (typeof rcMusicCollectMedia !== "function") return;
  const collector = rcMusicCollectMedia;
  let generation = 0;
  let current = null;

  function collect(session) {
    const observed = [];
    const snapshot = collector(document, {
      pageUrl: session.pageUrl, includeFrames: true, normalizeTitles: false,
      // `type: "snapshot"` adds 18 bytes to the pure collector's envelope.
      byteOverhead: 18,
      documentIdentity(owner, href) {
        let identity = session.identities.get(owner);
        if (!identity || identity.href !== href) {
          if (session.nextDocument >= Number.MAX_SAFE_INTEGER) return null;
          identity = { href, id: ++session.nextDocument };
          session.identities.set(owner, identity);
        }
        return "c" + session.generation + ":d" + identity.id;
      },
      onObservedRoot(root) { observed.push(root); },
    });
    session.synchronize(observed);
    return { type: "snapshot", ...snapshot };
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
