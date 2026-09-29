import assert from "node:assert/strict";
import test from "node:test";

import { applyAgentProfileDefaults, createComposerProfile, fillAgentOptions } from
  "../app/web/js/features/chat/composer-profile.js";
import { createDraftStore, projectDraftKey } from
  "../app/web/js/features/chat/draft-store.js";
import { createResourceStore } from "../app/web/js/state/store.js";

class Select extends EventTarget {
  constructor(values = []) {
    super();
    this.options = [];
    this.selected = "";
    this.disabled = false;
    for (const value of values) this.append({ value });
  }

  append(option) {
    this.options.push(option);
    if (this.options.length === 1) this.selected = option.value;
  }

  replaceChildren() {
    this.options = [];
    this.selected = "";
  }

  get value() { return this.selected; }
  set value(value) {
    this.selected = this.options.some((option) => option.value === value)
      ? value : "";
  }
}

globalThis.document = {
  createElement() {
    return { value: "", textContent: "", setAttribute(name, value) {
      this[name] = value;
    } };
  },
};

const models = [
  { id: "text", name: "Text", reasoning_efforts: ["medium"],
    default_reasoning_effort: "medium" },
  { id: "code", name: "Code", reasoning_efforts: ["medium", "high"],
    default_reasoning_effort: "medium" },
];

test("blank task follows the default Agent until the user chooses overrides", () => {
  const modelSelect = new Select();
  const reasoningSelect = new Select();
  const permissionSelect = new Select(["read-only", "balanced", "full-access"]);
  const modelsStore = createResourceStore({ default_model_id: "text", models });
  const agentsStore = createResourceStore({ items: [] });
  const projectsStore = createResourceStore({ items: [] });
  const sessionStore = createResourceStore(null);
  const navigation = { get: () => ({ projectId: "default", sessionId: "" }),
    subscribe: () => () => {} };
  const profile = createComposerProfile({ modelSelect, reasoningSelect,
    permissionSelect, navigation, sessionStore, modelsStore, agentsStore,
    projectsStore, isRunActive: () => false, onBusyChange: () => {} });

  assert.deepEqual(profile.selection(), { model_id: "text",
    reasoning_effort: "medium", permission_profile: "balanced" });
  agentsStore.setData({ items: [{ id: "mdo.default", model: "code",
    reasoning_effort: "high", permission_profile: "read-only" }] });
  assert.deepEqual(profile.selection(), { model_id: "code",
    reasoning_effort: "high", permission_profile: "read-only" });

  permissionSelect.value = "full-access";
  permissionSelect.dispatchEvent(new Event("change"));
  agentsStore.setData({ items: [{ id: "mdo.default", model: "code",
    reasoning_effort: "high", permission_profile: "balanced" }] });
  assert.equal(profile.selection().permission_profile, "full-access");

  modelSelect.value = "text";
  modelSelect.dispatchEvent(new Event("change"));
  agentsStore.setData({ items: [{ id: "mdo.default", model: "code",
    reasoning_effort: "high", permission_profile: "balanced" }] });
  assert.equal(profile.selection().model_id, "text");
  assert.equal(profile.selection().reasoning_effort, "medium");
  profile.destroy();
});

