import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import test from "node:test";
import { eventsToTimeline, resolveTimelineActionText } from "../app/web/js/features/chat/timeline.js";
import { decodeConversationPage } from "../app/web/js/features/chat/conversation-page.js";

const epoch = "a".repeat(64);
const e = (event_id, kind, fields = {}) => ({ event_id, kind, schema_version: 6,
  run_id: 1, agent_depth: 0, agent_turn: 3, time: event_id * 1e6,
  text: "", projection_epoch: epoch, ...fields });
const input = e(1, "agent_start", { user_message_sequence: 1, text: "Original input",
  attachments: ["c".repeat(32)], text_truncated: false });
const reply = e(20, "model_text_delta", { text: "Latest answer", action_prompt: input });
const done = e(22, "agent_done", { success: true });

test("a cold reply fragment has fork/retry actions without displaying the older input", () => {
  const items = eventsToTimeline([reply, done]);
  assert.equal(items.filter(i => i.kind === "user").length, 0);
  const answer = items.find(i => i.kind === "assistant");
  assert.equal(answer.forkThroughSequence, null);
  assert.equal(answer.retryPrompt.sourceEventId, 1);
  assert.equal(answer.retryPrompt.sequence, 1);
  assert.equal(answer.retryPrompt.text, input.text);
  assert.deepEqual(answer.retryPrompt.attachments, input.attachments);
});

test("a resumed reply can inherit the original input across different run IDs", () => {
  const items = eventsToTimeline([e(18, "agent_start", { run_id: 9, user_message_sequence: 0 }),
    { ...reply, run_id: 9 }, { ...done, run_id: 9 }]);
  const answer = items.find(i => i.kind === "assistant");
  assert.equal(answer.retryPrompt.sourceEventId, 1);
  assert.equal(answer.retryPrompt.sequence, 1);
  assert.equal(answer.forkThroughSequence, null);
});

test("paging the original input in preserves actions and displays exactly one input", () => {
  const items = eventsToTimeline([input, reply, done]);
  assert.equal(items.filter(i => i.kind === "user").length, 1);
  const answer = items.find(i => i.kind === "assistant");
  assert.equal(answer.retryPrompt.sourceEventId, 1);
  assert.equal(answer.retryPrompt.sequence, 1);
  assert.equal(answer.forkThroughSequence, null);
});

test("an action preview fetches the complete original input under the current owner", async () => {
  const preview = { ...input, text: "Original", text_truncated: true };
  const answer = eventsToTimeline([{ ...reply, action_prompt: preview }])[0];
  const owner = { projectId: "qa", sessionId: "branch" };
  const calls = [];
  const text = await resolveTimelineActionText(answer.retryPrompt, owner,
    async (...args) => { calls.push(args); return input.text; });
  assert.equal(text, input.text);
  assert.deepEqual(calls[0].slice(0, 4), ["qa", "branch", 1, "agent_start"]);
  assert.equal(calls[0][4].epoch, epoch);
});

function page(event) {
  const items_json = JSON.stringify([{ ...event, aggregate_end_id: 0,
    node_id: `branch:${epoch}:${event.event_id}`, content_hash: "b".repeat(64) }]);
  return { project_id: "qa", session_id: "branch", epoch, items_json,
    items_hash: createHash("sha256").update(items_json).digest("hex"),
    next_cursor: 20, latest_event_id: 20, next_before: 20,
    delta: false, has_more: true, history_lost: false };
}

test("snapshot validation rejects foreign, invalid and newer input anchors", () => {
  const owner = { projectId: "qa", sessionId: "branch" };
  assert.equal(decodeConversationPage(page(reply), owner).items.length, 1);
  for (const fields of [{ projection_epoch: "d".repeat(64) }, { event_id: 21 },
    { user_message_sequence: 0 }, { kind: "model_text_delta" }, { agent_depth: 1 },
    { attachments: ["../foreign-image"] }, { text_truncated: undefined }]) {
    assert.throws(() => decodeConversationPage(page({ ...reply,
      action_prompt: { ...input, ...fields } }), owner), { code: "conversation_invalid" });
  }
});

test("an incomplete fragment without a trusted anchor keeps mutation actions unavailable", () => {
  const { action_prompt, ...orphan } = reply;
  const answer = eventsToTimeline([orphan])[0];
  assert.equal(answer.retryPrompt, undefined);
  assert.equal("forkThroughSequence" in answer, false);
});
