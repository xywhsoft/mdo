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

test("completed tool-stage wall time belongs only to the final reply of its run", () => {
  const events = [
    { kind: "agent_start", event_id: 1, run_id: "reused", agent_depth: 0,
      user_message_sequence: 1, time: 1000000 },
    { kind: "model_start", event_id: 2, run_id: "reused", agent_turn: 1, time: 2000000 },
    { kind: "model_text_delta", event_id: 3, run_id: "reused", agent_turn: 1,
      text: "Calling a tool", time: 2100000 },
    { kind: "model_done", event_id: 4, run_id: "reused", agent_turn: 1,
      success: true, input_tokens: 7, output_tokens: 3, time: 2500000 },
    { kind: "tool_start", event_id: 5, run_id: "reused", tool_call_id: "ok", time: 3000000 },
    { kind: "tool_done", event_id: 6, run_id: "reused", tool_call_id: "ok",
      success: true, time: 4500000 },
    { kind: "tool_start", event_id: 7, run_id: "reused", tool_call_id: "failed", time: 5000000 },
    { kind: "tool_done", event_id: 8, run_id: "reused", tool_call_id: "failed",
      success: false, time: 6000000 },
    { kind: "tool_done", event_id: 9, run_id: "reused", tool_call_id: "missing",
      success: true, time: 6500000 },
    { kind: "model_start", event_id: 10, run_id: "reused", agent_turn: 2, time: 7000000 },
    { kind: "model_text_delta", event_id: 11, run_id: "reused", agent_turn: 2,
      text: "Final reply", time: 7100000 },
    { kind: "model_done", event_id: 12, run_id: "reused", agent_turn: 2,
      success: true, input_tokens: 7, output_tokens: 3, time: 8000000 },
    { kind: "agent_done", event_id: 13, run_id: "reused", success: true, time: 8500000 },
    { kind: "agent_start", event_id: 14, run_id: "reused", agent_depth: 0,
      user_message_sequence: 2, time: 9000000 },
    { kind: "model_start", event_id: 15, run_id: "reused", agent_turn: 1, time: 10000000 },
    { kind: "model_text_delta", event_id: 16, run_id: "reused", agent_turn: 1,
      text: "Another run", time: 10100000 },
    { kind: "model_done", event_id: 17, run_id: "reused", agent_turn: 1,
      success: true, input_tokens: 2, output_tokens: 1, time: 11000000 },
    { kind: "agent_done", event_id: 18, run_id: "reused", success: true, time: 11500000 },
  ];
  const answers = eventsToTimeline(events).filter((item) => item.kind === "assistant");
  assert.equal(answers.length, 3);
  assert.equal(answers[0].toolStageSeconds, undefined);
  assert.equal(answers[1].toolStageSeconds, 2.5);
  assert.deepEqual(answers[1].runUsage, { calls: 2, input: 14, output: 6 });
  assert.equal(answers[1].modelDurationSeconds, 1.5);
  assert.equal(answers[1].tokensPerSecond, 4);
  assert.equal(answers[2].toolStageSeconds, undefined);
  assert.equal(answers[2].runUsage, undefined);
});

test("tool-only model call contributes to the final reply's run usage", () => {
  const events = [
    { kind: "agent_start", event_id: 1, run_id: "tool-run", agent_depth: 0,
      user_message_sequence: 1, time: 1000000 },
    { kind: "model_start", event_id: 2, run_id: "tool-run", agent_turn: 1,
      time: 2000000 },
    { kind: "model_done", event_id: 3, run_id: "tool-run", agent_turn: 1,
      success: true, input_tokens: 7, output_tokens: 3, time: 3000000 },
    { kind: "tool_start", event_id: 4, run_id: "tool-run", tool_call_id: "read",
      time: 3100000 },
    { kind: "tool_done", event_id: 5, run_id: "tool-run", tool_call_id: "read",
      success: true, time: 3200000 },
    { kind: "model_start", event_id: 6, run_id: "tool-run", agent_turn: 2,
      time: 4000000 },
    { kind: "model_text_delta", event_id: 7, run_id: "tool-run", agent_turn: 2,
      text: "Read complete", time: 4100000 },
    { kind: "model_done", event_id: 8, run_id: "tool-run", agent_turn: 2,
      success: true, input_tokens: 8, output_tokens: 4, time: 5000000 },
    { kind: "agent_done", event_id: 9, run_id: "tool-run", success: true,
      time: 5100000 },
  ];
  const answer = eventsToTimeline(events).find((item) => item.kind === "assistant");
  assert.deepEqual(answer.runUsage, { calls: 2, input: 15, output: 7 });
  assert.equal(answer.modelDurationSeconds, 2);
  assert.equal(answer.tokensPerSecond, 3.5);
  assert.equal(answer.inputTokens, 8);
  assert.equal(answer.outputTokens, 4);
});

