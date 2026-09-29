import assert from "node:assert/strict";
import test from "node:test";

import { eventsToTimeline } from "../app/web/js/features/chat/timeline.js";

test("interleaved text, reasoning, and task events retain one card per model turn", () => {
  const events = [
    { kind: "agent_start", event_id: 1, run_id: "run-1", agent_depth: 0,
      user_message_sequence: 1, text: "Explain this", time: 1000000 },
    { kind: "model_start", event_id: 2, run_id: "run-1", agent_turn: 1,
      time: 2000000 },
    { kind: "model_text_delta", event_id: 3, run_id: "run-1", agent_turn: 1,
      model: "Ling", text: "Hel", time: 2100000 },
    { kind: "model_reasoning_delta", event_id: 4, run_id: "run-1", agent_turn: 1,
      text: "Think", time: 2200000 },
    { kind: "model_text_delta", event_id: 5, run_id: "run-1", agent_turn: 1,
      model: "Ling", text: "lo", time: 2300000 },
    { kind: "model_reasoning_delta", event_id: 6, run_id: "run-1", agent_turn: 1,
      text: " again", time: 2400000 },
    { kind: "task_updated", event_id: 7, run_id: "run-1", task_id: 4,
      text: "Background task completed", terminal: true, time: 2500000 },
    { kind: "model_text_delta", event_id: 8, run_id: "run-1", agent_turn: 1,
      model: "Ling", text: " world", time: 2600000 },
    { kind: "model_done", event_id: 9, run_id: "run-1", agent_turn: 1,
      success: true, input_tokens: 10, output_tokens: 3, time: 3000000 },
    { kind: "agent_done", event_id: 10, run_id: "run-1",
      success: true, time: 3100000 },
  ];
  const items = eventsToTimeline(events);
  assert.equal(new Set(items.map((item) => item.key)).size, items.length);
  assert.deepEqual(items.map((item) => item.kind),
    ["user", "assistant", "reasoning", "task"]);
  assert.equal(items.find((item) => item.kind === "assistant").text, "Hello world");
  assert.equal(items.find((item) => item.kind === "reasoning").text,
    "Think again");
  assert.equal(items.find((item) => item.kind === "assistant").feedbackEventId, 9);
  assert.equal(items.find((item) => item.kind === "assistant").outputTokens, 3);
  assert.equal(items.find((item) => item.kind === "assistant").modelDurationSeconds, 1);
});

test("a truncated event window does not invent an LLM duration", () => {
  const items = eventsToTimeline([
    { kind: "model_text_delta", event_id: 8, run_id: "run-2", agent_turn: 1,
      model: "Ling", text: "Reply", time: 2100000 },
    { kind: "model_done", event_id: 9, run_id: "run-2", agent_turn: 1,
      success: true, input_tokens: 7, output_tokens: 3, time: 3100000 },
  ]);
  const answer = items.find((item) => item.kind === "assistant");
  assert.equal(answer.modelDurationSeconds, undefined);
  assert.equal(answer.tokensPerSecond, 3);
});
