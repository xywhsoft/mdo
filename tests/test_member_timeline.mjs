import assert from "node:assert/strict";
import test from "node:test";
import { eventsToTimeline } from "../app/web/js/features/chat/timeline.js";

test("native speaker sources survive projection and mixed histories are not single-ledger action targets", () => {
  const source = { namespace: "team.s.g1", member: "writer", agent: "subagent.writer" };
  const events = [
    { event_id: 1, run_id: 1, kind: "agent_start", agent_depth: 0, schema_version: 8, user_message_sequence: 1, text: "Coordinator input" },
    { event_id: 2, run_id: 1, kind: "model_text_delta", agent_depth: 0, text: "Coordinator reply" },
    { event_id: 3, run_id: 2, kind: "agent_start", agent_depth: 0, schema_version: 8, user_message_sequence: 1, text: "Member input", source },
    { event_id: 4, run_id: 2, kind: "model_text_delta", agent_depth: 0, text: "Member reply", source },
    { event_id: 5, run_id: 3, kind: "agent_start", agent_depth: 0, schema_version: 8, user_message_sequence: 2, text: "Back to coordinator" },
    { event_id: 6, run_id: 3, kind: "model_text_delta", agent_depth: 0, text: "Root context" },
    { event_id: 7, run_id: 4, kind: "agent_start", agent_depth: 0, schema_version: 8, user_message_sequence: 0, text: "Resume member", source },
    { event_id: 8, run_id: 4, kind: "model_text_delta", agent_depth: 0, text: "Resumed member", source },
  ];
  const items = eventsToTimeline(events);
  assert.deepEqual(items.find(i => i.text === "Member reply").executionSource, source);
  assert.deepEqual(items.find(i => i.text === "Resumed member").executionSource, source);
  assert.equal(items.find(i => i.text === "Resumed member").retryPrompt.sourceEventId, 3,
    "A member resume must not attach to the coordinator's last input");
  assert.ok(items.every(i => i.historyReadOnly));
  assert.ok(eventsToTimeline(events.slice(0, 2)).every(i => !i.historyReadOnly), "Ordinary conversations retain their actions");
});
