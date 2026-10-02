import assert from "node:assert/strict";
import test from "node:test";
import { sessionRunActivities } from "../app/web/js/features/sessions/session-list.js";

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
