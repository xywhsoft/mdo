import assert from "node:assert/strict";
import test from "node:test";
import { createSettingsState } from "../app/web/js/state/settings.js";
import { api, setApiWriteGuard } from "../app/web/js/api/client.js";

const turn = () => new Promise(resolve => setImmediate(resolve));
function fixture(context) {
  let allowed = false, locale = "auto", revision = 1;
  const reads = [], writes = [], warnings = [];
  context.mock.method(console, "warn", (...args) => warnings.push(args));
  const snapshot = () => ({ data: { locale, revision, appearance: { theme: "dark" } },
    etag: `"settings-${revision}"` });
  const client = {
    async get(_path, options) { reads.push(options.signal); return snapshot(); },
    async patch(_path, document, options) {
      writes.push({ patch: document.patch, etag: options.ifMatch });
      if (!allowed) throw Object.assign(new Error("gate closed"), { code: "purge_review_required" });
      locale = document.patch.locale; revision++; return { data: { changed: true } };
    },
    async delete() { locale = "auto"; revision++; return { data: { restored: true } }; },
  };
  const settings = createSettingsState({ client, resolveLanguage: () => "zh-CN",
    canInitializeLocale: () => allowed });
  return { settings, client, reads, writes, warnings, snapshot,
    allow() { allowed = true; }, change(value) { locale = value; revision++; } };
}

test("initial locale displays promptly and waits for continuity before saving", async context => {
  const f = fixture(context);
  const initial = await f.settings.load();
  assert.equal(initial.status, "ready"); assert.equal(initial.data.locale, "zh-CN");
  assert.equal(f.reads.length, 1); assert.equal(f.writes.length, 0); assert.equal(f.warnings.length, 0);
  await f.settings.resumeLocaleInitialization();
  assert.equal(f.reads.length, 1);
  f.allow(); f.change("auto");
  await f.settings.resumeLocaleInitialization();
  assert.deepEqual(f.writes, [{ patch: { locale: "zh-CN" }, etag: '"settings-2"' }]);
  assert.equal(f.reads.length, 3); assert.equal(f.settings.store.get().data.locale, "zh-CN");
  await f.settings.resumeLocaleInitialization(); assert.equal(f.reads.length, 3);
});

test("a language selected by another page wins over deferred initialization", async context => {
  const f = fixture(context);
  await f.settings.load(); f.allow(); f.change("en-US");
  await f.settings.resumeLocaleInitialization();
  assert.equal(f.writes.length, 0); assert.equal(f.reads.length, 2);
  assert.equal(f.settings.store.get().data.locale, "en-US");
  assert.equal(f.settings.store.get().data.appearance.theme, "dark");
});

test("manual selection cancels an initializer even before the manual write completes", async context => {
  const f = fixture(context);
  await f.settings.load(); f.allow();
  let finishRead, finishWrite;
  const originalGet = f.client.get;
  f.client.get = async (path, options) => {
    if (f.reads.length === 1) {
      f.reads.push(options.signal);
      return new Promise(resolve => { finishRead = () => resolve(f.snapshot()); });
    }
    return originalGet(path, options);
  };
  const automatic = f.settings.resumeLocaleInitialization(); await turn();
  f.client.patch = async (_path, document, options) => {
    f.writes.push({ patch: document.patch, etag: options.ifMatch });
    if (document.patch.locale === "en-US") return new Promise(resolve => {
      finishWrite = () => { f.change("en-US"); resolve({ data: { changed: true } }); };
    });
    return { data: { changed: true } };
  };
  const manual = f.settings.apply({ locale: "en-US" }, '"settings-1"');
  await turn(); finishRead(); await automatic;
  assert.equal(f.writes.length, 1);
  finishWrite(); await manual;
  assert.equal(f.settings.store.get().data.locale, "en-US");
  await f.settings.resumeLocaleInitialization(); assert.equal(f.writes.length, 1);
});

test("repeated writable notifications do not duplicate an initialization read or write", async context => {
  const f = fixture(context);
  await f.settings.load(); f.allow();
  let finish;
  const originalGet = f.client.get;
  f.client.get = async (path, options) => {
    if (f.reads.length === 1) {
      f.reads.push(options.signal);
      return new Promise(resolve => { finish = () => resolve(f.snapshot()); });
    }
    return originalGet(path, options);
  };
  const first = f.settings.resumeLocaleInitialization(); await turn();
  await f.settings.resumeLocaleInitialization(); assert.equal(f.reads.length, 2);
  finish(); await first;
  assert.equal(f.writes.length, 1); assert.equal(f.reads.length, 3);
});

test("a lost initialization acknowledgement never turns into another automatic write", async context => {
  const f = fixture(context);
  await f.settings.load(); f.allow();
  f.client.patch = async () => {
    f.writes.push("lost acknowledgement");
    throw Object.assign(new Error("offline"), { code: "network_error" });
  };
  await f.settings.resumeLocaleInitialization(); await f.settings.resumeLocaleInitialization();
  assert.equal(f.writes.length, 1); assert.equal(f.reads.length, 3);
  assert.equal(f.settings.store.get().status, "ready"); assert.equal(f.warnings.length, 0);
});

test("restoring defaults supersedes a deferred automatic locale save", async context => {
  const f = fixture(context);
  await f.settings.load(); f.allow(); await f.settings.restore('"settings-1"');
  await f.settings.resumeLocaleInitialization();
  assert.equal(f.writes.length, 0); assert.equal(f.reads.length, 2);
  assert.equal(f.settings.store.get().data.locale, "zh-CN");
});

test("the public API guard defers initialization without sending a rejected PATCH", async context => {
  const originalFetch = globalThis.fetch, calls = [];
  let allowed = false, locale = "auto";
  const release = setApiWriteGuard(() => allowed);
  globalThis.fetch = async (_url, options) => {
    calls.push(options.method);
    if (options.method === "PATCH") {
      locale = JSON.parse(options.body).patch.locale;
      return Response.json({ ok: true, data: { changed: true } });
    }
    return Response.json({ ok: true, data: { locale } }, { headers: { ETag: '"settings-1"' } });
  };
  const warnings = []; context.mock.method(console, "warn", (...args) => warnings.push(args));
  try {
    const settings = createSettingsState({ client: api, resolveLanguage: () => "ru-RU" });
    await settings.load(); assert.deepEqual(calls, ["GET"]); assert.equal(warnings.length, 0);
    allowed = true; await settings.resumeLocaleInitialization();
    assert.deepEqual(calls, ["GET", "GET", "PATCH", "GET"]);
    assert.equal(locale, "ru-RU");
  } finally { release(); globalThis.fetch = originalFetch; }
});
