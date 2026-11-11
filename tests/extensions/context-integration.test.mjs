import assert from "node:assert/strict";
import test from "node:test";
import { createPopupController } from "../../extensions/chromium/main/popup-controller.mjs";
import * as policy from "../../extensions/chromium/main/policy.mjs";
import { sendDownloadRequest } from "../../extensions/chromium/main/native-client.mjs";
import {
  harness as detectorHarness, element, frameDocument, shadowRoot, assertStopped, extensionId, popupUrl,
} from "./support/detector-harness.mjs";

const requestId = "11111111-1111-4111-8111-111111111111";
const clone = (value) => value === undefined ? undefined : JSON.parse(JSON.stringify(value));

function event() {
  const listeners = new Set();
  return {
    listeners,
    addListener(listener) { listeners.add(listener); },
    removeListener(listener) { listeners.delete(listener); },
    emit(...args) { for (const listener of [...listeners]) listener(...args); },
  };
}

function integration(nodes, options = {}) {
  const env = detectorHarness(nodes, { portName: "rc-music-detection-v2", ...options });
  const links = [];
  const injections = [];
  const probes = [];
  const requests = [];
  const nativePorts = [];
  const controllers = [];
  const states = [];
  const tab = { id: 7, windowId: 3, url: env.location.href, active: true, status: "complete" };
  const browserWindow = { id: tab.windowId, focused: true };
  const documentId = "top-document-1";
  let nextProbeGate = null;
  let nextTabGate = null;
  const chrome = {
    windows: { getLastFocused: async () => ({ ...browserWindow }), onFocusChanged: event() },
    tabs: {
      query: async () => [{ ...tab }],
      async get(tabId) {
        assert.equal(tabId, tab.id);
        if (nextTabGate && --nextTabGate.remaining === 0) {
          const gate = nextTabGate;
          nextTabGate = null;
          gate.started = true;
          await gate.promise;
        }
        return { ...tab };
      },
      onUpdated: event(), onRemoved: event(), onActivated: event(),
      connect(tabId, settings) {
        assert.equal(tabId, tab.id);
        assert.equal(settings.documentId, documentId);
        const link = { settings: clone(settings), closed: false, content: null, popup: null };
        function disconnect() {
          if (link.closed) return;
          link.closed = true;
          link.content?.onDisconnect.emit();
          link.popup.onDisconnect.emit();
        }
        link.popup = {
          name: settings.name,
          sender: { id: extensionId, tab: { ...tab }, frameId: 0, documentId, url: tab.url },
          onMessage: event(), onDisconnect: event(), disconnect,
        };
        links.push(link);
        link.content = env.port({
          name: settings.name,
          sender: { id: extensionId, url: popupUrl },
          postMessage(message) {
            const wire = clone(message);
            this.messages.push(wire);
            // Chrome delivers Port messages after connect returns and the
            // receiving context can attach its listeners.
            env.clock.setTimeout(() => {
              if (!link.closed) link.popup.onMessage.emit(clone(wire));
            }, 0);
          },
          disconnect,
        });
        return link.popup;
      },
    },
    scripting: {
      async executeScript(settings) {
        assert.equal(settings.world, "ISOLATED");
        assert.equal(settings.target.tabId, tab.id);
        if (settings.files) {
          assert.deepEqual(settings.target, { tabId: tab.id, frameIds: [0] });
          assert.deepEqual(settings.files, ["media-collector.js", "detector.js"]);
          injections.push(clone(settings));
          env.inject();
          return [{ frameId: 0, documentId }];
        }
        assert.deepEqual(settings.target, { tabId: tab.id, documentIds: [documentId] });
        assert.equal(typeof settings.func, "function");
        // Execute the real browser probe in the detector's isolated VM. Do not
        // replace its result with a fabricated snapshot or inspect its source.
        const result = clone(env.execute(settings.func));
        probes.push(result);
        if (nextProbeGate && --nextProbeGate.remaining === 0) {
          const gate = nextProbeGate;
          nextProbeGate = null;
          gate.started = true;
          await gate.promise;
        }
        return [{ frameId: 0, documentId, result }];
      },
    },
    runtime: {
      connectNative(host) {
        assert.equal(host, "com.rcmusic.media");
        const port = {
          onMessage: event(), onDisconnect: event(), closed: false,
          disconnect() {
            if (this.closed) return;
            this.closed = true;
            this.onDisconnect.emit();
          },
          postMessage(request) {
            requests.push(clone(request));
            queueMicrotask(() => {
              if (!this.closed) this.onMessage.emit({
                schemaVersion: 1, type: "download.response", requestId: request.requestId, status: "accepted",
                jobIds: request.items.map((_, index) => `fixture-job-${index}`),
              });
            });
          },
        };
        nativePorts.push(port);
        return port;
      },
    },
  };
  function newController() {
    const controller = createPopupController({
      chrome,
      normalizeSnapshot: policy.normalizeSnapshot,
      buildDownloadRequest: policy.buildDownloadRequest,
      isSelectionCurrent: policy.isSelectionCurrent,
      sendDownloadRequest: (api, request, settings) => sendDownloadRequest(api, request, {
        ...settings, setTimer: env.clock.setTimeout, clearTimer: env.clock.clearTimeout,
      }),
      newRequestId: () => requestId,
      onState: (state) => states.push(clone(state)),
      setTimer: env.clock.setTimeout, clearTimer: env.clock.clearTimeout,
    });
    controllers.push(controller);
    return controller;
  }
  const controller = newController();
  return {
    env, controller, chrome, browserWindow, links, injections, probes, requests, nativePorts, states, newController,
    async flush(milliseconds = 0) {
      env.clock.advance(milliseconds);
      for (let index = 0; index < 25; index += 1) {
        await Promise.resolve();
        env.clock.advance(0);
      }
    },
    async ready(target = controller) {
      await target.refresh();
      await this.flush();
      assert.equal(target.getState().status, "ready");
    },
    deferNextProbe(call = 1) {
      let release;
      const promise = new Promise((resolve) => { release = resolve; });
      const gate = { promise, release, started: false, remaining: call };
      nextProbeGate = gate;
      return gate;
    },
    deferTabGet(call = 1) {
      let release;
      const promise = new Promise((resolve) => { release = resolve; });
      const gate = { promise, release, started: false, remaining: call };
      nextTabGate = gate;
      return gate;
    },
    dispose() { for (const target of controllers) target.dispose(); },
  };
}

