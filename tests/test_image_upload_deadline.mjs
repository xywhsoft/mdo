import assert from "node:assert/strict";
import test from "node:test";
import { uploadImageWithRecovery } from "../app/web/js/api/image-upload.js";

const id = "a".repeat(32), file = { name: "pixel.png", size: 80 }, mime = "image/png";
const saved = { id, size: file.size, file_name: file.name, mime_type: mime };
const flush = () => new Promise(resolve => setImmediate(resolve));
const lost = () => Object.assign(new Error("lost response"), { code: "network_error" });
function setup(overrides = {}) {
  let time = 0, serial = 0, settled = false, result, error;
  const timers = new Map(), events = new EventTarget(), calls = [];
  const options = { id, eventTarget: events, now: () => time, random: () => 0,
    setTimer(fn, ms) { timers.set(++serial, { fn, at: time + ms }); return serial; },
    clearTimer: id => timers.delete(id),
    upload: () => saved, read: () => saved, ...overrides };
  for (const method of ["upload", "read"]) {
    const operation = options[method];
    options[method] = signal => { calls.push(method); return operation(signal); };
  }
  const pending = uploadImageWithRecovery("default", "session", file, mime, options).then(
    value => { settled = true; result = value; }, value => { settled = true; error = value; });
  return { events, calls, timers, pending, get time() { return time; },
    get settled() { return settled; }, get result() { return result; }, get error() { return error; },
    async step() {
      await flush();
      if (settled) return;
      const [key, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0] ?? [];
      assert(timer, "a pending transfer must have its own deadline");
      timers.delete(key); time = timer.at; timer.fn(); await flush();
    },
    async finish() { for (let n = 0; !settled && n < 1200; n++) await this.step();
      assert(settled, "transfer never settled"); await pending; assert.equal(timers.size, 0); },
  };
}

test("an upload ignoring abort ends at thirty seconds and confirms the same image once", async () => {
  let signal, complete;
  const ctx = setup({ upload(value) { signal = value; return new Promise(resolve => { complete = resolve; }); } });
  await ctx.step(); assert.equal(ctx.time, 30000); assert.equal(signal.aborted, true);
  await ctx.finish(); assert.equal(ctx.time, 31000);
  assert.deepEqual(ctx.result, saved); assert.deepEqual(ctx.calls, ["upload", "read"]);
  complete({ ...saved, id: "b".repeat(32) }); await flush(); assert.deepEqual(ctx.result, saved);
});

test("a confirmation body ignoring abort retries only the read before returning its receipt", async () => {
  let reads = 0, signal;
  const ctx = setup({ upload: () => { throw lost(); }, read(value) {
    if (++reads === 1) { signal = value; return new Promise(() => {}); } return saved; } });
  await ctx.finish(); assert.equal(ctx.error, undefined); assert.equal(signal.aborted, true);
  assert.equal(ctx.time, 33000); assert.deepEqual(ctx.calls, ["upload", "read", "read"]);
});

test("uncooperative transfers exhaust the shared ninety-second budget without extra writes", async () => {
  const ctx = setup({ upload: () => new Promise(() => {}), read: () => new Promise(() => {}) });
  await ctx.finish(); assert.equal(ctx.time, 90000);
  assert.equal(ctx.error?.code, "image_upload_unavailable");
  assert.equal(ctx.error?.cause?.code, "image_upload_timeout");
  assert.deepEqual(ctx.calls, ["upload", "read", "read"]);
});

test("leaving the page releases an uncooperative transfer and starts no confirmation", async () => {
  let signal;
  const ctx = setup({ upload(value) { signal = value; return new Promise(() => {}); } });
  await flush(); ctx.events.dispatchEvent(new Event("pagehide")); await flush();
  assert.equal(ctx.settled, true); assert.equal(ctx.error?.name, "AbortError");
  assert.equal(signal.aborted, true); assert.equal(ctx.timers.size, 0);
  assert.deepEqual(ctx.calls, ["upload"]);
});

test("leaving during backoff ends its wait before another upload or read", async () => {
  const ctx = setup({ upload: () => { throw lost(); } });
  await flush(); ctx.events.dispatchEvent(new Event("pagehide"));
  await ctx.finish(); assert(ctx.time <= 100); assert.equal(ctx.error?.name, "AbortError");
  assert.deepEqual(ctx.calls, ["upload"]);
});

test("leaving before the transfer microtask sends nothing", async () => {
  const ctx = setup(); ctx.events.dispatchEvent(new Event("pagehide")); await flush();
  assert.equal(ctx.settled, true); assert.equal(ctx.error?.name, "AbortError");
  assert.deepEqual(ctx.calls, []); assert.equal(ctx.timers.size, 0);
});
