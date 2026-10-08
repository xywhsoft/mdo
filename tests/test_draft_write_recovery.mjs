import assert from "node:assert/strict";
import test from "node:test";
import { createDraftStore } from "../app/web/js/features/chat/draft-store.js";
import { createRequestRecovery } from "../app/web/js/api/request-recovery.js";
import { createDraftTransport } from "../app/web/js/features/chat/draft-transport.js";

const settle = () => new Promise(resolve => setImmediate(resolve));
const reply = data => Response.json({ ok: true, data });
const denied = (code, status = 409) => Response.json({ ok: false, error: { code, message: code } }, { status });
const seed = () => ({ revision: 0, text: "", attachments: [], submissions: [],
  composer_profile: null, run_admission_uncertain: false });
const intent = { id: "a".repeat(32), text: "send once", attachments: [], interrupt: false, state: "prepared" };
function fixture() {
  const previous = { window: globalThis.window, fetch: globalThis.fetch };
  let time = 0, serial = 0, fetcher;
  const timers = new Map(), calls = [], errors = [];
  const setTimer = (fn, delay) => { timers.set(++serial, { fn, at: time + delay }); return serial; };
  globalThis.window = Object.assign(new EventTarget(), { setTimeout: setTimer, clearTimeout: id => timers.delete(id) });
  globalThis.fetch = (url, options) => { calls.push({ url, options }); return fetcher(url, options); };
  const createRecovery = () => createRequestRecovery({ now: () => time, random: () => 0, eventTarget: null,
    setTimer, clearTimer: id => timers.delete(id) });
  const store = createDraftStore({ onRestore() {}, onSaved() {},
    onError: (error, operation = "save") => errors.push({ code: error.code, operation }),
    createRecovery });
  return { store, createRecovery, timers, calls, errors, get time() { return time; },
    set fetcher(value) { fetcher = value; },
    async step() {
      const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0] ?? [];
      assert.ok(timer, "every pending draft operation must have a deadline");
      timers.delete(id); time = timer.at; timer.fn(); await settle();
    },
    async run(promise) {
      let done = false, value, failure;
      promise.then(result => { value = result; done = true; }, error => { failure = error; done = true; });
      for (let index = 0; !done && index < 30; index++) {
        await settle(); if (!done) await this.step();
      }
      assert.equal(done, true); if (failure) throw failure; return value;
    },
    async restore() { window.dispatchEvent(new Event("pagehide")); await settle(); Object.assign(globalThis, previous); },
  };
}

test("saved PUT with a hanging body confirms quietly and saves a newer edit separately", async () => {
  const env = fixture(), key = "default/save"; let saved = seed(), writes = 0, reads = 0, finish, signal;
  env.fetcher = async (_url, options) => {
    if (options.method === "GET") {
      if (writes && ++reads <= 2) return denied("busy", 503);
      return reply(saved);
    }
    const body = JSON.parse(options.body); assert.equal(body.revision, saved.revision);
    saved = { ...body, revision: saved.revision + 1 };
    if (++writes === 1) { signal = options.signal; return { ok: true, status: 200, headers: new Headers(),
      json: () => new Promise(resolve => { finish = resolve; }) }; }
    return reply(saved);
  };
  try {
    env.store.select(key); await env.store.ensureLoaded(key); env.store.edit(key, "send once");
    env.store.appendSubmission(key, intent); const saving = env.store.flush(key); await settle();
    env.store.edit(key, "next draft 中文"); assert.equal(await env.run(saving), true);
    assert.equal(signal.aborted, true); assert.equal(env.time, 9500);
    assert.equal(writes, 2); assert.equal(saved.revision, 2); assert.equal(saved.text, "next draft 中文");
    assert.equal(env.store.isSubmissionDurable(key, intent), true); assert.deepEqual(env.errors, []);
    finish({ ok: true, data: { ...seed(), text: "stale" } }); await settle();
    assert.equal(env.store.text(key), "next draft 中文"); assert.equal(env.store.hasUnsaved(), false);
  } finally { await env.restore(); }
});

