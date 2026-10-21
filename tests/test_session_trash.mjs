import assert from "node:assert/strict";
import test from "node:test";
import { loadSession, sessionDetailStore, sessionsStore, trashSession } from "../app/web/js/state/sessions.js";

const active = { project_id: "default", id: "old", title: "Old", status: "active", revision: 2 };
const trashed = { ...active, status: "trash", revision: 3 };
const neighbour = { ...active, id: "keep" };
const reply = data => Response.json({ ok: true, data });

test("trash acknowledgement disables an open session and supersedes its delayed active read", async () => {
  const previous = globalThis.fetch;
  let resolveRead, signal;
  sessionDetailStore.setData(active);
  globalThis.fetch = async (path, options) => {
    if (options.method === "DELETE") {
      assert.equal(options.headers.get("If-Match"), '"mdo-session-old-2"');
      return reply(trashed);
    }
    if (path.endsWith("/old")) {
      signal = options.signal;
      return new Promise(resolve => { resolveRead = () => resolve(reply(active)); });
    }
    return reply({ items: [trashed, neighbour] });
  };
  try {
    const pending = loadSession("default", "old");
    for (let i = 0; !resolveRead && i < 30; ++i) await Promise.resolve();
    assert.equal(typeof resolveRead, "function");
    await trashSession(active);
    assert.equal(sessionDetailStore.get().data.status, "trash");
    assert.equal(signal.aborted, true);
    resolveRead(); await pending;
    assert.equal(sessionDetailStore.get().data.status, "trash");
    assert.equal(sessionDetailStore.get().data.revision, 3);
  } finally { sessionDetailStore.reset(); sessionsStore.reset(); globalThis.fetch = previous; }
});

test("changing selection before trash acknowledgement preserves the newer conversation", async () => {
  const previous = globalThis.fetch;
  let resolveDelete;
  sessionDetailStore.setData(active);
  globalThis.fetch = async (_path, options) => options.method === "DELETE"
    ? new Promise(resolve => { resolveDelete = () => resolve(reply(trashed)); })
    : reply({ items: [trashed, neighbour] });
  try {
    const pending = trashSession(active);
    for (let i = 0; !resolveDelete && i < 30; ++i) await Promise.resolve();
    assert.equal(typeof resolveDelete, "function");
    sessionDetailStore.setData(neighbour);
    resolveDelete(); await pending;
    assert.equal(sessionDetailStore.get().data, neighbour);
  } finally { sessionDetailStore.reset(); sessionsStore.reset(); globalThis.fetch = previous; }
});

test("a refused trash request preserves the selected active session", async () => {
  const previous = globalThis.fetch;
  sessionDetailStore.setData(active);
  globalThis.fetch = async () => Response.json({ ok: false,
    error: { code: "revision_conflict", message: "changed" } }, { status: 412 });
  try {
    await assert.rejects(trashSession(active), error => error.status === 412);
    assert.equal(sessionDetailStore.get().data, active);
  } finally { sessionDetailStore.reset(); globalThis.fetch = previous; }
});
