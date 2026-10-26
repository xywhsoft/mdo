import assert from "node:assert/strict";
import test from "node:test";

import { createTokenMeter, contextCheckpoint, executionCheckpoint, estimateInputTokens } from "../app/web/js/features/chat/token-meter.js";
import { createKeyboardShortcuts } from
  "../app/web/js/features/shell/keyboard-shortcuts.js";

class Node extends EventTarget {
  constructor(owner) {
    super();
    this.owner = owner;
    this.children = [];
    this.attributes = new Map();
    this.style = { values: new Map(), setProperty(name, value) {
      this.values.set(name, value);
    } };
    this.value = "";
    this.hidden = false;
    this.scrollTop = 0;
  }

  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = [...children]; this.scrollTop = 0; }
  contains(target) {
    return this === target || this.children.some((child) => child.contains?.(target));
  }
  setAttribute(name, value) { this.attributes.set(name, String(value)); }
  getAttribute(name) { return this.attributes.get(name) ?? null; }
  focus() { this.owner.activeElement = this; }
  querySelector() { return null; }
}

function resource(data) {
  return { get: () => ({ data }), subscribe(listener) {
    listener({ data });
    return () => {};
  } };
}

test("context meters select the exact member scope and use its committed delegated checkpoint", () => {
  const target = { namespace: "team.g1", id: "writer" };
  const root = { kind: "model_done", input_tokens: 100, output_tokens: 10 };
  const own = { kind: "model_done", input_tokens: 200, output_tokens: 20, source: { namespace: target.namespace, member: target.id } };
  const other = { kind: "model_done", input_tokens: 90000, output_tokens: 900, source: { namespace: "team.g2", member: target.id } };
  const events = [root, own, other];
  assert.equal(contextCheckpoint(events).latestCall, root);
  assert.equal(contextCheckpoint(events, target).latestCall, own);
  const checkpoint = { model_id: "small", usage_available: true, input_tokens: 300, output_tokens: 30,
    context: { exact: true, estimated_input_tokens: 330, input_budget_tokens: 1000, compact_threshold_tokens: 900 } };
  assert.equal(executionCheckpoint(events, { target, checkpoint }).latestCall, checkpoint);
  assert.equal(executionCheckpoint(events, { target, checkpoint }, true).latestCall, own);
  assert.equal(executionCheckpoint(events, { target }).latestCall, own);
  assert.deepEqual(executionCheckpoint(events, { available: false }), {});
});

test("Token meter Escape closes the panel before the global stop shortcut", () => {
  const previousDocument = globalThis.document;
  const document = new EventTarget();
  document.createElement = () => new Node(document);
  document.querySelector = () => null;
  document.body = new Node(document);
  document.activeElement = document.body;
  globalThis.document = document;
  const root = new Node(document);
  const trigger = new Node(document);
  const panel = new Node(document);
  const prompt = new Node(document);
  const estimate = new Node(document);
  const ring = new Node(document);
  const dialog = new Node(document);
  root.append(trigger, panel);
  let stops = 0;
  let meter;
  try {
    meter = createTokenMeter({ root, trigger, panel, prompt, estimate, ring,
      modelSelect: { value: "ornith" },
      sessionStore: resource(null), timelineStore: resource({ events: [] }),
      modelsStore: resource({ models: [{ id: "ornith", name: "Ling",
        context_window_tokens: 1024 }] }) });
    createKeyboardShortcuts({ dialog,
      navigation: { get: () => ({ view: "workspace", sessionId: "s" }) },
      search: { isOpen: () => false },
      onNew() {}, onExport() {}, onSettings() {}, onToggleTheme() {},
      onStop: () => { stops += 1; }, isRunning: () => true,
      isDrawerOpen: () => false, closeDrawers() {} });

    trigger.dispatchEvent(new Event("click"));
    assert.equal(panel.hidden, false);
    assert.equal(document.activeElement, panel);
    let close = panel.children[0].children[1];
    assert.equal(close.getAttribute("aria-label"), "关闭 token 用量");
    panel.scrollTop = 27;
    close.focus();
    meter.refresh();
    close = panel.children[0].children[1];
    assert.equal(panel.scrollTop, 27, "usage updates preserve reading position");
    assert.equal(document.activeElement, close, "usage updates preserve close focus");
    close.dispatchEvent(new Event("click"));
    assert.equal(panel.hidden, true);
    assert.equal(document.activeElement, trigger);
    trigger.dispatchEvent(new Event("click"));
    assert.equal(panel.scrollTop, 0, "reopening starts at the top");
    assert.equal(document.activeElement, panel);

    function escape(keyCode = 27) {
      const event = new Event("keydown", { cancelable: true });
      Object.defineProperties(event, { key: { value: "Escape" },
        keyCode: { value: keyCode } });
      document.dispatchEvent(event);
      return event;
    }
    assert.equal(escape(229).defaultPrevented, false);
    assert.equal(panel.hidden, false, "an IME candidate key must not close the panel");
    assert.equal(stops, 0);

    assert.equal(escape().defaultPrevented, true);
    assert.equal(panel.hidden, true);
    assert.equal(document.activeElement, trigger);
    assert.equal(stops, 0, "closing the panel must not stop the run");

    escape();
    assert.equal(stops, 1, "a subsequent bare Escape still stops the run");
  } finally {
    meter?.destroy();
    globalThis.document = previousDocument;
  }
});

