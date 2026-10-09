import assert from "node:assert/strict";
import test, { after, beforeEach } from "node:test";
import { conversationGroups, summarizeConversationEvents } from "../app/web/js/features/chat/conversation-history.js";
import { eventsToTimeline } from "../app/web/js/features/chat/timeline.js";

const previous = { window: globalThis.window, document: globalThis.document, fetch: globalThis.fetch };
let serial = 0;
const timers = new Map();
globalThis.window = Object.assign(new EventTarget(), {
  setTimeout(fn) { timers.set(++serial, fn); return serial; },
  clearTimeout(id) { timers.delete(id); },
});
const flush = () => new Promise(resolve => setImmediate(resolve));
async function settleReads(operation) {
  let finished = false;
  const result = Promise.resolve(operation).finally(() => { finished = true; });
  await flush();
  for (let i = 0; i < 16 && !finished; ++i) {
    const [id, callback] = [...timers][0] ?? [];
    assert(callback, "missing history recovery timer");
    timers.delete(id); callback(); await flush();
  }
  assert(finished, "history read failed to settle"); return result;
}
globalThis.document = Object.assign(new EventTarget(), { hidden: false });
const { timelineStore, selectTimeline, loadOlderTimeline, revealConversationTurn,
  loadOlderConversationIndex, applyLiveTimeline, clearTimeline, clearTimelineCache } = await import("../app/web/js/features/chat/timeline-store.js");
const reply = data => Response.json({ ok: true, data });
const e = (id, kind, fields = {}) => ({ event_id: id, kind, run_id: 1,
  schema_version: 5, agent_depth: 0, time: id * 1000000, ...fields });
const records = Array.from({ length: 12 }, (_, i) => [
  e(i * 4 + 1, "agent_start", { run_id: i + 1, user_message_sequence: i + 1, text: `Question ${i}` }),
  e(i * 4 + 2, "model_reasoning_delta", { run_id: i + 1, text: `Thought ${i}` }),
  e(i * 4 + 3, "model_text_delta", { run_id: i + 1, text: `Answer ${i}` }),
  e(i * 4 + 4, "agent_done", { run_id: i + 1, success: true }),
]).flat();
const summaries = summarizeConversationEvents(records);
const calls = [];
function respond(url) {
  if (url.includes("/conversation?")) return Response.json({ ok: false, error: { code: "route_not_found" } }, { status: 404 });
  const parsed = new URL(url, "http://fixture");
  const before = Number(parsed.searchParams.get("before"));
  const limit = Number(parsed.searchParams.get("limit"));
  if (parsed.pathname.endsWith("/turns")) {
    const all = summaries.filter(turn => !before || turn.first_event_id < before);
    const items = all.slice(-limit);
    return reply({ items, has_more: all.length > items.length,
      next_before: items[0]?.first_event_id ?? before, latest_event_id: 48 });
  }
  const after = Number(parsed.searchParams.get("after"));
  const items = records.filter(event => event.event_id > after).slice(0, limit);
  return reply({ items, next_cursor: items.at(-1)?.event_id ?? after, latest_event_id: 48 });
}
beforeEach(() => {
  clearTimeline(); clearTimelineCache(); timers.clear(); calls.length = 0;
  globalThis.fetch = async url => { calls.push(url); return respond(url); };
});
after(() => { clearTimeline(); Object.assign(globalThis, previous); });

test("opening a session loads only the last four turns, preserving the full navigation index", async () => {
  await selectTimeline("qa", "a");
  const data = timelineStore.get().data;
  assert.equal(data.cursor, 48);
  assert.equal(data.firstLoadedTurn, 33);
  assert.equal(data.events.length, 16);
  assert.equal(data.turns.length, 12);
  assert.equal(data.hasOlder, true);
  assert.ok(calls.filter(url => url.includes("/events")).length <= 2);
  assert.ok(!calls.some(url => url.includes("after=0")));
  assert.ok(calls.at(-1).includes("after=48") || calls.some(url => url.includes("after=32")));
});

test("prepending history cannot rewind the live cursor or replace concurrently arriving messages", async () => {
  await selectTimeline("qa", "a");
  let resolve;
  globalThis.fetch = url => url.includes("/events") ? new Promise(done => { resolve = () => done(respond(url)); })
    : Promise.resolve(respond(url));
  const older = loadOlderTimeline();
  await new Promise(done => setImmediate(done));
  applyLiveTimeline({ project_id: "qa", session_id: "a", next_cursor: 50, latest_event_id: 50,
    items: [e(49, "agent_start", { run_id: 20, user_message_sequence: 20, text: "New question" }),
      e(50, "model_text_delta", { run_id: 20, text: "New answer" })] });
  resolve(); await older;
  const data = timelineStore.get().data;
  assert.equal(data.cursor, 50);
  assert.equal(data.firstLoadedTurn, 17);
  assert.equal(data.events.at(-1).text, "New answer");
  assert.equal(data.events[0].event_id, 17);
});

