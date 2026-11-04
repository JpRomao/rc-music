import assert from "node:assert/strict";
import test from "node:test";
import { harness, element, frameDocument, shadowRoot, assertStopped, extensionId, popupUrl } from "./support/detector-harness.mjs";
test("discovers media in nested open shadow roots", () => {
  const nested = shadowRoot([element("audio", { src: "shadow-song.mp3" })]);
  const outer = shadowRoot([element("section", {}, { shadowRoot: nested })]);
  const env = harness([element("div", {}, { shadowRoot: outer })]);
  assert.deepEqual(env.port().messages[0].candidates.map((item) => item.url), ["https://cdn.example/media/shadow-song.mp3"]);
});

test("discovers accessible nested frames including inherited blank documents", () => {
  const nested = frameDocument([element("audio", { src: "inherited.mp3" })], {
    href: "about:blank", baseURI: "https://page.example/inherited/", title: "Inherited frame",
  });
  const child = frameDocument([element("iframe", {}, { contentDocument: nested })]);
  const env = harness([element("iframe", {}, { contentDocument: child })]);
  assert.deepEqual(env.port().messages[0].candidates.map((item) => item.url), ["https://page.example/inherited/inherited.mp3"]);
});

test("injection is idle and repeated injection registers one runtime listener", () => {
  const env = harness();
  env.inject();
  env.inject();
  assert.equal(env.onConnect.listeners.size, 1);
  assert.equal(env.treeReads, 0);
  assert.equal(env.observers.length, 0);
  assert.equal(env.clock.tasks.size, 0);
});

test("only the exact own popup without sender.tab can begin detection", () => {
  const env = harness([element("audio", { src: "sample.mp3" })]);
  for (const overrides of [
    { name: "other" },
    { sender: undefined },
    { sender: { id: "other", url: popupUrl } },
    { sender: { id: extensionId, url: "https://page.example/popup.html" } },
    { sender: { id: extensionId, url: `${popupUrl}?forged` } },
    { sender: { id: extensionId, url: popupUrl, tab: { id: 1 } } },
    { sender: { id: extensionId, url: popupUrl, tab: null } },
  ]) {
    assert.equal(env.port(overrides).messages.length, 0);
  }
  assert.equal(env.treeReads, 0);
  assert.equal(env.clock.tasks.size, 0);
  assert.equal(env.observers.length, 0);
});

test("discovers media sources and direct links with absolute deduplicated URLs", () => {
  const env = harness([
    element("audio", { src: "song.mp3#one", title: "Song" }, { currentSrc: "https://cdn.example/media/song.mp3#two" }),
    element("video", { src: "fallback.mp4", "aria-label": "A video" }, { currentSrc: "https://cdn.example/live" }, [
      element("source", { src: "alternate.mp4" }),
    ]),
    element("a", { href: "song.mp3#duplicate" }),
    element("a", { href: "../music.M4A?token=test#part", title: "Audio link" }),
    element("a", { href: "/clip.MP4?download=1" }),
    element("a", { href: "/watch?id=anything.mp4" }),
    element("picture", {}, {}, [element("source", { src: "not-audio.mp3" })]),
  ]);
  const channel = env.port();
  assert.deepEqual(channel.messages, [{
    type: "snapshot",
    pageUrl: "https://page.example/watch#part-one",
    candidates: [
      { url: "https://cdn.example/media/song.mp3", title: "Song", kind: "audio", sourceId: "c1:d1" },
      { url: "https://cdn.example/live", title: "A video", kind: "video", sourceId: "c1:d1" },
      { url: "https://cdn.example/media/fallback.mp4", title: "A video", kind: "video", sourceId: "c1:d1" },
      { url: "https://cdn.example/media/alternate.mp4", title: "A video", kind: "video", sourceId: "c1:d1" },
      { url: "https://cdn.example/music.M4A?token=test", title: "Audio link", kind: "audio", sourceId: "c1:d1" },
      { url: "https://cdn.example/clip.MP4?download=1", title: "Document title", kind: "video", sourceId: "c1:d1" },
    ],
    truncated: false,
  }]);
});

