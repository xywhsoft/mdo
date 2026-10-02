import assert from "node:assert/strict";
import test from "node:test";
import { applySettings, restoreSettings, settingsStore } from
  "../app/web/js/state/settings.js";

test("a committed save with an unreadable echo does not claim success or retry the write", async () => {
  const original = globalThis.fetch;
  const calls = [];
  globalThis.fetch = async (_url, options) => {
    calls.push(options.method);
    if (options.method === "PATCH") return Response.json({ ok: true, data: { changed: true } });
    throw new Error("read offline");
  };
  try {
    settingsStore.reset();
    await assert.rejects(applySettings({ appearance: { theme: "dark" } }, '"old"'),
      (error) => error.code === "network_error");
    assert.deepEqual(calls, ["PATCH", "GET"]);
    assert.equal(settingsStore.get().status, "error");
  } finally { globalThis.fetch = original; settingsStore.reset(); }
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