test("a write which did not arrive retries the identical versioned snapshot", async () => {
  const env = fixture(), key = "default/no-arrival"; let saved = seed(), writes = 0;
  env.fetcher = async (_url, options) => {
    if (options.method === "GET") return reply(saved);
    if (++writes === 1) throw new TypeError("connection lost before delivery");
    const body = JSON.parse(options.body); assert.equal(body.revision, saved.revision);
    saved = { ...body, revision: saved.revision + 1 }; return reply(saved);
  };
  try {
    env.store.select(key); await env.store.ensureLoaded(key); env.store.edit(key, "send once");
    env.store.appendSubmission(key, intent); assert.equal(await env.run(env.store.flush(key)), true);
    const puts = env.calls.filter(call => call.options.method === "PUT");
    assert.equal(puts.length, 2); assert.equal(puts[0].options.body, puts[1].options.body);
    assert.equal(saved.revision, 1); assert.deepEqual(saved.submissions.map(row => row.id), [intent.id]);
    assert.deepEqual(env.errors, []);
  } finally { await env.restore(); }
});

test("late original acceptance between read and CAS retry causes one logical save", async () => {
  const env = fixture(), key = "default/late-CAS"; let saved = seed(), original, reads = 0, writes = 0;
  env.fetcher = async (_url, options) => {
    if (options.method === "GET") {
      if (original && ++reads === 1) { const stale = saved; saved = { ...original, revision: 1 }; return reply(stale); }
      return reply(saved);
    }
    const body = JSON.parse(options.body);
    if (++writes === 1) { original = body; throw new TypeError("reply lost before request completion"); }
    assert.equal(body.revision, 0); assert.equal(saved.revision, 1); return denied("draft_conflict");
  };
  try {
    env.store.select(key); await env.store.ensureLoaded(key); env.store.edit(key, "late original");
    assert.equal(await env.run(env.store.flush(key)), true); assert.equal(writes, 2);
    assert.equal(saved.revision, 1); assert.equal(saved.text, "late original"); assert.deepEqual(env.errors, []);
  } finally { await env.restore(); }
});

test("a newer peer draft remains protected after an uncertain local write", async () => {
  const env = fixture(), key = "default/peer"; let saved = seed(), writes = 0;
  env.fetcher = async (_url, options) => {
    if (options.method === "GET") return reply(saved);
    ++writes; saved = { ...seed(), text: "peer draft", revision: 1 }; throw new TypeError("lost reply");
  };
  try {
    env.store.select(key); await env.store.ensureLoaded(key); env.store.edit(key, "local draft");
    assert.equal(await env.run(env.store.flush(key)), false); assert.equal(writes, 1);
    assert.equal(saved.text, "peer draft"); assert.equal(env.store.text(key), "local draft");
    assert.deepEqual(env.errors, [{ code: "draft_conflict", operation: "save" }]); assert.equal(env.timers.size, 0);
  } finally { await env.restore(); }
});

test("unreadable confirmations settle and later confirm the same save without another PUT", async () => {
  const env = fixture(), key = "default/confirm"; let saved = seed(), writes = 0, offline = false;
  env.fetcher = async (_url, options) => {
    if (options.method === "GET") return offline ? new Promise(() => {}) : reply(saved);
    ++writes; saved = { ...JSON.parse(options.body), revision: 1 }; offline = true; throw new TypeError("lost reply");
  };
  try {
    env.store.select(key); await env.store.ensureLoaded(key); env.store.edit(key, "confirmed later");
    assert.equal(await env.run(env.store.flush(key)), false); assert(env.time <= 60000);
    assert.equal(writes, 1); assert.deepEqual(env.errors, [{ code: "draft_save_unconfirmed", operation: "confirm" }]);
    assert.equal(env.store.text(key), "confirmed later"); offline = false;
    assert.equal(await env.run(env.store.flush(key)), true); assert.equal(writes, 1);
    assert.equal(env.store.hasUnsaved(), false);
  } finally { await env.restore(); }
});

