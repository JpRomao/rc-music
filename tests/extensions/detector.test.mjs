import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

const detectorPath = new URL("../../extensions/chromium/main/detector.js", import.meta.url);
const extensionId = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const popupUrl = `chrome-extension://${extensionId}/popup.html`;

class FakeEvent {
  listeners = new Set();
  addListener(listener) { this.listeners.add(listener); }
  removeListener(listener) { this.listeners.delete(listener); }
  emit(...args) { for (const listener of [...this.listeners]) listener(...args); }
}

class FakeTarget {
  listeners = new Map();
  addEventListener(type, listener, capture = false) {
    const entries = this.listeners.get(type) ?? [];
    entries.push({ listener, capture });
    this.listeners.set(type, entries);
  }
  removeEventListener(type, listener, capture = false) {
    this.listeners.set(type, (this.listeners.get(type) ?? []).filter(
      (entry) => entry.listener !== listener || entry.capture !== capture,
    ));
  }
  emit(type) { for (const { listener } of [...(this.listeners.get(type) ?? [])]) listener(); }
  get listenerCount() { return [...this.listeners.values()].reduce((sum, entries) => sum + entries.length, 0); }
}

class FakeClock {
  now = 0;
  nextId = 1;
  tasks = new Map();
  setTimeout = (callback, delay) => this.schedule(callback, delay, false);
  setInterval = (callback, delay) => this.schedule(callback, delay, true);
  clearTimeout = (id) => this.tasks.delete(id);
  clearInterval = (id) => this.tasks.delete(id);
  schedule(callback, delay, repeat) {
    const id = this.nextId++;
    this.tasks.set(id, { callback, delay, repeat, at: this.now + delay });
    return id;
  }
  advance(milliseconds) {
    const end = this.now + milliseconds;
    for (;;) {
      const due = [...this.tasks.entries()].filter(([, task]) => task.at <= end)
        .sort((a, b) => a[1].at - b[1].at || a[0] - b[0])[0];
      if (!due) break;
      const [id, task] = due;
      this.now = task.at;
      if (task.repeat) task.at += task.delay;
      else this.tasks.delete(id);
      task.callback();
    }
    this.now = end;
  }
}

function element(tagName, attributes = {}, properties = {}, children = []) {
  const node = {
    tagName: tagName.toUpperCase(),
    currentSrc: "",
    parentElement: null,
    textContent: "",
    children,
    getAttribute: (name) => attributes[name] ?? null,
    setAttribute: (name, value) => { attributes[name] = value; },
    ...properties,
  };
  for (const child of children) child.parentElement = node;
  return node;
}

function harness(nodes = [], options = {}) {
  const clock = new FakeClock();
  const document = new FakeTarget();
  const window = new FakeTarget();
  const location = { href: options.href ?? "https://page.example/watch#part-one" };
  const onConnect = new FakeEvent();
  const observers = [];
  let inspected = 0;
  let treeReads = 0;
  document.title = options.title ?? "Document title";
  document.baseURI = options.baseURI ?? "https://cdn.example/media/";
  document.documentElement = element("html", {}, {}, nodes);
  document.createTreeWalker = (root, show) => {
    assert.equal(show, 1);
    treeReads += 1;
    const all = [];
    const flatten = (node) => { all.push(node); for (const child of node.children) flatten(child); };
    if (root === document) flatten(document.documentElement);
    else for (const child of root.children) flatten(child);
    let index = 0;
    return { nextNode: () => {
      const node = all[index++] ?? null;
      if (!node) return null;
      return new Proxy(node, { get: (target, key) => {
        if (key === "tagName") inspected += 1;
        return target[key];
      } });
    } };
  };
  window.top = window;
  const sandbox = {
    document, window, location, URL, TextEncoder,
    NodeFilter: { SHOW_ELEMENT: 1 },
    setTimeout: clock.setTimeout, clearTimeout: clock.clearTimeout,
    setInterval: clock.setInterval, clearInterval: clock.clearInterval,
    chrome: { runtime: { id: extensionId, getURL: (path) => `chrome-extension://${extensionId}/${path}`, onConnect } },
    MutationObserver: class {
      constructor(callback) { this.callback = callback; this.active = false; observers.push(this); }
      observe(target, config) {
        if (options.observeThrows) throw new Error("document unavailable");
        assert.equal(target, document);
        assert.equal(config.subtree, true);
        assert.equal(config.childList, true);
        assert.equal(config.attributes, true);
        assert.ok(config.attributeFilter.includes("src"));
        assert.ok(config.attributeFilter.includes("href"));
        this.active = true;
      }
      disconnect() { this.active = false; }
    },
  };
  const context = vm.createContext(sandbox);
  const inject = () => vm.runInContext(readFileSync(detectorPath, "utf8"), context, { filename: "detector.js" });
  const port = (overrides = {}) => {
    const channel = {
      name: "rc-music-detection-v1",
      sender: { id: extensionId, url: popupUrl },
      messages: [],
      disconnectCount: 0,
      onDisconnect: new FakeEvent(),
      postMessage(message) { this.messages.push(JSON.parse(JSON.stringify(message))); },
      disconnect() { this.disconnectCount += 1; this.onDisconnect.emit(); },
      ...overrides,
    };
    onConnect.emit(channel);
    return channel;
  };
  inject();
  return {
    clock, document, window, location, onConnect, observers, inject, port,
    get treeReads() { return treeReads; },
    get inspected() { return inspected; },
    mutate() { for (const observer of observers.filter((item) => item.active)) observer.callback([]); },
    replaceNodes(replacements) { document.documentElement = element("html", {}, {}, replacements); },
  };
}

function assertStopped(env, channel) {
  assert.equal(env.observers.filter((observer) => observer.active).length, 0);
  assert.equal(env.clock.tasks.size, 0);
  assert.equal(env.document.listenerCount, 0);
  assert.equal(env.window.listenerCount, 0);
  assert.equal(channel.onDisconnect.listeners.size, 0);
}

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
      { url: "https://cdn.example/media/song.mp3", title: "Song", kind: "audio" },
      { url: "https://cdn.example/live", title: "A video", kind: "video" },
      { url: "https://cdn.example/media/fallback.mp4", title: "A video", kind: "video" },
      { url: "https://cdn.example/media/alternate.mp4", title: "A video", kind: "video" },
      { url: "https://cdn.example/music.M4A?token=test", title: "Audio link", kind: "audio" },
      { url: "https://cdn.example/clip.MP4?download=1", title: "Document title", kind: "video" },
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
    url: "https://other-cdn.example/collection/relative.mp3", title: "Document title", kind: "audio",
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
