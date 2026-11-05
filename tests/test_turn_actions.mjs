import assert from "node:assert/strict";
import test from "node:test";
import { eventsToTimeline } from "../app/web/js/features/chat/timeline.js";
import { conversationGroups } from "../app/web/js/features/chat/conversation-history.js";
import { conversationTurnActions, primaryTurnReply } from "../app/web/js/features/chat/turn-actions.js";

const e = (event_id, kind, fields = {}) => ({ event_id, kind, schema_version: 11,
  run_id: "main", agent_depth: 0, agent_turn: 1, time: event_id * 1e6, ...fields });
const input = () => e(1, "agent_start", { text: "Do the work", user_message_sequence: 3 });
const actions = events => conversationGroups(eventsToTimeline(events)).map(conversationTurnActions).filter(Boolean);

test("model completion alone exposes no turn actions; the enclosing turn must finish", () => {
  const events = [input(), e(2, "model_text_delta", { text: "Inspecting" }),
    e(3, "model_done", { success: true }), e(4, "tool_start", { tool_name: "exec", tool_call_id: "one" })];
  assert.equal(actions(events).length, 0);
  events.push(e(5, "tool_done", { tool_call_id: "one", success: true }),
    e(6, "model_text_delta", { agent_turn: 2, text: "Main answer" }), e(7, "agent_done", { success: true }));
  const [footer] = actions(events);
  assert.equal(actions(events).length, 1);
  assert.equal(footer.primaryReply.text, "Main answer");
  assert.equal(footer.retryPrompt.sequence, 3);
  assert.equal(footer.retryPrompt.sourceEventId, 1);
  assert.equal(footer.forkThroughSequence, null);
});

test("a resume hides the whole toolbar until it ends, then retries the original input", () => {
  const events = [input(), e(2, "model_text_delta", { text: "Partial" }), e(3, "agent_done", { success: false }),
    e(4, "agent_start", { run_id: "resume", user_message_sequence: 0 })];
  assert.equal(actions(events).length, 0);
  events.push(e(5, "model_text_delta", { run_id: "resume", text: "Complete" }),
    e(6, "agent_done", { run_id: "resume", success: true }));
  assert.equal(actions(events)[0].primaryReply.text, "Complete");
  assert.equal(actions(events)[0].retryPrompt.sourceEventId, 1);
});

test("turns have independent end boundaries and one toolbar each", () => {
  const events = [input(), e(2, "model_text_delta", { text: "First" }), e(3, "agent_done", { success: true }),
    e(4, "agent_start", { run_id: "next", user_message_sequence: 8, text: "Next" }),
    e(5, "model_text_delta", { run_id: "next", text: "Second" }), e(6, "agent_done", { run_id: "next", success: true })];
  assert.deepEqual(actions(events).map(item => [item.primaryReply.text, item.forkThroughSequence]), [["First", 7], ["Second", null]]);
});

test("an error before any reply permits whole-turn recovery without copying the error", () => {
  const [footer] = actions([input(), e(2, "error", { text: "Provider failed" })]);
  assert.equal(footer.state, "failed");
  assert.equal(footer.primaryReply, null);
  assert.equal(footer.retryPrompt.text, "Do the work");
  assert.equal(footer.forkThroughSequence, null);
});

test("a successful empty reply still has whole-turn commands using its durable input", () => {
  const [footer] = actions([input(), e(2, "agent_done", { success: true })]);
  assert.equal(footer.primaryReply, null);
  assert.equal(footer.retryPrompt.sourceEventId, 1);
  assert.equal(footer.forkThroughSequence, null);
});

test("compaction text cannot merge with public text or become the primary reply", () => {
  const events = [input(), e(2, "model_text_delta", { text: "Main answer" }),
    e(3, "compaction_start"), e(4, "model_text_delta", { text: "Internal summary" }),
    e(5, "compaction_done"), e(6, "agent_done", { success: true })];
  const items = eventsToTimeline(events), replies = items.filter(item => item.kind === "assistant");
  assert.equal(replies.length, 2);
  assert.notEqual(replies[0].key, replies[1].key);
  assert.equal(replies[0].text, "Main answer");
  assert.equal(replies[1].executionDetail, true);
  assert.equal(actions(events)[0].primaryReply.text, "Main answer");
});

test("child output after the main answer cannot own the toolbar or its copy target", () => {
  const [footer] = actions([input(), e(2, "model_text_delta", { text: "Main answer" }),
    e(3, "agent_start", { run_id: "child", agent_depth: 1 }),
    e(4, "model_text_delta", { run_id: "child", agent_depth: 1, text: "Child output" }),
    e(5, "agent_done", { run_id: "child", agent_depth: 1, success: true }), e(6, "agent_done", { success: true })]);
  assert.equal(footer.primaryReply.text, "Main answer");
});

const reply = text => ({ kind: "assistant", text });
const tool = (path, fields = {}) => ({ kind: "tool", role: "write", inputText: JSON.stringify({ path }), ...fields });
test("an explicit memory-only tail keeps the preceding public reply as the copy target", () => {
  const main = reply("The actual answer"), acknowledgement = reply("Memory saved");
  assert.equal(primaryTurnReply([main, tool("@memory-global/topic.md"), acknowledgement]), main);
  assert.equal(primaryTurnReply([main, tool("@memory-project/topic.md"), reply("Updating index"),
    tool("@memory-project/MEMORY.md"), acknowledgement]), main);
});

test("ordinary text, mixed targets and uncertain tool input are not guessed to be memory maintenance", () => {
  const main = reply("Earlier"), final = reply("This answer explains memory management");
  for (const operation of [tool("src/memory.c"), tool("@memory-global/../src/main.c"),
    tool("@memory-global/topic.md", { inputTruncated: true }),
    tool("@memory-global/topic.md", { role: "exec" }),
    tool("ignored", { inputText: JSON.stringify({ paths: ["@memory-global/a.md", "src/main.c"] }) })])
    assert.equal(primaryTurnReply([main, operation, final]), final);
  assert.equal(primaryTurnReply([main, final]), final);
  assert.equal(primaryTurnReply([main, tool("@memory-global/topic.md", { role: "read" }), final]), final,
    "recalling memory before answering is ordinary task work");
});

test("a fragment without the preceding answer keeps its visible reply; no missing answer is invented", () => {
  const final = reply("Only visible reply");
  assert.equal(primaryTurnReply([tool("@memory-global/a.md"), final]), final);
});

test("an earlier memory tail cannot capture a later continuation's normal reply", () => {
  const old = { ...reply("Earlier answer"), runKey: "main", runEpoch: 1 };
  const memory = { ...tool("@memory-global/topic.md"), runKey: "main", runEpoch: 1 };
  const latest = { ...reply("New continuation answer"), runKey: "resume", runEpoch: 9 };
  assert.equal(primaryTurnReply([old, memory, latest]), latest);
});
