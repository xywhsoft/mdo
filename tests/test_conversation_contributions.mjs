import assert from "node:assert/strict";
import test from "node:test";
import { TestElement as Node } from "./helpers/extension-dom.mjs";
import { createConversationContributions } from "../app/web/js/features/shell/conversation-contributions.js";

test("conversation contributions bind the originating session and clean up on navigation", async () => {
  const actions = new Node("div"), views = new Node("div");
  globalThis.document = { createElement: tag => new Node(tag), querySelector: id => id === "#sdk-conversation-actions" ? actions : views };
  let current = "first", notify;
  const navigation = { get: () => ({ view: "workspace" }), workspace: () => ({ projectId: "project", sessionId: current }),
    subscribe(fn) { notify = fn; return () => { notify = null; }; } };
  const api = { bind: value => ({ bound: value }) }, calls = [], errors = [];
  let disposed = 0;
  const manager = createConversationContributions({ navigation, onError: e => errors.push(e), sdkFor: sdk => sdk });
  manager.registerAction("test.action", { label: "Inspect", execute: ({ sdk }) => calls.push(sdk.bound.session) }, api);
  manager.registerView("test.view", { mount(root, { signal }) { root.textContent = current; signal.addEventListener("abort", () => disposed++); return () => disposed++; } }, api);
  await new Promise(resolve => setTimeout(resolve, 0));
  assert.equal(actions.children.length, 1); assert.equal(views.children.length, 1);
  const old = actions.children[0]; await old.onclick(); assert.deepEqual(calls, ["first"]);
  current = "second"; notify(); await new Promise(resolve => setTimeout(resolve, 0));
  assert.equal(disposed, 2); await old.onclick(); assert.deepEqual(calls, ["first"]);
  await actions.children[0].onclick(); assert.deepEqual(calls, ["first", "second"]);
  manager.destroy(); assert.equal(actions.children.length, 0); assert.equal(views.children.length, 0); assert.equal(disposed, 4);
  assert.equal(errors.length, 0);
});

test("late predicates and renderers cannot attach a retired chat or leak cleanup", async () => {
  const host = new Node("div"); globalThis.document = { createElement: tag => new Node(tag), querySelector: () => host };
  let current = "old", notify, unblock; const errors = [];
  const navigation = { get: () => ({ view: "workspace" }), workspace: () => ({ projectId: "p", sessionId: current }), subscribe(fn) { notify = fn; return () => {}; } };
  const manager = createConversationContributions({ navigation, onError: e => errors.push(e), sdkFor: api => api });
  const release = manager.registerAction("pending", { label: "Open", when: () => new Promise(r => { unblock = r; }), execute() {} }, { bind: () => ({}) });
  await new Promise(r => setTimeout(r, 0));
  release(); unblock(true); await new Promise(r => setTimeout(r, 0)); assert.equal(host.children.length, 0);
  let done, cleaned = 0;
  manager.registerView("async", { mount: () => new Promise(r => { done = r; }) }, { bind: () => ({}) });
  await new Promise(r => setTimeout(r, 0)); current = ""; notify(); done(() => cleaned++);
  await new Promise(r => setTimeout(r, 0)); assert.equal(cleaned, 1); assert.equal(host.children.length, 0);
  manager.destroy(); assert.deepEqual(errors, []);
});

test("retirement awaits in-flight render/action cleanup and retains failures", async () => {
  const host = new Node("div"); globalThis.document = { createElement: tag => new Node(tag), querySelector: () => host };
  const navigation = { get: () => ({ view: "workspace" }), workspace: () => ({ projectId: "p", sessionId: "s" }) };
  const errors = []; let finish, disposed = false, returned = false;
  const manager = createConversationContributions({ navigation, onError: e => errors.push(e), sdkFor: api => api });
  manager.registerView("pending-render", { mount: () => new Promise(resolve => { finish = resolve; }) }, { bind: () => ({}) });
  await new Promise(r => setTimeout(r, 0));
  const retired = manager.destroy().then(() => { returned = true; });
  await new Promise(r => setTimeout(r, 0)); assert.equal(returned, false);
  finish(async () => { await new Promise(r => setTimeout(r, 5)); disposed = true; });
  await retired; assert.equal(disposed, true);
  let completeAction, actionRetired = false;
  const actions = createConversationContributions({ navigation, onError: e => errors.push(e), sdkFor: api => api });
  actions.registerAction("pending-action", { execute: () => new Promise(resolve => { completeAction = resolve; }) }, { bind: () => ({}) });
  await new Promise(r => setTimeout(r, 0));
  const click = host.children[0].onclick(); await new Promise(r => setTimeout(r, 0));
  const actionCleanup = actions.destroy().then(() => { actionRetired = true; });
  await new Promise(r => setTimeout(r, 0)); assert.equal(actionRetired, false);
  completeAction(); await Promise.all([click, actionCleanup]); assert.equal(actionRetired, true);
  const broken = createConversationContributions({ navigation, onError: e => errors.push(e), sdkFor: api => api });
  broken.registerView("bad-cleanup", { mount: () => async () => { throw Error("cleanup failed"); } }, { bind: () => ({}) });
  await new Promise(r => setTimeout(r, 0));
  await assert.rejects(() => broken.destroy(), /cleanup/);
});