test("unsupported schemes and malformed URLs do not become media candidates", () => {
  const env = harness([
    ...["blob:https://page.example/id", "data:audio/mp3;base64,AA", "file:///C:/private.mp3", "javascript:alert(1)", "https://[bad", "https://user:secret@cdn.example/a.mp3", "https://cdn.example/a\u0000.mp3"].map((src) => element("audio", { src })),
    element("a", { href: "https://cdn.example/photo.png" }),
    element("source", { src: "orphan.mp3" }),
  ]);
  assert.deepEqual(env.port().messages[0].candidates, []);
});

test("dynamic changes are coalesced and unchanged snapshots are not repeated", () => {
  const env = harness();
  const channel = env.port();
  env.replaceNodes([element("audio", { src: "first.mp3" })]);
  env.mutate();
  env.clock.advance(100);
  assert.equal(channel.messages.length, 1);
  env.replaceNodes([element("audio", { src: "latest.mp3" })]);
  env.mutate();
  env.clock.advance(99);
  assert.equal(channel.messages.length, 1);
  env.clock.advance(1);
  assert.equal(channel.messages.length, 2);
  assert.equal(channel.messages[1].candidates[0].url, "https://cdn.example/media/latest.mp3");
  env.mutate();
  env.clock.advance(200);
  assert.equal(channel.messages.length, 2);
});

test("continuous mutations cannot postpone the latest media snapshot indefinitely", () => {
  const env = harness();
  const channel = env.port();
  for (let index = 0; index < 6; index += 1) {
    env.replaceNodes([element("audio", { src: `${index}.mp3` })]);
    env.mutate();
    env.clock.advance(100);
  }
  assert.equal(channel.messages.length, 4);
  assert.equal(channel.messages.at(-1).candidates[0].url, "https://cdn.example/media/5.mp3");
});

test("metadata events discover currentSrc and removals withdraw old candidates", () => {
  const media = element("audio");
  const env = harness([media]);
  const channel = env.port();
  media.currentSrc = "https://cdn.example/dynamic";
  env.document.emit("loadedmetadata");
  env.clock.advance(200);
  assert.equal(channel.messages.at(-1).candidates[0].url, media.currentSrc);
  media.currentSrc = "https://cdn.example/replaced";
  env.document.emit("durationchange");
  env.clock.advance(200);
  assert.equal(channel.messages.at(-1).candidates[0].url, media.currentSrc);
  env.replaceNodes([]);
  env.mutate();
  env.clock.advance(200);
  assert.deepEqual(channel.messages.at(-1).candidates, []);
  for (const name of ["loadedmetadata", "durationchange"]) {
    assert.equal(env.document.listeners.get(name)[0].capture, true);
  }
});

test("candidate cap preserves first 100 distinct URLs and reports omitted media", () => {
  const env = harness(Array.from({ length: 100 }, (_, index) => element("audio", { src: `${index}.mp3` })));
  const channel = env.port();
  assert.equal(channel.messages[0].candidates.length, 100);
  assert.equal(channel.messages[0].truncated, false);
  env.replaceNodes(Array.from({ length: 102 }, (_, index) => element("audio", { src: `${index}.mp3` })));
  env.mutate();
  env.clock.advance(200);
  assert.equal(channel.messages.at(-1).candidates.length, 100);
  assert.equal(channel.messages.at(-1).candidates.at(-1).url, "https://cdn.example/media/99.mp3");
  assert.equal(channel.messages.at(-1).truncated, true);
});

test("scan bounds inspected elements instead of first collecting an unlimited document", () => {
  const env = harness([
    ...Array.from({ length: 1999 }, () => element("div")),
    element("audio", { src: "beyond-cap.mp3" }),
  ]);
  const snapshot = env.port().messages[0];
  assert.equal(env.inspected, 2000);
  assert.deepEqual(snapshot.candidates, []);
  assert.equal(snapshot.truncated, true);
});

test("URL and title boundaries are enforced after normalization", () => {
  const prefix = "https://cdn.example/";
  const exact = prefix + "a".repeat(8192 - prefix.length);
  const env = harness([
    element("audio", { src: exact, title: "t".repeat(200) }),
    element("audio", { src: `${exact}b` }),
    element("audio", { src: prefix + "é".repeat(2000) }),
    element("audio", { src: "short.mp3", title: "x".repeat(199) + "😀" }),
  ]);
  const snapshot = env.port().messages[0];
  assert.equal(snapshot.candidates.length, 2);
  assert.equal(snapshot.candidates[0].url.length, 8192);
  assert.equal(snapshot.candidates[0].title.length, 200);
  assert.equal(snapshot.candidates[1].title, "x".repeat(199));
  assert.equal(snapshot.truncated, true);
});

