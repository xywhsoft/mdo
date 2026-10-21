import assert from "node:assert/strict";
import test from "node:test";
import { ApiError } from "../app/web/js/api/client.js";
import { purgeSession, sessionsStore } from "../app/web/js/state/sessions.js";
import { sessionActionItems, sessionActionDialogCopy } from "../app/web/js/features/sessions/session-actions.js";
import { createDraftStore } from "../app/web/js/features/chat/draft-store.js";

const session = { project_id: "default", id: "old", title: "Old", status: "trash", revision: 3 };
const neighbour = { ...session, id: "keep" };

test("only recycle-bin menus offer permanent deletion and explain its consequences", () => {
  assert.deepEqual(sessionActionItems(session).map(item => item.name), ["restore", "send_device", "purge"]);
  assert.equal(sessionActionItems(session).find(item => item.name === "purge").tone, "danger");
  for (const status of ["active", "archived"])
    assert.equal(sessionActionItems({ ...session, status }).some(item => item.name === "purge"), false);
  assert.match(sessionActionDialogCopy("purge", session)[1], /Old/);
  assert.match(sessionActionDialogCopy("purge", session)[1], /无法恢复/);
});

test("a lost deletion reply checks absence without a second destructive request", async () => {
  const previous = globalThis.fetch;
  let writes = 0;
  sessionsStore.setData({ items: [session, neighbour] });
  globalThis.fetch = async (path, options) => {
    if (options.method === "POST") {
      ++writes; assert.ok(path.endsWith("/old/purge"));
      assert.equal(options.headers.get("If-Match"), '"mdo-session-old-3"');
      assert.equal(options.body, undefined);
      throw new Error("acknowledgement lost");
    }
    if (path.endsWith("/old")) return Response.json({ ok: false,
      error: { code: "session_not_found", message: "gone" } }, { status: 404 });
    return Response.json({ ok: true, data: { items: [neighbour] } });
  };
  try {
    assert.equal((await purgeSession(session)).deleted, true);
    assert.equal(writes, 1);
    assert.deepEqual(sessionsStore.get().data.items.map(item => item.id), ["keep"]);
  } finally { globalThis.fetch = previous; }
});

test("a restored or changed session is retained after deletion is refused", async () => {
  const previous = globalThis.fetch; let calls = 0;
  sessionsStore.setData({ items: [session, neighbour] });
  globalThis.fetch = async () => {
    ++calls;
    return Response.json({ ok: false, error: { code: "revision_conflict", message: "changed" } }, { status: 412 });
  };
  try {
    await assert.rejects(purgeSession(session), error => error instanceof ApiError && error.status === 412);
    assert.equal(calls, 1); assert.equal(sessionsStore.get().data.items.length, 2);
    await assert.rejects(purgeSession({ ...session, status: "active" }), TypeError);
    assert.equal(calls, 1);
  } finally { globalThis.fetch = previous; }
});

test("discarding a deleted session draft prevents late reads and saves from bringing it back", async () => {
  const previousFetch = globalThis.fetch, previousWindow = globalThis.window;
  globalThis.window = { setTimeout, clearTimeout, addEventListener() {} };
  let resolveRead, reads = 0, writes = 0, restores = 0;
  globalThis.fetch = async (_path, options) => {
    if (options.method !== "GET") { ++writes; throw new Error("deleted draft must never be saved"); }
    ++reads;
    return new Promise(resolve => { resolveRead = () => resolve(Response.json({ ok: true,
      data: { revision: 1, text: "old", attachments: [], submissions: [] } })); });
  };
  try {
    const store = createDraftStore({ onRestore() { ++restores; }, onError() {}, onSaved() {} });
    store.select("default/old");
    const initialRestores = restores;
    const loading = store.ensureLoaded("default/old");
    for (let i = 0; !resolveRead && i < 20; ++i) await Promise.resolve();
    assert.equal(typeof resolveRead, "function");
    store.capture("default/old", "unsaved");
    store.forgetSession("default/old");
    resolveRead(); await loading;
    store.capture("default/old", "late capture"); await store.flush("default/old");
    assert.equal(reads, 1); assert.equal(writes, 0); assert.equal(restores, initialRestores);
    assert.equal(store.text("default/old"), ""); assert.equal(store.hasUnsaved(), false);
  } finally { globalThis.fetch = previousFetch; globalThis.window = previousWindow; }
});

test("a deleted session's cached new-task draft is discarded while other owners remain available", async () => {
  const previousFetch = globalThis.fetch, previousWindow = globalThis.window;
  globalThis.window = { setTimeout, clearTimeout, addEventListener() {} };
  const id = "a".repeat(32);
  const task = { project_id: "default", session_id: id, title: "Old", agent_id: "mdo.default",
    model_id: "model", reasoning_effort: "medium", permission_profile: "balanced", phase: "creating" };
  let saved = { revision: 1, text: "old pending input", attachments: [], submissions: [], new_task: task };
  globalThis.fetch = async () => Response.json({ ok: true, data: saved });
  try {
    const store = createDraftStore({ onRestore() {}, onError() {}, onSaved() {} });
    await store.ensureLoaded(""); assert.equal(store.newTask().session_id, id);
    saved = { revision: 0, text: "", attachments: [], submissions: [], new_task: null };
    store.forgetSession(`default/${id}`); await store.ensureLoaded("");
    assert.equal(store.newTask(), null); assert.equal(store.text(""), "");
    assert.equal(store.hasUnsaved(), false);
    store.setNewTask({ ...task, session_id: "b".repeat(32) });
    store.forgetSession(`default/${id}`);
    assert.equal(store.newTask().session_id, "b".repeat(32));
    store.forgetSession(`default/${"b".repeat(32)}`);
    await store.ensureLoaded("");
  } finally { globalThis.fetch = previousFetch; globalThis.window = previousWindow; }
});
