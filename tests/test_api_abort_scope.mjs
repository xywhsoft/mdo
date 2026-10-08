import assert from "node:assert/strict";
import test from "node:test";
import { api, apiRequest, uploadBackupChunk, hasPendingApiWrites,
  currentPageWriteToken, setApiNetworkErrorHandler, setApiWriteConflictHandler } from "../app/web/js/api/client.js";

const flush = () => new Promise(resolve => setImmediate(resolve));
const id = "a".repeat(32), file = { name: "pixel.png", size: 80 };
const requests = [
  ["JSON read", signal => api.get("/owned", { signal }), false],
  ["JSON write", signal => api.put("/owned", { text: "draft" }, { signal }), true],
  ["image write", signal => api.uploadImage("default", "session", file, "image/png", { uploadId: id, signal }), true],
  ["backup chunk", signal => uploadBackupChunk(id, 0, new Uint8Array([1]), { signal }), true],
];
for (const [name, request, writes] of requests) {
  test(`${name} releases an uncooperative body on abort and ignores late conflict feedback`, async () => {
    const originalFetch = globalThis.fetch, controller = new AbortController();
    let complete, settled = false, caught, conflicts = 0;
    setApiWriteConflictHandler(() => conflicts++);
    globalThis.fetch = async () => ({ ok: false, status: 409,
      json: () => new Promise(resolve => { complete = resolve; }) });
    try {
      const pending = request(controller.signal).then(() => { settled = true; }, error => { settled = true; caught = error; });
      await flush(); assert.equal(hasPendingApiWrites(), writes);
      controller.abort(); await flush();
      assert.equal(settled, true); assert.equal(caught?.name, "AbortError");
      assert.equal(hasPendingApiWrites(), false); await pending;
      complete({ ok: false, error: { code: "write_token_conflict", message: "old reply" } });
      await flush(); assert.equal(conflicts, 0);
    } finally { globalThis.fetch = originalFetch; setApiWriteConflictHandler(null);
      complete?.({ ok: true, data: {} }); await flush(); }
  });
}

test("abort releases an uncooperative fetch and suppresses its late network notification", async () => {
  const originalFetch = globalThis.fetch, controller = new AbortController();
  let reject, settled = false, caught, notices = 0;
  globalThis.fetch = () => new Promise((_, fail) => { reject = fail; });
  setApiNetworkErrorHandler(() => notices++);
  try {
    const pending = apiRequest("/owned", { method: "POST", signal: controller.signal }).then(
      () => { settled = true; }, error => { settled = true; caught = error; });
    await flush(); controller.abort(); await flush();
    assert.equal(settled, true); assert.equal(caught?.name, "AbortError");
    assert.equal(hasPendingApiWrites(), false); await pending;
    reject(new Error("late offline")); await flush(); assert.equal(notices, 0);
  } finally { globalThis.fetch = originalFetch; setApiNetworkErrorHandler(null);
    reject?.(new DOMException("cancelled", "AbortError")); await flush(); }
});

test("an already cancelled request starts neither its transport nor write tracking", async () => {
  const originalFetch = globalThis.fetch, controller = new AbortController();
  let sends = 0, complete; controller.abort();
  globalThis.fetch = () => { sends++; return new Promise(resolve => { complete = resolve; }); };
  try {
    let settled = false, error;
    const pending = api.post("/owned", {}, { signal: controller.signal }).then(
      () => { settled = true; }, value => { settled = true; error = value; });
    await flush(); assert.equal(settled, true); assert.equal(error?.name, "AbortError");
    assert.equal(sends, 0); assert.equal(hasPendingApiWrites(), false); await pending;
  } finally { globalThis.fetch = originalFetch; complete?.(Response.json({ ok: true, data: {} })); await flush(); }
});

test("a cancelled startup read cannot adopt a late write token", async () => {
  const originalFetch = globalThis.fetch, controller = new AbortController();
  let complete, settled = false, caught;
  globalThis.fetch = async () => ({ ok: true, status: 200,
    headers: new Headers({ "X-Mdo-Write-Token": `${id}-1` }),
    json: () => new Promise(resolve => { complete = resolve; }) });
  try {
    const pending = api.get("/project-purge-intent", { signal: controller.signal }).then(
      () => { settled = true; }, error => { settled = true; caught = error; });
    await flush(); controller.abort(); await flush();
    assert.equal(settled, true); assert.equal(caught?.name, "AbortError"); await pending;
    complete({ ok: true, data: { empty: true } }); await flush();
    assert.equal(currentPageWriteToken(), null);
  } finally { globalThis.fetch = originalFetch; complete?.({ ok: true, data: {} }); await flush(); }
});
