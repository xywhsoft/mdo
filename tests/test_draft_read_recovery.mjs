import assert from "node:assert/strict";
import test from "node:test";
import { createDraftStore } from "../app/web/js/features/chat/draft-store.js";

const settle = () => new Promise(resolve => setImmediate(resolve));
function fixture() {
  const oldWindow = globalThis.window, oldFetch = globalThis.fetch;
  const timers = new Map(), restores = [], errors = [], requests = [];
  let serial = 0, paused = false, respond;
  globalThis.window = {
    setTimeout(fn, delay) { timers.set(++serial, { fn, delay }); return serial; },
    clearTimeout(id) { timers.delete(id); }, addEventListener() {},
  };
  globalThis.fetch = async (url, options) => {
    requests.push({ url, method: options.method });
    return respond(url, options);
  };
  const options = { onRestore(text, images) { restores.push({ text, images }); },
    onError(error) { errors.push(error); }, onSaved() {},
    isWritePaused: () => paused, random: () => 1,
    // These checks isolate the outer scheduler after one recovery has ended.
    // test_draft_read_deadline exercises the production bounded recovery.
    createRecovery: () => ({ request: read => read(), assertActive() {}, dispose() {} }) };
  const store = createDraftStore(options);
  return { store, timers, restores, errors, requests, options,
    set respond(fn) { respond = fn; }, set paused(value) { paused = value; },
    async tick() {
      assert.equal(timers.size, 1);
      const [id, timer] = [...timers][0]; timers.delete(id); timer.fn();
      await settle(); return timer.delay;
    },
    restore() { globalThis.window = oldWindow; globalThis.fetch = oldFetch; },
  };
}
const empty = () => ({ revision: 4, text: "", attachments: [], submissions: [],
  run_admission_uncertain: false, composer_profile: null });

test("an edited cold draft loads once on reconnect and survives a new store", async () => {
  const env = fixture(), key = "project:default";
  let saved = empty(), online = false, release;
  env.paused = true;
  env.respond = async (_url, options) => {
    if (!online) throw new TypeError("offline");
    if (options.method === "GET") { await new Promise(resolve => { release = resolve; });
      return Response.json({ ok: true, data: saved }); }
    const body = JSON.parse(options.body);
    assert.equal(body.revision, saved.revision);
    saved = { ...body, revision: saved.revision + 1 };
    return Response.json({ ok: true, data: saved });
  };
  try {
    env.store.select(key);
    assert.equal(await env.store.ensureLoaded(key), false);
    env.store.edit(key, "typed offline");
    assert.equal(env.timers.size, 0);
    online = true; env.paused = false;
    env.store.resumeSaves({ retryReads: true }); env.store.resumeSaves();
    const loaded = env.store.ensureLoaded(key);
    assert.equal(env.requests.length, 2, "reconnect and explicit read share one GET");
    release(); assert.equal(await loaded, true);
    assert.equal(await env.store.flush(key), true);
    assert.equal(saved.text, "typed offline");
    const reopened = createDraftStore(env.options);
    env.respond = async () => Response.json({ ok: true, data: saved });
    reopened.select(key); assert.equal(await reopened.ensureLoaded(key), true);
    assert.equal(reopened.text(key), "typed offline");
    assert.equal(env.store.hasUnsaved(), false);
  } finally { env.restore(); }
});

test("cold reads back off with a cap and typing does not bypass their timer", async () => {
  const env = fixture(), key = "default/session";
  env.respond = async () => Response.json({ ok: false,
    error: { code: "service_busy", message: "try later" } }, { status: 503 });
  try {
    env.store.select(key); assert.equal(await env.store.ensureLoaded(key), false);
    for (const delay of [1200, 2400, 4800, 9600, 18000, 18000]) {
      env.store.edit(key, `draft ${delay}`); env.store.resumeSaves();
      assert.equal(await env.tick(), delay);
    }
    env.respond = async (_url, options) => Response.json({ ok: true,
      data: options.method === "GET" ? empty() : {
        ...JSON.parse(options.body), revision: 5 } });
    await env.tick(); assert.equal(env.store.isLoaded(key), true);
    assert.equal(await env.store.flush(key), true);
    assert.equal(env.store.text(key), "draft 18000");
  } finally { env.restore(); }
});

