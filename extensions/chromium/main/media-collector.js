function rcMusicCollectMedia(document, options) {
  "use strict";
  options ??= {};
  const maxCandidates = 100;
  const maxElements = 2000;
  const maxRoots = 64;
  const maxDocuments = 32;
  const maxUrlLength = 8192;
  const maxTitleLength = 200;
  const maxSnapshotBytes = 1024 * 1024;
  const encoder = new TextEncoder();
  let pageUrl;
  try { pageUrl = options.pageUrl ?? document.location?.href ?? document.URL; } catch { /* Unavailable document. */ }
  if (typeof pageUrl !== "string" || !pageUrl.length || pageUrl.length > maxUrlLength) {
    return { pageUrl: "", candidates: [], truncated: true };
  }
  const snapshot = { pageUrl, candidates: [], truncated: false };
  const seenUrls = new Set();
  const seenRoots = new Set();
  const documents = new Map();
  const roots = [];
  let inspected = 0;
  // The extension adds a fixed protocol envelope outside this pure collector.
  const overhead = Number.isInteger(options.byteOverhead) && options.byteOverhead >= 0 && options.byteOverhead <= 1024
    ? options.byteOverhead : 0;
  let byteLength = encoder.encode(JSON.stringify(snapshot)).length + overhead;

  function documentUrl(owner) {
    try {
      const value = owner.location?.href ?? owner.URL;
      if (typeof value !== "string" || !value.length) return null;
      if (value.length > maxUrlLength) { snapshot.truncated = true; return null; }
      return value;
    } catch {
      return null;
    }
  }

  function enqueue(root, owner) {
    if (!root || seenRoots.has(root)) return;
    if (seenRoots.size >= maxRoots) { snapshot.truncated = true; return; }
    if (!documents.has(owner)) {
      if (documents.size >= maxDocuments) { snapshot.truncated = true; return; }
      const href = documentUrl(owner);
      if (href === null) return;
      let baseURI;
      try { baseURI = owner.baseURI; } catch { return; }
      if (typeof baseURI !== "string" || !baseURI.length) return;
      if (baseURI.length > maxUrlLength) { snapshot.truncated = true; return; }
      const context = { baseURI };
      if (typeof options.documentIdentity === "function") {
        const sourceId = options.documentIdentity(owner, href);
        if (typeof sourceId !== "string" || !sourceId.length || sourceId.length > 64) {
          snapshot.truncated = true;
          return;
        }
        context.sourceId = sourceId;
      }
      documents.set(owner, context);
    }
    seenRoots.add(root);
    roots.push({ root, owner, ...documents.get(owner) });
  }

  function normalizeUrl(value, context) {
    if (typeof value !== "string" || !value.length) return null;
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

  function titleText(raw, kind) {
    if (raw.length > maxTitleLength) snapshot.truncated = true;
    if (options.normalizeTitles === false) {
      // Preserve the extension's raw snapshot contract; its policy normalizes
      // this title before displaying it or creating a native request.
      return raw.slice(0, maxTitleLength).replace(/[\uD800-\uDBFF]$/u, "")
        .replace(/[\u0000-\u001f\u007f]/gu, " ").trim();
    }
    let wellFormed = "";
    // Include one extra unit so a pair crossing the length boundary is
    // truncated together instead of being mistaken for an invalid surrogate.
    for (const character of raw.slice(0, maxTitleLength + 1)) {
      const unit = character.charCodeAt(0);
      wellFormed += character.length === 1 && unit >= 0xd800 && unit <= 0xdfff ? "\uFFFD" : character;
    }
    return wellFormed.replace(/[\u0000-\u001f\u007f-\u009f\u202a-\u202e\u2066-\u2069]/gu, "")
      .trim().slice(0, maxTitleLength).replace(/[\uD800-\uDBFF]$/u, "") || (kind === "audio" ? "Áudio" : "Vídeo");
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
    const candidate = { url: url.href, title: titleText(rawTitle, kind), kind };
    if (context.sourceId !== undefined) candidate.sourceId = context.sourceId;
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
    if (typeof options.onObservedRoot === "function") options.onObservedRoot(context.root);
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
      if (node.shadowRoot) enqueue(node.shadowRoot, context.owner);
      // Only an explicit caller opts into accessible frame documents. The DOM
      // enforces access; all documents share this call's aggregate budgets.
      if (options.includeFrames === true && (tag === "iframe" || tag === "frame")) {
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
  return snapshot;
}

// Native ExecuteScript invokes this entry inside its local IIFE. Keep its
// discovery policy in the shared asset exercised by the DOM contract tests.
function rcMusicCollectNativeMedia(document) {
  return rcMusicCollectMedia(document, { includeFrames: true });
}
