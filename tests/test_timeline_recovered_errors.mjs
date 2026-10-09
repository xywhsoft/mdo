import assert from "node:assert/strict";
import test from "node:test";
import { eventsToTimeline } from "../app/web/js/features/chat/timeline.js";
import { conversationGroups } from "../app/web/js/features/chat/conversation-history.js";

const event = (id, kind, run = 1, fields = {}) => ({ event_id: id, kind,
  run_id: run, schema_version: 6, agent_depth: 0, agent_turn: 1,
  time: id * 1e6, ...fields });
const failed = () => [
  event(1, "agent_start", 1, { user_message_sequence: 4, text: "Read my image" }),
  event(2, "tool_start", 1, { tool_call_id: "read", tool_name: "read", text: "image.txt" }),
  event(3, "error", 1, { text: "Session persistence failed" }),
];
const resume = (start = 4, run = 2) => [
  event(start, "agent_start", run, { user_message_sequence: 0 }),
  event(start + 1, "recovery_resolved", run, { text: "Pending tool call was retried after explicit recovery preflight." }),
  event(start + 2, "model_text_delta", run, { text: "Image read successfully" }),
  event(start + 3, "agent_done", run, { success: true }),
];
const publicErrors = items => conversationGroups(items).flatMap(group => group.entries)
  .filter(item => item.kind === "error");
const details = items => conversationGroups(items).flatMap(group => group.entries)
  .filter(item => item.kind === "process").flatMap(item => item.items);

test("a successful continuation folds resolved errors and recovery records, preserving their diagnostics", () => {
  const events = [...failed(), ...resume()];
  const original = structuredClone(events);
  const items = eventsToTimeline(events);
  assert.equal(publicErrors(items).length, 0);
  assert.equal(details(items).find(item => item.kind === "error").text, "Session persistence failed");
  assert.equal(details(items).filter(item => item.role === "恢复").length, 2);
  const [group] = conversationGroups(items);
  assert.deepEqual(group.entries.map(item => item.kind), ["process", "assistant"]);
  assert.equal(group.state, "done");
  assert.equal(group.entries[0].state, "done");
  assert.equal(group.entries[1].text, "Image read successfully");
  assert.deepEqual(events, original);
});

test("only an admitted continuation folds older failures; active recovery is still running", () => {
  assert.equal(publicErrors(eventsToTimeline(failed())).length, 1);
  const unrelated = eventsToTimeline([...failed(),
    event(4, "recovery_resolved", 2), event(5, "model_done", 2, { success: true })]);
  assert.equal(publicErrors(unrelated).length, 1);
  for (const tail of [resume().slice(0, 1), resume().slice(0, 2),
    [...resume().slice(0, 3), event(7, "model_done", 2, { success: true })]]) {
    const items = eventsToTimeline([...failed(), ...tail]);
    assert.equal(publicErrors(items).length, 0);
    assert.equal(conversationGroups(items)[0].state, "running");
    assert.equal(details(items).some(item => item.kind === "error"), true);
  }
});

test("cancelled continuations remain stopped and failed continuations show only the latest error", () => {
  for (const tail of [event(7, "agent_done", 2, { success: false }),
    event(7, "error", 2, { text: "Daily quota exhausted", model_error_kind: "quota_exceeded" })]) {
    const events = [...failed(), ...resume().slice(0, 3), tail];
    if (tail.kind === "error") events.push(event(8, "agent_done", 2, { success: true }));
    const items = eventsToTimeline(events);
    assert.equal(publicErrors(items).length, tail.kind === "error" ? 1 : 0);
    assert.equal(conversationGroups(items)[0].state, tail.kind === "error" ? "failed" : "cancelled");
    assert.equal(details(items).some(item => item.text === "Session persistence failed"), true);
  }
});

test("only preceding errors are resolved when another attempt later fails under a reused run ID", () => {
  const items = eventsToTimeline([...failed(), ...resume(4, 1),
    event(8, "agent_start", 1, { user_message_sequence: 0 }),
    event(9, "error", 1, { text: "Later endpoint unavailable" })]);
  assert.deepEqual(publicErrors(items).map(item => item.text), ["Later endpoint unavailable"]);
  assert.deepEqual(details(items).filter(item => item.kind === "error").map(item => item.text), ["Session persistence failed"]);
});

test("a new user question, even with identical text or sequence, cannot resolve the preceding error", () => {
  for (const sequence of [4, 5]) {
    const items = eventsToTimeline([...failed(),
      event(4, "agent_start", 2, { user_message_sequence: sequence, text: "Read my image" }),
      ...resume(5, 3)]);
    assert.equal(publicErrors(items).length, 1);
  }
});

test("subagent success does not resolve the main failure and child errors remain execution details", () => {
  const items = eventsToTimeline([...failed(),
    ...resume().map(item => ({ ...item, agent_depth: 1 })),
    event(8, "error", "child", { agent_depth: 1, text: "Child failed" })]);
  assert.deepEqual(publicErrors(items).map(item => item.text), ["Session persistence failed"]);
  assert.equal(details(items).some(item => item.text === "Child failed"), true);
});

test("missing or discarded original history and legacy starts cannot prove recovery of an error", () => {
  const cases = [
    [...failed().slice(1), ...resume()],
    [...failed(), event(4, "history_truncated", 1, { source_event_id: 1 }), ...resume(5)],
    [...failed(), ...resume().map(item => ({ ...item, schema_version: 2 }))],
  ];
  for (const events of cases) assert.equal(publicErrors(eventsToTimeline(events)).length, 1);
});

test("a successful new question alone and a malformed continuation start never conceal a final failure", () => {
  for (const tail of [
    [event(4, "agent_start", 2, { user_message_sequence: 5 }), event(5, "agent_done", 2, { success: true })],
    [event(0, "agent_start", 2, { user_message_sequence: 0 }), event(5, "agent_done", 2, { success: true })],
  ]) assert.equal(publicErrors(eventsToTimeline([...failed(), ...tail])).length, 1);
  for (const sequence of [undefined, null, "", "0", Number.NaN]) {
    const events = [...failed(), ...resume().map(item => item.kind === "agent_start"
      ? { ...item, user_message_sequence: sequence } : item)];
    assert.equal(publicErrors(eventsToTimeline(events)).length, 1);
  }
});

test("retained history boundaries and several failed attempts can be resolved by one exact continuation", () => {
  const items = eventsToTimeline([...failed(),
    event(4, "history_truncated", 1, { source_event_id: 4 }),
    event(5, "agent_start", 2, { user_message_sequence: 0 }),
    event(6, "error", 2, { text: "Temporary provider failure" }), ...resume(7, 3)]);
  assert.equal(publicErrors(items).length, 0);
  assert.equal(details(items).filter(item => item.kind === "error").length, 2);
  assert.equal(items.filter(item => item.kind === "error").length, 2,
    "search/export projection must retain both diagnostics");
});
