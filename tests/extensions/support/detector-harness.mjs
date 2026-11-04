import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";

const detectorPath = new URL("../../../extensions/chromium/main/detector.js", import.meta.url);
const collectorPath = new URL("../../../extensions/chromium/main/media-collector.js", import.meta.url);
export const extensionId = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
export const popupUrl = `chrome-extension://${extensionId}/popup.html`;

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

export function element(tagName, attributes = {}, properties = {}, children = []) {
  const node = {
    tagName: tagName.toUpperCase(), currentSrc: "", parentElement: null, textContent: "", children,
    shadowRoot: null,
    getAttribute: (name) => attributes[name] ?? null,
    setAttribute: (name, value) => { attributes[name] = value; },
    ...properties,
  };
  for (const child of children) child.parentElement = node;
  return node;
}

export function shadowRoot(children = []) {
  const root = new FakeTarget();
  root.children = children;
  root.mode = "open";
  return root;
}

export function frameDocument(nodes = [], options = {}) {
  const document = new FakeTarget();
  document.location = { href: options.href ?? "https://page.example/frame" };
  Object.defineProperty(document, "URL", { get: () => document.location.href });
  document.title = options.title ?? "Frame title";
  document.baseURI = options.baseURI ?? "https://page.example/frame-media/";
  document.documentElement = element("html", {}, {}, nodes);
  return document;
}

export function harness(nodes = [], options = {}) {
  const clock = new FakeClock();
  const document = frameDocument(nodes, {
    href: options.href ?? "https://page.example/watch#part-one",
    baseURI: options.baseURI ?? "https://cdn.example/media/", title: options.title ?? "Document title",
  });
  const window = new FakeTarget();
  const location = document.location;
  const onConnect = new FakeEvent();
  const observers = [];
  let inspected = 0;
  let treeReads = 0;
  const configured = new WeakSet();
  function prepare(root, owner = document) {
    if (configured.has(root)) return;
    configured.add(root);
    root.ownerDocument = owner;
    if (root.documentElement) {
      owner = root;
      root.ownerDocument = null;
      root.createTreeWalker = (walkRoot, show) => {
        assert.equal(show, 1);
        treeReads += 1;
        // Lazy iteration deliberately avoids precollecting a subtree in tests.
        const stack = [walkRoot.documentElement ? [walkRoot.documentElement][Symbol.iterator]()
          : (walkRoot.children ?? [])[Symbol.iterator]()];
        return { nextNode() {
          while (stack.length) {
            const result = stack.at(-1).next();
            if (result.done) { stack.pop(); continue; }
            const node = result.value;
            stack.push((node.children ?? [])[Symbol.iterator]());
            prepareNode(node, owner);
            return new Proxy(node, { get(target, key) {
              if (key === "tagName") inspected += 1;
              return Reflect.get(target, key);
            } });
          }
          return null;
        } };
      };
    }
  }
  function prepareNode(node, owner) {
    node.ownerDocument = owner;
    if (node.shadowRoot) prepare(node.shadowRoot, owner);
    if (!options.nativeOnly) {
      try { if (node.contentDocument) prepare(node.contentDocument, node.contentDocument); } catch { /* Inaccessible. */ }
    }
  }
  prepare(document, document);
  window.top = window;
  const sandbox = {
    document, window, location, URL, TextEncoder,
    NodeFilter: { SHOW_ELEMENT: 1 },
    setTimeout: clock.setTimeout, clearTimeout: clock.clearTimeout,
    setInterval: clock.setInterval, clearInterval: clock.clearInterval,
    chrome: { runtime: { id: extensionId, getURL: (path) => `chrome-extension://${extensionId}/${path}`, onConnect } },
    MutationObserver: class {
      constructor(callback) { this.callback = callback; this.active = false; this.target = null; observers.push(this); }
      observe(target, config) {
        if (options.observeThrows) throw new Error("document unavailable");
        assert.equal(config.subtree, true);
        assert.equal(config.childList, true);
        assert.equal(config.attributes, true);
        assert.ok(config.attributeFilter.includes("src"));
        assert.ok(config.attributeFilter.includes("href"));
        this.target = target;
        this.active = true;
      }
      disconnect() { this.active = false; }
    },
  };
  if (options.nativeOnly) {
    for (const name of ["chrome", "MutationObserver", "setTimeout", "clearTimeout", "setInterval", "clearInterval"]) delete sandbox[name];
  }
  const context = vm.createContext(sandbox);
  const inject = () => {
    vm.runInContext(readFileSync(collectorPath, "utf8"), context, { filename: "media-collector.js" });
    if (!options.nativeOnly) vm.runInContext(readFileSync(detectorPath, "utf8"), context, { filename: "detector.js" });
  };
  const port = (overrides = {}) => {
    const channel = {
      name: options.portName ?? "rc-music-detection-v2",
      sender: { id: extensionId, url: popupUrl }, messages: [], disconnectCount: 0,
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
    collect(settings = {}) { return JSON.parse(JSON.stringify(sandbox.rcMusicCollectMedia(document, settings))); },
    collectNative() { return JSON.parse(JSON.stringify(sandbox.rcMusicCollectNativeMedia(document))); },
    get treeReads() { return treeReads; },
    get inspected() { return inspected; },
    get sentinel() { return sandbox.__rcMusicDetectionV2; },
    snapshot() { return sandbox.__rcMusicDetectionV2?.snapshot() ?? null; },
    execute(func) { return vm.runInContext("(" + func.toString() + ")()", context); },
    descriptor() { return Object.getOwnPropertyDescriptor(sandbox, "__rcMusicDetectionV2"); },
    mutate(target) { for (const observer of observers.filter((item) => item.active && (!target || item.target === target))) observer.callback([]); },
    replaceNodes(replacements) { document.documentElement = element("html", {}, {}, replacements); },
  };
}

export function assertStopped(env, channel) {
  assert.equal(env.observers.filter((observer) => observer.active).length, 0);
  assert.equal(env.clock.tasks.size, 0);
  assert.equal(env.document.listenerCount, 0);
  assert.equal(env.window.listenerCount, 0);
  assert.equal(channel.onDisconnect.listeners.size, 0);
}