test("last-call usage uses its own model after the next model is selected", () => {
  const previousDocument = globalThis.document;
  const document = new EventTarget();
  document.createElement = () => new Node(document);
  document.querySelector = () => null;
  document.body = new Node(document);
  document.activeElement = document.body;
  globalThis.document = document;
  const root = new Node(document);
  const trigger = new Node(document);
  const panel = new Node(document);
  const prompt = new Node(document);
  const estimate = new Node(document);
  const ring = new Node(document);
  root.append(trigger, panel);
  let meter;
  let runItems = [{ project_id: "qa", session_id: "s", agent_run_id: 73,
    model_id: "previous" }];
  let event = { kind: "model_done", run_id: 73, agent_depth: 0,
    model: "wire-shared", input_tokens: 500, output_tokens: 25 };
  try {
    meter = createTokenMeter({ root, trigger, panel, prompt, estimate, ring,
      modelSelect: { value: "next" },
      sessionStore: resource({ project_id: "qa", id: "s" }),
      timelineStore: { get: () => ({ data: { projectId: "qa", sessionId: "s",
        events: [event] } }), subscribe: () => () => {} },
      modelsStore: resource({ models: [
        { id: "previous", name: "Previous", wire_model: "wire-shared",
          context_window_tokens: 1000 },
        { id: "next", name: "Next", wire_model: "wire-shared",
          context_window_tokens: 10000 },
      ] }), runsStore: { get: () => ({ data: { items: runItems } }),
        subscribe: () => () => {} } });
    assert.equal(ring.style.values.get("--meter-percent"), "53%",
      "last input plus output must use the previous model's context window");
    assert.match(trigger.title, /Previous/);
    assert.match(trigger.title, /525/);
    assert.match(trigger.title, /1,000/);
    const details = panel.children[1];
    const fields = details.children.map((child) => child.textContent);
    assert.ok(fields.includes("Next"), "the selected model remains visible");
    assert.ok(fields.includes("Previous"), "the last-call model is explicit");
    assert.ok(fields.includes("10,000"));
    assert.ok(fields.includes("1,000"));
    runItems = [];
    meter.refresh();
    assert.equal(ring.style.values.get("--meter-percent"), "0%",
      "shared wire names cannot identify the previous model without a run record");
    assert.doesNotMatch(trigger.title, /10,000/);
    assert.match(panel.children.at(-1).textContent, /unavailable|无法确定/);
    runItems = [{ project_id: "qa", session_id: "s", agent_run_id: 73,
      model_id: "previous" }];
    event = { ...event, agent_depth: 1 };
    meter.refresh();
    assert.equal(ring.style.values.get("--meter-percent"), "0%",
      "a subagent call cannot inherit the main run's model profile");
    runItems = [];
    event = { ...event, agent_depth: 0, model_id: "previous",
      context_window_tokens: 1000 };
    meter.refresh();
    assert.equal(ring.style.values.get("--meter-percent"), "53%",
      "persisted event profile survives loss of in-memory run records");
    assert.match(trigger.title, /Previous/);
  } finally {
    meter?.destroy();
    globalThis.document = previousDocument;
  }
});

test("context checkpoints exclude subagents and reset at history boundaries", () => {
  const parent = { event_id: 1, kind: "model_done", agent_depth: 0,
    input_tokens: 500, output_tokens: 25, success: true };
  const child = { event_id: 2, kind: "model_done", agent_depth: 1,
    input_tokens: 9000, output_tokens: 900, success: true };
  assert.equal(contextCheckpoint([parent, child]).latestCall, parent);
  const compacted = { event_id: 3, kind: "compaction_done", agent_depth: 0,
    context: { exact: false, estimated_input_tokens: 64, input_budget_tokens: 850,
      compact_threshold_tokens: 800, fill_tokens: 0, growth_tokens: 0 } };
  assert.equal(contextCheckpoint([parent, child, compacted]).contextEvent, compacted);
  const reset = contextCheckpoint([parent, compacted, { event_id: 4, kind: "history_truncated" }]);
  assert.equal(reset.contextEvent, undefined);
  assert.equal(reset.latestCall, undefined);
  assert.equal(contextCheckpoint([parent, { ...compacted, agent_depth: 1 }]).contextEvent, undefined);
});

