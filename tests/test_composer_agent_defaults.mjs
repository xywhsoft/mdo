import assert from "node:assert/strict";
import test from "node:test";

import { createComposerProfile, fillReasoningOptions } from
  "../app/web/js/features/chat/composer-profile.js";
import { createDraftStore, projectDraftKey } from
  "../app/web/js/features/chat/draft-store.js";
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

globalThis.document = {
  querySelector() { return null; },
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

test("a known non-reasoning model clears a previous thinking preference", () => {
  const select = new Select();
  assert.equal(fillReasoningOptions(select, { reasoning_efforts: [] }, "high"), "");
  assert.equal(select.options.length, 0);
  assert.equal(select.disabled, true);
  assert.equal(fillReasoningOptions(select, null, "high"), "high");
});

test("retired built-in references resolve in project defaults without shadowing a custom model", () => {
  const builtin = { id: "ornith-1.5-35b", aliases: ["ling-3.0-tiny", "ling-gpu"], default_reasoning_effort: "medium" };
  assert.equal(findModel([builtin], "ling-3.0-tiny"), builtin);
  const custom = { id: "ling-gpu" };
  assert.equal(findModel([builtin, custom], "ling-gpu"), custom);
  const profile = createComposerProfile({
    modelSelect: new Select(), reasoningSelect: new Select(),
    permissionSelect: new Select(["read-only", "balanced", "full-access"]),
    navigation: { get: () => ({ projectId: "old", sessionId: "" }),
      subscribe: () => () => {} },
    sessionStore: createResourceStore(null),
    modelsStore: createResourceStore({ models: [builtin], default_model_id: builtin.id }),
    projectsStore: createResourceStore({ items: [
      { id: "old", default_model_id: "ling-3.0-tiny" },
    ] }),
    agentsStore: createResourceStore({ items: [] }), onBusyChange: () => {},
  });
  assert.deepEqual(profile.selection(), {
    model_id: builtin.id, reasoning_effort: "medium", permission_profile: "balanced",
  });
  profile.destroy();
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

function savingProfile(updateProfile) {
  let route = { view: "workspace", projectId: "qa", sessionId: "first" };
  const listeners = new Set(), busy = [], refreshed = [];
  const modelSelect = new Select(), reasoningSelect = new Select();
  const session = (id, revision = 1, model = "text") => ({ id, project_id: "qa", revision,
    status: "active", title: id, model_id: model, reasoning_effort: "medium", permission_profile: "balanced" });
  const sessionStore = createResourceStore(session("first"));
  const navigation = { get: () => route, subscribe(fn) { listeners.add(fn); fn(route); return () => listeners.delete(fn); },
    select(id) { route = { ...route, sessionId: id }; for (const fn of listeners) fn(route); } };
  const profile = createComposerProfile({ modelSelect, reasoningSelect,
    permissionSelect: new Select(["read-only", "balanced", "full-access"]), navigation, sessionStore,
    modelsStore: createResourceStore({ models }), projectsStore: createResourceStore({ items: [] }),
    agentsStore: createResourceStore({ items: [] }), isRunActive: () => false,
    onBusyChange(value) { busy.push(value); },
    onProfileUpdated(value) { refreshed.push({ value, current: sessionStore.get().data }); }, updateProfile });
  return { profile, navigation, sessionStore, modelSelect, session, busy, refreshed,
    choose() { modelSelect.value = "code"; modelSelect.dispatchEvent(new Event("change")); } };
}

test("permissions update an active or queued task immediately without applying its deferred model", async () => {
  for (const active of [true, false]) {
    const session = { id: "live", project_id: "qa", revision: 1, status: "active",
      model_id: "text", reasoning_effort: "medium", permission_profile: "balanced" };
    const sessionStore = createResourceStore(session);
    let deferred = { model_id: "code", reasoning_effort: "high", permission_profile: "balanced" };
    let applied = 0, refreshed = 0;
    const permissionSelect = new Select(["balanced", "full-access"]);
    const env = createComposerProfile({ modelSelect: new Select(), reasoningSelect: new Select(),
      permissionSelect, navigation: { get: () => ({ projectId: "qa", sessionId: "live" }), subscribe: () => () => {} },
      sessionStore, modelsStore: createResourceStore({ models }), agentsStore: createResourceStore({ items: [] }),
      projectsStore: createResourceStore({ items: [] }), isRunActive: () => active,
      hasPendingSubmission: () => !active, onBusyChange() {}, onProfileUpdated() { refreshed++; },
      draftStore: { isLoaded: () => true, composerProfile: () => deferred,
        setComposerProfile(_key, value) { deferred = value; }, flush: async () => true },
      updateProfile() { throw new Error("must not change the model"); },
      async updatePermission(before, choice) {
        applied++; assert.equal(before.model_id, "text");
        return { ...before, revision: 2, permission_profile: choice.permission_profile };
      } });
    permissionSelect.value = "full-access"; permissionSelect.dispatchEvent(new Event("change"));
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(applied, 1); assert.equal(refreshed, 0);
    assert.equal(sessionStore.get().data.model_id, "text");
    assert.equal(sessionStore.get().data.permission_profile, "full-access");
    assert.deepEqual(deferred, { model_id: "code", reasoning_effort: "high", permission_profile: "full-access" });
    assert.equal(env.selection().permission_profile, "full-access");
    assert.equal(env.selection().model_id, "code"); env.destroy();
  }
});

test("leaving a model save cancels its confirmation and ignores the late settings", async () => {
  let signal, finish;
  const env = savingProfile((_session, _profile, options) => {
    signal = options.signal; return new Promise(resolve => { finish = resolve; });
  });
  env.choose(); assert.ok(env.profile.isBusy());
  env.navigation.select("second"); env.sessionStore.setData(env.session("second"));
  assert.ok(signal.aborted); assert.equal(env.profile.isBusy(), false);
  finish(env.session("first", 2, "code")); await new Promise(resolve => setImmediate(resolve));
  assert.equal(env.sessionStore.get().data.id, "second");
  assert.equal(env.profile.selection().model_id, "text");
  assert.equal(env.profile.hasInFlight(), false); assert.deepEqual(env.busy, [true, false]);
  assert.deepEqual(env.refreshed, []);
  env.profile.destroy();
});

test("newer model metadata wins over a late confirmed save", async () => {
  let finish;
  const env = savingProfile(() => new Promise(resolve => { finish = resolve; }));
  env.choose(); env.sessionStore.setData(env.session("first", 3));
  finish(env.session("first", 2, "code")); await new Promise(resolve => setImmediate(resolve));
  assert.equal(env.sessionStore.get().data.revision, 3);
  assert.equal(env.profile.selection().model_id, "text"); assert.equal(env.profile.isBusy(), false);
  assert.deepEqual(env.refreshed, []);
  env.profile.destroy();
});

test("destroying the profile view cancels an outstanding confirmation", async () => {
  let signal, finish;
  const env = savingProfile((_session, _profile, options) => {
    signal = options.signal; return new Promise(resolve => { finish = resolve; });
  });
  env.choose(); env.profile.destroy(); assert.ok(signal.aborted);
  finish(env.session("first", 2, "code")); await new Promise(resolve => setImmediate(resolve));
  assert.equal(env.sessionStore.get().data.revision, 1); assert.equal(env.profile.hasInFlight(), false);
  assert.deepEqual(env.refreshed, []);
});

test("a confirmed current model change refreshes recovery after updating session metadata", async () => {
  const env = savingProfile(async () => env.session("first", 2, "code"));
  env.choose(); await new Promise(resolve => setImmediate(resolve));
  assert.equal(env.refreshed.length, 1);
  assert.equal(env.refreshed[0].current, env.refreshed[0].value);
  assert.equal(env.refreshed[0].value.model_id, "code");
  assert.equal(env.refreshed[0].value.revision, 2);
  assert.equal(env.profile.isBusy(), false);
  env.profile.sync(); assert.equal(env.refreshed.length, 1);
  env.profile.destroy();
});

test("global permission changes affect blank tasks, preserve chat overrides, and never rewrite existing sessions", () => {
  const route = { projectId: "default", sessionId: "" };
  const navigation = { get: () => route, subscribe: () => () => {} };
  const settingsStore = createResourceStore({ agent: { permission_profile: "read-only" } });
  const sessionStore = createResourceStore(null);
  const permissionSelect = new Select(["read-only", "balanced", "full-access"]);
  const profile = createComposerProfile({
    modelSelect: new Select(), reasoningSelect: new Select(), permissionSelect,
    navigation, sessionStore, settingsStore,
    modelsStore: createResourceStore({ default_model_id: "text", models }),
    projectsStore: createResourceStore({ items: [] }),
    agentsStore: createResourceStore({ items: [{ id: "mdo.default", permission_profile: "balanced" }] }),
    onBusyChange() {},
  });
  assert.equal(profile.selection().permission_profile, "read-only");
  settingsStore.setData({ agent: { permission_profile: "full-access" } });
  assert.equal(profile.selection().permission_profile, "full-access");
  permissionSelect.value = "balanced";
  permissionSelect.dispatchEvent(new Event("change"));
  settingsStore.setData({ agent: { permission_profile: "read-only" } });
  assert.equal(profile.selection().permission_profile, "balanced");

  route.projectId = "another"; profile.sync();
  assert.equal(profile.selection().permission_profile, "read-only");
  const savedSession = { project_id: "another", id: "saved", status: "active",
    model_id: "text", reasoning_effort: "medium", permission_profile: "balanced" };
  route.sessionId = "saved"; sessionStore.setData(savedSession);
  assert.equal(profile.selection().permission_profile, "balanced");
  settingsStore.setData({ agent: { permission_profile: "full-access" } });
  assert.equal(profile.selection().permission_profile, "balanced");
  assert.equal(sessionStore.get().data.permission_profile, "balanced");
  route.sessionId = ""; profile.sync();
  assert.equal(profile.selection().permission_profile, "full-access");
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
