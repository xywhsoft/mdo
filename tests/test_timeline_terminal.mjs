import assert from "node:assert/strict";
import test from "node:test";
import { eventsToTimeline } from "../app/web/js/features/chat/timeline.js";

function event(kind, event_id, run_id, fields = {}) {
  return { kind, event_id, run_id, time: event_id * 1000000, ...fields };
}

test("a fatal stream error ends partial reply, reasoning and tool cards without losing content", () => {
  const items = eventsToTimeline([
    event("agent_start", 1, "a", { agent_depth: 0, user_message_sequence: 7, text: "Original input" }),
    event("model_start", 2, "a", { agent_turn: 1 }),
    event("model_text_delta", 3, "a", { agent_turn: 1, text: "Partial answer", text_truncated: true }),
    event("model_reasoning_delta", 4, "a", { agent_turn: 1, text: "Partial thought" }),
    event("tool_start", 5, "a", { tool_call_id: "unfinished", tool_name: "exec" }),
    event("error", 6, "a", { text: "Invalid provider stream", terminal: true }),
  ]);
  const answer = items.find(item => item.kind === "assistant");
  assert.equal(answer.state, "failed");
  assert.equal(answer.text, "Partial answer");
  assert.equal(answer.retryPrompt.sequence, 7);
  assert.equal(answer.feedbackEventId, undefined);
  assert.equal(answer.tokensPerSecond, undefined);
  assert.equal(answer.textTruncated, true);
  assert.equal(answer.copySpans[0].eventId, 3);
  assert.equal(items.find(item => item.kind === "reasoning").state, "failed");
  assert.equal(items.find(item => item.kind === "tool").state, "failed");
  assert.equal(items.find(item => item.kind === "error").text, "Invalid provider stream");
});

test("failure settles only its run and current epoch when runs interleave or IDs are reused", () => {
  const items = eventsToTimeline([
    event("agent_start", 1, "a"),
    event("model_text_delta", 2, "a", { agent_turn: 1, text: "Earlier successful answer" }),
    event("agent_done", 3, "a", { success: true }),
    event("agent_start", 4, "b"),
    event("model_text_delta", 5, "b", { agent_turn: 1, text: "Another active run" }),
    event("model_reasoning_delta", 6, "b", { agent_turn: 1, text: "Other thought" }),
    event("agent_start", 7, "a"),
    event("model_text_delta", 8, "a", { agent_turn: 1, text: "New partial answer" }),
    event("error", 9, "a", { text: "Failed second run" }),
  ]);
  const answers = items.filter(item => item.kind === "assistant");
  assert.deepEqual(answers.map(item => [item.text, item.state]), [
    ["Earlier successful answer", "done"], ["Another active run", "running"],
    ["New partial answer", "failed"],
  ]);
  assert.equal(items.find(item => item.kind === "reasoning").state, "running");
});

test("failure after completed model turns changes only the final answer and pending work", () => {
  const items = eventsToTimeline([
    event("agent_start", 1, "a"),
    event("model_text_delta", 2, "a", { agent_turn: 1, text: "First model turn" }),
    event("model_done", 3, "a", { agent_turn: 1, success: true, input_tokens: 2, output_tokens: 1 }),
    event("tool_start", 4, "a", { tool_call_id: "finished" }),
    event("tool_done", 5, "a", { tool_call_id: "finished", success: true }),
    event("model_text_delta", 6, "a", { agent_turn: 2, text: "Last model turn" }),
    event("model_done", 7, "a", { agent_turn: 2, success: true, input_tokens: 2, output_tokens: 1 }),
    event("tool_start", 8, "a", { tool_call_id: "pending" }),
    event("error", 9, "a", { text: "Tool-loop guard rejected further work" }),
  ]);
  assert.deepEqual(items.filter(item => item.kind === "assistant").map(item => item.state), ["done", "failed"]);
  assert.deepEqual(items.filter(item => item.kind === "tool").map(item => item.state), ["done", "failed"]);
});

test("failure before model output keeps the original error without inventing an empty answer", () => {
  const items = eventsToTimeline([
    event("agent_start", 1, "a"), event("error", 2, "a", { text: "Endpoint unavailable" }),
  ]);
  assert.equal(items.filter(item => item.kind === "assistant").length, 0);
  assert.equal(items.find(item => item.kind === "error").text, "Endpoint unavailable");
});

test("the library cancellation event remains stopped while partial content and retry input survive", () => {
  const items = eventsToTimeline([
    event("agent_start", 1, "a", { agent_depth: 0, user_message_sequence: 3, text: "Keep this input" }),
    event("model_text_delta", 2, "a", { agent_turn: 1, text: "Stopped partial answer" }),
    event("model_reasoning_delta", 3, "a", { agent_turn: 1, text: "Stopped thought" }),
    event("tool_start", 4, "a", { tool_call_id: "stopped" }),
    event("agent_done", 5, "a", { success: false, terminal: true }),
  ]);
  for (const item of items.filter(item => ["assistant", "reasoning", "tool"].includes(item.kind)))
    assert.equal(item.state, "cancelled");
  assert.equal(items.find(item => item.kind === "assistant").retryPrompt.text, "Keep this input");
  assert.equal(items.filter(item => item.kind === "error").length, 0);
});
