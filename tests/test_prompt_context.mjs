import test from "node:test";
import assert from "node:assert/strict";
import { newPromptContext, serializePromptContext, parsePromptContext, promptContextText,
  promptContextSummary, canAddPromptContext, MAX_PROMPT_BYTES, CONTEXT_START } from "../app/web/js/features/chat/prompt-context.js";
import { createPromptHistory, historyValues } from "../app/web/js/features/chat/prompt-history.js";
import { taskTitle } from "../app/web/js/features/chat/new-task-controller.js";

test("portable contexts preserve exact text, Unicode, paths and nested markers", () => {
  const contexts = [newPromptContext("directory", "src/", { path: "src/", project: "demo" }),
    newPromptContext("text", "日志", { content: `中文\n\"quoted\"${CONTEXT_START}not metadata` }),
    newPromptContext("quote", "Selected", { content: "<script>alert(1)</script>", project: "demo", session: "a", event: 19 })];
  const canonical = serializePromptContext("Keep my text\n", contexts);
  assert.deepEqual(parsePromptContext(canonical), { text: "Keep my text\n", contexts });
  assert.match(promptContextText(canonical), /中文/);
  assert.match(promptContextSummary(canonical), /\[src\/\]/);
  assert.equal(taskTitle(serializePromptContext("", contexts)), "[src/] [日志] [Selected]");
});

test("invalid, duplicate or incomplete envelopes remain ordinary editable text", () => {
  for (const raw of ["ordinary JSON {}", `${CONTEXT_START}{bad}\n<!--/mdo-context-->`,
    `${CONTEXT_START}{\"contexts\":[{\"kind\":\"unknown\"}]}\n<!--/mdo-context-->`])
    assert.deepEqual(parsePromptContext(raw), { text: raw, contexts: [] });
  const item = newPromptContext("text", "one", { content: "body" });
  assert.throws(() => serializePromptContext("", [item, item]), /Invalid/);
  assert.deepEqual(parsePromptContext(serializePromptContext("original", [item]).slice(0, -2)).contexts, []);
});

test("limits use actual serialized UTF-8 bytes and never silently cut attached content", () => {
  const item = newPromptContext("text", "file", { content: "长".repeat(2000) });
  assert.equal(canAddPromptContext("", [], item), true);
  assert.equal(canAddPromptContext("x".repeat(MAX_PROMPT_BYTES), [], item), false);
  assert.equal(canAddPromptContext("", Array.from({ length: 16 }, () => newPromptContext("text", "one", { content: "a" })), item), false);
  assert.equal(canAddPromptContext("", [], { ...item, label: "x".repeat(513) }), false);
});

test("input history deduplicates only consecutive equal inputs and rejects malformed records", () => {
  const rows = ["a", "a", "b", "a"].map((text, index) => ({ id: String(index).repeat(32), text }));
  assert.deepEqual(historyValues(rows), ["a", "b", "a"]);
  assert.throws(() => historyValues([{ id: "broken", text: "x" }]), /Invalid/);
  assert.throws(() => historyValues([rows[0], rows[0]]), /Invalid/);
});

function historyHarness(options = {}) {
  const input = new EventTarget(), listeners = [];
  input.value = ""; input.getAttribute = () => "false"; input.setSelectionRange = () => {};
  let route = { view: "workspace", projectId: "demo", sessionId: "one" };
  const history = createPromptHistory({ input, navigation: { get: () => route,
    subscribe(fn) { listeners.push(fn); fn(); } }, restore: text => { input.value = text; }, ...options });
  return { input, history, navigate(projectId) { route = { ...route, projectId }; listeners.forEach(fn => fn()); },
    key(key, fields = {}) { const event = new Event("keydown", { cancelable: true });
      Object.assign(event, { key, ...fields }); history.onKeyDown(event); return event; } };
}

test("history arrows restore the empty editor, respect IME and keep project isolation", async () => {
  const rows = ["first", "second"].map((text, index) => ({ id: String(index).repeat(32), text }));
  const context = historyHarness({ read: async () => ({ data: { items: rows } }) });
  await context.history.load();
  assert.equal(context.key("ArrowUp", { isComposing: true }).defaultPrevented, false);
  context.key("ArrowUp"); assert.equal(context.input.value, "second");
  context.key("ArrowUp"); assert.equal(context.input.value, "first");
  context.key("ArrowDown"); assert.equal(context.input.value, "second");
  context.key("ArrowDown"); assert.equal(context.input.value, "");
  context.navigate("other");
  assert.equal(context.key("ArrowUp").defaultPrevented, false);
  assert.equal(context.input.value, "");
});

test("accepted inputs record serially, and a lost history ACK causes only a confirming read", async () => {
  let release, reads = 0; const posts = [];
  const first = new Promise(resolve => { release = resolve; });
  const context = historyHarness({ read: async () => { ++reads; return { data: { items: [] } }; },
    post: async (_path, body) => { posts.push(body.text); if (posts.length === 1) await first;
      else throw new Error("lost ACK"); return { data: { items: [body] } }; } });
  const a = context.history.record("demo", { id: "a".repeat(32), text: "first" });
  const b = context.history.record("demo", { id: "b".repeat(32), text: "second" });
  await Promise.resolve(); assert.deepEqual(posts, ["first"]);
  release(); await Promise.all([a, b]);
  assert.deepEqual(posts, ["first", "second"]); assert.equal(reads, 1);
});
