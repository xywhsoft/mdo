import assert from "node:assert/strict";
import test from "node:test";
import { sessionRunActivities, sessionRunOutcomes } from "../app/web/js/features/sessions/session-list.js";

const run = (changes = {}) => ({ id: "run-1", project_id: "default",
  session_id: "task", state: "running", terminal: false,
  cancel_requested: false, ...changes });

test("the sidebar distinguishes preparing, ongoing and acknowledged stopping", () => {
  for (const [changes, expected] of [[{ state: "created" }, "created"],
    [{}, "running"], [{ cancel_requested: true }, "stopping"]])
    assert.equal(sessionRunActivities([run(changes)]).get("default/task"), expected);
});

test("finished and incomplete records cannot invent a running indicator", () => {
  const records = [run({ terminal: true }), run({ terminal: undefined }),
    run({ state: "succeeded" }), run({ state: "failed" }), run({ state: "cancelled" }),
    run({ state: "timed_out" }), run({ state: "unknown" }), run({ project_id: "" }),
    run({ session_id: null }), run({ project_id: {} }), null];
  assert.equal(sessionRunActivities(records).size, 0);
  assert.equal(sessionRunActivities().size, 0);
});

test("equal session IDs in different projects keep their own activity", () => {
  assert.deepEqual([...sessionRunActivities([run(), run({ project_id: "other",
    cancel_requested: true })])], [["default/task", "running"], ["other/task", "stopping"]]);
});

test("older completion does not erase a live run in either snapshot order", () => {
  const completed = run({ id: "old", terminal: true, state: "succeeded" });
  assert.equal(sessionRunActivities([run(), completed]).get("default/task"), "running");
  assert.equal(sessionRunActivities([completed, run()]).get("default/task"), "running");
});

test("a new active run takes priority over stopping or preparing in either order", () => {
  const preparing = run({ state: "created" }), stopping = run({ cancel_requested: true });
  for (const records of [[preparing, stopping, run()], [run(), stopping, preparing]])
    assert.equal(sessionRunActivities(records).get("default/task"), "running");
  assert.equal(sessionRunActivities([preparing, stopping]).get("default/task"), "stopping");
});

test("terminal outcomes use the newest timestamp independently of snapshot order", () => {
  const success = run({ terminal: true, state: "succeeded", ended_at: 10 });
  const failed = run({ terminal: true, state: "failed", ended_at: 20 });
  for (const items of [[success, failed], [failed, success]])
    assert.equal(sessionRunOutcomes(items).get("default/task"), "failed");
  assert.equal(sessionRunOutcomes([run({ terminal: true, state: "timed_out", ended_at: 30 })]).get("default/task"), "failed");
});

test("persisted outcomes survive empty run history and newer blockage beats old success", () => {
  const session = { project_id: "default", id: "task", last_run_at: 30, last_run_outcome: "failed" };
  assert.equal(sessionRunOutcomes([], [session]).get("default/task"), "failed");
  assert.equal(sessionRunOutcomes([run({ terminal: true, state: "succeeded", ended_at: 20 })], [session]).get("default/task"), "failed");
  assert.equal(sessionRunOutcomes([], [{ ...session, last_run_outcome: "succeeded" }]).get("default/task"), "succeeded");
});

test("new start clears old result and cancellation stays neutral rather than green or red", () => {
  const old = run({ terminal: true, state: "succeeded", ended_at: 20 });
  const session = { project_id: "default", id: "task", last_run_at: 30, last_run_outcome: "" };
  assert.equal(sessionRunOutcomes([old], [session]).size, 0);
  assert.equal(sessionRunOutcomes([run({ terminal: true, state: "cancelled", ended_at: 40 })], [session]).get("default/task"), "cancelled");
  assert.equal(sessionRunOutcomes([old, run({ terminal: true, state: "succeeded", project_id: "other", ended_at: 50 })], [session]).get("other/task"), "succeeded");
});

test("malformed or unfinished outcomes cannot invent a completion dot", () => {
  assert.equal(sessionRunOutcomes([run(), run({ terminal: true, state: "failed" }),
    run({ terminal: true, state: "unknown", ended_at: 10 }),
    run({ terminal: true, state: "succeeded", project_id: null, ended_at: 10 }), null]).size, 0);
});
