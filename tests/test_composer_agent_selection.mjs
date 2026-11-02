import assert from "node:assert/strict";
import test from "node:test";
import { createComposerAgent } from "../app/web/js/features/chat/composer-agent.js";
import { createResourceStore } from "../app/web/js/state/store.js";
import { TestElement } from "./helpers/extension-dom.mjs";
const tick = () => new Promise(setImmediate);
function fixture(update) {
  const doc = { createElement: tag => new TestElement(tag) }, select = new TestElement("select"); select.ownerDocument = doc;
  const sessions = createResourceStore(null), agents = createResourceStore({ items: [] }), selected = [], busy = [];
  agents.setData({ items: [{ id: "mdo.default", flags: 1 }, { id: "agent.custom", name: "Custom", flags: 3 }, { id: "subagent.only", flags: 2 }] });
  let route = { projectId: "default", sessionId: "", view: "workspace" }; const subscribers = new Set();
  const navigation = { get: () => route, subscribe: fn => { subscribers.add(fn); return () => subscribers.delete(fn); } };
  const profile = { model_id: "qa", reasoning_effort: "high", permission_profile: "read-only" };
  const control = createComposerAgent({ select, navigation, sessionStore: sessions, agentsStore: agents,
    profile: () => profile, update, onBusy: value => busy.push(value), onSelected: value => selected.push(value) });
  return { control, select, sessions, selected, busy, profile,
    navigate(value) { route = { ...route, ...value }; subscribers.forEach(fn => fn()); } };
}

test("a new task can select every main-capable Agent without creating a session or exposing SubAgent-only roles", () => {
  let writes = 0; const f = fixture(() => writes++);
  try {
    assert.deepEqual(f.select.children.map(option => option.value), ["mdo.default", "agent.custom"]);
    f.select.value = "agent.custom"; f.select.dispatchEvent(new Event("change"));
    assert.equal(f.control.selection(), "agent.custom"); assert.equal(writes, 0);
    f.navigate({ projectId: "another" }); assert.equal(f.control.selection(), "mdo.default");
    f.navigate({ projectId: "default" }); assert.equal(f.control.selection(), "agent.custom");
  } finally { f.control.destroy(); }
});

test("an existing session changes Agent only after its acknowledgement while preserving its model and permission", async () => {
  let finish, captured; const f = fixture((session, profile) => { captured = profile; return new Promise(resolve => finish = resolve); });
  try {
    f.navigate({ sessionId: "one" }); f.sessions.setData({ id: "one", project_id: "default", status: "active", agent_id: "mdo.default" });
    f.select.value = "agent.custom"; f.select.dispatchEvent(new Event("change")); await tick();
    assert.equal(f.select.disabled, true); assert.equal(f.control.selection(), "mdo.default");
    assert.deepEqual(captured, { ...f.profile, agent_id: "agent.custom" });
    finish({ ...f.sessions.get().data, ...captured }); await tick();
    assert.equal(f.control.selection(), "agent.custom"); assert.deepEqual(f.busy, [true, false]);
    assert.equal(f.selected.at(-1).session, "one");
  } finally { f.control.destroy(); }
});

test("a late Agent acknowledgement never replaces a different conversation", async () => {
  let finish; const f = fixture(() => new Promise(resolve => finish = resolve));
  try {
    f.navigate({ sessionId: "one" }); const first = { id: "one", project_id: "default", status: "active", agent_id: "mdo.default" }; f.sessions.setData(first);
    f.select.value = "agent.custom"; f.select.dispatchEvent(new Event("change")); await tick();
    f.navigate({ sessionId: "two" }); f.sessions.setData({ ...first, id: "two" });
    finish({ ...first, agent_id: "agent.custom" }); await tick();
    assert.equal(f.sessions.get().data.id, "two"); assert.equal(f.control.selection(), "mdo.default");
  } finally { f.control.destroy(); }
});
