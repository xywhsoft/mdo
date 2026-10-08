import assert from "node:assert/strict";
import test from "node:test";
import { createRequestRecovery } from "../app/web/js/api/request-recovery.js";
import { loadSessionTranscript, readCompleteSessionEventText } from "../app/web/js/state/sessions.js";
import { resolveTimelineCopyText, resolveTimelineActionText } from "../app/web/js/features/chat/timeline.js";
import { resolveToolSectionText } from "../app/web/js/features/chat/tool-content.js";

const session = { project_id: "default", id: "text" };
const owner = { projectId: session.project_id, sessionId: session.id };
const epoch = "a".repeat(64);
const flush = () => new Promise(resolve => setImmediate(resolve));
const envelope = data => Response.json({ ok: true, data });
const temporary = () => Response.json({ ok: false, error: { code: "temporary_unavailable" } }, { status: 503 });
const event = (id, text, truncated = false) => ({ event_id: id, kind: "model_text_delta",
  run_id: 1, agent_id: 1, agent_turn: 1, agent_depth: 0, text, text_truncated: truncated });
const page = items => envelope({ items, next_cursor: items.at(-1)?.event_id ?? 0,
  latest_event_id: 2, history_lost: false });

function environment(context, fetch) {
  let time = 0, serial = 0, owners = 0;
  const timers = new Map(), events = new EventTarget(), calls = [], waits = [];
  const setTimer = (fn, delay) => {
    timers.set(++serial, { fn, delay, at: time + delay }); return serial;
  };
  const clearTimer = id => timers.delete(id);
  context.mock.method(globalThis, "fetch", (url, options) => {
    calls.push({ url: new URL(url, "http://localhost"), signal: options.signal, at: time });
    return fetch(calls.at(-1).url, options);
  });
  const options = { createRecovery: bounds => { owners++; return createRequestRecovery({
    ...bounds, now: () => time, random: () => 0, setTimer, clearTimer, eventTarget: events }); } };
  return { events, calls, waits, options, elapsed: () => time, owners: () => owners,
    async finish(promise) {
      let done = false, value, error;
      promise.then(result => { done = true; value = result; }, failure => { done = true; error = failure; });
      for (let n = 0; !done && n < 100; n++) {
        await flush(); if (done) break;
        const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0] ?? [];
        assert(timer, "text read stayed pending without an owned deadline");
        timers.delete(id); time = timer.at; waits.push(timer.delay); timer.fn();
      }
      assert(done, "text read never settled"); assert.equal(timers.size, 0);
      if (error) throw error; return value;
    },
  };
}

test("transcript pages and full bodies quietly retry transient reads within one owner", async context => {
  let pages = 0, full = 0;
  const env = environment(context, async url => {
    if (url.searchParams.has("full_text")) return ++full < 3 ? temporary()
      : envelope({ items: [event(1, "first complete body")] });
    if (url.searchParams.get("after") === "0") return ++pages < 3 ? temporary() : page([event(1, "first", true)]);
    return page([event(2, "second")]);
  });
  const result = await env.finish(loadSessionTranscript(session, env.options));
  assert.equal(result.textTruncated, false);
  assert.deepEqual(result.events.map(row => row.text), ["first complete body", "second"]);
  assert.equal(env.owners(), 1); assert.deepEqual(env.waits, [500, 1000, 500, 1000]);
});

test("multi-event originals share their deadline with recursive full-text reads", async context => {
  let full = 0;
  const env = environment(context, async url => url.searchParams.has("full_text")
    ? ++full < 3 ? temporary() : envelope({ items: [event(1, "first complete")] })
    : envelope({ epoch, items: [event(1, "first", true), event(2, " second")], next_cursor: 2 }));
  const result = await env.finish(readCompleteSessionEventText("default", "text", 1,
    "model_text_delta", { ...env.options, endEventId: 2, epoch }));
  assert.equal(result, "first complete second"); assert.equal(env.owners(), 1);
  assert.deepEqual(env.waits, [500, 1000]);
  assert(env.calls.every(row => row.url.searchParams.get("epoch") === epoch));
});