test("blank task choices survive refresh per project without freezing other defaults", async () => {
  const oldWindow = globalThis.window;
  const oldFetch = globalThis.fetch;
  const documents = new Map();
  globalThis.window = { setTimeout() { return 1; }, clearTimeout() {},
    addEventListener() {} };
  globalThis.fetch = async (url, options) => {
    const current = documents.get(String(url)) ?? { revision: 0, text: "",
      attachments: [], submissions: [], composer_profile: null };
    if (options.method === "GET")
      return Response.json({ ok: true, data: current });
    const body = JSON.parse(options.body);
    assert.equal(body.revision, current.revision);
    const next = { ...body, revision: current.revision + 1 };
    documents.set(String(url), next);
    return Response.json({ ok: true, data: next });
  };
  const route = { projectId: "alpha", sessionId: "" };
  const listeners = new Set();
  const navigation = { get: () => route, subscribe(listener) {
    listeners.add(listener);
    listener(route);
    return () => listeners.delete(listener);
  } };
  const modelsStore = createResourceStore({ default_model_id: "text", models });
  const agentsStore = createResourceStore({ items: [{ id: "mdo.default",
    model: "code", reasoning_effort: "high",
    permission_profile: "balanced" }] });
  const projectsStore = createResourceStore({ items: [] });
  const sessionStore = createResourceStore(null);
  const makeStore = () => createDraftStore({ onRestore() {}, onSaved() {},
    onError(error) { throw error; } });
  const makeProfile = (draftStore) => {
    const modelSelect = new Select();
    const reasoningSelect = new Select();
    const permissionSelect = new Select(["read-only", "balanced", "full-access"]);
    return { profile: createComposerProfile({ modelSelect, reasoningSelect,
      permissionSelect, navigation, sessionStore, modelsStore, agentsStore,
      projectsStore, draftStore, isRunActive: () => false,
      onBusyChange() {} }), modelSelect, reasoningSelect, permissionSelect };
  };
  try {
    const alpha = projectDraftKey("alpha");
    const beta = projectDraftKey("beta");
    const firstStore = makeStore();
    firstStore.select(alpha);
    assert.equal(await firstStore.ensureLoaded(alpha), true);
    const first = makeProfile(firstStore);
    assert.deepEqual(first.profile.selection(), { model_id: "code",
      reasoning_effort: "high", permission_profile: "balanced" });
    first.permissionSelect.value = "full-access";
    first.permissionSelect.dispatchEvent(new Event("change"));
    await new Promise(setImmediate);
    assert.equal(await firstStore.flush(alpha), true);
    assert.deepEqual(documents.get("/api/v1/projects/alpha/draft").composer_profile,
      { model_id: "", reasoning_effort: "",
        permission_profile: "full-access" });
    first.profile.destroy();

    const reopened = makeStore();
    reopened.select(alpha);
    assert.equal(await reopened.ensureLoaded(alpha), true);
    const second = makeProfile(reopened);
    assert.deepEqual(second.profile.selection(), { model_id: "code",
      reasoning_effort: "high", permission_profile: "full-access" });
    agentsStore.setData({ items: [{ id: "mdo.default", model: "text",
      reasoning_effort: "medium", permission_profile: "balanced" }] });
    assert.deepEqual(second.profile.selection(), { model_id: "text",
      reasoning_effort: "medium", permission_profile: "full-access" });

    route.projectId = "beta";
    for (const listener of listeners) listener(route);
    reopened.select(beta);
    assert.equal(await reopened.ensureLoaded(beta), true);
    second.profile.sync();
    assert.deepEqual(second.profile.selection(), { model_id: "text",
      reasoning_effort: "medium", permission_profile: "balanced" });
    second.modelSelect.value = "code";
    second.modelSelect.dispatchEvent(new Event("change"));
    second.reasoningSelect.value = "high";
    second.reasoningSelect.dispatchEvent(new Event("change"));
    await new Promise(setImmediate);
    assert.equal(await reopened.flush(beta), true);
    assert.deepEqual(documents.get("/api/v1/projects/beta/draft").composer_profile,
      { model_id: "code", reasoning_effort: "high",
        permission_profile: "" });
    route.projectId = "alpha";
    for (const listener of listeners) listener(route);
    assert.deepEqual(second.profile.selection(), { model_id: "text",
      reasoning_effort: "medium", permission_profile: "full-access" });
    second.profile.destroy();
  } finally {
    globalThis.window = oldWindow;
    globalThis.fetch = oldFetch;
  }
});

test("new-session dialog applies a selected Agent's declared defaults", () => {
  const modelSelect = new Select(["text", "code"]);
  const reasoningSelect = new Select();
  const permissionSelect = new Select(["read-only", "balanced", "full-access"]);
  const controls = { models, modelSelect, reasoningSelect, permissionSelect,
    fallback: { model_id: "text", reasoning_effort: "medium",
      permission_profile: "balanced" } };
  applyAgentProfileDefaults({ ...controls, agent: { model: "code",
    reasoning_effort: "high", permission_profile: "read-only" } });
  assert.deepEqual([modelSelect.value, reasoningSelect.value,
    permissionSelect.value], ["code", "high", "read-only"]);

  applyAgentProfileDefaults({ ...controls, agent: { model: "missing" } });
  assert.deepEqual([modelSelect.value, reasoningSelect.value,
    permissionSelect.value], ["missing", "medium", "balanced"]);
  assert.ok(modelSelect.options.some((option) => option.value === "missing"));
});

test("Agent catalog starts with the built-in default and preserves a manual choice", () => {
  const select = new Select();
  const agents = [{ id: "qa.profile", name: "QA Profile" },
    { id: "mdo.default", name: "Default" }];
  fillAgentOptions(select, agents);
  assert.equal(select.value, "mdo.default");
  select.value = "qa.profile";
  fillAgentOptions(select, agents);
  assert.equal(select.value, "qa.profile");
  fillAgentOptions(select, agents.slice(1));
  assert.equal(select.value, "mdo.default");
});
