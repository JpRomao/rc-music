import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";
import { harness, element, frameDocument, shadowRoot } from "./support/detector-harness.mjs";

const native = (nodes = [], options = {}) => harness(nodes, { ...options, nativeOnly: true });

test("the native IIFE scopes the shared collector and returns serializable data without globals", () => {
  const env = native([element("audio", { src: "local-to-page.mp3", title: "Native probe" })]);
  const context = vm.createContext({ document: env.document, URL, TextEncoder, NodeFilter: { SHOW_ELEMENT: 1 } });
  const source = readFileSync(new URL("../../extensions/chromium/main/media-collector.js", import.meta.url), "utf8");
  const value = vm.runInContext("(() => {\n" + source + "\nreturn rcMusicCollectNativeMedia(document);\n})()", context);
  assert.deepEqual(JSON.parse(JSON.stringify(value)), {
    pageUrl: env.location.href,
    candidates: [{ url: "https://cdn.example/media/local-to-page.mp3", title: "Native probe", kind: "audio" }],
    truncated: false,
  });
  assert.equal(Object.hasOwn(context, "rcMusicCollectMedia"), false);
  assert.equal(Object.hasOwn(context, "rcMusicCollectNativeMedia"), false);
  assert.equal(Object.hasOwn(context, "chrome"), false);
  assert.equal(Object.hasOwn(context, "__rcMusicDetectionV2"), false);
  assert.equal(env.observers.length, 0);
  assert.equal(env.clock.tasks.size, 0);
});

test("native collection returns only bounded data without extension APIs, timers or listeners", () => {
  const env = native([
    element("audio", { src: "song.mp3#fallback", title: "Song" }, {
      currentSrc: "https://cdn.example/media/song.mp3?sig=A%2fb+%20&token=%252F#part",
    }),
    element("video", { "aria-label": "Clip" }, { currentSrc: "https://cdn.example/play?id=7" }, [
      element("source", { src: "fallback.mp4" }),
    ]),
    element("a", { href: "../audio.M4A?signature=Ab%2fCD#part", title: "Direct link" }),
    element("div", {}, { shadowRoot: shadowRoot([element("audio", { src: "shadow.mp3" })]) }),
  ]);
  const snapshot = env.collect();
  assert.deepEqual(Object.keys(snapshot).sort(), ["pageUrl", "candidates", "truncated"].sort());
  assert.equal(snapshot.pageUrl, env.location.href);
  assert.equal(snapshot.truncated, false);
  assert.equal(snapshot.candidates[0].url, "https://cdn.example/media/song.mp3?sig=A%2fb+%20&token=%252F");
  assert.deepEqual(snapshot.candidates.map((item) => item.kind), ["audio", "audio", "video", "video", "audio", "audio"]);
  assert.ok(snapshot.candidates.every((item) => Object.keys(item).sort().join(",") === "kind,title,url"));
  assert.equal(snapshot.candidates.at(-1).url, "https://cdn.example/media/shadow.mp3");
  assert.equal(env.onConnect.listeners.size, 0);
  assert.equal(env.observers.length, 0);
  assert.equal(env.clock.tasks.size, 0);
  assert.equal(env.document.listenerCount, 0);
  assert.equal(env.window.listenerCount, 0);
  assert.equal(env.sentinel, undefined);
});

test("the collector can explicitly omit frame documents while traversing open shadow roots", () => {
  let frameReads = 0;
  const frame = element("iframe");
  Object.defineProperty(frame, "contentDocument", {
    get() { frameReads += 1; return frameDocument([element("audio", { src: "frame.mp3" })]); },
  });
  const env = native([
    frame,
    element("div", {}, { shadowRoot: shadowRoot([element("audio", { src: "open.mp3" })]) }),
  ]);
  const before = frameReads;
  assert.deepEqual(env.collect().candidates.map((item) => item.url), ["https://cdn.example/media/open.mp3"]);
  assert.equal(frameReads, before);
});

