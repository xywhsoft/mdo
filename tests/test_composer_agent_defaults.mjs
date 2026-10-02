import assert from "node:assert/strict";
import test from "node:test";

import { applyAgentProfileDefaults, createComposerProfile, fillAgentOptions,
  projectProfileDefaults } from
  "../app/web/js/features/chat/composer-profile.js";
import { createDraftStore, projectDraftKey } from
  "../app/web/js/features/chat/draft-store.js";
import { createNewSessionProfile } from
  "../app/web/js/features/chat/new-session-profile.js";
import { createResourceStore } from "../app/web/js/state/store.js";
import { findModel } from "../app/web/js/utils/models.js";

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
  focus() { globalThis.document.activeElement = this; }
  set value(value) {
    this.selected = this.options.some((option) => option.value === value)
      ? value : "";
  }
}

class Button extends EventTarget {
  hidden = true;
  disabled = false;
  focus() { globalThis.document.activeElement = this; }
}

class OptionsList {
  options = [];
  append(option) { this.options.push(option); }
  replaceChildren() { this.options = []; }
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

test("retired built-in references resolve in project defaults without shadowing a custom model", () => {
  const builtin = { id: "ornith-1.5-35b", aliases: ["ling-3.0-tiny", "ling-gpu"], default_reasoning_effort: "medium" };
  assert.equal(findModel([builtin], "ling-3.0-tiny"), builtin);
  const custom = { id: "ling-gpu" };
  assert.equal(findModel([builtin, custom], "ling-gpu"), custom);
  assert.deepEqual(projectProfileDefaults("old", [{ id: "old", default_model_id: "ling-3.0-tiny" }], [],
    { models: [builtin], default_model_id: builtin.id }), {
    model_id: builtin.id, reasoning_effort: "medium", permission_profile: "balanced",
  });
});

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
    const resetButton = new Button();
    return { profile: createComposerProfile({ modelSelect, reasoningSelect,
      permissionSelect, navigation, sessionStore, modelsStore, agentsStore,
      projectsStore, draftStore, resetButton, isRunActive: () => false,
      onBusyChange() {} }), modelSelect, reasoningSelect, permissionSelect,
    resetButton };
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
    assert.equal(second.resetButton.hidden, false);
    second.resetButton.focus();
    second.resetButton.dispatchEvent(new Event("click"));
    await new Promise(setImmediate);
    assert.equal(await reopened.flush(alpha), true);
    assert.equal(documents.get("/api/v1/projects/alpha/draft").composer_profile,
      null);
    assert.deepEqual(second.profile.selection(), { model_id: "text",
      reasoning_effort: "medium", permission_profile: "balanced" });
    assert.equal(second.resetButton.hidden, true);
    assert.equal(globalThis.document.activeElement, second.modelSelect);
    agentsStore.setData({ items: [{ id: "mdo.default", model: "code",
      reasoning_effort: "high", permission_profile: "read-only" }] });
    assert.deepEqual(second.profile.selection(), { model_id: "code",
      reasoning_effort: "high", permission_profile: "read-only" });
    const afterReset = makeStore();
    afterReset.select(alpha);
    assert.equal(await afterReset.ensureLoaded(alpha), true);
    const third = makeProfile(afterReset);
    assert.equal(third.resetButton.hidden, true);
    assert.deepEqual(third.profile.selection(), second.profile.selection());
    third.profile.destroy();
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

test("configured task follows its target project's model unless the Agent declares one", () => {
  const catalog = { default_model_id: "text", models };
  const projects = [{ id: "beta", default_model_id: "code" }];
  const agents = [{ id: "mdo.default", model: "",
    permission_profile: "balanced" }];
  const fallback = projectProfileDefaults("beta", projects, agents, catalog);
  assert.deepEqual(fallback, { model_id: "code", reasoning_effort: "medium",
    permission_profile: "balanced" });
  const modelSelect = new Select(["text", "code"]);
  const reasoningSelect = new Select();
  const permissionSelect = new Select(["read-only", "balanced", "full-access"]);
  applyAgentProfileDefaults({ agent: { model: "text", reasoning_effort: "medium",
    permission_profile: "read-only" }, fallback, models,
  modelSelect, reasoningSelect, permissionSelect });
  assert.deepEqual([modelSelect.value, reasoningSelect.value,
    permissionSelect.value], ["text", "medium", "read-only"]);
  assert.equal(projectProfileDefaults("unknown", projects, agents,
    catalog).model_id, "text");
});

test("configured task project input updates defaults and keeps later manual model choice", () => {
  const projectInput = new EventTarget();
  projectInput.value = "default";
  const projectOptions = new OptionsList();
  const agentSelect = new Select();
  const modelSelect = new Select();
  const reasoningSelect = new Select();
  const permissionSelect = new Select(["read-only", "balanced", "full-access"]);
  const projectsStore = createResourceStore({ items: [
    { id: "default", name: "Default" },
    { id: "beta", name: "Beta", default_model_id: "code" },
  ] });
  const agentsStore = createResourceStore({ items: [
    { id: "mdo.default", name: "Default", model: "",
      permission_profile: "balanced" },
    { id: "qa.agent", name: "QA", model: "text",
      permission_profile: "read-only" },
  ] });
  const modelsStore = createResourceStore({ default_model_id: "text", models });
  const profile = createNewSessionProfile({ projectInput, projectOptions,
    agentSelect, modelSelect, reasoningSelect, permissionSelect,
    projectsStore, agentsStore, modelsStore,
    currentSelection: () => ({ model_id: "text", reasoning_effort: "medium",
      permission_profile: "balanced" }) });
  assert.deepEqual(projectOptions.options.map((option) => option.value),
    ["default", "beta"]);
  profile.resetForOpen();
  assert.equal(modelSelect.value, "text");
  projectInput.value = "beta";
  projectInput.dispatchEvent(new Event("input"));
  assert.equal(modelSelect.value, "code");
  modelSelect.value = "text";
  modelSelect.dispatchEvent(new Event("change"));
  assert.equal(modelSelect.value, "text");
  projectInput.value = "be";
  projectInput.dispatchEvent(new Event("input"));
  assert.equal(modelSelect.value, "text");
  projectInput.value = "beta";
  projectInput.dispatchEvent(new Event("change"));
  assert.equal(modelSelect.value, "code");
  agentSelect.value = "qa.agent";
  agentSelect.dispatchEvent(new Event("change"));
  assert.deepEqual([modelSelect.value, permissionSelect.value],
    ["text", "read-only"]);
  profile.destroy();
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