test("page exit cancels a saved PUT wait and returning confirms before writing", async () => {
  const env = fixture(), key = "default/page"; let saved = seed(), writes = 0, finish, signal, done = false;
  env.fetcher = async (_url, options) => {
    if (options.method === "GET") return reply(saved);
    ++writes; saved = { ...JSON.parse(options.body), revision: 1 }; signal = options.signal;
    return { ok: true, status: 200, headers: new Headers(), json: () => new Promise(resolve => { finish = resolve; }) };
  };
  try {
    env.store.select(key); await env.store.ensureLoaded(key); env.store.edit(key, "saved before exit");
    void env.store.flush(key).then(() => { done = true; }); await settle();
    window.dispatchEvent(new Event("pagehide")); await settle();
    assert.equal(done, true); assert.equal(signal.aborted, true); assert.deepEqual(env.errors, []);
    window.dispatchEvent(new Event("pageshow"));
    assert.equal(await env.run(env.store.flush(key)), true); assert.equal(writes, 1);
    finish({ ok: true, data: seed() }); await settle(); assert.equal(env.store.text(key), "saved before exit");
  } finally { await env.restore(); }
});

test("a hung submission refresh uses bounded read recovery without replaying a write", async () => {
  const env = fixture(), key = "default/refresh"; let reads = 0, signal;
  env.fetcher = async (_url, options) => {
    assert.equal(options.method, "GET");
    if (++reads === 2) { signal = options.signal; return new Promise(() => {}); }
    return reply(seed());
  };
  try {
    env.store.select(key); await env.store.ensureLoaded(key);
    assert.equal(await env.run(env.store.refreshSessionSubmissions(key)), true);
    assert.equal(signal.aborted, true); assert.equal(env.time, 8500); assert.equal(reads, 3);
    assert.deepEqual(env.errors, []);
  } finally { await env.restore(); }
});

test("submission state and removal with hung acknowledgements confirm by exact ID", async () => {
  for (const remove of [false, true]) {
    const env = fixture(), key = "default/intent"; let saved = { ...seed(), submissions: [intent] }, writes = 0, signal;
    env.fetcher = async (_url, options) => {
      if (options.method === "GET") return reply(saved);
      ++writes; saved = { ...saved, revision: 1, submissions: remove ? [] : [{ ...intent, state: "posting" }] };
      signal = options.signal;
      return { ok: true, status: 200, headers: new Headers(), json: () => new Promise(() => {}) };
    };
    try {
      env.store.select(key); await env.store.ensureLoaded(key);
      const result = remove ? env.store.removeSessionSubmission(key, intent.id) :
        env.store.changeSessionSubmissionState(key, intent.id, "posting");
      assert.equal(await env.run(result), true); assert.equal(signal.aborted, true); assert.equal(writes, 1);
      assert.deepEqual(env.store.submissions(key).map(row => row.state), remove ? [] : ["posting"]);
    } finally { await env.restore(); }
  }
});

test("a target change while confirming cannot retry the old save on another device", async () => {
  const env = fixture(); let owner = "first", writes = 0;
  const transport = createDraftTransport({ createRecovery: env.createRecovery, epoch: () => owner,
    matchesSnapshot: (_key, data, body) => data.text === body.text });
  env.fetcher = async (_url, options) => {
    if (options.method === "GET") { owner = "second"; return reply(seed()); }
    ++writes; throw new TypeError("lost reply");
  };
  try {
    await assert.rejects(env.run(transport.save("default/first", { ...seed(), text: "old device" })), { name: "AbortError" });
    assert.equal(writes, 1); assert.equal(transport.hasPendingSave("default/first"), true);
    await assert.rejects(env.run(transport.save("default/first", { ...seed(), text: "new device" })), { name: "AbortError" });
    assert.equal(writes, 1);
  } finally { transport.cancel(); await env.restore(); }
});