test("worst-size candidate snapshot stays within 1 MiB UTF-8", () => {
  const env = harness(Array.from({ length: 101 }, (_, index) => element("audio", {
    src: `https://cdn.example/${index}/` + "a".repeat(8100),
    title: "😀".repeat(120),
  })));
  const snapshot = env.port().messages[0];
  assert.ok(new TextEncoder().encode(JSON.stringify(snapshot)).length <= 1024 * 1024);
  assert.equal(snapshot.candidates.length, 100);
  assert.ok(snapshot.candidates.every((candidate) => candidate.title.length <= 200));
});

test("JSON escape amplification cannot exceed the serialized snapshot byte budget", () => {
  const env = harness(Array.from({ length: 100 }, (_, index) => element("audio", {
    src: `https://cdn.example/${index}?q=` + "\\".repeat(8000),
  })));
  const snapshot = env.port().messages[0];
  assert.ok(snapshot.candidates.length > 0);
  assert.ok(snapshot.candidates.length < 100);
  assert.equal(snapshot.truncated, true);
  assert.ok(new TextEncoder().encode(JSON.stringify(snapshot)).length <= 1024 * 1024);
});

test("base URI changes replace resolved sources without retaining old candidates", () => {
  const env = harness([element("audio", { src: "relative.mp3" })]);
  const channel = env.port();
  env.document.baseURI = "https://other-cdn.example/collection/";
  env.mutate();
  env.clock.advance(200);
  assert.deepEqual(channel.messages.at(-1).candidates, [{
    url: "https://other-cdn.example/collection/relative.mp3", title: "Document title", kind: "audio", sourceId: "c1:d1",
  }]);
});

test("markup stays literal while title controls are removed", () => {
  const env = harness([element("audio", { src: "sample.mp3", title: "<img src=x onerror=alert(1)>\u0000\nText" })]);
  assert.equal(env.port().messages[0].candidates[0].title, "<img src=x onerror=alert(1)>  Text");
});

test("a document exactly at the element limit is not falsely marked truncated", () => {
  const env = harness(Array.from({ length: 1999 }, () => element("div")));
  assert.equal(env.port().messages[0].truncated, false);
  assert.equal(env.inspected, 2000);
});

test("disconnect removes observers, capture listeners, navigation listeners and timers", () => {
  const env = harness();
  const channel = env.port();
  const observer = env.observers[0];
  env.mutate();
  channel.onDisconnect.emit();
  assertStopped(env, channel);
  observer.callback([]);
  env.document.emit("loadedmetadata");
  env.clock.advance(2000);
  assert.equal(channel.messages.length, 1);
  assertStopped(env, channel);
});

test("reopening the popup replaces an old session without accumulating observation", () => {
  const env = harness();
  const first = env.port();
  const firstObserver = env.observers[0];
  const second = env.port();
  assert.equal(first.disconnectCount, 1);
  assert.equal(firstObserver.active, false);
  assert.equal(first.onDisconnect.listeners.size, 0);
  assert.equal(env.observers.filter((observer) => observer.active).length, 1);
  assert.equal(second.messages.length, 1);
  env.inject();
  assert.equal(env.onConnect.listeners.size, 1);
  env.port({ sender: { id: "foreign", url: popupUrl } });
  assert.equal(second.disconnectCount, 0);
  second.onDisconnect.emit();
  assertStopped(env, second);
});

for (const event of ["popstate", "hashchange"]) {
  test(`${event} navigation invalidates the old page without publishing new media`, () => {
    const env = harness([element("audio", { src: "old.mp3" })]);
    const channel = env.port();
    env.replaceNodes([element("audio", { src: "new.mp3" })]);
    env.location.href = event === "hashchange" ? "https://page.example/watch#changed" : "https://page.example/new-page";
    env.window.emit(event);
    assert.equal(channel.disconnectCount, 1);
    assert.equal(channel.messages.length, 1);
    assertStopped(env, channel);
  });
}

test("same URL history events preserve detection", () => {
  const env = harness();
  const channel = env.port();
  env.window.emit("popstate");
  env.window.emit("hashchange");
  assert.equal(channel.disconnectCount, 0);
  assert.equal(env.observers.filter((observer) => observer.active).length, 1);
});

