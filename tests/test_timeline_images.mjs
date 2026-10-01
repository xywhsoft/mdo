import assert from "node:assert/strict";
import test from "node:test";
import { eventsToTimeline } from "../app/web/js/features/chat/timeline.js";

function turn(text, attachments) {
  return eventsToTimeline([
    { kind: "agent_start", schema_version: 5, event_id: 1, run_id: 7,
      agent_depth: 0, user_message_sequence: 9, text, attachments },
    { kind: "model_text_delta", event_id: 2, run_id: 7, text: "Answer" },
    { kind: "agent_done", event_id: 3, run_id: 7, success: true },
  ]);
}

test("an image-only user row and its retry keep empty text and durable attachment references", () => {
  const attachments = ["a".repeat(32), "b".repeat(32)];
  const items = turn("", attachments);
  const user = items.find(item => item.kind === "user");
  const answer = items.find(item => item.kind === "assistant");
  assert.equal(user.text, "");
  assert.equal(user.userMessageSequence, 9);
  assert.equal(user.sourceEventId, 1);
  assert.deepEqual(user.attachments, attachments);
  assert.equal(answer.retryPrompt.text, "");
  assert.equal(answer.retryPrompt.sequence, 9);
  assert.equal(answer.retryPrompt.sourceEventId, 1);
  assert.deepEqual(answer.retryPrompt.attachments, attachments);
});

test("literal image-placeholder text remains user content with or without attachments", () => {
  for (const attachments of [[], ["a".repeat(32)]]) {
    const items = turn("[Image attachment]", attachments);
    assert.equal(items.find(item => item.kind === "user").text, "[Image attachment]");
    assert.equal(items.find(item => item.kind === "assistant").retryPrompt.text,
      "[Image attachment]");
  }
});
