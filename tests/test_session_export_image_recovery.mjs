import assert from "node:assert/strict";
import test from "node:test";
import { createRequestRecovery } from "../app/web/js/api/request-recovery.js";
import { loadSessionMarkdownImages } from "../app/web/js/features/sessions/session-export-images.js";

const id = "a".repeat(32), nextId = "b".repeat(32);
const session = { project_id: "default", id: "export" };
const transcript = { events: [{ event_id: 1, kind: "agent_start", run_id: 1,
  agent_depth: 0, user_message_sequence: 1, text: "image", attachments: [id, nextId] }] };
const png = new Uint8Array([137, 80, 78, 71]);
const metadata = value => Response.json({ ok: true, data: { id: value,
  schema_version: 2, mime_type: "image/png", size: png.length, file_name: "export.png" } });
const temporary = () => Response.json({ ok: false, error: { code: "temporary_unavailable" } }, { status: 503 });
const response = () => new Response(png, { headers: { "Content-Type": "image/png" } });
const flush = () => new Promise(resolve => setImmediate(resolve));

function environment(context, fetch) {
  let time = 0, serial = 0;
  const timers = new Map(), events = new EventTarget(), calls = [], waits = [];
  const setTimer = (fn, delay) => {
    timers.set(++serial, { fn, delay, at: time + delay }); return serial;
  };
  const clearTimer = id => timers.delete(id);
  // Observe both the legacy deadline and the shared recovery timers without
  // wall-clock sleeps; every test restores these globals automatically.
  context.mock.method(globalThis, "setTimeout", setTimer);
  context.mock.method(globalThis, "clearTimeout", clearTimer);
  context.mock.method(globalThis, "fetch", (url, options) => {
    calls.push(String(url)); return fetch(String(url), options);
  });
  const options = { timeoutMs: 30000, createRecovery: bounds => createRequestRecovery({
    ...bounds, now: () => time, random: () => 0, setTimer, clearTimer, eventTarget: events }) };
  return { events, calls, waits, options, timers, elapsed: () => time,
    async finish(promise) {
      let done = false, value, error;
      promise.then(result => { done = true; value = result; }, failure => { done = true; error = failure; });
      for (let n = 0; !done && n < 100; n++) {
        await flush(); if (done) break;
        const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0] ?? [];
        assert(timer, "export stayed pending after its deadline/cancellation");
        timers.delete(id); time = timer.at; waits.push(timer.delay); timer.fn();
      }
      assert(done, "export never settled"); assert.equal(timers.size, 0);
      if (error) throw error; return value;
    },
  };
}

test("temporary image metadata errors recover quietly before adding an incomplete warning", async context => {
  let reads = 0;
  const env = environment(context, async url => {
    if (url.endsWith("/info")) return ++reads < 3 ? temporary() : metadata(id);
    return response();
  });
  const result = await env.finish(loadSessionMarkdownImages(session,
    { events: [{ ...transcript.events[0], attachments: [id] }] }, env.options));
  assert.equal(result.imagesIncomplete, false); assert.equal(result.images.size, 1);
  assert.deepEqual(env.waits, [500, 1000]); assert.equal(reads, 3);
});

test("temporary binary image failures retry the same read without consuming a second reservation", async context => {
  let reads = 0;
  const env = environment(context, async url => url.endsWith("/info") ? metadata(url.includes(nextId) ? nextId : id)
    : ++reads < 3 ? temporary() : response());
  const result = await env.finish(loadSessionMarkdownImages(session, transcript,
    { ...env.options, maxBytes: png.length }));
  assert.equal(result.images.size, 1); assert.equal(result.imagesIncomplete, true);
  assert.equal(reads, 3); assert.deepEqual(env.waits, [500, 1000]);
  assert.equal(env.calls.filter(url => url.includes(nextId) && !url.endsWith("/info")).length, 0);
});

test("a disconnected fetch or image stream recovers before reporting an incomplete export", async context => {
  let reads = 0, cancelled = 0;
  const env = environment(context, async url => {
    if (url.endsWith("/info")) return metadata(id);
    if (++reads === 1) throw new TypeError("Failed to fetch");
    if (reads === 2) return { ok: true, headers: new Headers({ "Content-Type": "image/png" }),
      body: { getReader: () => ({ read: async () => { throw new TypeError("Connection lost"); },
        cancel: async () => { cancelled++; }, releaseLock() {} }) } };
    return response();
  });
  const result = await env.finish(loadSessionMarkdownImages(session,
    { events: [{ ...transcript.events[0], attachments: [id] }] }, env.options));
  assert.equal(result.imagesIncomplete, false); assert.equal(result.images.size, 1);
  assert.equal(reads, 3); assert.equal(cancelled, 1);
  assert.deepEqual(env.waits, [500, 1000]);
});

test("an image fetch ignoring abort returns an honest partial export at the shared deadline", async context => {
  let signal;
  const env = environment(context, async (url, options) => url.endsWith("/info") ? metadata(id)
    : (signal = options.signal, new Promise(() => {})));
  const result = await env.finish(loadSessionMarkdownImages(session, transcript, env.options));
  assert.equal(env.elapsed(), 30000); assert.equal(signal.aborted, true);
  assert.equal(result.images.size, 0); assert.equal(result.imagesIncomplete, true);
  assert.equal(env.calls.length, 2);
});

test("a response reader and its cancel promise cannot hold export beyond the deadline", async context => {
  let signal, cancelled = 0, released = 0;
  const env = environment(context, async (url, options) => {
    if (url.endsWith("/info")) return metadata(id);
    signal = options.signal;
    return { ok: true, headers: new Headers({ "Content-Type": "image/png" }), body: {
      getReader: () => ({ read: () => new Promise(() => {}),
        cancel: () => { cancelled++; return new Promise(() => {}); }, releaseLock: () => { released++; } }),
    } };
  });
  const result = await env.finish(loadSessionMarkdownImages(session, transcript, env.options));
  assert.equal(env.elapsed(), 30000); assert.equal(signal.aborted, true);
  assert.equal(result.images.size, 0); assert.equal(result.imagesIncomplete, true);
  assert.equal(cancelled, 1); assert.equal(released, 1); assert.equal(env.calls.length, 2);
});

test("leaving the page cancels image recovery without creating a partial download", async context => {
  let signal;
  const env = environment(context, async (url, options) => url.endsWith("/info") ? metadata(id)
    : (signal = options.signal, new Promise(() => {})));
  const pending = loadSessionMarkdownImages(session, transcript, env.options);
  await flush(); env.events.dispatchEvent(new Event("pagehide"));
  await assert.rejects(env.finish(pending), { name: "AbortError" });
  assert.equal(signal.aborted, true); assert.equal(env.calls.length, 2);
  assert.equal(env.elapsed(), 0);
});

test("a late image response cannot add a payload after export settled", async context => {
  let complete, cancelled = 0;
  const env = environment(context, async url => url.endsWith("/info") ? metadata(id)
    : new Promise(resolve => { complete = resolve; }));
  const result = await env.finish(loadSessionMarkdownImages(session, transcript, env.options));
  complete({ ok: true, body: { cancel: async () => { cancelled++; } } }); await flush();
  assert.equal(result.images.size, 0); assert.equal(cancelled, 1);
  assert.equal(env.calls.length, 2);
});
