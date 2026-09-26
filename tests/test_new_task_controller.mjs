import assert from "node:assert/strict";
import test from "node:test";

import { createDraftStore } from "../app/web/js/features/chat/draft-store.js";
import { createNewTaskController } from "../app/web/js/features/chat/new-task-controller.js";

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
    assert.equal(await controller.submit({ projectId: "default",
      text: "second", profile }), true);
    assert.equal(documents.get(globalPath).submissions.length, 2);
    assert.equal(documents.get(globalPath).new_task.phase, "creating");
    releaseCreate();
    assert.equal(await done, `default/${"1".padStart(32, "0")}`);
    const sessionPath = `/api/v1/projects/default/sessions/${
      "1".padStart(32, "0")}/draft`;
    assert.deepEqual(documents.get(sessionPath).submissions.map((item) =>
      item.text), ["first", "second"]);
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
