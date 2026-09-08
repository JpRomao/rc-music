(() => {
  "use strict";

  // The isolated world survives popup reopens; only its runtime listener persists.
  const sentinel = "__rcMusicDetectionV1";
  if (globalThis[sentinel]) return;
  Object.defineProperty(globalThis, sentinel, { value: true });

  const maxCandidates = 100;
  const maxElements = 2000;
  const maxUrlLength = 8192;
  const maxTitleLength = 200;
  const maxSnapshotBytes = 1024 * 1024;
  const encoder = new TextEncoder();
  let closeCurrent = null;

  function collect(pageUrl) {
    const snapshot = { type: "snapshot", pageUrl, candidates: [], truncated: false };
    const seen = new Set();
    let byteLength = encoder.encode(JSON.stringify(snapshot)).length;

    function normalizeUrl(value) {
      if (typeof value !== "string" || value.length === 0) return null;
      if (value.length > maxUrlLength) {
        snapshot.truncated = true;
        return null;
      }
      if (/[\u0000-\u001f\u007f]/u.test(value)) return null;
      try {
        const url = new URL(value, document.baseURI);
        if (!["https:", "http:"].includes(url.protocol) || url.username || url.password) return null;
        url.hash = "";
        if (url.href.length > maxUrlLength) {
          snapshot.truncated = true;
          return null;
        }
        return url;
      } catch {
        return null;
      }
    }

    function add(value, kind, owner, directLink = false) {
      const url = normalizeUrl(value);
      if (!url) return;
      if (directLink) {
        const extension = /\.(mp3|mp4|m4a)$/iu.exec(url.pathname)?.[1].toLowerCase();
        if (!extension) return;
        kind = extension === "mp4" ? "video" : "audio";
      }
      if (seen.has(url.href)) return;
      if (snapshot.candidates.length >= maxCandidates) {
        snapshot.truncated = true;
        return;
      }
      const rawTitle = owner.getAttribute("title") || owner.getAttribute("aria-label") || document.title || "";
      if (rawTitle.length > maxTitleLength) snapshot.truncated = true;
      const title = rawTitle.slice(0, maxTitleLength).replace(/[\uD800-\uDBFF]$/u, "")
        .replace(/[\u0000-\u001f\u007f]/gu, " ").trim();
      const candidate = { url: url.href, title, kind };
      const additionalBytes = encoder.encode(JSON.stringify(candidate)).length + (snapshot.candidates.length ? 1 : 0);
      if (byteLength + additionalBytes > maxSnapshotBytes) {
        snapshot.truncated = true;
        return;
      }
      snapshot.candidates.push(candidate);
      seen.add(url.href);
      byteLength += additionalBytes;
    }

    // A bounded walk does not first allocate an unbounded querySelectorAll result.
    const walker = document.createTreeWalker(document, NodeFilter.SHOW_ELEMENT);
    let inspected = 0;
    let node;
    while ((node = walker.nextNode())) {
      if (inspected >= maxElements) {
        snapshot.truncated = true;
        break;
      }
      inspected += 1;
      const tag = node.tagName.toLowerCase();
      if (tag === "audio" || tag === "video") {
        add(node.currentSrc, tag, node);
        add(node.getAttribute("src"), tag, node);
      } else if (tag === "source") {
        const parent = node.parentElement;
        const parentTag = parent?.tagName.toLowerCase();
        if (parentTag === "audio" || parentTag === "video") add(node.getAttribute("src"), parentTag, parent);
      } else if (tag === "a") {
        add(node.getAttribute("href"), "audio", node, true);
      }
    }
    return snapshot;
  }

  chrome.runtime.onConnect.addListener((port) => {
    if (port.name !== "rc-music-detection-v1" || port.sender?.id !== chrome.runtime.id
      || port.sender.url !== chrome.runtime.getURL("popup.html") || "tab" in port.sender) return;

    closeCurrent?.();
    const pageUrl = location.href;
    let active = true;
    let pendingScan = null;
    let navigationTimer = null;
    let observer = null;
    let lastSnapshot = null;

    function stop(disconnect = true) {
      if (!active) return;
      active = false;
      clearTimeout(pendingScan);
      clearInterval(navigationTimer);
      observer?.disconnect();
      document.removeEventListener("loadedmetadata", scheduleScan, true);
      document.removeEventListener("durationchange", scheduleScan, true);
      window.removeEventListener("popstate", checkNavigation);
      window.removeEventListener("hashchange", checkNavigation);
      window.removeEventListener("pagehide", close);
      port.onDisconnect.removeListener(onDisconnect);
      if (closeCurrent === close) closeCurrent = null;
      lastSnapshot = null;
      if (disconnect) {
        try { port.disconnect(); } catch { /* The browser may already have closed the channel. */ }
      }
    }

    function close() { stop(); }
    function onDisconnect() { stop(false); }

    function checkNavigation() {
      if (active && location.href !== pageUrl) stop();
      return active;
    }

    function scan() {
      pendingScan = null;
      if (!checkNavigation()) return;
      try {
        const snapshot = collect(pageUrl);
        if (!checkNavigation()) return;
        const serialized = JSON.stringify(snapshot);
        if (serialized !== lastSnapshot) {
          port.postMessage(snapshot);
          lastSnapshot = serialized;
        }
      } catch {
        stop();
      }
    }

    function scheduleScan() {
      if (!checkNavigation()) return;
      // Keep the first deadline: continuous DOM updates must not starve detection.
      if (pendingScan === null) pendingScan = setTimeout(scan, 200);
    }

    closeCurrent = close;
    port.onDisconnect.addListener(onDisconnect);
    if (pageUrl.length > maxUrlLength) {
      stop();
      return;
    }
    try {
      observer = new MutationObserver(scheduleScan);
      observer.observe(document, {
        subtree: true, childList: true, attributes: true, characterData: true,
        attributeFilter: ["src", "href", "title", "aria-label"],
      });
      document.addEventListener("loadedmetadata", scheduleScan, true);
      document.addEventListener("durationchange", scheduleScan, true);
      window.addEventListener("popstate", checkNavigation);
      window.addEventListener("hashchange", checkNavigation);
      window.addEventListener("pagehide", close);
      // pushState/replaceState have no popstate event; never patch the page's MAIN world.
      navigationTimer = setInterval(checkNavigation, 500);
      scan();
    } catch {
      stop();
    }
  });
})();
