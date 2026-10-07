import assert from "node:assert/strict";
import test from "node:test";
import { uploadImageWithRecovery } from "../app/web/js/api/image-upload.js";
import { api, hasPendingApiWrites } from "../app/web/js/api/client.js";

const id = "a".repeat(32), file = { name: "pixel.png", size: 80 }, mime = "image/png";
const saved = { id, size: file.size, file_name: file.name, mime_type: mime };
const transient = () => Object.assign(new Error("lost connection"), { code: "network_error" });
const absent = () => Object.assign(new Error("not saved"), { code: "attachment_not_found", status: 404 });
function fixture(extra = {}) {
  let time = 0, serial = 0;
  const timers = new Map(), delays = [], calls = [];
  const options = { id, now: () => time, random: () => 0,
    setTimer(fn, delay) { timers.set(++serial, { fn, at: time + delay, delay }); return serial; },
    clearTimer(timer) { timers.delete(timer); }, ...extra };
  return { calls, delays, timers, options,
    async run() {
      let result, error, settled = false;
      uploadImageWithRecovery("default", "session", file, mime, options).then(
        value => { result = value; settled = true; }, value => { error = value; settled = true; });
      for (let step = 0; !settled && step < 1200; ++step) {
        await new Promise(resolve => setImmediate(resolve));
        if (settled) break;
        assert.ok(timers.size, "waiting recovery must have a bounded timer");
        const [timer, entry] = [...timers].sort((a, b) => a[1].at - b[1].at)[0];
        timers.delete(timer); time = entry.at; delays.push(entry.delay); entry.fn();
      }
      assert.equal(settled, true); assert.equal(timers.size, 0);
      if (error) throw error;
      return result;
    }, get elapsed() { return time; },
  };
}

test("a lost upload acknowledgement is read back without a second write", async () => {
  const env = fixture({ upload: async () => { env.calls.push("PUT"); throw transient(); },
    read: async () => { env.calls.push("GET"); return saved; } });
  assert.deepEqual(await env.run(), saved);
  assert.deepEqual(env.calls, ["PUT", "GET"]); assert.equal(env.elapsed, 1000);
});

test("an upload that was not accepted retries the same identity after read confirmation", async () => {
  let writes = 0;
  const env = fixture({ upload: async () => { env.calls.push("PUT");
    if (!writes++) throw transient(); return saved; },
    read: async () => { env.calls.push("GET"); throw absent(); } });
  assert.deepEqual(await env.run(), saved);
  assert.deepEqual(env.calls, ["PUT", "GET", "PUT"]);
});

test("a failing confirmation read backs off without replaying the image", async () => {
  let reads = 0;
  const env = fixture({ upload: async () => { env.calls.push("PUT"); throw transient(); },
    read: async () => { env.calls.push("GET"); if (++reads < 3) throw transient(); return saved; } });
  assert.deepEqual(await env.run(), saved);
  assert.deepEqual(env.calls, ["PUT", "GET", "GET", "GET"]);
  assert.equal(env.elapsed, 7000);
});

test("temporary failures exhaust once with bounded delay and a useful final cause", async () => {
  const env = fixture({ random: () => 1,
    upload: async () => { env.calls.push("PUT"); throw transient(); },
    read: async () => { env.calls.push("GET"); throw transient(); } });
  await assert.rejects(env.run(), error => error.code === "image_upload_unavailable" &&
    error.cause.code === "network_error");
  assert.deepEqual(env.calls, ["PUT", "GET", "GET", "GET", "GET", "GET"]);
  assert.equal(env.elapsed, 36000);
});

test("an actual aborted transfer obeys the shared recovery deadline", async () => {
  const hung = signal => new Promise((_resolve, reject) => {
    signal.addEventListener("abort", () => reject(new DOMException("timeout", "AbortError")), { once: true });
  });
  const env = fixture({ upload: hung, read: hung });
  await assert.rejects(env.run(), { code: "image_upload_unavailable" });
  assert.equal(env.elapsed, 90000);
});

test("quota, format, conflicts and write-generation fences never retry", async () => {
  for (const code of ["attachment_storage_full", "image_type_invalid", "image_upload_conflict",
      "write_token_conflict", "purge_review_required", "remote_runtime_changed"]) {
    let writes = 0, reads = 0;
    const env = fixture({ upload: async () => { writes++; throw Object.assign(new Error(code), { code }); },
      read: async () => { reads++; return saved; } });
    await assert.rejects(env.run(), { code });
    assert.equal(writes, 1); assert.equal(reads, 0); assert.equal(env.elapsed, 0);
  }
});

test("an older device rejecting PUT does not fall back to an uncertain POST", async () => {
  let writes = 0;
  const env = fixture({ upload: async () => { writes++; throw Object.assign(new Error("old device"), { status: 405 }); } });
  await assert.rejects(env.run(), { code: "image_upload_unsupported" }); assert.equal(writes, 1);
});

test("changing the editor during recovery prevents any further request", async () => {
  let selected = true;
  const env = fixture({ canContinue: () => selected,
    upload: async () => { env.calls.push("PUT"); selected = false; throw transient(); },
    read: async () => { env.calls.push("GET"); return saved; } });
  await assert.rejects(env.run(), { name: "AbortError" });
  assert.deepEqual(env.calls, ["PUT"]);
});

test("an accepted response after navigation stays available for owner cleanup", async () => {
  let selected = true;
  const env = fixture({ canContinue: () => selected,
    upload: async () => { selected = false; return saved; } });
  assert.deepEqual(await env.run(), saved);
});

test("a mismatched receipt never adds another file to the editor", async () => {
  const env = fixture({ upload: async () => ({ ...saved, id: "b".repeat(32) }) });
  await assert.rejects(env.run(), { code: "image_upload_conflict" });
});

test("binary transport uses a keyed PUT and releases write tracking on cancellation", async () => {
  const originalFetch = globalThis.fetch;
  const controller = new AbortController(); let observed;
  globalThis.fetch = async (url, options) => {
    observed = { url, options };
    return new Promise((_resolve, reject) => options.signal.addEventListener("abort", () =>
      reject(new DOMException("cancelled", "AbortError")), { once: true }));
  };
  try {
    const pending = api.uploadImage("default", "session", file, mime,
      { uploadId: id, signal: controller.signal });
    assert.equal(hasPendingApiWrites(), true);
    assert.equal(observed.url, `/api/v1/projects/default/sessions/session/attachments/${id}`);
    assert.equal(observed.options.method, "PUT"); assert.equal(observed.options.body, file);
    controller.abort(); await assert.rejects(pending, { name: "AbortError" });
    assert.equal(hasPendingApiWrites(), false);
  } finally { globalThis.fetch = originalFetch; }
});