test("jumping loads just the selected range and leaves live subscription history intact", async () => {
  await selectTimeline("qa", "a"); calls.length = 0;
  await revealConversationTurn(1);
  const data = timelineStore.get().data;
  assert.equal(data.cursor, 48);
  assert.equal(data.events.length, 20);
  assert.equal(data.events[0].text, "Question 0");
  assert.equal(data.events[4].event_id, 33);
  assert.equal(calls.length, 1);
  assert.ok(calls[0].includes("after=0&limit=32"));
});

test("history failure preserves the conversation and permits an explicit retry", async () => {
  await selectTimeline("qa", "a");
  globalThis.fetch = async () => { throw new Error("offline"); };
  await settleReads(loadOlderTimeline());
  assert.equal(timelineStore.get().data.loadingHistory, false);
  assert.equal(timelineStore.get().data.events.length, 16);
  assert.ok(timelineStore.get().data.historyError);
  globalThis.fetch = async url => respond(url);
  await loadOlderTimeline();
  assert.equal(timelineStore.get().data.events.length, 32);
});

test("earlier navigation summaries load independently without downloading their conversation text", async () => {
  await selectTimeline("qa", "a");
  const current = timelineStore.get().data;
  timelineStore.setData({ ...current, turns: summaries.slice(4), indexHasMore: true, indexBefore: 17 });
  calls.length = 0;
  await loadOlderConversationIndex();
  assert.equal(timelineStore.get().data.turns.length, 12);
  assert.equal(timelineStore.get().data.events.length, 16);
  assert.equal(timelineStore.get().data.cursor, 48);
  assert.equal(calls.length, 1);
  assert.ok(calls[0].endsWith("/turns?before=17&limit=64"));
});

test("switching sessions rejects a late historical response even when transport ignores abort", async () => {
  await selectTimeline("qa", "a");
  let finish;
  globalThis.fetch = url => new Promise(resolve => { finish = () => resolve(respond(url)); });
  const older = loadOlderTimeline();
  await flush();
  clearTimeline(); finish(); await older;
  assert.equal(timelineStore.get().data.sessionId, "");
  assert.deepEqual(timelineStore.get().data.events, []);
});

test("a clear during historical loading removes obsolete records and summaries", async () => {
  await selectTimeline("qa", "a");
  let finish;
  globalThis.fetch = url => url.includes("/events") ? new Promise(resolve => { finish = () => resolve(respond(url)); })
    : Promise.resolve(respond(url));
  const older = loadOlderTimeline(); await new Promise(done => setImmediate(done));
  applyLiveTimeline({ project_id: "qa", session_id: "a", next_cursor: 49, latest_event_id: 49,
    items: [e(49, "history_truncated", { source_event_id: 1 })] });
  finish(); await older;
  assert.deepEqual(timelineStore.get().data.events.map(item => item.event_id), [49]);
  assert.deepEqual(timelineStore.get().data.turns, []);
  assert.equal(timelineStore.get().data.hasOlder, false);
});

test("all main replies stay visible between chronologically folded execution blocks", () => {
  const events = [e(1, "agent_start", { user_message_sequence: 1, text: "Question" }),
    e(2, "model_reasoning_delta", { agent_turn: 1, text: "Thought" }),
    e(3, "model_text_delta", { agent_turn: 1, text: "Let me inspect" }),
    e(4, "model_done", { agent_turn: 1, success: true }),
    e(5, "tool_start", { tool_call_id: "read", tool_name: "read", text: "file.txt" }),
    e(6, "tool_done", { tool_call_id: "read", success: true, text: "Data" }),
    e(7, "model_text_delta", { agent_turn: 2, text: "Final answer" }),
    e(8, "agent_done", { success: true })];
  const [group] = conversationGroups(eventsToTimeline(events));
  assert.equal(group.state, "done");
  assert.deepEqual(group.entries.map(item => item.kind), ["process", "assistant", "process", "assistant"]);
  assert.equal(group.entries[0].items[0].text, "Thought");
  assert.equal(group.entries[1].text, "Let me inspect");
  assert.equal(group.entries[2].items[0].outputText, "Data");
  assert.equal(group.entries[3].text, "Final answer");
  assert.ok(group.entries.filter(item => item.kind === "process").every(item => item.state === "done"));
});

test("a report cannot be hidden by later file verification or memory housekeeping", () => {
  const events = [e(1, "agent_start", { user_message_sequence: 1 }),
    e(2, "model_text_delta", { agent_turn: 1, text: "Complete research report" }),
    e(3, "model_done", { agent_turn: 1, success: true }),
    e(4, "model_reasoning_delta", { agent_turn: 2, text: "Verify the saved file" }),
    e(5, "tool_start", { agent_turn: 2, tool_call_id: "check", tool_name: "exec" }),
    e(6, "tool_done", { agent_turn: 2, tool_call_id: "check", success: true }),
    e(7, "model_text_delta", { agent_turn: 3, text: "Files verified; memory updated" }),
    e(8, "agent_done", { success: true })];
  const [group] = conversationGroups(eventsToTimeline(events));
  assert.deepEqual(group.entries.map(item => item.kind), ["assistant", "process", "assistant"]);
  assert.equal(group.entries[0].text, "Complete research report");
  assert.deepEqual(group.entries[1].items.map(item => item.kind), ["reasoning", "tool"]);
  assert.equal(group.entries[2].text, "Files verified; memory updated");
});

