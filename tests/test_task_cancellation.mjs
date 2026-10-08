import assert from "node:assert/strict";
import test from "node:test";
import { cancelTask, tasksStore } from "../app/web/js/state/tasks.js";

test("an accepted stop snapshot survives a failed task refresh", async () => {
  const fetch = globalThis.fetch;
  const active = { id: 1, state: "running", terminal: false, stop_requested: false };
  tasksStore.setData({ total: 2, items: [active, { id: 2, state: "succeeded" }] });
  globalThis.fetch = async (_path, options) => options.method === "DELETE"
    ? Response.json({ ok: true, data: { ...active, stop_requested: true } })
    : Response.json({ ok: false, error: { message: "refresh unavailable" } }, { status: 503 });
  try {
    const acknowledged = await cancelTask(1);
    assert.equal(acknowledged.stop_requested, true);
    const state = tasksStore.get();
    assert.equal(state.status, "error");
    assert.equal(state.data.items[0].stop_requested, true);
    assert.equal(state.data.items[0].terminal, false);
    assert.equal(state.data.items[1].state, "succeeded");
  } finally { globalThis.fetch = fetch; tasksStore.reset(); }
});

test("a rejected stop leaves the active task available", async () => {
  const fetch = globalThis.fetch;
  const active = { id: 3, state: "running", stop_requested: false };
  tasksStore.setData({ total: 1, items: [active] });
  globalThis.fetch = async () => Response.json({ ok: false,
    error: { message: "stop failed" } }, { status: 409 });
  try {
    await assert.rejects(cancelTask(3), /stop failed/);
    assert.equal(tasksStore.get().data.items[0].stop_requested, false);
  } finally { globalThis.fetch = fetch; tasksStore.reset(); }
});

test("a lost stop acknowledgement is quietly confirmed without another cancellation", async () => {
  const fetch = globalThis.fetch;
  const active = { id: 4, kind: "process", state: "running", terminal: false,
    stop_requested: false, owner_agent_id: 7, owner_run_id: 9, parent_task_id: 0,
    owner_session: "default/session", created_at: 100, schedule_id: "", schedule_generation: 0 };
  const stopped = { ...active, state: "cancelled", terminal: true, stop_requested: true };
  let deletes = 0, reads = 0;
  tasksStore.setData({ total: 1, items: [active] });
  globalThis.fetch = async (path, options) => {
    if (options.method === "DELETE") { ++deletes; throw new TypeError("lost reply"); }
    if (path === "/api/v1/tasks/4") { ++reads; return Response.json({ ok: true, data: stopped }); }
    return Response.json({ ok: true, data: { total: 1, items: [stopped] } });
  };
  try {
    const result = await cancelTask(4);
    assert.equal(result.stop_requested, true);
    assert.equal(deletes, 1); assert.equal(reads, 1);
    assert.equal(tasksStore.get().data.items[0].state, "cancelled");
  } finally { globalThis.fetch = fetch; tasksStore.reset(); }
});