test("a cold read restores a saved reply guard and queue before saving a local edit", async () => {
  const env = fixture(), key = "default/guard";
  const pending = { id: "a".repeat(32), text: "accepted intent", attachments: [],
    interrupt: false, state: "posting" };
  let saved = { ...empty(), run_admission_uncertain: true, submissions: [pending] };
  env.respond = async () => { throw new TypeError("offline"); };
  try {
    env.store.select(key); await env.store.ensureLoaded(key);
    env.store.edit(key, "next input", ["b".repeat(32)]);
    env.respond = async (_url, options) => {
      if (options.method === "GET") return Response.json({ ok: true, data: saved });
      const body = JSON.parse(options.body);
      assert.equal(body.run_admission_uncertain, true);
      assert.deepEqual(body.submissions, [pending]);
      saved = { ...body, revision: saved.revision + 1 };
      return Response.json({ ok: true, data: saved });
    };
    env.store.resumeSaves({ retryReads: true });
    assert.equal(await env.store.ensureLoaded(key), true);
    assert.equal(await env.store.flush(key), true);
    assert.equal(saved.text, "next input");
    assert.deepEqual(saved.attachments, ["b".repeat(32)]);
    assert.equal(env.requests.filter(item => item.method === "POST").length, 0);
  } finally { env.restore(); }
});

test("permanent access errors do not automatically retry cold reads or save edits", async () => {
  const env = fixture(), key = "project:locked";
  env.respond = async () => Response.json({ ok: false,
    error: { code: "access_denied", message: "denied" } }, { status: 403 });
  try {
    env.store.select(key); await env.store.ensureLoaded(key);
    env.store.edit(key, "keep locally"); env.store.resumeSaves({ retryReads: true });
    await settle(); assert.equal(env.timers.size, 0);
    assert.equal(env.requests.length, 1);
    assert.equal(env.errors.length, 1);
    assert.equal(env.store.text(key), "keep locally");
  } finally { env.restore(); }
});

test("a cold remote timeout restores its saved editor on a read retry without writing", async () => {
  const env = fixture(), key = "default/remote";
  env.respond = async () => { throw Object.assign(new Error("relay read expired"),
    { code: "remote_timeout" }); };
  try {
    env.store.select(key); assert.equal(await env.store.ensureLoaded(key), false);
    env.respond = async () => Response.json({ ok: true,
      data: { ...empty(), text: "remote saved draft" } });
    assert.equal(await env.tick(), 1200);
    assert.equal(env.store.text(key), "remote saved draft");
    assert.equal(env.restores.at(-1).text, "remote saved draft");
    assert.equal(env.timers.size, 0);
    assert.deepEqual(env.requests.map(item => item.method), ["GET", "GET"]);
  } finally { env.restore(); }
});

test("a late cold recovery saves its owner without replacing another selected editor", async () => {
  const env = fixture(); let release;
  env.respond = async (url, options) => {
    if (options.method === "PUT") return Response.json({ ok: true,
      data: { ...JSON.parse(options.body), revision: 5 } });
    if (url.endsWith("/first/draft")) await new Promise(resolve => { release = resolve; });
    return Response.json({ ok: true, data: { ...empty(),
      text: url.endsWith("/second/draft") ? "second draft" : "" } });
  };
  try {
    env.store.select("default/first"); env.store.edit("default/first", "first local");
    env.store.select("default/second"); await env.store.ensureLoaded("default/second");
    release(); await env.store.ensureLoaded("default/first");
    assert.equal(env.restores.at(-1).text, "second draft");
    assert.equal(await env.store.flush("default/first"), true);
    assert.equal(env.store.text("default/second"), "second draft");
  } finally { env.restore(); }
});

test("a peer write after cold recovery is still protected by the saved revision", async () => {
  const env = fixture(), key = "project:default";
  let saved = empty();
  env.respond = async () => { throw new TypeError("offline"); };
  try {
    env.store.select(key); await env.store.ensureLoaded(key);
    env.store.edit(key, "local draft");
    env.respond = async (_url, options) => options.method === "GET"
      ? Response.json({ ok: true, data: saved })
      : Response.json({ ok: false, error: { code: "draft_conflict",
        message: "peer updated" } }, { status: 409 });
    await env.store.ensureLoaded(key); saved = { ...saved, revision: 5, text: "peer draft" };
    assert.equal(await env.store.flush(key), false);
    env.store.resumeSaves(); assert.equal(env.timers.size, 0);
    assert.equal(saved.text, "peer draft");
    assert.equal(env.store.text(key), "local draft");
    assert.equal(env.errors.at(-1).code, "draft_conflict");
  } finally { env.restore(); }
});
