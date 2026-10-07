import assert from "node:assert/strict";
import test from "node:test";
import { applySettings, restoreSettings, settingsStore, createSettingsState } from
  "../app/web/js/state/settings.js";
import { createResourceStore } from "../app/web/js/state/store.js";
import { isTransientReadError } from "../app/web/js/api/read-recovery.js";
import { api } from "../app/web/js/api/client.js";
import { readFileSync } from "node:fs";
import { loadLocale } from "../app/web/js/i18n.js";
import { errorMessage } from "../app/web/js/utils/dom.js";

const network = () => Object.assign(new Error("read offline"), { code: "network_error" });
const snapshot = (locale = "ru-RU", etag = '"new"') => ({
  data: { revision: 8, locale, appearance: { theme: "dark" },
    composer: { submit_mode: "guide" } }, etag,
});
const flush = async () => { for (let i = 0; i < 40; ++i) await Promise.resolve(); };
function fixture(client) {
  let time = 1000, next = 1; const timers = new Map();
  const store = createResourceStore(null, { recoverRead: isTransientReadError,
    now: () => time, random: () => 0,
    setTimer(callback, delay) { const id = next++; timers.set(id, { callback, at: time + delay }); return id; },
    clearTimer(id) { timers.delete(id); },
  });
  const states = []; store.subscribe(state => states.push(state.status));
  const settings = createSettingsState({ store, client, resolveLanguage: () => "ru-RU" });
  return { store, settings, states, get elapsed() { return time - 1000; }, get pending() { return timers.size; },
    async step() {
      await flush();
      const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0] ?? [];
      if (!timer) return false;
      timers.delete(id); time = timer.at; timer.callback(); await flush(); return true;
    },
    async drain() {
      for (let i = 0; i < 50; ++i) if (!await this.step()) return;
      assert.fail("unbounded settings recovery");
    },
  };
}

test("a committed save distinguishes a failed confirmation and never repeats the write", async () => {
  const original = globalThis.fetch;
  const calls = [];
  globalThis.fetch = async (_url, options) => {
    calls.push(options.method);
    if (options.method === "PATCH") return Response.json({ ok: true, data: { changed: true } });
    throw new Error("read offline");
  };
  try {
    const env = fixture(api);
    const failed = assert.rejects(env.settings.apply({ appearance: { theme: "dark" } }, '"old"'),
      (error) => error.code === "settings_confirmation_failed" &&
        error.details.read_error === "network_error");
    await env.drain(); await failed;
    assert.deepEqual(calls, ["PATCH", ...Array(6).fill("GET")]);
    assert.equal(env.store.get().status, "error"); assert.equal(env.elapsed, 15500);
    assert.equal(env.states.filter(state => state === "error").length, 1);
  } finally { globalThis.fetch = original; }
});

test("an accepted write with a permanently denied confirmation is identified accurately", async () => {
  const denied = Object.assign(new Error("denied"), { status: 403, code: "permission_denied" });
  let reads = 0, writes = 0;
  const env = fixture({ patch: async () => { writes++; return { data: {} }; },
    get: async () => { reads++; throw denied; } });
  await assert.rejects(env.settings.apply({}, '"old"'), error =>
    error.code === "settings_confirmation_failed" && error.status === 403 &&
    error.details.read_error === "permission_denied");
  assert.equal(reads, 1); assert.equal(writes, 1); assert.equal(env.elapsed, 0);
  assert.equal(env.store.get().error, denied);
});