test("a truncated run does not present partial usage as the run total", () => {
  const answer = eventsToTimeline([
    { kind: "model_done", event_id: 10, run_id: "partial", agent_turn: 1,
      success: true, input_tokens: 7, output_tokens: 3, time: 1000000 },
    { kind: "model_text_delta", event_id: 11, run_id: "partial", agent_turn: 2,
      text: "Visible reply", time: 2000000 },
    { kind: "model_done", event_id: 12, run_id: "partial", agent_turn: 2,
      success: true, input_tokens: 8, output_tokens: 4, time: 3000000 },
    { kind: "agent_done", event_id: 13, run_id: "partial", success: true,
      time: 3100000 },
  ]).find((item) => item.kind === "assistant");
  assert.equal(answer.runUsage, undefined);
  assert.equal(answer.inputTokens, 8);
  assert.equal(answer.outputTokens, 4);
});

test("incomplete model timing hides mixed-scope duration and speed", () => {
  const answer = eventsToTimeline([
    { kind: "agent_start", event_id: 1, run_id: "timing-gap", agent_depth: 0,
      user_message_sequence: 1, time: 1000000 },
    { kind: "model_done", event_id: 2, run_id: "timing-gap", agent_turn: 1,
      success: true, input_tokens: 7, output_tokens: 3, time: 2000000 },
    { kind: "model_text_delta", event_id: 3, run_id: "timing-gap", agent_turn: 2,
      text: "Reply", time: 3000000 },
    { kind: "model_done", event_id: 4, run_id: "timing-gap", agent_turn: 2,
      success: true, input_tokens: 8, output_tokens: 4, time: 4000000 },
    { kind: "agent_done", event_id: 5, run_id: "timing-gap", success: true,
      time: 4100000 },
  ]).find((item) => item.kind === "assistant");
  assert.deepEqual(answer.runUsage, { calls: 2, input: 15, output: 7 });
  assert.equal(answer.modelDurationSeconds, undefined);
  assert.equal(answer.tokensPerSecond, undefined);
});

test("agent completion text receives run totals when no text deltas arrived", () => {
  const answer = eventsToTimeline([
    { kind: "agent_start", event_id: 1, run_id: "fallback", agent_depth: 0,
      user_message_sequence: 1, time: 1000000 },
    { kind: "model_start", event_id: 2, run_id: "fallback", agent_turn: 1,
      time: 2000000 },
    { kind: "model_done", event_id: 3, run_id: "fallback", agent_turn: 1,
      success: true, input_tokens: 2, output_tokens: 1, time: 3000000 },
    { kind: "model_start", event_id: 4, run_id: "fallback", agent_turn: 2,
      time: 4000000 },
    { kind: "model_done", event_id: 5, run_id: "fallback", agent_turn: 2,
      success: true, input_tokens: 3, output_tokens: 2, time: 6000000 },
    { kind: "agent_done", event_id: 6, run_id: "fallback", success: true,
      text: "Summary", time: 6100000 },
  ]).find((item) => item.kind === "assistant");
  assert.equal(answer.text, "Summary");
  assert.deepEqual(answer.runUsage, { calls: 2, input: 5, output: 3 });
  assert.equal(answer.modelDurationSeconds, 3);
  assert.equal(answer.tokensPerSecond, 1);
});

test("single model completion text retains its usage without text deltas", () => {
  const answer = eventsToTimeline([
    { kind: "agent_start", event_id: 1, run_id: "single-fallback", agent_depth: 0,
      user_message_sequence: 1, time: 1000000 },
    { kind: "model_start", event_id: 2, run_id: "single-fallback", agent_turn: 1,
      time: 2000000 },
    { kind: "model_done", event_id: 3, run_id: "single-fallback", agent_turn: 1,
      success: true, input_tokens: 5, output_tokens: 2, time: 3000000 },
    { kind: "agent_done", event_id: 4, run_id: "single-fallback", success: true,
      text: "Summary", time: 3100000 },
  ]).find((item) => item.kind === "assistant");
  assert.equal(answer.inputTokens, 5);
  assert.equal(answer.outputTokens, 2);
  assert.equal(answer.modelDurationSeconds, 1);
  assert.equal(answer.tokensPerSecond, 2);
  assert.equal(answer.runUsage, undefined);
});
