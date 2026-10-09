import assert from "node:assert/strict";
import test from "node:test";
import { createSettingsState } from "../app/web/js/state/settings.js";
import { createResourceStore } from "../app/web/js/state/store.js";
import { createRequestRecovery } from "../app/web/js/api/request-recovery.js";

const offline = () => Object.assign(new Error("reply lost"), { code: "network_error" });
const flush = async () => { for (let i = 0; i < 60; i++) await Promise.resolve(); };
const tag = revision => `"mdo-config-${revision}"`;
const reply = (revision, extra = {}) => ({ etag: tag(revision), data: {
  revision, locale: "en-US", appearance: { theme: "light" },
  user_patches: { settings: true }, transaction_service: { runtime_consistent: true }, ...extra,
} });
const ack = (revision, restored = false) => ({ etag: tag(revision), data: {
  domain: "settings", previous_revision: 7, revision, changed: revision !== 7, restored,
} });
const preview = (revision, changes = false) => ({ data: {
  domain: "settings", current_revision: revision, valid: true, changes,
} });
function fixture(client, options = {}) {
  let time = 0, next = 0; const timers = new Map(), events = new EventTarget();
  const clock = { now: () => time, random: () => 0,
    setTimer(fn, delay) { const id = ++next; timers.set(id, { fn, at: time + delay }); return id; },
    clearTimer(id) { timers.delete(id); }, eventTarget: events };
  const store = createResourceStore(); store.setData({ ...reply(7).data, etag: tag(7) });
  const states = []; store.subscribe(state => states.push(state));
  let epoch = "first";
  const settings = createSettingsState({ store, client, resolveLanguage: () => "en-US",
    writeOptions: { createRecovery: () => createRequestRecovery(clock),
      epoch: () => epoch, token: () => "", ...options } });
  return { store, settings, states, events, timers, get elapsed() { return time; },
    switchTarget() { epoch = "second"; },
    async step() {
      await flush();
      const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0] ?? [];
      if (!timer) return false;
      timers.delete(id); time = timer.at; timer.fn(); await flush(); return true;
    },
    async drain() {
      for (let i = 0; i < 50; ++i) if (!await this.step()) return;
      assert.fail("unbounded settings recovery");
    },
  };
}

test("a lost accepted PATCH is confirmed quietly, without replaying the mutation", async () => {
  const calls = [];
  const env = fixture({ patch: async (path, body) => {
    calls.push(path); assert.equal(body.patch.appearance.theme, "dark");
    if (path.endsWith("/preview")) return preview(8);
    throw offline();
  }, get: async () => { calls.push("GET"); return reply(8, { appearance: { theme: "dark" } }); } });
  const operation = env.settings.apply({ appearance: { theme: "dark" } }, tag(7));
  await env.drain(); assert.equal((await operation).revision, 8);
  assert.deepEqual(calls, ["/settings/settings", "GET", "/settings/settings/preview"]);
  assert.equal(env.store.get().data.appearance.theme, "dark");
  assert.ok(!env.states.some(state => state.error)); assert.equal(env.elapsed, 500);
});

test("a lost DELETE acknowledgement confirms cleared user settings and keeps the reset read-only", async () => {
  let writes = 0, reads = 0;
  const env = fixture({ delete: async () => { writes++; throw offline(); },
    get: async () => { reads++; return reply(8, { user_patches: { settings: false } }); } });
  const operation = env.settings.restore(tag(7)); await env.drain();
  assert.equal((await operation).restored, true); assert.equal(writes, 1); assert.equal(reads, 1);
  assert.ok(!env.states.some(state => state.error));
});

test("a request lost before commit is replayed only with its original revision", async () => {
  for (const restore of [false, true]) {
    const calls = []; let writes = 0;
    const send = async (_path, body, options) => {
      if (restore) options = body;
      calls.push(options.ifMatch); if (++writes === 1) throw offline(); return ack(8, restore);
    };
    const env = fixture({ patch: async (path, body, options) => path.endsWith("/preview")
      ? preview(7, true) : send(path, body, options), delete: send,
      get: async () => { calls.push("GET"); return reply(writes === 1 ? 7 : 8); } });
    const operation = restore ? env.settings.restore(tag(7)) : env.settings.apply({ locale: "en-US" }, tag(7));
    await env.drain(); assert.equal((await operation).revision, 8);
    assert.deepEqual(calls, [tag(7), "GET", tag(7), "GET"]); assert.equal(env.elapsed, 1500);
  }
});

