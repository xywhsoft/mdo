import assert from "node:assert/strict";
import test from "node:test";

import { applyAgentProfileDefaults, createComposerProfile } from
  "../app/web/js/features/chat/composer-profile.js";
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
