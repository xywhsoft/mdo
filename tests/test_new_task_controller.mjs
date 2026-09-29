import assert from "node:assert/strict";
import test from "node:test";

import { createDraftStore, projectDraftKey } from "../app/web/js/features/chat/draft-store.js";
import { createNewTaskController, taskTitle } from "../app/web/js/features/chat/new-task-controller.js";
import { SESSION_TITLE_UTF8_LIMIT, sessionTitleUtf8Bytes } from "../app/web/js/features/sessions/session-title.js";

test("new-task titles stay within the UTF-8 session title limit", () => {
  const title = taskTitle("😀".repeat(80));
  assert.equal(Array.from(title).length, 64);
  assert.equal(sessionTitleUtf8Bytes(title), SESSION_TITLE_UTF8_LIMIT);
  assert.equal(sessionTitleUtf8Bytes("测".repeat(85)), 255);
  assert.equal(sessionTitleUtf8Bytes("测".repeat(86)), 258);
  assert.equal(taskTitle(" \n", "图片任务"), "图片任务");
});

test("attachment-first creation recovers a lost create response", async () => {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  const documents = new Map();
  const sessionId = "b".repeat(32);
  let createCalls = 0;
  let lookups = 0;
  let migrated = "";
  globalThis.window = { setTimeout, clearTimeout, addEventListener() {} };
  globalThis.fetch = async (path, options) => {
    const current = documents.get(path) ?? { revision: 0, text: "",
      attachments: [], run_admission_uncertain: false,
      submissions: [], new_task: null };
    if (options.method === "GET")
      return Response.json({ ok: true, data: current });
    const body = JSON.parse(options.body);
    assert.equal(body.revision, current.revision);
    const next = { ...body, revision: current.revision + 1 };
    documents.set(path, next);
    return Response.json({ ok: true, data: next });
  };
  try {
    const draftStore = createDraftStore({ onRestore() {},
      onError(error) { throw error; }, onSaved() {} });
    const projectKey = projectDraftKey("default");
    draftStore.select(projectKey);
    assert.equal(await draftStore.ensureLoaded(projectKey), true);
    draftStore.edit(projectKey, "image prompt", [], true);
    const controller = createNewTaskController({ draftStore,
      newId() { return sessionId; },
      async createSession(input) {
        createCalls += 1;
        assert.equal(input.client_session_id, sessionId);
        assert.equal(documents.get("/api/v1/draft").new_task.session_id,
          sessionId);
        throw new TypeError("response lost");
      },
      async findSession(projectId, id) {
        lookups += 1;
        assert.equal(projectId, "default");
        assert.equal(id, sessionId);
        return { project_id: projectId, id };
      },
      onPersisted() {}, onMigrated(key) { migrated = key; },
      onReview(error) { assert.fail(error.message); }, onChange() {},
    });
    const result = await controller.createForAttachment({
      projectId: "default", title: "image prompt",
      profile: { model_id: "image-model", reasoning_effort: "medium",
        permission_profile: "balanced" },
    });
    assert.deepEqual(result, { projectId: "default", sessionId });
    assert.equal(migrated, `default/${sessionId}`);
    assert.equal(documents.get("/api/v1/draft").new_task, null);
    assert.equal(documents.get("/api/v1/draft").text, "");
    assert.equal(documents.get("/api/v1/projects/default/draft").text, "");
    const target = documents.get(`/api/v1/projects/default/sessions/${sessionId}/draft`);
    assert.equal(target.text, "image prompt");
    assert.deepEqual(target.submissions, []);
    assert.equal(createCalls, 1);
    assert.equal(lookups, 1);
  } finally {
    globalThis.window = originalWindow;
    globalThis.fetch = originalFetch;
  }
});