test("pushState-style URL change without a DOM event is detected by an ephemeral timer", () => {
  const env = harness();
  const channel = env.port();
  env.location.href = "https://page.example/new-route";
  env.clock.advance(1000);
  assert.equal(channel.disconnectCount, 1);
  assert.equal(channel.messages.length, 1);
  assertStopped(env, channel);
});

test("scan checks URL before collecting a changed SPA document", () => {
  const env = harness();
  const channel = env.port();
  env.replaceNodes([element("audio", { src: "new-page.mp3" })]);
  env.location.href = "https://page.example/other";
  env.mutate();
  env.clock.advance(200);
  assert.equal(channel.disconnectCount, 1);
  assert.equal(env.treeReads, 1);
  assert.equal(channel.messages.length, 1);
  assertStopped(env, channel);
});

test("pagehide stops the document session including bfcache suspension", () => {
  const env = harness();
  const channel = env.port();
  env.window.emit("pagehide");
  assert.equal(channel.disconnectCount, 1);
  assertStopped(env, channel);
});

test("postMessage failure ends observation without leaving idle background work", () => {
  const env = harness();
  const channel = env.port({ postMessage() { throw new Error("disconnected"); } });
  assert.equal(channel.disconnectCount, 1);
  assertStopped(env, channel);
});

test("an observer setup failure closes the port and releases partially acquired resources", () => {
  const env = harness([], { observeThrows: true });
  const channel = env.port();
  assert.equal(channel.messages.length, 0);
  assert.equal(channel.disconnectCount, 1);
  assertStopped(env, channel);
});

test("an overlong page URL is rejected without serializing page data", () => {
  const env = harness([], { href: "https://page.example/" + "x".repeat(8192) });
  const channel = env.port();
  assert.equal(channel.messages.length, 0);
  assert.equal(channel.disconnectCount, 1);
  assert.equal(env.treeReads, 0);
  assertStopped(env, channel);
});

test("V2 exposes a frozen readonly fresh snapshot only during the active session", () => {
  const media = element("audio", { src: "one.mp3" });
  const env = harness([media]);
  assert.equal(env.snapshot(), null);
  assert.equal(Object.isFrozen(env.sentinel), true);
  assert.equal(env.descriptor().writable, false);
  assert.equal(env.descriptor().configurable, false);
  assert.equal(env.port({ name: "rc-music-detection-v1" }).messages.length, 0);
  const channel = env.port();
  const first = env.snapshot();
  const again = env.snapshot();
  assert.notEqual(first, again);
  assert.notEqual(first.candidates, again.candidates);
  assert.deepEqual(Object.keys(first).sort(), ["candidates", "pageUrl", "truncated", "type"]);
  assert.deepEqual(Object.keys(first.candidates[0]).sort(), ["kind", "sourceId", "title", "url"]);
  assert.equal(first.candidates[0].sourceId, "c1:d1");
  first.candidates[0].title = "caller mutation";
  media.setAttribute("src", "two.mp3");
  const fresh = env.execute(() => globalThis.__rcMusicDetectionV2.snapshot());
  assert.equal(fresh.candidates[0].url, "https://cdn.example/media/two.mp3");
  assert.equal(fresh.candidates[0].title, "Document title");
  assert.equal(channel.messages.length, 1, "the snapshot probe does not publish or submit work");
  channel.onDisconnect.emit();
  assert.equal(env.snapshot(), null);
  assertStopped(env, channel);
});

test("V2 source identities are stable and change across popup connections", () => {
  const env = harness([element("audio", { src: "one.mp3" })]);
  const first = env.port();
  assert.equal(env.snapshot().candidates[0].sourceId, "c1:d1");
  env.clock.advance(1500);
  assert.equal(first.messages.length, 1);
  assert.equal(env.snapshot().candidates[0].sourceId, "c1:d1");
  const second = env.port();
  assert.equal(first.disconnectCount, 1);
  assert.equal(second.messages[0].candidates[0].sourceId, "c2:d1");
  const [connection, document] = second.messages[0].candidates[0].sourceId.match(/[0-9]+/gu).map(Number);
  assert.equal(Number.isSafeInteger(connection), true);
  assert.equal(Number.isSafeInteger(document), true);
  assert.ok(connection > 0 && document > 0);
});