function childFixture() {
  const child = frameDocument([element("audio", { src: "song.mp3", title: "Child song" })], {
    href: "https://page.example/child#one", baseURI: "https://media.example/child/",
  });
  return { child, frame: element("iframe", {}, { contentDocument: child }), url: "https://media.example/child/song.mp3" };
}

function selectAndAuthorize(h, url, controller = h.controller) {
  assert.ok(controller.getState().candidates.some((candidate) => candidate.url === url));
  controller.setSelected(url, true);
  controller.setAuthorized(true);
  assert.equal(controller.getState().canSend, true);
}

test("real detector, policy and controller send only explicitly consented nested-context media as Native v1", async (t) => {
  const child = frameDocument([element("video", { src: "chosen.mp4", title: "Frame video" })], {
    href: "https://page.example/child", baseURI: "https://media.example/frame/",
  });
  const nested = shadowRoot([element("audio", { src: "shadow.mp3", title: "Shadow song" })]);
  const outer = shadowRoot([
    element("section", {}, { shadowRoot: nested }),
    element("iframe", {}, { contentDocument: child }),
  ]);
  const h = integration([element("audio", { src: "unselected.mp3" }), element("div", {}, { shadowRoot: outer })]);
  t.after(() => h.dispose());
  await h.ready();
  assert.deepEqual(h.injections[0], {
    target: { tabId: 7, frameIds: [0] }, world: "ISOLATED", files: ["media-collector.js", "detector.js"],
  });
  assert.deepEqual(h.links[0].settings, { documentId: "top-document-1", name: "rc-music-detection-v2" });
  assert.deepEqual(new Set(h.controller.getState().candidates.map((candidate) => candidate.url)), new Set([
    "https://cdn.example/media/unselected.mp3", "https://cdn.example/media/shadow.mp3", "https://media.example/frame/chosen.mp4",
  ]));
  assert.ok(h.controller.getState().candidates.every((candidate) => /^c[1-9]\d*:d[1-9]\d*$/.test(candidate.sourceId)));
  h.controller.setSelected("https://cdn.example/media/shadow.mp3", true);
  h.controller.setSelected("https://media.example/frame/chosen.mp4", true);
  await h.controller.submit();
  assert.equal(h.requests.length, 0, "selection without consent must not reach Native Messaging");
  h.controller.setAuthorized(true);
  await h.controller.submit();
  assert.deepEqual(h.requests, [{
    schemaVersion: 1, type: "download.request", requestId,
    items: [
      { url: "https://cdn.example/media/shadow.mp3", title: "Shadow song", kind: "audio" },
      { url: "https://media.example/frame/chosen.mp4", title: "Frame video", kind: "video" },
    ],
  }]);
  assert.ok(h.requests[0].items.every((item) => !Object.hasOwn(item, "sourceId")));
  assert.equal(h.controller.getState().status, "accepted");
  assert.equal(h.controller.getState().acceptedCount, 2);
  assert.equal(h.nativePorts[0].closed, true);
});