test("a newer different configuration is a conflict, never an automatic overwrite", async () => {
  for (const restore of [false, true]) {
    let writes = 0;
    const env = fixture({ patch: async path => {
      if (path.endsWith("/preview")) return preview(8, true);
      writes++; throw offline();
    }, delete: async () => { writes++; throw offline(); }, get: async () => reply(8) });
    const failed = assert.rejects(restore ? env.settings.restore(tag(7)) :
      env.settings.apply({ appearance: { theme: "dark" } }, tag(7)),
    error => error.code === "revision_conflict" && error.status === 412);
    await env.drain(); await failed; assert.equal(writes, 1);
  }
});

test("credential replacement uses the complete patch preview, not the configured boolean", async () => {
  for (const matches of [true, false]) {
    const patch = { transport: { proxy: { credential: { secret_ref: "vault:replacement" } } } };
    let previews = 0, writes = 0;
    const env = fixture({ patch: async (path, body) => {
      assert.deepEqual(body.patch, patch);
      if (path.endsWith("/preview")) { previews++; return preview(8, !matches); }
      writes++; throw offline();
    }, get: async () => reply(8, { transport: { proxy: { credential_configured: true } } }) });
    const operation = env.settings.apply(patch, tag(7));
    const failed = matches ? null : assert.rejects(operation, { code: "revision_conflict" });
    await env.drain(); if (matches) assert.equal((await operation).recovered, true); else await failed;
    assert.equal(writes, 1); assert.equal(previews, 1);
  }
});

test("an uncertain save retains its submitted intent while later edits change the caller's object", async () => {
  const patch = { appearance: { theme: "dark" } }; let release, writes = 0;
  const env = fixture({ patch: async (path, body) => {
    assert.equal(body.patch.appearance.theme, "dark");
    if (path.endsWith("/preview")) return preview(8);
    writes++; return new Promise((_resolve, reject) => { release = () => reject(offline()); });
  }, get: async () => reply(8, { locale: "auto" }) });
  const operation = env.settings.apply(patch, tag(7)); await flush();
  patch.appearance.theme = "light"; release(); await env.drain(); await operation;
  assert.equal(writes, 1); assert.equal(env.store.get().data.locale, "en-US");
});

test("preview crossing a revision change is read again, without replaying the write", async () => {
  let reads = 0, previews = 0, writes = 0;
  const env = fixture({ patch: async path => {
    if (path.endsWith("/preview")) { previews++; return preview(9); }
    writes++; throw offline();
  }, get: async () => reply(++reads === 1 ? 8 : 9) });
  const operation = env.settings.apply({}, tag(7)); await env.drain(); await operation;
  assert.equal(writes, 1); assert.equal(reads, 2); assert.equal(previews, 2);
  assert.equal(env.store.get().data.revision, 9);
});

test("unknown delivery exhausts one bounded backoff budget and reports an uncertain result once", async () => {
  let reads = 0, writes = 0;
  const env = fixture({ patch: async () => { writes++; throw offline(); },
    get: async () => { reads++; throw Object.assign(offline(), { status: 429 }); } });
  const failed = assert.rejects(env.settings.apply({}, tag(7)), error =>
    error.code === "settings_save_unconfirmed" && error.details.read_error === "network_error");
  await env.drain(); await failed;
  assert.equal(writes, 1); assert.equal(reads, 5); assert.equal(env.elapsed, 15500);
  assert.equal(env.timers.size, 0); assert.equal(env.store.get().data.revision, 7);
});

test("invalid success envelopes are confirmed instead of being treated as failed writes", async () => {
  let writes = 0;
  const env = fixture({ patch: async path => {
    if (path.endsWith("/preview")) return preview(8);
    writes++; return { data: { changed: true } };
  }, get: async () => reply(8) });
  const operation = env.settings.apply({}, tag(7)); await env.drain();
  assert.equal((await operation).recovered, true); assert.equal(writes, 1);
});

test("known pre-transaction merge failures retry the guarded request and never claim an unknown commit", async () => {
  let writes = 0;
  const env = fixture({ patch: async () => { writes++;
    throw Object.assign(new Error("merge unavailable"), { code: "configuration_merge_unavailable", status: 503 });
  }, get: async () => assert.fail("known pre-transaction failure must not need a read") });
  const failed = assert.rejects(env.settings.apply({}, tag(7)), { code: "settings_save_unavailable" });
  await env.drain(); await failed; assert.equal(writes, 6); assert.equal(env.elapsed, 15500);
});

