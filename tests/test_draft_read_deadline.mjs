import assert from "node:assert/strict";
import test from "node:test";
import { createDraftStore } from "../app/web/js/features/chat/draft-store.js";
import { createRequestRecovery } from "../app/web/js/api/request-recovery.js";

const settle = () => new Promise(resolve => setImmediate(resolve));
const draft = (text = "", extra = {}) => ({ revision: 4, text, attachments: [],
  submissions: [], composer_profile: null, ...extra });
const reply = data => Response.json({ ok: true, data });
function fixture() {
  const previous = { window: globalThis.window, fetch: globalThis.fetch };
  let time = 0, serial = 0, fetcher;
  const timers = new Map(), errors = [], restores = [], calls = [];
  const setTimer = (fn, delay) => { timers.set(++serial, { fn, at: time + delay }); return serial; };
  globalThis.window = Object.assign(new EventTarget(), {
    setTimeout: setTimer, clearTimeout: id => timers.delete(id),
  });
  globalThis.fetch = (url, options) => { calls.push({ url, options }); return fetcher(url, options); };
  const store = createDraftStore({ onRestore: text => restores.push(text),
    onError: (error, operation) => errors.push({ code: error.code, operation }), onSaved() {},
    createRecovery: () => createRequestRecovery({ now: () => time, random: () => 0,
      setTimer, clearTimer: id => timers.delete(id), eventTarget: null }) });
  return { store, timers, errors, restores, calls, get time() { return time; },
    set fetcher(value) { fetcher = value; },
    async step() {
      const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0] ?? [];
      assert.ok(timer, "a cold draft read must have a deadline or a retry timer");
      timers.delete(id); time = timer.at; timer.fn(); await settle();
    },
    restore() { window.dispatchEvent(new Event("pagehide")); Object.assign(globalThis, previous); },
  };
}

test("a hanging cold body retries quietly and preserves input before its late completion", async () => {
  const env = fixture(), key = "default/cold";
  let finish, firstSignal, settled = false, reads = 0;
  env.fetcher = async (_url, options) => {
    if (options.method === "PUT") return reply({ ...JSON.parse(options.body), revision: 5 });
    assert.equal(options.method, "GET");
    if (++reads === 1) { firstSignal = options.signal; return { ok: true, status: 200,
      headers: new Headers(), json: () => new Promise(resolve => { finish = resolve; }) }; }
    return reply(draft("server text", { run_admission_uncertain: true }));
  };
  try {
    env.store.select(key); void env.store.ensureLoaded(key).then(() => { settled = true; });
    await settle(); env.store.edit(key, "local Chinese draft 中文");
    await env.step(); // editor debounce waits for the existing load
    await env.step(); assert.equal(env.time, 8000);
    assert.equal(firstSignal.aborted, true); assert.deepEqual(env.errors, []);
    await env.step(); assert.equal(env.time, 8500); assert.equal(settled, true);
    assert.equal(reads, 2); assert.equal(env.store.text(key), "local Chinese draft 中文");
    assert.equal(env.store.isRunUncertain(key), true);
    finish({ ok: true, data: draft("stale response") }); await settle();
    assert.equal(env.store.text(key), "local Chinese draft 中文");
    assert.equal(env.store.isRunUncertain(key), true);
    assert.equal(await env.store.flush(key), true);
  } finally { env.restore(); }
});

test("six ignored cold reads finish one minute with one read failure and no writes", async () => {
  const env = fixture(); let done = false;
  env.fetcher = () => new Promise(() => {});
  try {
    env.store.select("default/no-body");
    void env.store.ensureLoaded("default/no-body").then(value => { assert.equal(value, false); done = true; });
    await settle();
    for (let index = 0; !done && index < 15; index++) await env.step();
    assert.equal(done, true); assert.equal(env.time, 60000);
    assert.equal(env.calls.length, 6);
    assert(env.calls.every(({ options }) => options.method === "GET" && options.signal.aborted));
    assert.deepEqual(env.errors, [{ code: "network_error", operation: "read" }]);
  } finally { env.restore(); }
});

test("temporary cold errors use quiet exponential waits while permanent denial stays exact", async () => {
  const env = fixture(); let reads = 0;
  env.fetcher = async () => ++reads <= 2 ? Response.json({ ok: false,
    error: { code: "temporary_busy", message: "later" } }, { status: 503 }) : reply(draft("saved"));
  try {
    env.store.select("default/retry"); const pending = env.store.ensureLoaded("default/retry");
    await settle(); assert.deepEqual(env.errors, []);
    await env.step(); assert.equal(env.time, 500); assert.deepEqual(env.errors, []);
    await env.step(); assert.equal(env.time, 1500); assert.equal(await pending, true);
    assert.equal(env.store.text("default/retry"), "saved");
    env.fetcher = async () => Response.json({ ok: false,
      error: { code: "access_denied", message: "No access" } }, { status: 403 });
    env.store.select("default/denied"); assert.equal(await env.store.ensureLoaded("default/denied"), false);
    assert.deepEqual(env.errors, [{ code: "access_denied", operation: "read" }]);
    assert.equal(env.calls.length, 4); assert.equal(env.timers.size, 0);
  } finally { env.restore(); }
});

test("page exit releases a cold read without an error and returning reads the preserved editor", async () => {
  const env = fixture(), key = "default/return";
  let done = false, finish, signal;
  env.fetcher = (_url, options) => { signal = options.signal; return new Promise(resolve => { finish = resolve; }); };
  try {
    env.store.select(key); env.store.edit(key, "keep before exit");
    void env.store.ensureLoaded(key).then(() => { done = true; }); await settle();
    window.dispatchEvent(new Event("pagehide")); await settle();
    assert.equal(done, true); assert.equal(signal.aborted, true); assert.deepEqual(env.errors, []);
    assert.equal(env.timers.size, 0); assert.equal(env.calls.length, 1);
    env.fetcher = async (_url, options) => reply(options.method === "GET" ? draft() :
      { ...JSON.parse(options.body), revision: 5 });
    window.dispatchEvent(new Event("pageshow")); await settle();
    assert.equal(await env.store.ensureLoaded(key), true); assert.equal(env.store.text(key), "keep before exit");
    assert.equal(await env.store.flush(key), true); assert.equal(env.store.hasUnsaved(), false);
    finish(reply(draft("old"))); await settle(); assert.equal(env.store.text(key), "keep before exit");
  } finally { env.restore(); }
});
