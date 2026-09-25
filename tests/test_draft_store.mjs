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
    saved = { text: body.text, attachments: body.attachments };
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
