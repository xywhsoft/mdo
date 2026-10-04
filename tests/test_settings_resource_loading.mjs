import assert from "node:assert/strict";
import test from "node:test";
import { ensureSettingsResources, loadResource, modulesStore, skillsStore,
  mcpStore, permissionsStore, storageStore, diagnosticsStore, migrationsStore } from
  "../app/web/js/state/resources.js";

const stores = [modulesStore, skillsStore, mcpStore, permissionsStore,
  storageStore, diagnosticsStore, migrationsStore];
const reply = (data = { items: [] }) => Response.json({ ok: true, data });
const defer = () => {
  let resolve;
  const promise = new Promise((done) => { resolve = done; });
  return { promise, resolve };
};

async function withFetch(fetcher, check) {
  const original = globalThis.fetch;
  stores.forEach((store) => store.reset());
  globalThis.fetch = fetcher;
  try { await check(); }
  finally { globalThis.fetch = original; stores.forEach((store) => store.reset()); }
}

test("chat and ordinary preferences do not load management catalogs", async () => {
  const calls = [];
  await withFetch(async (url) => { calls.push(url); return reply(); }, async () => {
    for (const page of ["", "general", "agent", "web", "projects", "models",
      "retired-page", "schedules", "constructor", "toString", "unknown"])
      assert.deepEqual(await ensureSettingsResources(page), []);
    assert.deepEqual(calls, []);
    assert.ok(stores.every((store) => store.get().status === "idle"));
  });
});

test("rapid extension page visits share reads and keep the loaded catalog", async () => {
  const calls = [], gates = new Map();
  await withFetch((url) => {
    calls.push(url);
    const gate = defer(); gates.set(url, gate);
    return gate.promise;
  }, async () => {
    const first = ensureSettingsResources("extensions");
    const second = ensureSettingsResources("extensions");
    assert.deepEqual(calls, ["/api/v1/modules", "/api/v1/skills", "/api/v1/mcp"]);
    assert.equal(modulesStore.get().status, "refreshing");
    assert.equal(permissionsStore.get().status, "idle");
    for (const [path, gate] of gates) gate.resolve(reply({ items: [{ id: path }] }));
    await Promise.all([first, second]);
    await ensureSettingsResources("extensions");
    assert.equal(calls.length, 3);
    assert.equal(skillsStore.get().data.items[0].id, "/api/v1/skills");
  });
});

test("a failed read is visible and retries only on a new visit or explicit reload", async () => {
  const calls = [];
  let failed = false;
  await withFetch(async (url) => {
    calls.push(url);
    if (url.endsWith("/modules") && !failed) {
      failed = true;
      return Response.json({ ok: false, error: { message: "temporary failure" } }, { status: 503 });
    }
    return reply();
  }, async () => {
    await ensureSettingsResources("extensions");
    assert.equal(modulesStore.get().status, "error");
    assert.equal(modulesStore.get().error.status, 503);
    await ensureSettingsResources("general");
    assert.equal(calls.length, 3);
    await ensureSettingsResources("extensions");
    assert.equal(calls.length, 4);
    assert.equal(modulesStore.get().status, "ready");
    await loadResource("skills");
    assert.equal(calls.length, 5);
    assert.equal(calls.at(-1), "/api/v1/skills");
  });
});

test("a superseded read cannot replace or forget an explicit refresh", async () => {
  const old = defer(), newer = defer();
  let moduleReads = 0;
  await withFetch((url) => {
    if (!url.endsWith("/modules")) return Promise.resolve(reply());
    return ++moduleReads === 1 ? old.promise : newer.promise;
  }, async () => {
    const firstVisit = ensureSettingsResources("extensions");
    const refresh = loadResource("modules");
    old.resolve(reply({ modules: [{ id: "old" }] }));
    await firstVisit;
    const anotherVisit = ensureSettingsResources("extensions");
    assert.equal(moduleReads, 2);
    assert.equal(modulesStore.get().status, "refreshing");
    newer.resolve(reply({ modules: [{ id: "new" }] }));
    await Promise.all([refresh, anotherVisit]);
    assert.equal(modulesStore.get().data.modules[0].id, "new");
    await ensureSettingsResources("extensions");
    assert.equal(moduleReads, 2);
  });
});

test("permissions and diagnostics load only their own page data", async () => {
  const calls = [];
  await withFetch(async (url) => { calls.push(url); return reply(); }, async () => {
    await ensureSettingsResources("permissions");
    assert.deepEqual(calls, ["/api/v1/permissions"]);
    assert.equal(storageStore.get().status, "idle");
    await ensureSettingsResources("diagnostics");
    assert.deepEqual(calls.slice(1), ["/api/v1/storage", "/api/v1/diagnostics", "/api/v1/migrations/legacy"]);
    assert.equal(modulesStore.get().status, "idle");
    await ensureSettingsResources("permissions");
    await ensureSettingsResources("diagnostics");
    assert.equal(calls.length, 4);
  });
});
