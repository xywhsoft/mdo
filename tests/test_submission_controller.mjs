import assert from "node:assert/strict";
import test from "node:test";

import { createDraftStore } from "../app/web/js/features/chat/draft-store.js";
import { createSubmissionController } from "../app/web/js/features/chat/submission-controller.js";

test("a second Enter is durable while the first queue POST is still waiting", async () => {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  let revision = 0;
  let saved = { text: "", attachments: [], run_admission_uncertain: false,
    submissions: [] };
  let releaseFirst;
  const firstGate = new Promise((resolve) => { releaseFirst = resolve; });
  let signalFirst;
  const firstStarted = new Promise((resolve) => { signalFirst = resolve; });
  let signalDone;
  const done = new Promise((resolve) => { signalDone = resolve; });
  let nextId = 0;
  const queue = [];
  const persisted = [];
  const promoted = [];
  const reviews = [];
  globalThis.window = { setTimeout, clearTimeout, addEventListener() {} };
  globalThis.fetch = async (_path, options) => {
    if (options.method === "GET")
      return Response.json({ ok: true, data: { ...saved, revision } });
    const body = JSON.parse(options.body);
    assert.equal(body.revision, revision);
    saved = { text: body.text, attachments: body.attachments,
      run_admission_uncertain: body.run_admission_uncertain,
      submissions: body.submissions };
    revision += 1;
    return Response.json({ ok: true, data: { ...saved, revision } });
  };
  try {
    const key = "default/ordered";
    const draftStore = createDraftStore({ onRestore() {}, onError(error) {
      throw error;
    }, onSaved() {} });
    draftStore.select(key);
    assert.equal(await draftStore.ensureLoaded(key), true);
    const promptQueue = {
      newId() { nextId += 1; return nextId.toString(16).padStart(32, "0"); },
      async stage(_project, _session, item) {
        if (item.text === "first") {
          signalFirst();
          await firstGate;
        }
        queue.push({ id: item.id, text: item.text, priority: item.interrupt,
          attachments: item.attachments, state: "staged" });
        return item.id;
      },
      find(_project, _session, id) {
        return queue.find((item) => item.id === id);
      },
      hasStaged() { return queue.some((item) => item.state === "staged"); },
      async promote(_project, _session, id) {
        queue.find((item) => item.id === id).state = "pending";
        return true;
      },
    };
    const controller = createSubmissionController({ draftStore, promptQueue,
      onPersisted(_key, item) { persisted.push(item.text); },
      onPromoted(_key, item) {
        promoted.push(item.text);
        if (promoted.length === 2) signalDone();
      },
      onReview(_key, item) { reviews.push(item.id); },
      onRestored() {}, onChange() {},
    });
    assert.equal(await controller.submit(key, "first", [], false), true);
    await firstStarted;
    assert.equal(await controller.submit(key, "second", [], false), true);
    assert.deepEqual(persisted, ["first", "second"]);
    assert.deepEqual(saved.submissions.map((item) => item.text),
      ["first", "second"]);
    assert.deepEqual(saved.submissions.map((item) => item.state),
      ["posting", "prepared"]);
    assert.equal(queue.length, 0);
    releaseFirst();
    await done;
    assert.deepEqual(queue.map((item) => item.text), ["first", "second"]);
    assert.deepEqual(promoted, ["first", "second"]);
    assert.deepEqual(reviews, []);
    assert.deepEqual(saved.submissions, []);
  } finally {
    globalThis.window = originalWindow;
    globalThis.fetch = originalFetch;
  }
});

test("a recovered staged predecessor holds later submissions until continued", async () => {
  const key = "default/recovered";
  const later = { id: "2".repeat(32), text: "later", attachments: [],
    interrupt: false, state: "prepared" };
  const saved = [later];
  const queue = [{ id: "1".repeat(32), text: "earlier", attachments: [],
    priority: false, state: "staged" }];
  let posts = 0;
  const draftStore = {
    submissions() { return [...saved]; },
    async ensureLoaded() { return true; },
    updateSubmissionState(_key, id, state) {
      const item = saved.find((candidate) => candidate.id === id);
      if (!item) return false;
      item.state = state;
      return true;
    },
    clearSubmission(_key, id) {
      const index = saved.findIndex((item) => item.id === id);
      if (index < 0) return false;
      saved.splice(index, 1);
      return true;
    },
    async flush() { return true; },
  };
  const promptQueue = {
    hasStaged() { return queue.some((item) => item.state === "staged"); },
    receipt() { return null; },
    find(_project, _session, id) {
      return queue.find((item) => item.id === id);
    },
    async stage(_project, _session, item) {
      posts += 1;
      queue.push({ id: item.id, text: item.text,
        attachments: item.attachments, priority: item.interrupt,
        state: "staged" });
      return item.id;
    },
    async promote(_project, _session, id) {
      queue.find((item) => item.id === id).state = "pending";
      return true;
    },
  };
  const controller = createSubmissionController({ draftStore, promptQueue,
    onPersisted() {}, onPromoted() {}, onReview() {},
    onRestored() {}, onChange() {},
  });
  await controller.reconcile(key);
  assert.equal(posts, 0);
  assert.deepEqual(saved, [later]);
  queue[0].state = "pending";
  await controller.pump(key);
  assert.equal(posts, 1);
  assert.deepEqual(queue.map((item) => item.text), ["earlier", "later"]);
  assert.deepEqual(queue.map((item) => item.state), ["pending", "pending"]);
  assert.deepEqual(saved, []);
});