test("draft estimates match native ASCII rounding and Unicode scalar counting", () => {
  assert.equal(estimateInputTokens(""), 0);
  assert.equal(estimateInputTokens("a"), 1);
  assert.equal(estimateInputTokens("aaaaa"), 2);
  assert.equal(estimateInputTokens("abc世界🙂é"), 5);
});

test("the ring uses native context growth, draft and compaction state together", () => {
  const previousDocument = globalThis.document;
  const document = new EventTarget();
  document.createElement = () => new Node(document);
  document.querySelector = () => null;
  document.body = new Node(document);
  document.activeElement = document.body;
  globalThis.document = document;
  const [root, trigger, panel, prompt, estimate, ring] = Array.from({ length: 6 }, () => new Node(document));
  let events = [{ kind: "model_done", event_id: 1, agent_depth: 0, success: true,
    model_id: "main", context_window_tokens: 1000, input_tokens: 500, output_tokens: 25,
    context: { exact: true, fill_tokens: 525, growth_tokens: 0, estimated_input_tokens: 525,
      input_budget_tokens: 900, compact_threshold_tokens: 850 } }];
  let meter;
  try {
    meter = createTokenMeter({ root, trigger, panel, prompt, estimate, ring,
      modelSelect: { value: "main" }, sessionStore: resource({ project_id: "p", id: "s" }),
      timelineStore: { get: () => ({ data: { projectId: "p", sessionId: "s", events } }),
        subscribe: () => () => {} },
      modelsStore: resource({ models: [{ id: "main", name: "Main", context_window_tokens: 1000 }] }) });
    assert.equal(ring.style.values.get("--meter-percent"), "53%");
    prompt.value = "x".repeat(100);
    meter.refresh();
    assert.equal(ring.style.values.get("--meter-percent"), "56%", "draft includes the native message overhead");
    events.push({ ...events[0], event_id: 2, kind: "model_start", context: { ...events[0].context,
      growth_tokens: 125, estimated_input_tokens: 650 } });
    meter.refresh();
    assert.equal(ring.style.values.get("--meter-percent"), "69%");
    assert.match(trigger.title, /687/);
    const fields = panel.children[1].children.map(node => node.textContent);
    assert.ok(fields.includes("850"), "the native compaction threshold is visible");
    events.push({ ...events[0], event_id: 3, kind: "compaction_done", context: {
      ...events[0].context, exact: false, fill_tokens: 0, estimated_input_tokens: 64 } });
    meter.refresh();
    assert.equal(ring.style.values.get("--meter-percent"), "10%", "compaction replaces the old usage baseline");
    prompt.value = "";
    events = JSON.parse(JSON.stringify(events));
    meter.refresh();
    assert.equal(ring.style.values.get("--meter-percent"), "6%", "restored event checkpoints retain the compacted context");
  } finally {
    meter?.destroy();
    globalThis.document = previousDocument;
  }
});

test("composer estimate names uncounted image tokens and restores text-only state", () => {
  const previousDocument = globalThis.document;
  const document = new EventTarget();
  document.createElement = () => new Node(document);
  document.querySelector = () => null;
  document.body = new Node(document);
  document.activeElement = document.body;
  globalThis.document = document;
  const root = new Node(document);
  const trigger = new Node(document);
  const panel = new Node(document);
  const prompt = new Node(document);
  const estimate = new Node(document);
  const ring = new Node(document);
  let images = [];
  let meter;
  try {
    meter = createTokenMeter({ root, trigger, panel, prompt, estimate, ring,
      attachments: () => images,
      modelSelect: { value: "ornith" },
      sessionStore: resource(null), timelineStore: resource({ events: [] }),
      modelsStore: resource({ models: [] }) });
    assert.equal(estimate.textContent, "", "empty composer should stay quiet");
    prompt.value = "abcd";
    meter.refresh();
    assert.match(estimate.textContent, /~1 tok/);
    assert.doesNotMatch(estimate.textContent, /图片|image/i);
    images = ["image-1"];
    meter.refresh();
    assert.match(estimate.textContent, /~1 tok/);
    assert.match(estimate.textContent, /图片|image/i);
    assert.match(estimate.title, /图片|image/i);
    assert.match(panel.children.at(-1).textContent, /图片|image/i);
    prompt.value = "";
    meter.refresh();
    assert.match(estimate.textContent, /~0 tok/,
      "image-only drafts must not look like an empty composer");
    assert.match(estimate.textContent, /图片|image/i);
    prompt.value = "abcd";
    images = [];
    meter.refresh();
    assert.doesNotMatch(estimate.textContent, /图片|image/i);
    assert.doesNotMatch(panel.children.at(-1).textContent, /图片|image/i);
  } finally {
    meter?.destroy();
    globalThis.document = previousDocument;
  }
});