test("new-task inputs survive a delayed create and move in order", async () => {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  const documents = new Map();
  let releaseCreate;
  const createGate = new Promise((resolve) => { releaseCreate = resolve; });
  let createStarted;
  const started = new Promise((resolve) => { createStarted = resolve; });
  let migrated;
  const done = new Promise((resolve) => { migrated = resolve; });
  let id = 0;
  let creates = 0;
  const persisted = [];
  const reviews = [];
  const globalPath = "/api/v1/draft";
  globalThis.window = { setTimeout, clearTimeout, addEventListener() {} };
  globalThis.fetch = async (path, options) => {
    const current = documents.get(path) ?? { revision: 0, text: "",
      attachments: [], run_admission_uncertain: false,
      submissions: [], new_task: null };
    if (options.method === "GET")
      return Response.json({ ok: true, data: current });
    const body = JSON.parse(options.body);
    assert.equal(body.revision, current.revision);
    const next = { ...body, revision: current.revision + 1 };
    documents.set(path, next);
    return Response.json({ ok: true, data: next });
  };
  try {
    const draftStore = createDraftStore({ onRestore() {}, onError(error) {
      throw error;
    }, onSaved() {} });
    draftStore.select("");
    assert.equal(await draftStore.ensureLoaded(""), true);
    const controller = createNewTaskController({
      draftStore,
      newId() { id += 1; return id.toString(16).padStart(32, "0"); },
      async createSession(input) {
        creates += 1;
        createStarted();
        await createGate;
        return { project_id: input.project_id, id: input.client_session_id };
      },
      async findSession() { throw new Error("not found"); },
      onPersisted(item) { persisted.push(item.text); },
      onMigrated(key) { migrated(key); },
      onReview(error) { reviews.push(error.message); },
      onChange() {},
    });
    const profile = { model_id: "ling-3.0-tiny",
      reasoning_effort: "medium", permission_profile: "balanced" };
    assert.equal(await controller.submit({ projectId: "default",
      text: "first", profile }), true);
    await started;
    assert.equal(documents.get("/api/v1/projects/default/draft").text, "");
    assert.equal(await controller.submit({ projectId: "default",
      text: "second", profile }), true);
    assert.equal(documents.get(globalPath).submissions.length, 2);
    assert.deepEqual(documents.get(globalPath).submissions.map((item) =>
      item.profile), [profile, profile]);
    assert.equal(documents.get(globalPath).new_task.phase, "creating");
    releaseCreate();
    assert.equal(await done, `default/${"1".padStart(32, "0")}`);
    const sessionPath = `/api/v1/projects/default/sessions/${
      "1".padStart(32, "0")}/draft`;
    assert.deepEqual(documents.get(sessionPath).submissions.map((item) =>
      item.text), ["first", "second"]);
    assert.deepEqual(documents.get(sessionPath).submissions.map((item) =>
      item.profile), [profile, profile]);
    assert.deepEqual(documents.get(globalPath).submissions, []);
    assert.equal(documents.get(globalPath).new_task, null);
    assert.deepEqual(persisted, ["first", "second"]);
    assert.deepEqual(reviews, []);
    assert.equal(creates, 1);
  } finally {
    globalThis.window = originalWindow;
    globalThis.fetch = originalFetch;
  }
});

test("a rejected create can change profile without losing queued inputs", async () => {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  const documents = new Map();
  let releaseReject;
  const rejectGate = new Promise((resolve) => { releaseReject = resolve; });
  let createStarted;
  const started = new Promise((resolve) => { createStarted = resolve; });
  let showReview;
  const reviewed = new Promise((resolve) => { showReview = resolve; });
  let id = 0;
  let creates = 0;
  let migrated = "";
  globalThis.window = { setTimeout, clearTimeout, addEventListener() {} };
  globalThis.fetch = async (path, options) => {
    const current = documents.get(path) ?? { revision: 0, text: "",
      attachments: [], run_admission_uncertain: false,
      submissions: [], new_task: null };
    if (options.method === "GET")
      return Response.json({ ok: true, data: current });
    const body = JSON.parse(options.body);
    assert.equal(body.revision, current.revision);
    const next = { ...body, revision: current.revision + 1 };
    documents.set(path, next);
    return Response.json({ ok: true, data: next });
  };
  try {
    const draftStore = createDraftStore({ onRestore() {},
      onError(error) { throw error; }, onSaved() {} });
    draftStore.select("");
    assert.equal(await draftStore.ensureLoaded(""), true);
    const controller = createNewTaskController({ draftStore,
      newId() { id += 1; return id.toString(16).padStart(32, "0"); },
      async createSession(input) {
        creates += 1;
        if (creates === 1) {
          createStarted();
          await rejectGate;
          throw { status: 422, code: "session_profile_invalid" };
        }
        assert.equal(input.client_session_id, "3".padStart(32, "0"));
        assert.equal(input.reasoning_effort, "high");
        return { project_id: input.project_id, id: input.client_session_id };
      },
      async findSession() { assert.fail("definitive rejection needs no lookup"); },
      onPersisted() {}, onMigrated(key) { migrated = key; },
      onReview() { showReview(); }, onChange() {},
    });
    const profile = { model_id: "ling-3.0-tiny",
      reasoning_effort: "medium", permission_profile: "balanced" };
    assert.equal(await controller.submit({ projectId: "default",
      text: "first", profile }), true);
    await started;
    assert.equal(await controller.submit({ projectId: "default",
      text: "second", profile }), true);
    releaseReject();
    await reviewed;
    assert.equal(controller.canChangeProfile(), true);
    assert.equal(documents.get("/api/v1/draft").new_task.phase,
      "rejected");
    const reloadedStore = createDraftStore({
      onRestore() {}, onError(error) { throw error; }, onSaved() {} });
    let restoredReviews = 0;
    const reloaded = createNewTaskController({ draftStore: reloadedStore,
      newId() { id += 1; return id.toString(16).padStart(32, "0"); },
      async createSession(input) {
        creates += 1;
        assert.equal(input.client_session_id, "3".padStart(32, "0"));
        assert.equal(input.reasoning_effort, "high");
        return { project_id: input.project_id, id: input.client_session_id };
      },
      async findSession() { assert.fail("definitive rejection needs no lookup"); },
      onPersisted() {}, onMigrated(key) { migrated = key; },
      onReview() { restoredReviews += 1; }, onChange() {},
    });
    await reloaded.reconcile();
    assert.equal(restoredReviews, 1);
    assert.equal(creates, 1);
    assert.equal(reloaded.canChangeProfile(), true);
    const before = documents.get("/api/v1/draft");
    assert.deepEqual(before.submissions.map((item) => item.text),
      ["first", "second"]);
    assert.equal(await reloaded.review({ ...profile,
      reasoning_effort: "high" }), true);
    assert.equal(migrated, `default/${"3".padStart(32, "0")}`);
    const target = documents.get(`/api/v1/projects/default/sessions/${
      "3".padStart(32, "0")}/draft`);
    assert.deepEqual(target.submissions.map((item) => item.text),
      ["first", "second"]);
    assert.deepEqual(target.submissions.map((item) => item.profile),
      [{ ...profile, reasoning_effort: "high" },
        { ...profile, reasoning_effort: "high" }]);
    assert.equal(target.submissions[0].id, "3".padStart(32, "0"));
    assert.equal(documents.get("/api/v1/draft").new_task, null);
    assert.equal(creates, 2);
  } finally {
    globalThis.window = originalWindow;
    globalThis.fetch = originalFetch;
  }
});