test("the native entry collects accessible nested frames with their own bases and titles", () => {
  const nested = native([element("video", { src: "nested.mp4" })], {
    href: "https://page.example/nested", baseURI: "https://cdn.example/nested/", title: "Nested clip",
  });
  const child = native([
    element("audio", { src: "child.mp3" }),
    element("iframe", {}, { contentDocument: nested.document }),
    element("div", {}, { shadowRoot: shadowRoot([element("audio", { src: "shadow.mp3" })]) }),
  ], { href: "https://page.example/child", baseURI: "https://cdn.example/child/", title: "Child song" });
  const env = native([element("iframe", {}, { contentDocument: child.document })]);
  assert.deepEqual(env.collectNative(), {
    pageUrl: env.location.href,
    candidates: [
      { url: "https://cdn.example/child/child.mp3", title: "Child song", kind: "audio" },
      { url: "https://cdn.example/nested/nested.mp4", title: "Nested clip", kind: "video" },
      { url: "https://cdn.example/child/shadow.mp3", title: "Child song", kind: "audio" },
    ],
    truncated: false,
  });
  assert.equal(env.observers.length, 0);
  assert.equal(env.clock.tasks.size, 0);
  assert.equal(child.document.listenerCount, 0);
  assert.equal(nested.document.listenerCount, 0);
});

test("the native entry ignores inaccessible and missing frame documents without losing accessible media", () => {
  const blocked = element("iframe");
  let reads = 0;
  Object.defineProperty(blocked, "contentDocument", { get() { reads += 1; throw new Error("SecurityError"); } });
  const child = native([element("audio", { src: "allowed.mp3" })]);
  const env = native([blocked, element("frame", {}, { contentDocument: null }),
    element("iframe", {}, { contentDocument: child.document })]);
  assert.deepEqual(env.collectNative().candidates.map((item) => item.url), ["https://cdn.example/media/allowed.mp3"]);
  assert.equal(reads, 1);
  assert.equal(env.observers.length, 0);
  assert.equal(env.clock.tasks.size, 0);
});

test("native frame discovery shares one document and candidate budget across the entire snapshot", () => {
  const children = Array.from({ length: 32 }, (_, index) => native([element("audio", { src: `${index}.mp3` })]));
  const documents = native(children.map((child) => element("iframe", {}, { contentDocument: child.document })));
  const documentSnapshot = documents.collectNative();
  assert.equal(documentSnapshot.candidates.length, 31);
  assert.equal(documentSnapshot.truncated, true);
  const groups = [0, 1].map((group) => native(Array.from({ length: 60 }, (_, index) =>
    element("audio", { src: `${group}-${index}.mp3` }))));
  const candidates = native(groups.map((child) => element("iframe", {}, { contentDocument: child.document })));
  assert.equal(candidates.collectNative().candidates.length, 100);
  assert.equal(candidates.collectNative().truncated, true);
});

test("native frame discovery shares the element budget rather than restarting it in each document", () => {
  const children = [0, 1].map((group) => native([
    ...Array.from({ length: 1000 }, () => element("span")), element("audio", { src: `${group}.mp3` }),
  ]));
  const env = native(children.map((child) => element("iframe", {}, { contentDocument: child.document })));
  const result = env.collectNative();
  assert.equal(result.truncated, true);
  assert.deepEqual(result.candidates.map((item) => item.url), ["https://cdn.example/media/0.mp3"]);
});

test("native frame rescans replace removed documents and read changed sources freshly", () => {
  const song = element("audio", { src: "before.mp3", title: "Before" });
  const child = native([song]);
  const frame = element("iframe", {}, { contentDocument: child.document });
  const env = native([frame]);
  const before = env.collectNative();
  assert.equal(before.candidates.length, 1);
  assert.equal(before.candidates[0].title, "Before");
  song.setAttribute("src", "after.mp3");
  song.setAttribute("title", "After");
  assert.deepEqual(env.collectNative().candidates, [
    { url: "https://cdn.example/media/after.mp3", title: "After", kind: "audio" },
  ]);
  frame.contentDocument = native([element("video", { src: "replacement.mp4" })]).document;
  const replacement = env.collectNative();
  assert.equal(replacement.candidates.length, 1);
  assert.equal(replacement.candidates[0].kind, "video");
  frame.contentDocument = null;
  assert.deepEqual(env.collectNative().candidates, []);
});

test("native titles have safe nonempty fallbacks and remove controls, bidi and unpaired surrogates", () => {
  const env = native([
    element("audio", { src: "a.mp3", title: "\u0000\u0085\u202e\u2066  " }),
    element("video", { src: "b.mp4" }),
    element("audio", { src: "c.mp3", title: "<img src=x onerror=alert(1)>\ud800\u0000\u202e" }),
  ], { title: "" });
  assert.deepEqual(env.collect().candidates.map((item) => item.title), ["Áudio", "Vídeo", "<img src=x onerror=alert(1)>�"]);
});