for (const replacement of [false, true]) {
  test(`fresh real probe blocks consented child ${replacement ? "document replacement at the same URL" : "same-document URL drift"} without a stream update`, async (t) => {
    const fixture = childFixture();
    const h = integration([fixture.frame]);
    t.after(() => h.dispose());
    await h.ready();
    selectAndAuthorize(h, fixture.url);
    const messages = h.links[0].content.messages.length;
    const previous = h.controller.getState().candidates[0].sourceId;
    if (replacement) {
      fixture.frame.contentDocument = frameDocument([element("audio", { src: "song.mp3", title: "Child song" })], {
        href: fixture.child.location.href, baseURI: fixture.child.baseURI,
      });
    } else fixture.child.location.href = "https://page.example/child#two";
    assert.equal(h.links[0].content.messages.length, messages);
    await h.controller.submit();
    assert.equal(h.requests.length, 0);
    assert.equal(h.controller.getState().authorized, false);
    const fresh = h.probes.findLast((probe) => probe?.type === "snapshot");
    assert.ok(fresh, "the submit path must execute the detector's fresh isolated-world snapshot");
    assert.notEqual(fresh.candidates.find((candidate) => candidate.url === fixture.url).sourceId, previous);
  });
}

test("a context change affecting only unselected media preserves the consented selection", async (t) => {
  const selected = childFixture();
  const other = element("iframe", {}, { contentDocument: frameDocument([element("audio", { src: "ignored.mp3" })]) });
  const h = integration([selected.frame, other]);
  t.after(() => h.dispose());
  await h.ready();
  selectAndAuthorize(h, selected.url);
  other.contentDocument = frameDocument([element("video", { src: "different.mp4" })]);
  await h.controller.submit();
  assert.deepEqual(h.requests[0]?.items, [{ url: selected.url, title: "Child song", kind: "audio" }]);
  assert.equal(h.requests.length, 1);
  assert.equal(h.controller.getState().status, "accepted");
});

test("a real context stream update during final tab verification revokes only selected submissions", async (t) => {
  for (const mutateSelected of [true, false]) {
    const selected = childFixture();
    const other = frameDocument([element("audio", { src: "ignored.mp3" })]);
    const h = integration([selected.frame, element("iframe", {}, { contentDocument: other })]);
    t.after(() => h.dispose());
    await h.ready();
    selectAndAuthorize(h, selected.url);
    const link = h.links[0];
    const previousMessages = link.content.messages.length;
    const gate = h.deferTabGet(2);
    const pending = h.controller.submit();
    await h.flush();
    assert.equal(gate.started, true, "the final tab check must remain pending after the fresh probe");
    assert.ok(h.probes.some((probe) => probe?.type === "snapshot"));
    assert.equal(h.requests.length, 0);
    const changed = mutateSelected ? selected.child : other;
    changed.location.href += "#changed-during-verification";
    await h.flush(500);
    assert.equal(link.content.messages.length, previousMessages + 1, "the real periodic detector must publish the context change");
    assert.equal(h.requests.length, 0, "Native Messaging must not start while final verification is pending");
    if (mutateSelected) assert.equal(h.controller.getState().authorized, false);
    else assert.equal(h.controller.getState().status, "sending");
    gate.release();
    await pending;
    await h.flush();
    assert.equal(h.requests.length, mutateSelected ? 0 : 1);
    if (!mutateSelected) {
      assert.deepEqual(h.requests[0].items, [{ url: selected.url, title: "Child song", kind: "audio" }]);
      assert.equal(h.controller.getState().status, "accepted");
    }
  }
});

