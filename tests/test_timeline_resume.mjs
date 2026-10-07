import assert from "node:assert/strict";
import test from "node:test";
import { eventsToTimeline } from "../app/web/js/features/chat/timeline.js";
import { conversationGroups } from "../app/web/js/features/chat/conversation-history.js";

function event(kind, id, run, fields = {}) {
  return { kind, event_id: id, run_id: run, schema_version: 6,
    agent_depth: 0, agent_turn: 1, time: id * 1e6, ...fields };
}
const original = () => [
  event("agent_start", 1, 1, { user_message_sequence: 9, text: "Inspect the image",
    attachments: ["a".repeat(32)], text_truncated: true }),
  event("model_text_delta", 2, 1, { text: "Partial answer" }),
  event("agent_done", 3, 1, { success: false }),
];
const resumed = (start, run) => [
  event("agent_start", start, run, { user_message_sequence: 0, text: "Resume marker" }),
  event("model_text_delta", start + 1, run, { text: "Completed answer" }),
  event("agent_done", start + 2, run, { success: true }),
];

test("a resumed reply retains the original durable input for retry and fork", () => {
  const events = [...original(), ...resumed(4, 2)];
  const frozen = structuredClone(events);
  const items = eventsToTimeline(events);
  const replies = items.filter(item => item.kind === "assistant");
  assert.deepEqual(replies[1].retryPrompt, replies[0].retryPrompt);
  assert.equal(replies[1].retryPrompt.sourceEventId, 1);
  assert.equal(replies[1].retryPrompt.sequence, 9);
  assert.deepEqual(replies[1].retryPrompt.attachments, ["a".repeat(32)]);
  assert.equal(replies[1].retryPrompt.copySpans[0].eventId, 1);
  assert.equal(replies[1].forkThroughSequence, null);
  assert.equal(items.filter(item => item.kind === "user").length, 1);
  const groups = conversationGroups(items);
  assert.equal(groups.length, 1);
  assert.equal(groups[0].state, "done");
  assert.deepEqual(events, frozen);
});

test("multiple resumes and reused run IDs retain the same input until a new user turn", () => {
  const events = [...original(), ...resumed(4, 2), ...resumed(7, 2),
    event("agent_start", 10, 1, { user_message_sequence: 13, text: "New input" }),
    ...resumed(11, 3)];
  const replies = eventsToTimeline(events).filter(item => item.kind === "assistant");
  assert.deepEqual(replies.map(item => item.retryPrompt?.sequence), [9, 9, 9, 13]);
  assert.deepEqual(replies.map(item => item.forkThroughSequence), [12, 12, 12, null]);
});

test("subagent prompts cannot become the original input of a resumed main Agent", () => {
  const events = [...original(),
    event("agent_start", 4, "child", { agent_depth: 1,
      user_message_sequence: 100, text: "Child task" }),
    event("agent_done", 5, "child", { agent_depth: 1, success: true }),
    ...resumed(6, 2)];
  const answer = eventsToTimeline(events).filter(item => item.kind === "assistant").at(-1);
  assert.equal(answer.retryPrompt?.sequence, 9);
});

test("missing original history and legacy zero-sequence starts never invent action targets", () => {
  for (const events of [resumed(4, 2), [...original(),
    ...resumed(4, 2).map(item => ({ ...item, schema_version: 2 }))]]) {
    const answer = eventsToTimeline(events).filter(item => item.kind === "assistant").at(-1);
    assert.equal(answer.retryPrompt, undefined);
    assert.equal("forkThroughSequence" in answer, false);
  }
});

test("a history boundary invalidates discarded action targets even when a run ID is reused", () => {
  const events = [...original(),
    event("history_truncated", 4, 1, { source_event_id: 1, text: "会话历史已清空" }),
    ...resumed(5, 1)];
  const answer = eventsToTimeline(events).filter(item => item.kind === "assistant").at(-1);
  assert.equal(answer.retryPrompt, undefined);
  assert.equal("forkThroughSequence" in answer, false);
});

test("a retained original input survives a later history boundary", () => {
  const events = [...original(),
    event("history_truncated", 5, 2, { source_event_id: 4, text: "会话历史已截断" }),
    ...resumed(6, 3)];
  const answer = eventsToTimeline(events).filter(item => item.kind === "assistant").at(-1);
  assert.equal(answer.retryPrompt?.sourceEventId, 1);
});