test("confirmed-write errors explain the saved result in all UI languages", async () => {
  const original = globalThis.fetch;
  const root = new URL("../app/web/", import.meta.url);
  globalThis.fetch = async path => Response.json(JSON.parse(readFileSync(
    new URL(String(path).replace(/^\//, ""), root), "utf8")));
  try {
    for (const [locale, pattern] of [["zh-CN", /更改已保存/],
      ["en-US", /Changes were saved/], ["ru-RU", /Изменения сохранены/]]) {
      await loadLocale(locale);
      assert.match(errorMessage({ code: "settings_confirmation_failed", message: "raw" }), pattern);
    }
  } finally { globalThis.fetch = original; }
});

test("restoring defaults requires a fresh readable snapshot", async () => {
  const original = globalThis.fetch;
  const calls = [];
  globalThis.fetch = async (_url, options) => {
    calls.push(options.method);
    if (options.method === "DELETE") return Response.json({ ok: true, data: { restored: true } });
    return Response.json({ ok: true, data: { revision: 8 } }, { headers: { ETag: '"new"' } });
  };
  try {
    settingsStore.reset();
    assert.equal((await restoreSettings('"old"')).restored, true);
    assert.deepEqual(calls, ["DELETE", "GET"]);
    assert.equal(settingsStore.get().data.etag, '"new"');
  } finally { globalThis.fetch = original; settingsStore.reset(); }
});

test("startup releases its first failed read and quietly restores saved preferences", async () => {
  let reads = 0;
  const env = fixture({ get: async (_path, options) => {
    assert.ok(options.signal instanceof AbortSignal);
    if (++reads === 1) throw network(); return snapshot();
  } });
  assert.equal((await env.settings.load()).status, "loading");
  assert.equal(env.store.get().error, null); assert.equal(reads, 1);
  await env.drain(); assert.equal(reads, 2); assert.equal(env.elapsed, 500);
  assert.equal(env.store.get().data.locale, "ru-RU");
  assert.equal(env.store.get().data.appearance.theme, "dark");
  assert.equal(env.store.get().data.composer.submit_mode, "guide");
  assert.ok(!env.states.includes("error"));
});

test("an accepted save waits for readback recovery through the actual API", async () => {
  const original = globalThis.fetch; const calls = []; let reads = 0;
  globalThis.fetch = async (_url, options) => {
    calls.push(options.method);
    if (options.method === "PATCH") return Response.json({ ok: true, data: { changed: true } });
    if (++reads === 1) throw new Error("echo lost");
    const value = snapshot();
    return Response.json({ ok: true, data: value.data }, { headers: { ETag: value.etag } });
  };
  try {
    const env = fixture(api); let settled = false;
    const operation = env.settings.apply({ appearance: { theme: "dark" } }, '"old"')
      .then(result => { settled = true; return result; });
    await flush(); assert.equal(settled, false); assert.equal(env.store.get().error, null);
    await env.drain(); assert.equal((await operation).changed, true);
    assert.deepEqual(calls, ["PATCH", "GET", "GET"]);
    assert.equal(env.store.get().data.etag, '"new"');
    assert.equal(env.store.get().status, "ready"); assert.ok(!env.states.includes("error"));
  } finally { globalThis.fetch = original; }
});

test("restoring defaults retries confirmation reads, never DELETE", async () => {
  let reads = 0, writes = 0;
  const env = fixture({ delete: async () => { writes++; return { data: { restored: true } }; },
    get: async () => { if (++reads === 1) throw network(); return snapshot(); } });
  const operation = env.settings.restore('"old"'); await env.drain();
  assert.equal((await operation).restored, true); assert.equal(writes, 1); assert.equal(reads, 2);
});

test("locale initialization is attempted once even when its acknowledgement and echo are lost", async () => {
  for (const confirmedLocale of ["auto", "ru-RU"]) {
    let reads = 0, writes = 0;
    const env = fixture({
      get: async () => {
        reads++;
        if (reads === 1) return snapshot("auto", '"old"');
        if (reads === 2) throw network();
        return snapshot(confirmedLocale);
      },
      patch: async (_path, body, options) => {
        writes++; assert.equal(body.patch.locale, "ru-RU"); assert.equal(options.ifMatch, '"old"');
        assert.ok(options.signal instanceof AbortSignal); throw network();
      },
    });
    await env.settings.load(); await env.drain();
    assert.equal(reads, 3); assert.equal(writes, 1);
    assert.equal(env.store.get().data.etag, '"new"');
    assert.equal(env.store.get().data.locale, "ru-RU"); assert.ok(!env.states.includes("error"));
  }
});

test("an obsolete auto-locale response cannot write after a newer read", async () => {
  let finish, signal, reads = 0, writes = 0;
  const env = fixture({ get: async (_path, options) => {
    if (++reads === 1) { signal = options.signal; return new Promise(resolve => { finish = resolve; }); }
    return snapshot("en-US");
  }, patch: async () => { writes++; return { data: {} }; } });
  const old = env.settings.load(); await flush();
  await env.settings.load(); assert.equal(signal.aborted, true);
  finish(snapshot("auto", '"obsolete"')); await old; await env.drain();
  assert.equal(writes, 0); assert.equal(env.store.get().data.locale, "en-US");
});

test("permanent read and write failures never retry", async () => {
  const denied = Object.assign(new Error("denied"), { status: 403, code: "permission_denied" });
  let reads = 0, writes = 0;
  const env = fixture({ get: async () => { reads++; throw denied; },
    patch: async () => { writes++; throw denied; } });
  assert.equal((await env.settings.load()).error, denied);
  await env.settings.recover(); await env.drain(); assert.equal(reads, 1);
  await assert.rejects(env.settings.apply({}, '"old"'), error => error === denied);
  assert.equal(writes, 1); assert.equal(reads, 1); assert.equal(env.elapsed, 0);
});

test("reset cancels a pending confirmation and removes its retries", async () => {
  let writes = 0;
  const env = fixture({ patch: async () => { writes++; return { data: {} }; },
    get: async () => { throw network(); } });
  const cancelled = assert.rejects(env.settings.apply({}, '"old"'), { name: "AbortError" });
  await flush(); env.store.reset(); await cancelled; await env.drain();
  assert.equal(writes, 1); assert.equal(env.pending, 0); assert.equal(env.store.get().status, "idle");
});

test("foreground recovery reads an exhausted snapshot without auto-locale writes", async () => {
  let reads = 0, writes = 0;
  const env = fixture({ get: async () => { reads++; return snapshot("auto"); },
    patch: async () => { writes++; return { data: {} }; } });
  env.store.setError(network()); await env.settings.recover(); await env.settings.recover();
  assert.equal(reads, 1); assert.equal(writes, 0); assert.equal(env.store.get().data.locale, "ru-RU");
});

test("a newer snapshot wins a locale-initialization revision conflict", async () => {
  let reads = 0, writes = 0;
  const env = fixture({ get: async () => ++reads === 1 ? snapshot("auto", '"old"') : snapshot("en-US"),
    patch: async () => { writes++; throw Object.assign(new Error("revision conflict"), { status: 412 }); } });
  await env.settings.load(); assert.equal(writes, 1); assert.equal(reads, 2);
  assert.equal(env.store.get().data.locale, "en-US"); assert.equal(env.store.get().data.etag, '"new"');
});

test("explicit save confirmation of auto locale is read-only", async () => {
  let reads = 0, writes = 0;
  const env = fixture({ get: async () => { reads++; return snapshot("auto"); },
    patch: async () => { writes++; return { data: { changed: true } }; } });
  assert.equal((await env.settings.apply({ locale: "auto" }, '"old"')).changed, true);
  assert.equal(writes, 1); assert.equal(reads, 1); assert.equal(env.store.get().data.locale, "ru-RU");
});

test("public settings exports still confirm an ordinary successful save", async () => {
  const original = globalThis.fetch; const calls = [];
  globalThis.fetch = async (_url, options) => {
    calls.push(options.method);
    return options.method === "PATCH"
      ? Response.json({ ok: true, data: { changed: true } })
      : Response.json({ ok: true, data: snapshot().data }, { headers: { ETag: '"new"' } });
  };
  try {
    settingsStore.reset(); assert.equal((await applySettings({}, '"old"')).changed, true);
    assert.deepEqual(calls, ["PATCH", "GET"]); assert.equal(settingsStore.get().data.etag, '"new"');
  } finally { globalThis.fetch = original; settingsStore.reset(); }
});
