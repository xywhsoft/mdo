import assert from "node:assert/strict";
import test from "node:test";

import { asksStore, clearAsks, selectAsks, refreshSelectedAsks } from
  "../app/web/js/state/asks.js";

const pending = { total: 1, items: [{ id: 7, run_id: 8,
  question: "Choose the next step", options: ["Continue", "Review"] }] };
const ready = (data = pending) => Response.json({ ok: true, data });
const failed = () => Response.json({ ok: false,
  error: { code: "fixture_read_failed", message: "Temporary read failure" } }, { status: 503 });

function deferred() {
  let resolve;
  const promise = new Promise((done) => { resolve = done; });
  return { promise, resolve };
}

test("a healthy unchanged question snapshot clears a transient read error", async () => {
  const original = globalThis.fetch;
  let fail = false;
  globalThis.fetch = async () => fail ? failed() : ready();
  try {
    await selectAsks("qa", "session");
    const previous = asksStore.get().data;
    fail = true;
    await refreshSelectedAsks();
    assert.equal(asksStore.get().status, "error");
    assert.equal(asksStore.get().data, previous);
    fail = false;
    await refreshSelectedAsks();
    assert.equal(asksStore.get().status, "ready");
    assert.equal(asksStore.get().error, null);
    assert.deepEqual(asksStore.get().data.items, pending.items);
    assert.equal(asksStore.get().data.loaded, true);
  } finally { globalThis.fetch = original; clearAsks(); }
});

test("a late question snapshot cannot redisplay a question absent from the newer read", async () => {
  const original = globalThis.fetch;
  const late = deferred();
  let next = ready;
  globalThis.fetch = async () => next();
  try {
    await selectAsks("qa", "session");
    next = () => late.promise;
    const stale = refreshSelectedAsks();
    next = () => ready({ total: 0, items: [] });
    await refreshSelectedAsks();
    assert.equal(asksStore.get().data.total, 0);
    late.resolve(ready());
    await stale;
    assert.deepEqual(asksStore.get().data.items, []);
    assert.equal(asksStore.get().data.total, 0);
    assert.equal(asksStore.get().status, "ready");
  } finally { late.resolve(ready()); globalThis.fetch = original; clearAsks(); }
});

test("an older failure cannot replace a newer healthy read in the same session", async () => {
  const original = globalThis.fetch;
  const late = deferred();
  let next = ready;
  globalThis.fetch = async () => next();
  try {
    await selectAsks("qa", "session");
    next = () => late.promise;
    const stale = refreshSelectedAsks();
    next = ready;
    await refreshSelectedAsks();
    late.resolve(failed());
    await stale;
    assert.equal(asksStore.get().status, "ready");
    assert.equal(asksStore.get().error, null);
  } finally { late.resolve(ready()); globalThis.fetch = original; clearAsks(); }
});

test("switching or clearing questions invalidates old reads even with reused IDs", async () => {
  const original = globalThis.fetch;
  const late = deferred();
  const cleared = deferred();
  globalThis.fetch = async () => ready();
  try {
    await selectAsks("qa", "first");
    globalThis.fetch = async () => late.promise;
    const stale = refreshSelectedAsks();
    globalThis.fetch = async () => ready({ total: 1,
      items: [{ ...pending.items[0], question: "Second session" }] });
    await selectAsks("qa", "second");
    late.resolve(ready());
    await stale;
    assert.equal(asksStore.get().data.sessionId, "second");
    assert.equal(asksStore.get().data.items[0].question, "Second session");
    globalThis.fetch = async () => cleared.promise;
    const pendingClear = refreshSelectedAsks();
    clearAsks();
    cleared.resolve(ready());
    await pendingClear;
    assert.equal(asksStore.get().data.loaded, false);
    assert.equal(asksStore.get().data.sessionId, "");
    assert.deepEqual(asksStore.get().data.items, []);
  } finally {
    late.resolve(ready()); cleared.resolve(ready());
    globalThis.fetch = original; clearAsks();
  }
});

test("identical healthy snapshots do not republish or remount the question editor", async () => {
  const original = globalThis.fetch;
  globalThis.fetch = async () => ready();
  let count = 0;
  let unsubscribe;
  try {
    await selectAsks("qa", "session");
    const previous = asksStore.get();
    unsubscribe = asksStore.subscribe(() => { count += 1; });
    await refreshSelectedAsks();
    await refreshSelectedAsks();
    assert.equal(asksStore.get(), previous);
    assert.equal(count, 1);
  } finally { unsubscribe?.(); globalThis.fetch = original; clearAsks(); }
});