test("a valid detector snapshot cannot authorize a send after its window loses focus", async (t) => {
  const fixture = childFixture();
  const h = integration([fixture.frame]);
  t.after(() => h.dispose());
  await h.ready();
  selectAndAuthorize(h, fixture.url);

  const gate = h.deferNextProbe(2);
  const pending = h.controller.submit();
  await h.flush();
  assert.equal(gate.started, true);
  assert.equal(h.probes.at(-1)?.type, "snapshot");
  assert.equal(h.nativePorts.length, 0);

  // The original tab remains active in its own window; Chromium has not
  // delivered the focus event yet. Only the live window check can see this.
  h.browserWindow.focused = false;
  gate.release();
  await pending;
  await h.flush();
  assert.equal(h.requests.length, 0);
  assert.equal(h.nativePorts.length, 0);
  assert.equal(h.controller.getState().status, "disconnected");
  assert.equal(h.controller.getState().authorized, false);
  assert.equal(h.links[0].closed, true);
});

test("reopening a detector session revokes an old pending submission and ignores its queued snapshot", async (t) => {
  const url = "https://cdn.example/media/song.mp3";
  const h = integration([element("audio", { src: "song.mp3", title: "Song" })]);
  t.after(() => h.dispose());
  await h.ready();
  selectAndAuthorize(h, url);
  const oldLink = h.links[0];
  const oldSnapshot = clone(oldLink.content.messages.at(-1));
  const oldMessage = [...oldLink.popup.onMessage.listeners][0];
  const gate = h.deferNextProbe();
  const pending = h.controller.submit();
  await h.flush();
  assert.equal(gate.started, true);
  const reopened = h.newController();
  await h.ready(reopened);
  const current = h.links.at(-1).content.messages.at(-1);
  assert.notEqual(current.candidates[0].sourceId, oldSnapshot.candidates[0].sourceId);
  assert.equal(oldLink.closed, true);
  const before = reopened.getState();
  oldMessage(oldSnapshot);
  gate.release();
  await pending;
  await h.flush();
  assert.equal(h.requests.length, 0);
  assert.deepEqual(reopened.getState(), before);
  assert.equal(h.controller.getState().status, "disconnected");
  selectAndAuthorize(h, url, reopened);
  await reopened.submit();
  assert.equal(h.requests.length, 1);
});

test("removed roots stay retired and stopping the real composition makes queued callbacks inert", async (t) => {
  const fixture = childFixture();
  const nested = shadowRoot([element("audio", { src: "shadow.mp3" }), fixture.frame]);
  const surviving = element("audio", { src: "retained.mp3" });
  const h = integration([surviving, element("div", {}, { shadowRoot: nested })]);
  t.after(() => h.dispose());
  await h.ready();
  assert.equal(h.controller.getState().candidates.length, 3);
  const removed = h.env.observers.filter((observer) => observer.target === nested || observer.target === fixture.child);
  assert.equal(removed.length, 2);
  h.env.replaceNodes([surviving]);
  h.env.mutate(h.env.document);
  await h.flush(200);
  assert.deepEqual(h.controller.getState().candidates.map((candidate) => candidate.url), ["https://cdn.example/media/retained.mp3"]);
  assert.ok(removed.every((observer) => !observer.active));
  assert.equal(nested.listenerCount, 0);
  assert.equal(fixture.child.listenerCount, 0);
  for (const observer of removed) observer.callback([]);
  fixture.child.emit("loadedmetadata");
  await h.flush(201);
  assert.ok(removed.every((observer) => !observer.active));
  assert.deepEqual(h.controller.getState().candidates.map((candidate) => candidate.url), ["https://cdn.example/media/retained.mp3"]);
  const late = [...h.links[0].popup.onMessage.listeners][0];
  h.dispose();
  const reads = h.env.treeReads;
  const states = h.states.length;
  for (const observer of h.env.observers) observer.callback([]);
  late(clone(h.links[0].content.messages[0]));
  await h.flush(1000);
  assert.equal(h.env.treeReads, reads);
  assert.equal(h.states.length, states);
  assert.equal(h.requests.length, 0);
  assertStopped(h.env, h.links[0].content);
  for (const name of ["onUpdated", "onRemoved", "onActivated"]) assert.equal(h.chrome.tabs[name].listeners.size, 0);
  assert.equal(h.chrome.windows.onFocusChanged.listeners.size, 0);
});