test("polling an uncertain queue admission asks for review only once", async () => {
  const first = { id: "a".repeat(32), text: "uncertain", attachments: [],
    interrupt: false, state: "posting" };
  let reviews = 0;
  const controller = createSubmissionController({
    draftStore: {
      async ensureLoaded() { return true; },
      submissions() { return [first]; },
    },
    promptQueue: { find() { return null; }, receipt() { return null; } },
    onPersisted() {}, onPromoted() {}, onReview() { reviews += 1; },
    onRestored() {}, onChange() {},
  });
  assert.equal(await controller.reconcile("default/uncertain"), false);
  assert.equal(await controller.reconcile("default/uncertain"), false);
  assert.equal(reviews, 1);
});

test("a consumed queue receipt releases only its matching saved intent", async () => {
  const key = "default/consumed";
  const items = [
    { id: "a".repeat(32), text: "already run", attachments: [],
      interrupt: false, state: "posting" },
    { id: "b".repeat(32), text: "next", attachments: [],
      interrupt: false, state: "prepared" },
  ];
  const staged = [];
  const consumed = [];
  let done;
  const promoted = new Promise((resolve) => { done = resolve; });
  const draftStore = {
    async ensureLoaded() { return true; },
    submissions() { return [...items]; },
    clearSubmission(_key, id) {
      const index = items.findIndex((item) => item.id === id);
      if (index < 0) return false;
      items.splice(index, 1);
      return true;
    },
    updateSubmissionState(_key, id, state) {
      const item = items.find((candidate) => candidate.id === id);
      if (!item) return false;
      item.state = state;
      return true;
    },
    async flush() { return true; },
  };
  const promptQueue = {
    find(_project, _session, id) {
      return staged.find((item) => item.id === id) ?? null;
    },
    receipt(_project, _session, id) {
      return id === "a".repeat(32)
        ? { id, state: "accepted", run_id: "run-accepted" } : null;
    },
    hasStaged() { return false; },
    async stage(_project, _session, item) {
      staged.push({ id: item.id, text: item.text,
        priority: item.interrupt, attachments: item.attachments,
        state: "staged" });
      return item.id;
    },
    async promote(_project, _session, id) {
      staged.find((item) => item.id === id).state = "pending";
    },
  };
  const controller = createSubmissionController({ draftStore, promptQueue,
    onConsumed(_key, receipt) { consumed.push(receipt.run_id); },
    onPromoted() { done(); }, onReview() { throw new Error("unexpected review"); },
    onChange() {},
  });
  assert.equal(await controller.reconcile(key), true);
  await promoted;
  assert.deepEqual(consumed, ["run-accepted"]);
  assert.deepEqual(staged.map((item) => item.text), ["next"]);
  assert.deepEqual(items, []);
});

test("receipt read failure cannot turn review into a duplicate queue POST", async () => {
  const first = { id: "c".repeat(32), text: "possibly run", attachments: [],
    interrupt: false, state: "posting" };
  const second = { id: "d".repeat(32), text: "later", attachments: [],
    interrupt: false, state: "prepared" };
  let retries = 0;
  let reviews = 0;
  const controller = createSubmissionController({
    draftStore: { submissions() { return [first, second]; } },
    promptQueue: {
      async select() {}, find() { return null; },
      async receipt() { throw { status: 503, code: "queue_unavailable" }; },
      async stage() { retries += 1; },
    },
    onReview() { reviews += 1; }, onChange() {},
  });
  assert.equal(await controller.review("default/uncertain"), false);
  assert.equal(retries, 0);
  assert.equal(reviews, 1);
});

test("a definite queue-full rejection stays blocked across reconciliation", async () => {
  const key = "default/rejected";
  const items = [
    { id: "a".repeat(32), text: "first", attachments: [],
      interrupt: false, state: "prepared" },
    { id: "b".repeat(32), text: "second", attachments: [],
      interrupt: false, state: "prepared" },
  ];
  const queue = [];
  const reviews = [];
  let posts = 0;
  let completed;
  const done = new Promise((resolve) => { completed = resolve; });
  const draftStore = {
    submissions() { return [...items]; },
    async ensureLoaded() { return true; },
    updateSubmissionState(_key, id, state) {
      const item = items.find((candidate) => candidate.id === id);
      if (!item) return false;
      item.state = state;
      return true;
    },
    clearSubmission(_key, id) {
      const index = items.findIndex((item) => item.id === id);
      if (index < 0) return false;
      items.splice(index, 1);
      return true;
    },
    async flush() { return true; },
  };
  const promptQueue = {
    async select() {},
    hasStaged() { return false; },
    receipt() { return null; },
    find(_project, _session, id) {
      return queue.find((item) => item.id === id);
    },
    async stage(_project, _session, item) {
      posts += 1;
      if (posts === 1) throw { status: 422, code: "queue_full" };
      queue.push({ id: item.id, text: item.text, attachments: [],
        priority: item.interrupt, state: "staged" });
      return item.id;
    },
    async promote(_project, _session, id) {
      queue.find((item) => item.id === id).state = "pending";
    },
  };
  const controller = createSubmissionController({ draftStore, promptQueue,
    onPersisted() {}, onPromoted() {
      if (queue.length === 2) completed();
    },
    onReview(_key, item) { reviews.push(item.state); },
    onRestored() {}, onChange() {},
  });
  await controller.pump(key);
  assert.deepEqual(items.map((item) => item.state), ["rejected", "prepared"]);
  assert.deepEqual(reviews, ["rejected"]);
  assert.equal(await controller.reconcile(key), false);
  assert.equal(posts, 1);
  assert.equal(await controller.review(key), true);
  await done;
  assert.equal(posts, 3);
  assert.deepEqual(queue.map((item) => item.text), ["first", "second"]);
  assert.deepEqual(items, []);
});