test("cancelling a submission refresh releases an ignored abort without another read", async () => {
  const env = fixture(), key = "default/cancel"; let signal, reads = 0;
  env.fetcher = async (_url, options) => {
    if (++reads === 1) return reply(seed());
    signal = options.signal; return new Promise(() => {});
  };
  try {
    env.store.select(key); await env.store.ensureLoaded(key);
    const controller = new AbortController();
    const refreshing = env.store.refreshSessionSubmissions(key, { signal: controller.signal });
    const failure = assert.rejects(refreshing, { name: "AbortError" });
    await settle(); controller.abort(); await failure;
    assert.equal(signal.aborted, true); assert.equal(reads, 2); assert.equal(env.timers.size, 0);
  } finally { await env.restore(); }
});

test("permanent access and quota refusals preserve input without retries or confirmation reads", async () => {
  for (const [code, status] of [["access_denied", 403], ["quota_exceeded", 429]]) {
    const env = fixture(), key = "default/refused"; let writes = 0;
    env.fetcher = async (_url, options) => {
      if (options.method === "GET") return reply(seed());
      ++writes; return denied(code, status);
    };
    try {
      env.store.select(key); await env.store.ensureLoaded(key); env.store.edit(key, "keep input");
      assert.equal(await env.run(env.store.flush(key)), false); assert.equal(writes, 1);
      assert.equal(env.calls.filter(call => call.options.method === "GET").length, 1);
      assert.equal(env.store.text(key), "keep input");
      assert.deepEqual(env.errors, [{ code, operation: "save" }]);
      assert.equal(env.timers.size, 0, "a permanent rejection must not schedule another background write");
      env.store.resumeSaves(); assert.equal(env.timers.size, 0);
      env.fetcher = async (_url, options) => reply(options.method === "GET" ? seed() :
        { ...JSON.parse(options.body), revision: 1 });
      assert.equal(await env.run(env.store.flush(key)), true, "an explicit retry remains available");
    } finally { await env.restore(); }
  }
});

test("a permanent confirmation refusal retains the uncertain snapshot without background retries", async () => {
  const env = fixture(), key = "default/confirmation-refused"; let saved = seed(), writes = 0;
  env.fetcher = async (_url, options) => {
    if (options.method === "GET") return writes ? denied("access_denied", 403) : reply(saved);
    ++writes; saved = { ...JSON.parse(options.body), revision: 1 }; throw new TypeError("lost reply");
  };
  try {
    env.store.select(key); await env.store.ensureLoaded(key); env.store.edit(key, "keep uncertain input");
    assert.equal(await env.run(env.store.flush(key)), false); assert.equal(writes, 1);
    assert.deepEqual(env.errors, [{ code: "draft_save_unconfirmed", operation: "confirm" }]);
    assert.equal(env.timers.size, 0); env.store.resumeSaves(); assert.equal(env.timers.size, 0);
    env.fetcher = async (_url, options) => { assert.equal(options.method, "GET"); return reply(saved); };
    assert.equal(await env.run(env.store.flush(key)), true); assert.equal(writes, 1);
  } finally { await env.restore(); }
});

test("an oversized next edit cannot prevent confirmation of an already saved submission", async () => {
  const env = fixture(), key = "default/oversized-next"; let saved = seed(), writes = 0, offline = false;
  env.fetcher = async (_url, options) => {
    if (options.method === "GET") return offline ? new Promise(() => {}) : reply(saved);
    ++writes; saved = { ...JSON.parse(options.body), revision: 1 }; offline = true; throw new TypeError("lost reply");
  };
  try {
    env.store.select(key); await env.store.ensureLoaded(key); env.store.edit(key, intent.text);
    env.store.appendSubmission(key, intent); assert.equal(await env.run(env.store.flush(key)), false);
    const next = "大".repeat(22000); env.store.edit(key, next); offline = false;
    assert.equal(await env.run(env.store.flush(key)), false, "the next edit still exceeds the save limit");
    assert.equal(env.store.isSubmissionDurable(key, intent), true, "the earlier send has been confirmed");
    assert.equal(writes, 1); assert.equal(saved.text, intent.text); assert.equal(env.store.text(key), next);
    assert.deepEqual(env.errors.map(row => row.code), ["draft_save_unconfirmed", "draft_too_large"]);
    assert.equal(env.timers.size, 0);
  } finally { await env.restore(); }
});
