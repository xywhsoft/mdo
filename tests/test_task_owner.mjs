import assert from "node:assert/strict";
import test from "node:test";

import { taskBelongsToSession, taskOwnerLocation } from
  "../app/web/js/features/tasks/task-owner.js";

test("background tasks belong to their snapshot session on Windows and Unix", () => {
  const windows = { owner_session:
    "D:\\mdo\\mdo-home\\sessions\\default\\session-1\\snapshot.json" };
  const unix = { owner_session:
    "/opt/mdo/mdo-home/sessions/qa/session-1/snapshot.json" };
  assert.deepEqual(taskOwnerLocation(windows.owner_session),
    { projectId: "default", sessionId: "session-1" });
  assert.deepEqual(taskOwnerLocation(unix.owner_session),
    { projectId: "qa", sessionId: "session-1" });
  assert.equal(taskBelongsToSession(windows, "default", "session-1"), true);
  assert.equal(taskBelongsToSession(windows, "qa", "session-1"), false);
  assert.equal(taskBelongsToSession(unix, "qa", "session-1"), true);
  assert.equal(taskBelongsToSession(unix, "qa", "session-2"), false);
});

test("unrecognized owners never attach a task to the current conversation", () => {
  for (const owner_session of ["session-1", "", null,
    "/tmp/sessions/qa/session-1/snapshot.json.bak",
    "/tmp/other/qa/session-1/snapshot.json"]) {
    assert.equal(taskBelongsToSession({ owner_session }, "qa", "session-1"), false);
  }
});