test("a conflicting create response never attaches input to another session", async () => {
  const task = { project_id: "default", session_id: "a".repeat(32),
    title: "first", agent_id: "mdo.default", model_id: "ling-3.0-tiny",
    reasoning_effort: "medium", permission_profile: "balanced",
    phase: "creating" };
  const item = { id: task.session_id, text: "first", attachments: [],
    interrupt: false, state: "prepared" };
  let lookups = 0;
  let reviewed = 0;
  const controller = createNewTaskController({
    draftStore: {
      newTask() { return task; }, submissions() { return [item]; },
      text(key) { return key ? "" : item.text; },
      async ensureLoaded() { return true; },
      async flush() { return true; },
    },
    newId() { return item.id; },
    async createSession() { throw { status: 409, code: "session_create_conflict" }; },
    async findSession() { lookups += 1; return { id: task.session_id }; },
    onPersisted() {}, onMigrated() { assert.fail("must not migrate"); },
    onReview() { reviewed += 1; }, onChange() {},
  });
  await controller.reconcile();
  assert.equal(lookups, 0);
  assert.equal(reviewed, 1);
  assert.equal(controller.isBlocked(), true);
});

test("an uncertain create keeps its original ID and profile on review", async () => {
  const task = { project_id: "default", session_id: "c".repeat(32),
    title: "first", agent_id: "mdo.default", model_id: "ling-3.0-tiny",
    reasoning_effort: "medium", permission_profile: "balanced",
    phase: "creating" };
  const item = { id: task.session_id, text: "first", attachments: [],
    interrupt: false, state: "prepared" };
  const attempts = [];
  let reviews = 0;
  const controller = createNewTaskController({
    draftStore: {
      newTask() { return task; }, submissions() { return [item]; },
      text(key) { return key ? "" : item.text; },
      async ensureLoaded() { return true; }, async flush() { return true; },
      reseedNewTask() { assert.fail("uncertain create must keep its ID"); },
    },
    newId() { assert.fail("uncertain create must not allocate an ID"); },
    async createSession(input) {
      attempts.push(input);
      throw new TypeError("response lost");
    },
    async findSession() { return null; },
    onPersisted() {}, onMigrated() { assert.fail("must not migrate"); },
    onReview() { reviews += 1; }, onChange() {},
  });
  await controller.reconcile();
  assert.equal(controller.canChangeProfile(), false);
  assert.equal(await controller.review({ model_id: "changed",
    reasoning_effort: "high", permission_profile: "full-access" }), false);
  assert.equal(attempts.length, 2);
  for (const attempt of attempts) {
    assert.equal(attempt.client_session_id, task.session_id);
    assert.equal(attempt.model_id, task.model_id);
    assert.equal(attempt.reasoning_effort, task.reasoning_effort);
  }
  assert.equal(reviews, 2);
});