test("each frame and its shadow roots use their own base URI and document title", () => {
  const root = shadowRoot([element("audio", { src: "relative.mp3" })]);
  const child = frameDocument([element("section", {}, { shadowRoot: root })], {
    href: "about:srcdoc", baseURI: "https://page.example/embedded/", title: "Embedded title",
  });
  const env = harness([element("iframe", {}, { contentDocument: child }), element("audio", { src: "top.mp3" })]);
  const candidates = env.port().messages[0].candidates;
  assert.equal(candidates[0].sourceId, "c1:d1");
  assert.equal(candidates[1].url, "https://page.example/embedded/relative.mp3");
  assert.equal(candidates[1].title, "Embedded title");
  assert.equal(candidates[1].sourceId, "c1:d2");
});

test("inaccessible frame getters and closed shadow roots are ignored", () => {
  const hidden = element("iframe");
  Object.defineProperty(hidden, "contentDocument", { get() { throw new Error("SecurityError"); } });
  const env = harness([
    hidden, element("iframe", {}, { contentDocument: null }),
    element("div", {}, { shadowRoot: null, closedShadowRoot: shadowRoot([element("audio", { src: "closed.mp3" })]) }),
    element("audio", { src: "visible.mp3" }),
  ]);
  const channel = env.port();
  assert.deepEqual(channel.messages[0].candidates.map((item) => item.url), ["https://cdn.example/media/visible.mp3"]);
  assert.equal(env.observers.filter((item) => item.active).length, 1);
  assert.equal(channel.messages[0].truncated, false);
});

test("a late open shadow root is found by the bounded periodic scan without DOM patches", () => {
  const host = element("section");
  const env = harness([host]);
  const channel = env.port();
  host.shadowRoot = shadowRoot([element("audio", { src: "late.mp3" })]);
  env.clock.advance(499);
  assert.equal(channel.messages.length, 1);
  env.clock.advance(1);
  assert.equal(channel.messages.at(-1).candidates[0].url, "https://cdn.example/media/late.mp3");
  assert.equal(env.observers.filter((item) => item.active).length, 2);
  channel.onDisconnect.emit();
  assert.equal(host.shadowRoot.listenerCount, 0);
  assertStopped(env, channel);
});

test("fresh probes replace child-document source IDs before a streamed update arrives", () => {
  const child = frameDocument([element("audio", { src: "same.mp3" })]);
  const frame = element("frame", {}, { contentDocument: child });
  const env = harness([frame]);
  const channel = env.port();
  assert.equal(channel.messages[0].candidates[0].sourceId, "c1:d2");
  child.location.href = "https://page.example/child-spa";
  assert.equal(env.snapshot().candidates[0].sourceId, "c1:d3");
  assert.equal(channel.messages.length, 1);
  frame.contentDocument = frameDocument([element("audio", { src: "same.mp3" })], { href: child.location.href });
  assert.equal(env.snapshot().candidates[0].sourceId, "c1:d4");
  assert.equal(child.listenerCount, 0);
  env.clock.advance(500);
  assert.equal(channel.messages.at(-1).candidates[0].sourceId, "c1:d4");
});

test("global URL deduplication changes source identity when its first source is removed", () => {
  const first = frameDocument([element("audio", { src: "https://cdn.example/same.mp3" })]);
  const second = frameDocument([element("audio", { src: "https://cdn.example/same.mp3" })]);
  const secondFrame = element("iframe", {}, { contentDocument: second });
  const env = harness([element("iframe", {}, { contentDocument: first }), secondFrame]);
  const channel = env.port();
  assert.equal(channel.messages[0].candidates.length, 1);
  assert.equal(channel.messages[0].candidates[0].sourceId, "c1:d2");
  env.replaceNodes([secondFrame]);
  const fresh = env.snapshot();
  assert.equal(fresh.candidates.length, 1);
  assert.equal(fresh.candidates[0].sourceId, "c1:d3");
  assert.equal(first.listenerCount, 0);
  assert.equal(env.observers.filter((item) => item.active).length, 2);
});

