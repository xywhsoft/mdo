import assert from "node:assert/strict";
import test from "node:test";

import { eventsToTimeline, resolveTimelineCopyText,
  resolveTimelineActionText } from
  "../app/web/js/features/chat/timeline.js";

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

test("an explicit first history boundary replaces the generic gap notice", () => {
  const clear = { kind: "history_truncated", event_id: 6,
    text: "会话历史已清空", time: 1000000 };
  const cleared = eventsToTimeline([clear], true);
  assert.equal(cleared.length, 1);
  assert.equal(cleared[0].text, clear.text);
  assert.equal(cleared[0].key, "history-6");

  const unexplained = eventsToTimeline([
    { kind: "agent_start", event_id: 7, run_id: 3, text: "later",
      user_message_sequence: 1, time: 2000000 }, clear,
  ], true);
  assert.equal(unexplained[0].key, "history-gap");
});

test("timeline marks incomplete user and assistant text before copy", () => {
  const items = eventsToTimeline([
    { kind: "agent_start", event_id: 10, run_id: 5, time: 1000000,
      user_message_sequence: 2, text: "visible user prefix",
      text_truncated: true },
    { kind: "model_text_delta", event_id: 11, run_id: 5, time: 2000000,
      text: "visible answer prefix", text_truncated: true },
  ]);
  assert.deepEqual(items.filter((item) => ["user", "assistant"].includes(item.kind))
    .map((item) => [item.kind, item.textTruncated]),
  [["user", true], ["assistant", true]]);
});

test("copy resolves a truncated user event without changing the visible timeline", async () => {
  const [item] = eventsToTimeline([{ kind: "agent_start", event_id: 10,
    run_id: 5, time: 1000000, text: "prefix", text_truncated: true }]);
  const owner = { projectId: "p", sessionId: "s" };
  const copied = await resolveTimelineCopyText(item, owner,
    async (project, session, id, kind) => {
      assert.deepEqual([project, session, id, kind], ["p", "s", 10, "agent_start"]);
      return "prefix and the rest";
    });
  assert.deepEqual(copied, { text: "prefix and the rest", complete: true });
  assert.equal(item.text, "prefix");
  assert.deepEqual(await resolveTimelineCopyText(item, owner, async () => null),
    { text: "prefix", complete: false });
  const oversized = "prefix" + "x".repeat(1048576);
  assert.deepEqual(await resolveTimelineCopyText(item, owner,
    async () => oversized), { text: "prefix", complete: false });
});

test("copy restores only truncated assistant chunks in their original order", async () => {
  const items = eventsToTimeline([
    { kind: "model_text_delta", event_id: 20, run_id: 5,
      time: 1000000, text: "first " },
    { kind: "model_text_delta", event_id: 21, run_id: 5,
      time: 1000001, text: "vis", text_truncated: true },
    { kind: "model_text_delta", event_id: 22, run_id: 5,
      time: 1000002, text: " last" },
  ]);
  const answer = items.find((item) => item.kind === "assistant");
  assert.deepEqual(await resolveTimelineCopyText(answer,
    { projectId: "p", sessionId: "s" }, async () => "visible"),
  { text: "first visible last", complete: true });
  assert.deepEqual(await resolveTimelineCopyText(answer,
    { projectId: "p", sessionId: "s" }, async () => "different"),
  { text: "first vis last", complete: false });
});

test("long-message edit and retry require the original prompt before changing history", async () => {
  const items = eventsToTimeline([
    { kind: "agent_start", event_id: 31, run_id: 6, time: 1000000,
      agent_depth: 0, user_message_sequence: 4, text: "visible",
      text_truncated: true },
    { kind: "model_text_delta", event_id: 32, run_id: 6,
      time: 2000000, text: "answer" },
  ]);
  const owner = { projectId: "p", sessionId: "s" };
  const prompt = items.find((item) => item.kind === "user");
  const retry = items.find((item) => item.kind === "assistant").retryPrompt;
  assert.deepEqual(retry.copySpans, prompt.copySpans);
  assert.equal(await resolveTimelineActionText(prompt, owner,
    async () => "visible remainder"), "visible remainder");
  assert.equal(await resolveTimelineActionText(retry, owner,
    async () => "visible remainder"), "visible remainder");
  await assert.rejects(resolveTimelineActionText(retry, owner,
    async () => null), /完整消息/);
});
