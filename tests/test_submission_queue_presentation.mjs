import assert from "node:assert/strict";
import test from "node:test";
import { TestElement, testDocument } from "./helpers/extension-dom.mjs";
import { createSubmissionTimeline } from "../app/web/js/features/chat/submission-timeline.js";
import { createPromptQueue } from "../app/web/js/features/chat/prompt-queue.js";

const id = char => char.repeat(32);
const message = (char, text = char) => ({ id: id(char), text, state: "pending", attachments: [], priority: false });
const tick = () => new Promise(resolve => setImmediate(resolve));
class QueueElement extends TestElement {
  insertBefore(node, before) {
    if (before == null) this.append(node);
    else {
      assert(this.children.includes(before), "insertBefore must target a connected child");
      super.insertBefore(node, before);
    }
  }
  // The queue reuses equal DOM cards; the small base fake has no outerHTML.
  get outerHTML() {
    return JSON.stringify([this.tagName, this.attributes, this.className, this.disabled,
      this.hidden, this.text, this.children.map(node => typeof node === "object" ? node.outerHTML : node)]);
  }
}
function fixture(context) {
  globalThis.document = testDocument();
  document.createElement = tag => new QueueElement(tag);
  globalThis.window = Object.assign(new EventTarget(), { setTimeout, clearTimeout });
  const host = new QueueElement("section"), conversation = new QueueElement("ol"), pending = new QueueElement("div");
  pending.ownerDocument = document;
  host.append(conversation); document.body.append(host, pending);
  let view, active = false, route = { projectId: "default", sessionId: "one" }, drafts = [], events = [], retries = 0;
  const data = { items: [], managed: true, paused: false, error: "", revision: 1, actions: [] }, writes = [];
  context.mock.method(globalThis, "fetch", async (url, options) => {
    if (options.method === "DELETE") {
      writes.push(options.method);
      data.items = data.items.filter(item => item.id !== url.split("/").at(-1));
      data.revision++;
    } else assert.equal(options.method, "GET", "presentation must not submit messages");
    return Response.json({ ok: true, data });
  });
  const queue = createPromptQueue({ container: pending,
    navigation: { get: () => route, subscribe() {} },
    isRunActive: () => active, stagedEntries: () => drafts.map(item => ({ ...item, staged: true })),
    isMessagePresented: value => view?.isPresented(value) ?? false,
    onUpdated: () => view?.render(), onRetry: () => { retries++; },
    onRemoved: (_key, item) => view.forget(item.id),
  });
  view = createSubmissionTimeline({ container: conversation, host, scope: () => "local",
    onPresentationChange: () => queue.render(),
    snapshot: () => ({ ...route, active, entries: [...queue.snapshot(route.projectId, route.sessionId).items ?? [], ...drafts],
      events, paused: data.paused, error: data.error }),
  });
  context.after(() => view.destroy());
  return { queue, view, conversation, pending, data, writes,
    active(value) { active = value; view.render(); queue.render(); },
    drafts(value) { drafts = value; view.render(); queue.render(); },
    events(value) { events = value; view.render(); },
    route(value) { route = value; }, retries: () => retries,
    async load() { await queue.select(route.projectId, route.sessionId); },
    shown() { return pending.querySelectorAll("li").map(node => node.dataset.queueItemId); } };
}

test("the first message leaves the pending view before and after queue admission", async context => {
  const f = fixture(context), first = message("a", "first prompt");
  f.view.begin({ ...first, projectId: "default", sessionId: "one" });
  f.drafts([first]);
  assert.equal(f.conversation.children.length, 2);
  assert.equal(f.pending.hidden, true);
  f.data.items = [first]; f.drafts([]); await f.load();
  assert.equal(f.pending.hidden, true);
  assert.equal(f.queue.snapshot("default", "one").items.length, 1, "durable dispatch state is retained");
  assert.deepEqual(f.writes, []);
});

test("new-session draft migration keeps the initial prompt in just one place", async context => {
  const f = fixture(context), first = message("a", "new session prompt");
  f.route({ projectId: "default", sessionId: "" });
  f.view.begin({ ...first, projectId: "default", sessionId: "" });
  f.drafts([first]);
  assert.equal(f.pending.hidden, true);
  f.route({ projectId: "default", sessionId: "created" });
  f.data.items = [first]; f.drafts([]); await f.load();
  f.view.bindProject("default", "created");
  assert.equal(f.view.isPresented(first.id), true);
  assert.equal(f.conversation.children.length, 2);
  assert.equal(f.pending.hidden, true);
  assert.deepEqual(f.writes, []);
});