test("native collection deduplicates normalized URLs while preserving the first candidate", () => {
  const env = native([
    element("audio", { src: "song.mp3#one", title: "First" }),
    element("video", { src: "song.mp3#two", title: "Second" }),
    element("a", { href: "song.mp3", title: "Third" }),
  ]);
  assert.deepEqual(env.collect().candidates, [{ url: "https://cdn.example/media/song.mp3", title: "First", kind: "audio" }]);
});

test("candidate limit is inclusive and reports only omitted distinct media as truncated", () => {
  const nodes = Array.from({ length: 100 }, (_, index) => element("audio", { src: `${index}.mp3` }));
  const env = native(nodes);
  assert.equal(env.collect().candidates.length, 100);
  assert.equal(env.collect().truncated, false);
  env.replaceNodes([...nodes, element("audio", { src: "extra.mp3" })]);
  assert.equal(env.collect().candidates.length, 100);
  assert.equal(env.collect().truncated, true);
});

test("element limit stays inclusive and does not precollect an unbounded subtree", () => {
  const nodes = Array.from({ length: 1999 }, () => element("span"));
  const exact = native(nodes);
  assert.equal(exact.collect().truncated, false);
  assert.equal(exact.inspected, 2000);
  const extra = native([...nodes, element("audio", { src: "unvisited.mp3" })]);
  assert.equal(extra.collect().truncated, true);
  assert.equal(extra.inspected, 2000);
  assert.equal(extra.collect().candidates.length, 0);
});

test("native collection bounds open roots and reports additional roots without retaining them", () => {
  const env = native(Array.from({ length: 70 }, (_, index) => element("div", {}, {
    shadowRoot: shadowRoot([element("audio", { src: `${index}.mp3` })]),
  })));
  const snapshot = env.collect();
  assert.equal(snapshot.candidates.length, 63);
  assert.equal(snapshot.truncated, true);
  assert.equal(env.observers.length, 0);
});

test("URL and title boundaries produce a bounded, well formed native JSON snapshot", () => {
  const prefix = "https://cdn.example/";
  const exact = prefix + "a".repeat(8192 - prefix.length);
  const env = native([
    element("audio", { src: exact, title: "t".repeat(200) }),
    element("audio", { src: exact + "x" }),
    element("audio", { src: "short.mp3", title: "x".repeat(199) + "😀" }),
  ]);
  const snapshot = env.collect();
  assert.equal(snapshot.candidates.length, 2);
  assert.equal(snapshot.candidates[0].url.length, 8192);
  assert.equal(snapshot.candidates[0].title.length, 200);
  assert.equal(snapshot.candidates[1].title, "x".repeat(199));
  assert.equal(snapshot.truncated, true);
  assert.ok(new TextEncoder().encode(JSON.stringify(snapshot)).length <= 1024 * 1024);
});

test("oversized page or base URI does not trigger an unbounded scan", () => {
  const page = native([element("audio", { src: "a.mp3" })], { href: "https://page.example/" + "x".repeat(8192) });
  const snapshot = page.collect();
  assert.equal(snapshot.pageUrl, "");
  assert.equal(snapshot.truncated, true);
  assert.equal(snapshot.candidates.length, 0);
  assert.equal(page.treeReads, 0);
  const base = native([element("audio", { src: "a.mp3" })], { baseURI: "https://cdn.example/" + "x".repeat(8192) });
  assert.equal(base.collect().candidates.length, 0);
  assert.equal(base.collect().truncated, true);
  assert.equal(base.treeReads, 0);
});

test("unsupported source schemes and nonmedia anchors never become candidates", () => {
  const env = native([
    ...["blob:https://page.example/id", "data:audio/mp3;base64,AA", "file:///C:/private.mp3", "javascript:alert(1)",
      "https://user:secret@cdn.example/a.mp3", "https://cdn.example/a\u0000.mp3"].map((src) => element("audio", { src })),
    element("a", { href: "page.html" }), element("a", { href: "archive.zip" }),
  ]);
  assert.deepEqual(env.collect().candidates, []);
});

test("each native collection reads fresh data without preserving prior document state", () => {
  const media = element("audio", { src: "before.mp3", title: "Before" });
  const env = native([media]);
  const before = env.collect();
  media.setAttribute("src", "after.mp3");
  media.setAttribute("title", "After");
  const after = env.collect();
  assert.equal(before.candidates[0].url, "https://cdn.example/media/before.mp3");
  assert.equal(after.candidates[0].url, "https://cdn.example/media/after.mp3");
  assert.equal(after.candidates[0].title, "After");
  assert.equal(env.clock.tasks.size, 0);
});