test("copying several spans cannot restart the one-minute budget for every span", async context => {
  let first = 0;
  const env = environment(context, async url => url.searchParams.get("after") === "0"
    ? ++first < 6 ? temporary() : envelope({ items: [event(1, "a complete")] })
    : new Promise(() => {}));
  const item = { text: "ab", textTruncated: true, copySpans: [
    { eventId: 1, kind: "model_text_delta", start: 0, end: 1 },
    { eventId: 2, kind: "model_text_delta", start: 1, end: 2 },
  ] };
  const result = await env.finish(resolveTimelineCopyText(item, owner, undefined, env.options));
  assert.deepEqual(result, { text: "ab", complete: false });
  assert.equal(env.elapsed(), 60000); assert.equal(env.owners(), 1);
  assert.equal(first, 6); assert(env.calls.at(-1).signal.aborted);
  assert.equal(item.text, "ab");
});

test("hung event pages end export with a final read error rather than a partial success", async context => {
  const env = environment(context, async url => url.searchParams.get("after") === "0"
    ? page([event(1, "first")]) : new Promise(() => {}));
  await assert.rejects(env.finish(loadSessionTranscript(session, env.options)), { code: "network_error" });
  assert.equal(env.elapsed(), 60000); assert.equal(env.owners(), 1);
  assert(env.calls.slice(1).every(row => row.signal.aborted));
});

test("hung full bodies produce an honest preview export without holding later reads", async context => {
  const env = environment(context, async url => url.searchParams.has("full_text")
    ? new Promise(() => {}) : page([event(1, "first", true), event(2, "second", true)]));
  const result = await env.finish(loadSessionTranscript(session, env.options));
  assert.equal(result.textTruncated, true); assert.equal(env.elapsed(), 60000);
  assert.deepEqual(result.events.map(row => row.text), ["first", "second"]);
  assert(env.calls.slice(1).every(row => row.url.searchParams.get("after") === "0"));
  assert.equal(env.owners(), 1);
});

test("leaving the page cancels export and copy instead of completing an old action", async context => {
  for (const run of [options => loadSessionTranscript(session, options),
    options => resolveTimelineCopyText({ text: "first", textTruncated: true,
      copySpans: [{ eventId: 1, kind: "model_text_delta", start: 0, end: 5 }] }, owner, undefined, options),
    options => resolveToolSectionText({ outputText: "summary", outputEventId: 2, artifactId: 1 },
      "output", owner, undefined, undefined, options)]) {
    const env = environment(context, () => new Promise(() => {}));
    const pending = run(env.options);
    await flush(); env.events.dispatchEvent(new Event("pagehide"));
    await assert.rejects(env.finish(pending), { name: "AbortError" });
    assert.equal(env.elapsed(), 0); assert.equal(env.calls.length, 1);
    assert.equal(env.calls[0].signal.aborted, true);
    context.mock.restoreAll();
  }
});

test("history edit/retry does not accept a visible prefix after recovery is exhausted", async context => {
  const env = environment(context, async () => temporary());
  await assert.rejects(env.finish(resolveTimelineActionText({ text: "first", textTruncated: true,
    copySpans: [{ eventId: 1, kind: "model_text_delta", start: 0, end: 5 }] }, owner, undefined, env.options)), /完整消息/);
  assert.equal(env.calls.length, 6); assert.equal(env.owners(), 1);
});

test("tool artifact bodies recover quietly and then return complete UTF-8 text", async context => {
  let reads = 0;
  const previous = globalThis.window;
  globalThis.window = { atob };
  context.after(() => { if (previous === undefined) delete globalThis.window; else globalThis.window = previous; });
  const env = environment(context, async () => ++reads < 3 ? temporary()
    : envelope({ eof: true, media_type: "text/plain", data: btoa("original output") }));
  const result = await env.finish(resolveToolSectionText({ outputText: "summary",
    outputEventId: 2, artifactId: 1 }, "output", owner, undefined, undefined, env.options));
  assert.deepEqual(result, { text: "original output", complete: true });
  assert.equal(env.owners(), 1); assert.deepEqual(env.waits, [500, 1000]);
});

test("hung tool artifacts return the visible section within the common deadline", async context => {
  const env = environment(context, () => new Promise(() => {}));
  const result = await env.finish(resolveToolSectionText({ outputText: "summary",
    outputEventId: 2, artifactId: 1 }, "output", owner, undefined, undefined, env.options));
  assert.deepEqual(result, { text: "summary", complete: false });
  assert.equal(env.elapsed(), 60000); assert.equal(env.owners(), 1);
  assert(env.calls.every(row => row.signal.aborted));
});