test("storage, runtime, and service fences are permanent even with a retryable HTTP status", async () => {
  for (const code of ["configuration_persistence_failed", "runtime_inconsistent", "runtime_rejected",
    "settings_unavailable", "write_token_conflict", "home_restart_required", "permission_denied"]) {
    const error = Object.assign(new Error(code), { code, status: code === "permission_denied" ? 403 : 503 });
    let calls = 0;
    const env = fixture({ patch: async () => { calls++; throw error; },
      get: async () => { assert.fail("permanent write must not be inspected or replayed"); } });
    await assert.rejects(env.settings.apply({}, tag(7)), cause => cause === error);
    assert.equal(calls, 1); assert.equal(env.timers.size, 0);
  }
});

test("a degraded runtime and a changed service token do not masquerade as a saved result", async () => {
  for (const changedToken of [false, true]) {
    let writes = 0;
    const token = "0123456789abcdef0123456789abcdef-0";
    const env = fixture({ patch: async () => { writes++; throw offline(); }, get: async () => ({
      ...reply(8, { transaction_service: { runtime_consistent: changedToken } }),
      writeToken: changedToken ? "another" : token,
    }) }, { token: () => token });
    const failed = assert.rejects(env.settings.apply({}, tag(7)), {
      code: changedToken ? "write_token_conflict" : "runtime_inconsistent",
    });
    await env.drain(); await failed; assert.equal(writes, 1);
  }
});

test("an acknowledged save does not wrap a later service fence in an ordinary read failure", async () => {
  const error = Object.assign(new Error("restarted"), { code: "write_token_conflict" });
  const env = fixture({ patch: async () => ack(8), get: async () => { throw error; } });
  await assert.rejects(env.settings.apply({}, tag(7)), cause => cause === error);
  assert.equal(env.store.get().data.revision, 7);
});

test("cancellation releases pending adapters and recovery waits, with no late publication", async () => {
  for (const kind of ["signal", "pagehide", "reset", "target"]) {
    let late, writes = 0;
    const controller = new AbortController();
    const env = fixture({ patch: async () => { writes++; throw offline(); },
      get: async () => new Promise(resolve => { late = () => resolve(reply(8)); }) });
    const failed = assert.rejects(env.settings.apply({}, tag(7), { signal: controller.signal }), { name: "AbortError" });
    await env.step(); // first retry has entered its confirmation GET
    if (kind === "signal") controller.abort();
    else if (kind === "pagehide") env.events.dispatchEvent(new Event("pagehide"));
    else if (kind === "reset") env.store.reset();
    else env.switchTarget();
    late(); await failed; await env.drain();
    assert.equal(writes, 1); assert.notEqual(env.store.get().data?.revision, 8);
    assert.equal(env.timers.size, 0);
  }
});

test("a delayed first mutation and its replay can change the guarded revision only once", async () => {
  let revision = 7, finishFirst, writes = 0;
  const env = fixture({ patch: async (path, _body, options) => {
    if (path.endsWith("/preview")) return preview(revision, revision === 7);
    assert.equal(options.ifMatch, tag(7));
    const commit = () => {
      if (revision !== 7) throw Object.assign(new Error("late conflict"), { code: "revision_conflict", status: 412 });
      revision++; return ack(revision);
    };
    if (++writes === 1) return new Promise((resolve, reject) => {
      finishFirst = () => { try { resolve(commit()); } catch (error) { reject(error); } };
    });
    return commit();
  }, get: async () => reply(revision) });
  const operation = env.settings.apply({}, tag(7)); await env.drain();
  assert.equal((await operation).revision, 8); assert.equal(writes, 2);
  finishFirst(); await flush(); assert.equal(revision, 8);
  assert.equal(env.store.get().data.revision, 8); assert.equal(env.timers.size, 0);
});

test("late confirmation does not replace a newer settings snapshot already published by another view", async () => {
  let finish;
  const env = fixture({ patch: async () => ack(8),
    get: async () => new Promise(resolve => { finish = () => resolve(reply(8)); }) });
  const operation = env.settings.apply({}, tag(7)); await flush();
  env.store.setData({ ...reply(9, { locale: "ru-RU" }).data, etag: tag(9) });
  finish(); await operation;
  assert.equal(env.store.get().data.revision, 9); assert.equal(env.store.get().data.locale, "ru-RU");
});

test("invalid original revision guards never reach the server", async () => {
  const env = fixture({ patch: async () => assert.fail("invalid guard sent") });
  for (const etag of ["", '"mdo-config-0"', '"mdo-config-01"', '"mdo-config-9007199254740992"', '"old"'])
    await assert.rejects(env.settings.apply({}, etag), TypeError);
});
