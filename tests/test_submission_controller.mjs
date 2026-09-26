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
  globalThis.fetch = async (path, options) => {
    if (options.method === "GET")
      return Response.json({ ok: true, data: { ...saved, revision } });
    if (path.includes("/draft/submissions/")) {
      const id = path.split("/").at(-1);
      if (options.method === "DELETE")
        saved.submissions = saved.submissions.filter((item) => item.id !== id);
      else {
        const state = JSON.parse(options.body).state;
        saved.submissions = saved.submissions.map((item) =>
          item.id === id ? { ...item, state } : item);
      }
      revision += 1;
      return Response.json({ ok: true, data: { ...saved, revision } });
    }
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

test("a stale tab appends its stable intent without replacing another tab's intent", async () => {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  const key = "default/cross-tab";
  const a = { id: "a".repeat(32), text: "tab A", attachments: [],
    interrupt: false, state: "prepared" };
  let remote = { revision: 0, text: "", attachments: [],
    run_admission_uncertain: false, submissions: [] };
  let appendCalls = 0;
  globalThis.window = { setTimeout, clearTimeout, addEventListener() {} };
  globalThis.fetch = async (path, options) => {
    if (options.method === "GET")
      return Response.json({ ok: true, data: structuredClone(remote) });
    const body = JSON.parse(options.body);
    if (path.endsWith("/draft/submissions")) {
      appendCalls += 1;
      assert.equal(options.method, "POST");
      assert.equal(remote.submissions.length, 1);
      remote = { ...remote, revision: remote.revision + 1,
        submissions: [...remote.submissions, body] };
      return Response.json({ ok: true, data: structuredClone(remote) },
        { status: 201 });
    }
    assert.equal(options.method, "PUT");
    if (body.revision !== remote.revision)
      return Response.json({ ok: false, error: {
        code: "draft_conflict", message: "stale revision",
      } }, { status: 409 });
    remote = { ...remote, ...body, revision: remote.revision + 1 };
    return Response.json({ ok: true, data: structuredClone(remote) });
  };
  try {
    const callbacks = { onRestore() {}, onError() {}, onSaved() {} };
    const tabA = createDraftStore(callbacks);
    const tabB = createDraftStore(callbacks);
    tabA.select(key);
    assert.equal(await tabA.ensureLoaded(key), true);
    assert.equal(tabA.appendSubmission(key, a), true);
    assert.equal(await tabA.flush(key), true);
    tabB.select(key);
    assert.equal(await tabB.ensureLoaded(key), true);
    assert.equal(tabA.updateSubmissionState(key, a.id, "posting"), true);
    assert.equal(await tabA.flush(key), true);
    const reviewed = [];
    const queue = { newId: () => "b".repeat(32), hasStaged: () => true };
    const controller = createSubmissionController({ draftStore: tabB,
      promptQueue: queue,
      onPersisted(_key, item) {
        tabB.clearIfMatches(key, item.text, item.attachments);
      },
      onPromoted() {}, onReview(_key, item) { reviewed.push(item.id); },
      onRestored() {}, onChange() {},
    });
    assert.equal(await controller.submit(key, "tab B", [], false), true);
    assert.equal(appendCalls, 1);
    assert.deepEqual(remote.submissions.map((item) => item.text),
      ["tab A", "tab B"]);
    assert.deepEqual(remote.submissions.map((item) => item.state),
      ["posting", "prepared"]);
    assert.deepEqual(reviewed, []);
    const reopened = createDraftStore(callbacks);
    reopened.select(key);
    assert.equal(await reopened.ensureLoaded(key), true);
    assert.deepEqual(reopened.submissions(key).map((item) => item.text),
      ["tab A", "tab B"]);
  } finally {
    globalThis.window = originalWindow;
    globalThis.fetch = originalFetch;
  }
});

test("a lost draft PUT response reuses the same saved submission ID", async () => {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  let remote = { revision: 0, text: "", attachments: [],
    run_admission_uncertain: false, submissions: [] };
  let losePut = true;
  let appendCalls = 0;
  globalThis.window = { setTimeout: () => 0, clearTimeout() {},
    addEventListener() {} };
  globalThis.fetch = async (path, options) => {
    if (options.method === "GET")
      return Response.json({ ok: true, data: structuredClone(remote) });
    const body = JSON.parse(options.body);
    if (path.endsWith("/draft/submissions")) {
      appendCalls += 1;
      assert.equal(remote.submissions[0].id, body.id);
      assert.equal(remote.submissions[0].text, body.text);
      return Response.json({ ok: true, data: structuredClone(remote) });
    }
    assert.equal(options.method, "PUT");
    remote = { ...remote, ...body, revision: remote.revision + 1 };
    if (losePut) { losePut = false; throw new TypeError("lost response"); }
    return Response.json({ ok: true, data: structuredClone(remote) });
  };
  try {
    const key = "default/lost-put";
    const draftStore = createDraftStore({ onRestore() {}, onError() {},
      onSaved() {} });
    draftStore.select(key);
    assert.equal(await draftStore.ensureLoaded(key), true);
    const controller = createSubmissionController({ draftStore,
      promptQueue: { newId: () => "c".repeat(32), hasStaged: () => true },
      onPersisted(_key, item) {
        draftStore.clearIfMatches(key, item.text, item.attachments);
      },
      onPromoted() {}, onReview() {}, onRestored() {}, onChange() {},
    });
    assert.equal(await controller.submit(key, "saved once", [], false), true);
    assert.equal(appendCalls, 0);
    assert.deepEqual(remote.submissions.map((item) => item.id),
      ["c".repeat(32)]);
    const reopened = createDraftStore({ onRestore() {}, onError() {},
      onSaved() {} });
    reopened.select(key);
    assert.equal(await reopened.ensureLoaded(key), true);
    assert.deepEqual(reopened.submissions(key).map((item) => item.text),
      ["saved once"]);
  } finally {
    globalThis.window = originalWindow;
    globalThis.fetch = originalFetch;
  }
});

test("a submission with no durable acknowledgement keeps the composer text", async () => {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  const key = "default/offline";
  let offline = false;
  let cleared = 0;
  globalThis.window = { setTimeout: () => 0, clearTimeout() {},
    addEventListener() {} };
  globalThis.fetch = async (_path, options) => {
    if (offline) throw new TypeError("offline");
    assert.equal(options.method, "GET");
    return Response.json({ ok: true, data: { revision: 0, text: "",
      attachments: [], run_admission_uncertain: false, submissions: [] } });
  };
  try {
    const draftStore = createDraftStore({ onRestore() {}, onError() {},
      onSaved() {} });
    draftStore.select(key);
    assert.equal(await draftStore.ensureLoaded(key), true);
    offline = true;
    const controller = createSubmissionController({ draftStore,
      promptQueue: { newId: () => "d".repeat(32) },
      onPersisted() { cleared += 1; }, onPromoted() {}, onReview() {},
      onRestored() {}, onChange() {},
    });
    assert.equal(await controller.submit(key, "keep my input", [], false), false);
    assert.equal(cleared, 0);
    assert.equal(draftStore.text(key), "keep my input");
    assert.equal(draftStore.submissions(key).length, 1);
  } finally {
    globalThis.window = originalWindow;
    globalThis.fetch = originalFetch;
  }
});

test("a peer-observed intent still confirms its original submitter", async () => {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  const key = "default/peer-observed";
  const item = { id: "9".repeat(32), text: "peer saved",
    attachments: [], interrupt: false, state: "prepared" };
  let remote = { revision: 0, text: "", attachments: [],
    run_admission_uncertain: false, submissions: [] };
  let posts = 0;
  globalThis.window = { setTimeout: () => 0, clearTimeout() {},
    addEventListener() {} };
  globalThis.fetch = async (_path, options) => {
    if (options.method === "POST") posts += 1;
    return Response.json({ ok: true, data: structuredClone(remote) });
  };
  try {
    const draftStore = createDraftStore({ onRestore() {}, onError() {},
      onSaved() {} });
    draftStore.select(key);
    assert.equal(await draftStore.ensureLoaded(key), true);
    assert.equal(draftStore.appendSubmission(key, item), true);
    remote = { ...remote, revision: 1,
      submissions: [{ ...item, state: "posting" }] };
    assert.equal(await draftStore.refreshSessionSubmissions(key), true);
    assert.equal(await draftStore.persistUnconfirmedSubmission(key, item), true);
    assert.equal(posts, 0);
  } finally {
    globalThis.window = originalWindow;
    globalThis.fetch = originalFetch;
  }
});

test("an in-flight composer clear is not undone by an older draft response", async () => {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  const key = "default/inflight-clear";
  const item = { id: "8".repeat(32), text: "already submitted",
    attachments: [], interrupt: false, state: "prepared" };
  const oldDraft = { revision: 1, text: item.text, attachments: [],
    run_admission_uncertain: false, submissions: [item] };
  let resolvePut;
  let putStarted;
  const started = new Promise((resolve) => { putStarted = resolve; });
  let displayed = "";
  globalThis.window = { setTimeout: () => 0, clearTimeout() {},
    addEventListener() {} };
  globalThis.fetch = async (_path, options) => {
    if (options.method === "GET")
      return Response.json({ ok: true, data: structuredClone(oldDraft) });
    assert.equal(options.method, "PUT");
    putStarted();
    return new Promise((resolve) => { resolvePut = resolve; });
  };
  try {
    const draftStore = createDraftStore({ onRestore(text) { displayed = text; },
      onError() {}, onSaved() {} });
    draftStore.select(key);
    assert.equal(await draftStore.ensureLoaded(key), true);
    draftStore.edit(key, "", [], true);
    displayed = "";
    const saving = draftStore.flush(key);
    await started;
    assert.equal(await draftStore.persistUnconfirmedSubmission(key, item), true);
    assert.equal(draftStore.text(key), "");
    assert.equal(displayed, "");
    resolvePut(Response.json({ ok: true, data: {
      ...oldDraft, revision: 2, text: "" } }));
    assert.equal(await saving, true);
    assert.equal(draftStore.text(key), "");
  } finally {
    globalThis.window = originalWindow;
    globalThis.fetch = originalFetch;
  }
});

test("an accepted receipt confirms a submitter after its draft intent was consumed", async () => {
  const key = "default/consumed-before-ack";
  const item = { id: "a".repeat(32), text: "already accepted",
    attachments: [], interrupt: false, state: "prepared" };
  let persisted = 0;
  let reviewed = 0;
  const controller = createSubmissionController({
    draftStore: {
      capture() {}, appendSubmission() { return true; },
      async flush() { return false; },
      async persistUnconfirmedSubmission() { return false; },
      async refreshSessionSubmissions() { return true; },
      submissions() { return []; },
    },
    promptQueue: {
      newId() { return item.id; },
      async select() {}, find() { return null; },
      async receipt() { return { id: item.id, state: "accepted",
        run_id: "run-accepted" }; },
    },
    onPersisted() { persisted += 1; }, onPromoted() {},
    onReview() { reviewed += 1; }, onRestored() {}, onChange() {},
  });
  assert.equal(await controller.submit(key, item.text, [], false), true);
  assert.equal(persisted, 1);
  assert.equal(reviewed, 0);
});

test("a late receipt clears the original input after an initially uncertain submit", async () => {
  const key = "default/late-receipt";
  const id = "b".repeat(32);
  let accepted = false;
  let persisted = 0;
  let reviewed = 0;
  const controller = createSubmissionController({
    draftStore: {
      capture() {}, appendSubmission() { return true; },
      async flush() { return false; },
      async persistUnconfirmedSubmission() { return false; },
      async ensureLoaded() { return true; },
      async refreshSessionSubmissions() { return true; },
      isSubmissionDurable() { return false; },
      submissions() { return []; },
    },
    promptQueue: {
      newId() { return id; }, async select() {},
      find() { return null; },
      async receipt() { return accepted ? { id, state: "accepted",
        run_id: "run-late" } : null; },
    },
    onPersisted() { persisted += 1; }, onPromoted() {},
    onReview() { reviewed += 1; }, onRestored() {}, onChange() {},
  });
  assert.equal(await controller.submit(key, "late acknowledgement", [], false), false);
  assert.equal(persisted, 0);
  assert.equal(reviewed, 1);
  await assert.rejects(controller.submit(key, "late acknowledgement", [], false));
  accepted = true;
  assert.equal(await controller.reconcile(key), true);
  assert.equal(persisted, 1);
  assert.equal(await controller.reconcile(key), true);
  assert.equal(persisted, 1);
});

test("review restores text after removing an uncertain submission", async () => {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  const key = "default/review-restore";
  const item = { id: "e".repeat(32), text: "restore me", attachments: [],
    interrupt: false, state: "posting" };
  let remote = { revision: 1, text: item.text, attachments: [],
    run_admission_uncertain: false, submissions: [item] };
  let restored;
  globalThis.window = { setTimeout: () => 0, clearTimeout() {},
    addEventListener() {} };
  globalThis.fetch = async (_path, options) => {
    if (options.method === "DELETE") {
      remote = { ...remote, revision: remote.revision + 1, text: "",
        submissions: [] };
    } else if (options.method === "PUT") {
      const body = JSON.parse(options.body);
      assert.equal(body.revision, remote.revision);
      remote = { ...remote, ...body, revision: remote.revision + 1 };
    }
    return Response.json({ ok: true, data: structuredClone(remote) });
  };
  try {
    const draftStore = createDraftStore({ onRestore() {}, onError() {},
      onSaved() {} });
    draftStore.select(key);
    assert.equal(await draftStore.ensureLoaded(key), true);
    const controller = createSubmissionController({ draftStore,
      promptQueue: { async select() {}, find() { return null; },
        async receipt() { return null; } },
      onPersisted() {}, onPromoted() {}, onReview() {},
      onRestored(_key, value) { restored = value; }, onChange() {},
    });
    assert.equal(await controller.review(key), true);
    assert.equal(restored.text, item.text);
    assert.equal(draftStore.text(key), item.text);
    assert.equal(await draftStore.flush(key), true);
    assert.equal(remote.text, item.text);
    assert.deepEqual(remote.submissions, []);
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
    async refreshSessionSubmissions() { return true; },
    async changeSessionSubmissionState(_key, id, state) {
      const item = saved.find((candidate) => candidate.id === id);
      if (!item) return false;
      item.state = state;
      return true;
    },
    async removeSessionSubmission(_key, id) {
      const index = saved.findIndex((item) => item.id === id);
      if (index < 0) return false;
      saved.splice(index, 1);
      return true;
    },
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
      async refreshSessionSubmissions() { return true; },
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

test("a second tab does not warn or enqueue while another tab claims the intent", async () => {
  const key = "default/claimed-elsewhere";
  const prepared = { id: "f".repeat(32), text: "one intent",
    attachments: [], interrupt: false, state: "prepared" };
  let current = prepared;
  let posts = 0;
  let reviews = 0;
  const controller = createSubmissionController({
    draftStore: {
      async refreshSessionSubmissions() { return true; },
      submissions() { return [current]; },
      async changeSessionSubmissionState() {
        current = { ...prepared, state: "posting" };
        return false;
      },
    },
    promptQueue: {
      hasStaged() { return false; },
      async stage() { posts += 1; },
    },
    onPersisted() {}, onPromoted() {}, onReview() { reviews += 1; },
    onRestored() {}, onChange() {},
  });
  await controller.pump(key);
  assert.equal(posts, 0);
  assert.equal(reviews, 0);
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
    async refreshSessionSubmissions() { return true; },
    submissions() { return [...items]; },
    async removeSessionSubmission(_key, id) {
      const index = items.findIndex((item) => item.id === id);
      if (index < 0) return false;
      items.splice(index, 1);
      return true;
    },
    async changeSessionSubmissionState(_key, id, state) {
      const item = items.find((candidate) => candidate.id === id);
      if (!item) return false;
      item.state = state;
      return true;
    },
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
    async refreshSessionSubmissions() { return true; },
    async changeSessionSubmissionState(_key, id, state) {
      const item = items.find((candidate) => candidate.id === id);
      if (!item) return false;
      item.state = state;
      return true;
    },
    async removeSessionSubmission(_key, id) {
      const index = items.findIndex((item) => item.id === id);
      if (index < 0) return false;
      items.splice(index, 1);
      return true;
    },
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