test("main errors remain visible and subagent text stays in its execution block", () => {
  const events = [e(1, "agent_start", { user_message_sequence: 1 }),
    e(2, "model_text_delta", { text: "Useful partial answer" }),
    e(3, "model_text_delta", { run_id: 2, agent_depth: 1, text: "Child working notes" }),
    e(4, "error", { text: "Main operation failed" })];
  const [group] = conversationGroups(eventsToTimeline(events));
  assert.deepEqual(group.entries.map(item => item.kind), ["assistant", "process", "error"]);
  assert.equal(group.entries[0].text, "Useful partial answer");
  assert.equal(group.entries[1].items[0].text, "Child working notes");
  assert.equal(group.state, "failed");
});

test("model completion alone cannot collapse a turn still awaiting a tool or further model call", () => {
  const events = [e(1, "agent_start", { user_message_sequence: 1 }),
    e(2, "model_text_delta", { text: "Planning" }), e(3, "model_done", { success: true }),
    e(4, "tool_start", { tool_call_id: "work" })];
  assert.equal(conversationGroups(eventsToTimeline(events))[0].state, "running");
});

test("resuming without a new user input summarizes only the last run's answer even if model turn IDs repeat", () => {
  const events = [e(1, "agent_start", { user_message_sequence: 1, text: "Original question" }),
    e(2, "model_text_delta", { agent_turn: 1, text: "Earlier partial answer" }),
    e(3, "agent_done", { success: false }),
    e(4, "agent_start", { run_id: 2, user_message_sequence: 0 }),
    e(5, "model_text_delta", { run_id: 2, agent_turn: 1, text: "Continuation" }),
    e(6, "agent_done", { run_id: 2, success: true })];
  assert.equal(summarizeConversationEvents(events).length, 1);
  assert.equal(summarizeConversationEvents(events)[0].answer, "Continuation");
  const replies = conversationGroups(eventsToTimeline(events))[0].entries
    .filter(item => item.kind === "assistant");
  assert.deepEqual(replies.map(item => item.text), ["Earlier partial answer", "Continuation"]);
});

test("a cold history fragment and its completed turn share one reply/process projection", () => {
  const events = [e(1, "agent_start", { user_message_sequence: 1, text: "Research" }),
    e(2, "model_text_delta", { agent_turn: 1, text: "Core report" }),
    e(3, "model_done", { agent_turn: 1, success: true }),
    e(4, "tool_start", { agent_turn: 2, tool_call_id: "check", tool_name: "exec" }),
    e(5, "tool_done", { agent_turn: 2, tool_call_id: "check", success: true }),
    e(6, "model_reasoning_delta", { agent_turn: 2, text: "Verification notes" }),
    e(7, "model_done", { agent_turn: 2, success: true }),
    e(8, "model_text_delta", { agent_turn: 3, text: "Verification complete" }),
    e(9, "agent_done", { success: true })];
  const [full] = conversationGroups(eventsToTimeline(events));
  const shape = entries => entries.map(item => item.kind === "process"
    ? { kind: item.kind, state: item.state, children: item.items.map(child => child.kind) }
    : { kind: item.kind, text: item.text });
  for (const offset of [1, 3]) {
    const [fragment] = conversationGroups(eventsToTimeline(events.slice(offset)));
    assert.equal(fragment.user, null);
    assert.equal(fragment.state, "done");
    assert.equal("standalone" in fragment, false);
    assert.deepEqual(shape(fragment.entries), shape(full.entries.slice(offset === 1 ? 0 : 1)));
    assert.equal(fragment.entries.find(item => item.kind === "process").key,
      full.entries.find(item => item.kind === "process").key);
  }
});

test("a live fragment folds execution without inventing a user or hiding public output", () => {
  const [fragment] = conversationGroups(eventsToTimeline([
    e(100, "model_text_delta", { text: "Visible progress" }),
    e(101, "model_done", { success: true }),
    e(102, "tool_start", { tool_call_id: "read", tool_name: "read" }),
    e(103, "model_reasoning_delta", { agent_depth: 1, run_id: 2, text: "Child notes" }),
  ]));
  assert.equal(fragment.user, null);
  assert.equal(fragment.state, "running");
  assert.deepEqual(fragment.entries.map(item => item.kind), ["assistant", "process"]);
  assert.equal(fragment.entries[0].text, "Visible progress");
  assert.equal(fragment.entries[1].state, "running");
  assert.deepEqual(fragment.entries[1].items.map(item => item.kind), ["tool", "reasoning"]);
});