test("metadata duration and frame load events are observed on every root and retired on detach", () => {
  const media = element("audio");
  const shadow = shadowRoot([media]);
  const child = frameDocument([element("div", {}, { shadowRoot: shadow })]);
  const env = harness([element("iframe", {}, { contentDocument: child })]);
  const channel = env.port();
  for (const root of [env.document, child, shadow]) {
    for (const event of ["loadedmetadata", "durationchange", "load"]) {
      assert.equal(root.listeners.get(event).length, 1);
      assert.equal(root.listeners.get(event)[0].capture, true);
    }
  }
  media.currentSrc = "https://cdn.example/updated.mp3";
  shadow.emit("loadedmetadata");
  env.clock.advance(200);
  assert.equal(channel.messages.at(-1).candidates[0].url, media.currentSrc);
  env.replaceNodes([]);
  env.mutate(env.document);
  env.clock.advance(200);
  assert.equal(shadow.listenerCount, 0);
  assert.equal(child.listenerCount, 0);
  assert.equal(env.observers.filter((item) => item.active).length, 1);
  shadow.emit("durationchange");
  child.emit("load");
  assert.equal(env.clock.tasks.size, 1);
  channel.onDisconnect.emit();
  assertStopped(env, channel);
});

test("the root budget includes documents and never observes over 64 roots", () => {
  const roots = Array.from({ length: 64 }, (_, index) => shadowRoot([element("audio", { src: `${index}.mp3` })]));
  const env = harness(roots.map((root) => element("div", {}, { shadowRoot: root })));
  const channel = env.port();
  assert.equal(channel.messages[0].candidates.length, 63);
  assert.equal(channel.messages[0].truncated, true);
  assert.equal(env.observers.filter((item) => item.active).length, 64);
  assert.equal(roots.at(-1).listenerCount, 0);
  channel.onDisconnect.emit();
  for (const root of roots) assert.equal(root.listenerCount, 0);
  assertStopped(env, channel);
});

test("the document budget includes the top document and accepts at most 31 child documents", () => {
  const children = Array.from({ length: 32 }, (_, index) => frameDocument([element("audio", { src: `${index}.mp3` })]));
  const env = harness(children.map((child) => element("iframe", {}, { contentDocument: child })));
  const channel = env.port();
  assert.equal(channel.messages[0].candidates.length, 31);
  assert.equal(channel.messages[0].truncated, true);
  assert.equal(env.observers.filter((item) => item.active).length, 32);
  assert.equal(children.at(-1).listenerCount, 0);
  channel.onDisconnect.emit();
  for (const child of children) assert.equal(child.listenerCount, 0);
  assertStopped(env, channel);
});

test("element and candidate budgets aggregate across document and shadow roots", () => {
  const child = frameDocument([
    ...Array.from({ length: 1997 }, () => element("div")), element("audio", { src: "beyond.mp3" }),
  ]);
  const env = harness([element("iframe", {}, { contentDocument: child })]);
  const snapshot = env.port().messages[0];
  assert.equal(env.inspected, 2000);
  assert.equal(snapshot.candidates.length, 0);
  assert.equal(snapshot.truncated, true);
  const many = frameDocument(Array.from({ length: 60 }, (_, index) => element("audio", { src: `frame-${index}.mp3` })));
  const other = harness([
    ...Array.from({ length: 60 }, (_, index) => element("audio", { src: `top-${index}.mp3` })),
    element("iframe", {}, { contentDocument: many }),
  ]);
  assert.equal(other.port().messages[0].candidates.length, 100);
  assert.equal(other.snapshot().truncated, true);
});

test("the fresh API invalidates top navigation and does not retain DOM references", () => {
  const env = harness([element("audio", { src: "one.mp3" })]);
  const channel = env.port();
  assert.doesNotThrow(() => JSON.stringify(env.snapshot()));
  env.location.href = "https://page.example/other";
  assert.equal(env.snapshot(), null);
  assert.equal(channel.disconnectCount, 1);
  assertStopped(env, channel);
});

test("child href and base URI limits reject the root before retaining source context", () => {
  for (const oversized of ["href", "baseURI"]) {
    const child = frameDocument([element("audio", { src: "https://cdn.example/one.mp3" })], {
      [oversized]: "https://page.example/" + "x".repeat(8192),
    });
    const env = harness([element("iframe", {}, { contentDocument: child })]);
    const channel = env.port();
    assert.equal(channel.messages[0].candidates.length, 0);
    assert.equal(channel.messages[0].truncated, true);
    assert.equal(child.listenerCount, 0);
    if (oversized === "href") child.location.href = "https://page.example/safe-child";
    else child.baseURI = "https://cdn.example/safe/";
    const fresh = env.snapshot();
    assert.equal(fresh.candidates[0].sourceId, "c1:d2");
    assert.equal(fresh.truncated, false);
  }
});
