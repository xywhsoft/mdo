import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { eventsToTimeline } from "../app/web/js/features/chat/timeline.js";
import { conversationGroups } from "../app/web/js/features/chat/conversation-history.js";
import { loadLocale, supportedLocales } from "../app/web/js/i18n.js";

const recovered = "Pending tool call was retried after explicit recovery preflight.";
const uncertain = "Pending tool call was not retried and was recorded as uncertain.";
const event = (kind, id, run, fields = {}) => ({ kind, event_id: id, run_id: run,
  schema_version: 6, agent_depth: 0, agent_turn: 2, time: id * 1e6, ...fields });
const call = { tool_name: "web_search", tool_call_id: "search-1", text: '{"query":"restart"}' };
const pending = () => [
  event("agent_start", 1, 6, { user_message_sequence: 2, text: "Original input" }),
  event("tool_start", 2, 6, call),
  event("agent_done", 3, 6, { success: false }),
];
const resume = () => [
  event("agent_start", 4, 1, { user_message_sequence: 0 }),
  event("tool_start", 5, 1, call),
  event("tool_done", 6, 1, { ...call, success: true, text: "Saved result", artifact_id: 9 }),
  event("recovery_resolved", 7, 1, { text: recovered, success: true }),
  event("model_text_delta", 8, 1, { agent_turn: 3, text: "Completed answer" }),
  event("agent_done", 9, 1, { success: true }),
];
const tools = events => eventsToTimeline(events).filter(item => item.kind === "tool");

test("explicit recovery completes the original unfinished tool card under a new process run ID", () => {
  const events = [...pending(), ...resume()];
  const frozen = structuredClone(events);
  const items = eventsToTimeline(events);
  const [tool] = items.filter(item => item.kind === "tool");
  assert.equal(tools(events).length, 1);
  assert.equal(tool.key, "tool-2");
  assert.equal(tool.sourceEventId, 2);
  assert.equal(tool.inputEventId, 5);
  assert.equal(tool.outputEventId, 6);
  assert.equal(tool.artifactId, 9);
  assert.equal(tool.state, "done");
  assert.equal(tool.runKey, "1");
  assert.equal(tool.runEpoch, 4);
  assert.equal(tool.time, 5e6);
  assert.equal(tool.durationSeconds, 1, "offline time is not charged to the resumed tool stage");
  assert.equal(items.filter(item => item.kind === "assistant").length, 1);
  assert.equal(items.at(-1).retryPrompt.sequence, 2);
  assert.equal(items.at(-1).toolStageSeconds, 1);
  assert.equal(items.filter(item => item.kind === "user").length, 1);
  assert.equal(conversationGroups(items)[0].state, "done");
  assert.deepEqual(events, frozen);
});

test("a pending card stays running across repeated explicit resumes and reused run IDs", () => {
  const events = [...pending(), ...resume().slice(0, 2)];
  const active = eventsToTimeline(events);
  assert.equal(active.filter(item => item.kind === "tool").length, 1);
  assert.equal(active.find(item => item.kind === "tool").state, "running");
  assert.equal(active.filter(item => item.kind === "assistant").length, 0);
  assert.equal(conversationGroups(active)[0].entries.filter(item => item.kind === "process").length, 1);
  events.push(event("agent_start", 6, 1, { user_message_sequence: 0 }),
    event("tool_start", 7, 1, call),
    event("tool_done", 8, 1, { ...call, success: false, text: "Search quota exhausted" }),
    event("agent_done", 9, 1, { success: true }));
  assert.equal(tools(events).length, 1);
  assert.equal(tools(events)[0].state, "failed");
  assert.equal(tools(events)[0].outputText, "Search quota exhausted");
});

test("recovery retains partial stopped replies and does not invent a terminal event for an abrupt exit", () => {
  const events = pending();
  events.splice(2, 0, event("model_text_delta", 2.5, 6, { text: "Keep this partial reply" }));
  assert.deepEqual(eventsToTimeline([...events, ...resume()]).filter(item => item.kind === "assistant")
    .map(item => [item.text, item.state]), [["Keep this partial reply", "cancelled"], ["Completed answer", "done"]]);
  assert.equal(tools([...pending().slice(0, 2), ...resume()]).length, 1);
});

test("completed, failed and normally settled calls remain distinct history", () => {
  for (const ending of [
    event("tool_done", 3, 6, { ...call, success: true, text: "First result" }),
    event("tool_done", 3, 6, { ...call, success: false, text: "First failure" }),
    event("error", 3, 6, { text: "Fatal error" }),
    event("agent_done", 3, 6, { success: true }),
  ]) {
    const events = [...pending().slice(0, 2), ending, ...resume()];
    assert.equal(tools(events).length, 2, ending.kind + String(ending.success));
  }
});

test("matching requires durable original input, round, tool identity and complete identical arguments", () => {
  for (const fields of [
    { agent_turn: 3 }, { agent_turn: 0 }, { agent_turn: undefined },
    { tool_name: "web_open" }, { tool_call_id: "search-2" },
    { text: '{"query":"different"}' }, { text_truncated: true },
    { schema_version: 2 }, { agent_depth: 1 }, { agent_depth: undefined },
  ]) {
    const next = resume().slice(0, 2);
    Object.assign(next[1], fields);
    assert.equal(tools([...pending(), ...next]).length, 2, JSON.stringify(fields));
  }
  for (const fields of [{ text_truncated: true }, { schema_version: 2 }, { agent_depth: 1 }]) {
    const prior = pending();
    Object.assign(prior[1], fields);
    assert.equal(tools([...prior, ...resume().slice(0, 2)]).length, 2, JSON.stringify(fields));
  }
  for (const fields of [{ user_message_sequence: 3 }, { schema_version: 2 }, { agent_depth: 1 }]) {
    const next = resume().slice(0, 2);
    Object.assign(next[0], fields);
    assert.equal(tools([...pending(), ...next]).length, 2, JSON.stringify(fields));
  }
  assert.equal(tools([...pending().slice(1), ...resume()]).length, 2, "missing original input");
  const repeated = pending();
  repeated[2] = event("tool_start", 3, 6, call);
  assert.equal(tools(repeated).length, 2, "two starts in one epoch are not a recovery");
});

test("history boundaries and a new user turn prevent matching an older pending call", () => {
  for (const between of [
    event("history_truncated", 3.5, 6, { source_event_id: 1, text: "会话历史已清空" }),
    event("agent_start", 3.5, 7, { user_message_sequence: 2, text: "Same sequence, new input" }),
  ]) assert.equal(tools([...pending(), between, ...resume()]).length, 2);
});

test("canonical recovery explanations follow all UI languages while custom notes remain verbatim", async context => {
  const packs = Object.fromEntries(supportedLocales.map(locale => [locale,
    JSON.parse(readFileSync(new URL(`../app/web/lang/${locale}.json`, import.meta.url), "utf8"))]));
  context.mock.method(globalThis, "fetch", async path => ({ ok: true,
    async json() { return packs[/^\/lang\/(.+)\.json$/.exec(path)[1]]; } }));
  const custom = " " + recovered;
  const events = [recovered, uncertain, custom].map((text, i) => event("recovery_resolved", i + 1, 1, { text }));
  const frozen = structuredClone(events);
  for (const locale of supportedLocales) {
    await loadLocale(locale);
    assert.deepEqual(eventsToTimeline(events).map(item => item.text), [
      packs[locale]["timeline.toolRecoveryCompleted"], packs[locale]["timeline.toolRecoveryUncertain"], custom]);
    assert.deepEqual(events, frozen);
  }
  await loadLocale("en-US");
});
