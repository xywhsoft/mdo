import assert from "node:assert/strict";
import test from "node:test";

import { createDraftStore } from "../app/web/js/features/chat/draft-store.js";

test("a failed send restores its text and images ahead of a newer saved draft", async () => {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  const firstImage = "a".repeat(32);
  const nextImage = "b".repeat(32);
  let revision = 1;
  let saved = { text: "", attachments: [] };
  globalThis.window = { setTimeout, clearTimeout, addEventListener() {} };
  globalThis.fetch = async (_path, options) => {
    if (options.method === "GET")
      return Response.json({ ok: true, data: { ...saved, revision } });
    assert.equal(options.method, "PUT");
    const body = JSON.parse(options.body);
    assert.equal(body.revision, revision);
    saved = { text: body.text, attachments: body.attachments,
      run_admission_uncertain: body.run_admission_uncertain };
    revision += 1;
    return Response.json({ ok: true, data: { ...saved, revision } });
  };
  try {
    const store = createDraftStore({ onRestore() {}, onError: (error) => {
      throw error;
    }, onSaved() {} });
    const key = "default/qa";
    store.select(key);
    await store.flush(key);
    store.edit(key, "submitted", [firstImage], true);
    store.clear(key);
    store.edit(key, "next draft", [nextImage], true);
    const recovered = store.restoreUnsent(key, "submitted", [firstImage]);
    assert.deepEqual(recovered, {
      text: "submitted\n\nnext draft",
      attachments: [firstImage, nextImage], merged: true,
    });
    assert.equal(await store.flush(key), true);
    assert.deepEqual(saved, {
      text: recovered.text, attachments: recovered.attachments,
      run_admission_uncertain: false,
    });
    assert.equal(store.clearIfMatches(key, "submitted", [firstImage]), false);
    store.edit(key, "submitted", [nextImage], true);
    assert.deepEqual(store.restoreUnsent(key, "submitted", [firstImage]), {
      text: "submitted", attachments: [firstImage, nextImage], merged: true,
    });
    assert.equal(await store.flush(key), true);
  } finally {
    globalThis.window = originalWindow;
    globalThis.fetch = originalFetch;
  }
});

test("an edit before draft loading keeps the persisted run review guard", async () => {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  let releaseGet;
  const getReady = new Promise((resolve) => { releaseGet = resolve; });
  let saved;
  globalThis.window = { setTimeout, clearTimeout, addEventListener() {} };
  globalThis.fetch = async (_path, options) => {
    if (options.method === "GET") {
      await getReady;
      return Response.json({ ok: true, data: { revision: 1,
        text: "prior text", attachments: [], run_admission_uncertain: true } });
    }
    saved = JSON.parse(options.body);
    return Response.json({ ok: true, data: { revision: 2, ...saved } });
  };
  try {
    const key = "default/slow-load";
    let restored;
    const store = createDraftStore({
      onRestore(text, _attachments, uncertain) {
        restored = { text, uncertain };
      },
      onError(error) { throw error; },
      onSaved() {},
    });
    store.select(key);
    assert.equal(store.isLoaded(key), false);
    store.edit(key, "typed during load", [], true);
    releaseGet();
    assert.equal(await store.flush(key), true);
    assert.equal(store.isLoaded(key), true);
    assert.deepEqual(restored, { text: "typed during load", uncertain: true });
    assert.equal(saved.text, "typed during load");
    assert.equal(saved.run_admission_uncertain, true);
  } finally {
    globalThis.window = originalWindow;
    globalThis.fetch = originalFetch;
  }
});

test("an uncertain run blocks retry across draft edits and reload until acknowledged", async () => {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  let revision = 0;
  let saved = { text: "", attachments: [], run_admission_uncertain: false };
  globalThis.window = { setTimeout, clearTimeout, addEventListener() {} };
  globalThis.fetch = async (_path, options) => {
    if (options.method === "GET")
      return Response.json({ ok: true, data: { ...saved, revision } });
    const body = JSON.parse(options.body);
    assert.equal(body.revision, revision);
    saved = { text: body.text, attachments: body.attachments,
      run_admission_uncertain: body.run_admission_uncertain };
    revision += 1;
    return Response.json({ ok: true, data: { ...saved, revision } });
  };
  try {
    const key = "default/review";
    const first = createDraftStore({ onRestore() {}, onError: (error) => {
      throw error;
    }, onSaved() {} });
    first.select(key);
    first.edit(key, "maybe sent", [], true);
    first.setRunUncertain(key, true);
    assert.equal(await first.flush(key), true);
    // select() may finish its initial GET after the edit and schedule a flush.
    await new Promise((resolve) => setTimeout(resolve, 10));
    assert.equal(await first.flush(key), true);
    assert.equal(saved.run_admission_uncertain, true);
    const restored = createDraftStore({ onRestore() {}, onError: (error) => {
      throw error;
    }, onSaved() {} });
    restored.select(key);
    assert.equal(await restored.flush(key), true);
    assert.equal(restored.isRunUncertain(key), true);
    restored.edit(key, "new draft", [], true);
    assert.equal(await restored.flush(key), true);
    assert.equal(saved.run_admission_uncertain, true);
    restored.setRunUncertain(key, false);
    assert.equal(await restored.flush(key), true);
    assert.equal(saved.run_admission_uncertain, false);
  } finally {
    globalThis.window = originalWindow;
    globalThis.fetch = originalFetch;
  }
});