test("later messages stay editable in the queue and move into chat on their own turn", async context => {
  const f = fixture(context); f.data.items = [message("a"), message("b"), message("c")];
  await f.load();
  assert.equal(f.view.isPresented(id("a")), true);
  assert.deepEqual(f.shown(), [id("b"), id("c")]);
  f.active(true); f.data.items[0].state = "sending"; await f.load();
  f.events([{ kind: "agent_start", queue_item_id: id("a") }]);
  assert.deepEqual(f.shown(), [id("b"), id("c")], "a lagging queue read cannot repeat durable history");
  f.data.items.shift(); await f.load(); f.active(false);
  assert.equal(f.view.isPresented(id("b")), true);
  assert.deepEqual(f.shown(), [id("c")]);
  assert.equal(f.queue.snapshot("default", "one").items.length, 2);
  assert.deepEqual(f.writes, []);
});

test("restored paused delivery retains retry and removal without repeating the prompt", async context => {
  const f = fixture(context); f.data.items = [message("a", "recoverable input")];
  f.data.paused = true; f.data.error = "profile_model_missing";
  await f.load();
  assert.equal(f.conversation.children.length, 2);
  assert.equal(f.conversation.children[1].dataset.state, "error");
  assert.deepEqual(f.shown(), []);
  assert.equal(f.pending.hidden, false);
  assert(!f.pending.textContent.includes("recoverable input"));
  f.pending.querySelector('[data-queue-focus="retry"]').click(); await tick();
  assert.equal(f.retries(), 1);
  f.pending.querySelector('[data-queue-focus="review-remove"]').click(); await tick(); await tick();
  assert.deepEqual(f.writes, ["DELETE"]);
  assert.equal(f.queue.snapshot("default", "one").items.length, 0);
  assert.equal(f.pending.hidden, true);
  assert.equal(f.conversation.hidden, true);
});

test("presentation ownership cannot hide a pending message in another session", async context => {
  const f = fixture(context); f.data.items = [message("a")]; await f.load();
  f.active(true); f.route({ projectId: "default", sessionId: "two" });
  assert.equal(f.view.isPresented(id("a")), false);
  await f.load();
  assert.equal(f.view.isPresented(id("a")), false);
  assert.deepEqual(f.shown(), [id("a")]);
});

test("identical prompts with different submission IDs remain distinct", async context => {
  const f = fixture(context); f.data.items = [message("a", "same text"), message("b", "same text")];
  await f.load();
  assert.equal(f.view.isPresented(id("a")), true);
  assert.deepEqual(f.shown(), [id("b")]);
  assert.equal(f.queue.snapshot("default", "one").items.length, 2);
});

test("a pending message withdrawn by another client cannot reappear as a sent bubble", async context => {
  const f = fixture(context); f.active(true); f.data.items = [message("b")]; await f.load();
  assert.deepEqual(f.shown(), [id("b")]);
  f.data.items = []; await f.load(); f.active(false);
  assert.equal(f.view.isPresented(id("b")), false);
  assert.equal(f.conversation.hidden, true);
  assert.equal(f.pending.hidden, true);
});

test("a failed queued submission still exposes its error in the conversation", context => {
  const f = fixture(context), queued = message("b", "failed queued input"); f.active(true);
  f.view.begin({ ...queued, projectId: "default", sessionId: "one" });
  f.drafts([queued]);
  assert.deepEqual(f.shown(), [queued.id]);
  f.view.fail(queued.id, new Error("Queue is full"));
  assert.equal(f.view.isPresented(queued.id), true);
  assert.equal(f.conversation.children[1].dataset.state, "error");
  assert.equal(f.pending.hidden, true);
});

test("a paused later message cannot label an earlier visible submission as failed", async context => {
  const f = fixture(context); f.data.items = [message("a")]; await f.load();
  f.active(true); f.data.items = [message("b")];
  f.data.paused = true; f.data.error = "profile_model_missing"; await f.load();
  assert.equal(f.view.isPresented(id("a")), true);
  assert.equal(f.conversation.children[1].dataset.state, "preparing");
  assert.deepEqual(f.shown(), [id("b")]);
});
