import assert from "node:assert/strict";
import test from "node:test";

import { eventsToTimeline } from "../app/web/js/features/chat/timeline.js";

test("tool results expose their artifact even without a separate artifact event", () => {
  const items = eventsToTimeline([
    { kind: "tool_start", event_id: 1, run_id: 7, tool_call_id: "call-1",
      tool_name: "read", text: '{"path":"example.txt"}', time: 1000000 },
    { kind: "tool_done", event_id: 2, run_id: 7, tool_call_id: "call-1",
      tool_name: "read", text: "output truncated", artifact_id: 3,
      artifact_path: "artifacts/run-7/3-read.txt", success: true, time: 2000000 },
  ]);
  assert.equal(items.length, 1);
  assert.equal(items[0].kind, "tool");
  assert.equal(items[0].artifactId, 3);
  assert.equal(items[0].artifactEventId, 2);
  assert.equal(items[0].artifactPath, "artifacts/run-7/3-read.txt");
});

test("unpaired tool results still expose their artifact", () => {
  const [item] = eventsToTimeline([
    { kind: "tool_done", event_id: 9, run_id: 7, tool_call_id: "call-9",
      artifact_id: 4, success: true, time: 1000000 },
  ]);
  assert.equal(item.artifactId, 4);
  assert.equal(item.artifactEventId, 9);
});
