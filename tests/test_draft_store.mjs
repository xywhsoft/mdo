import assert from "node:assert/strict";
import test from "node:test";

import { createDraftStore } from "../app/web/js/features/chat/draft-store.js";

test("an oversized draft stays unsaved without repeated retries and recovers after editing", async () => {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  let saved = "";
  let puts = 0;
  const errors = [];
  globalThis.window = { setTimeout, clearTimeout, addEventListener() {} };
  globalThis.fetch = async (_path, options) => {
    if (options.method === "GET")
      return Response.json({ ok: true, data: { revision: 1, text: saved,
        attachments: [] } });
    puts += 1;
    saved = JSON.parse(options.body).text;
    return Response.json({ ok: true, data: { revision: 2, text: saved,
      attachments: [] } });
  };
  try {
    const key = "default/oversized";
    const store = createDraftStore({ onRestore() {}, onError(error) {
      errors.push(error);
    }, onSaved() {} });
    store.select(key);
    assert.equal(await store.ensureLoaded(key), true);
    store.edit(key, "界".repeat(22000), [], true);
    assert.equal(await store.flush(key), false);
    assert.equal(errors.length, 1);
    assert.equal(errors[0].code, "draft_too_large");
    assert.equal(puts, 0);
    store.select("");
    store.select(key);
    assert.equal(errors.length, 2, "the unsaved error survives navigation");
    await new Promise((resolve) => setTimeout(resolve, 700));
    assert.equal(errors.length, 2, "oversized text is not retried on a timer");
    store.edit(key, "smaller draft", [], true);
    assert.equal(await store.flush(key), true);
    assert.equal(saved, "smaller draft");
    assert.equal(puts, 1);
  } finally {
    globalThis.window = originalWindow;
    globalThis.fetch = originalFetch;
  }
});

test("a failed send restores its text and images ahead of a newer saved draft", async () => {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  const firstImage = "a".repeat(32);
  const nextImage = "b".repeat(32);
  let revision = 1;
  let saved = { text: "", attachments: [] };
  globalThis.window = { setTimeout, clearTimeout, addEventListener() {} };
  globalThis.fetch = async (_path, options) => {
    if (options.method === "GET")
      return Response.json({ ok: true, data: { ...saved, revision } });
    assert.equal(options.method, "PUT");
    const body = JSON.parse(options.body);
    assert.equal(body.revision, revision);
    saved = { text: body.text, attachments: body.attachments,
      run_admission_uncertain: body.run_admission_uncertain };
    revision += 1;
    return Response.json({ ok: true, data: { ...saved, revision } });
  };
  try {
    const store = createDraftStore({ onRestore() {}, onError: (error) => {
      throw error;
    }, onSaved() {} });
    const key = "default/qa";
    store.select(key);
    await store.flush(key);
    store.edit(key, "submitted", [firstImage], true);
    store.clear(key);
    store.edit(key, "next draft", [nextImage], true);
    const recovered = store.restoreUnsent(key, "submitted", [firstImage]);
    assert.deepEqual(recovered, {
      text: "submitted\n\nnext draft",
      attachments: [firstImage, nextImage], merged: true,
    });
    assert.equal(await store.flush(key), true);
    assert.deepEqual(saved, {
      text: recovered.text, attachments: recovered.attachments,
      run_admission_uncertain: false,
    });
    assert.equal(store.clearIfMatches(key, "submitted", [firstImage]), false);
    store.edit(key, "submitted", [nextImage], true);
    assert.deepEqual(store.restoreUnsent(key, "submitted", [firstImage]), {
      text: "submitted", attachments: [firstImage, nextImage], merged: true,
    });
    assert.equal(await store.flush(key), true);
  } finally {
    globalThis.window = originalWindow;
    globalThis.fetch = originalFetch;
  }
});

test("an edit before draft loading keeps the persisted run review guard", async () => {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  let releaseGet;
  const getReady = new Promise((resolve) => { releaseGet = resolve; });
  let saved;
  globalThis.window = { setTimeout, clearTimeout, addEventListener() {} };
  globalThis.fetch = async (_path, options) => {
    if (options.method === "GET") {
      await getReady;
      return Response.json({ ok: true, data: { revision: 1,
        text: "prior text", attachments: [], run_admission_uncertain: true } });
    }
    saved = JSON.parse(options.body);
    return Response.json({ ok: true, data: { revision: 2, ...saved } });
  };
  try {
    const key = "default/slow-load";
    let restored;
    const store = createDraftStore({
      onRestore(text, _attachments, uncertain) {
        restored = { text, uncertain };
      },
      onError(error) { throw error; },
      onSaved() {},
    });
    store.select(key);
    assert.equal(store.isLoaded(key), false);
    store.edit(key, "typed during load", [], true);
    releaseGet();
    assert.equal(await store.flush(key), true);
    assert.equal(store.isLoaded(key), true);
    assert.deepEqual(restored, { text: "typed during load", uncertain: true });
    assert.equal(saved.text, "typed during load");
    assert.equal(saved.run_admission_uncertain, true);
  } finally {
    globalThis.window = originalWindow;
    globalThis.fetch = originalFetch;
  }
});

test("an uncertain run blocks retry across draft edits and reload until acknowledged", async () => {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  let revision = 0;
  let saved = { text: "", attachments: [], run_admission_uncertain: false };
  globalThis.window = { setTimeout, clearTimeout, addEventListener() {} };
  globalThis.fetch = async (_path, options) => {
    if (options.method === "GET")
      return Response.json({ ok: true, data: { ...saved, revision } });
    const body = JSON.parse(options.body);
    assert.equal(body.revision, revision);
    saved = { text: body.text, attachments: body.attachments,
      run_admission_uncertain: body.run_admission_uncertain };
    revision += 1;
    return Response.json({ ok: true, data: { ...saved, revision } });
  };
  try {
    const key = "default/review";
    const first = createDraftStore({ onRestore() {}, onError: (error) => {
      throw error;
    }, onSaved() {} });
    first.select(key);
    first.edit(key, "maybe sent", [], true);
    first.setRunUncertain(key, true);
    assert.equal(await first.flush(key), true);
    // select() may finish its initial GET after the edit and schedule a flush.
    await new Promise((resolve) => setTimeout(resolve, 10));
    assert.equal(await first.flush(key), true);
    assert.equal(saved.run_admission_uncertain, true);
    const restored = createDraftStore({ onRestore() {}, onError: (error) => {
      throw error;
    }, onSaved() {} });
    restored.select(key);
    assert.equal(await restored.flush(key), true);
    assert.equal(restored.isRunUncertain(key), true);
    restored.edit(key, "new draft", [], true);
    assert.equal(await restored.flush(key), true);
    assert.equal(saved.run_admission_uncertain, true);
    restored.setRunUncertain(key, false);
    assert.equal(await restored.flush(key), true);
    assert.equal(saved.run_admission_uncertain, false);
  } finally {
    globalThis.window = originalWindow;
    globalThis.fetch = originalFetch;
  }
});

test("an in-flight submission survives refresh while the next draft stays editable", async () => {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  const firstImage = "a".repeat(32);
  let revision = 0;
  let saved = { text: "", attachments: [], run_admission_uncertain: false,
    submissions: [] };
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
    const key = "default/durable";
    const first = createDraftStore({ onRestore() {}, onError(error) {
      throw error;
    }, onSaved() {} });
    first.select(key);
    assert.equal(await first.ensureLoaded(key), true);
    const submission = { id: "f".repeat(32), text: "first message",
      attachments: [firstImage], interrupt: false };
    const posting = { ...submission, state: "posting" };
    first.edit(key, submission.text, submission.attachments);
    assert.equal(first.stageSubmission(key, submission), true);
    assert.equal(first.stageSubmission(key, submission), false);
    assert.equal(await first.flush(key), true);
    assert.deepEqual(saved.submissions, [posting]);
    assert.equal(first.clearIfMatches(key, submission.text,
      submission.attachments), true);
    first.edit(key, "next draft", []);
    assert.equal(await first.flush(key), true);
    assert.equal(saved.text, "next draft");
    assert.deepEqual(saved.submissions, [posting]);

    let restored;
    const reopened = createDraftStore({ onRestore(text, attachments,
      uncertainRun, pending) {
      restored = { text, attachments, uncertainRun, pending };
    }, onError(error) { throw error; }, onSaved() {} });
    reopened.select(key);
    assert.equal(await reopened.ensureLoaded(key), true);
    assert.deepEqual(restored, { text: "next draft", attachments: [],
      uncertainRun: false, pending: posting });
    assert.equal(reopened.clearSubmission(key, submission.id), true);
    assert.equal(await reopened.flush(key), true);
    assert.equal(saved.text, "next draft");
    assert.deepEqual(saved.submissions, []);
  } finally {
    globalThis.window = originalWindow;
    globalThis.fetch = originalFetch;
  }
});

test("multiple pending submissions keep their order and state across reload", async () => {
  const originalWindow = globalThis.window;
  const originalFetch = globalThis.fetch;
  let revision = 0;
  let saved = { text: "", attachments: [], run_admission_uncertain: false,
    submissions: [] };
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
    const first = { id: "1".repeat(32), text: "first",
      attachments: [], interrupt: false, state: "prepared" };
    const second = { id: "2".repeat(32), text: "second",
      attachments: [], interrupt: true, state: "prepared" };
    const store = createDraftStore({ onRestore() {}, onError(error) {
      throw error;
    }, onSaved() {} });
    store.select(key);
    assert.equal(await store.ensureLoaded(key), true);
    assert.equal(store.appendSubmission(key, first), true);
    assert.equal(store.appendSubmission(key, second), true);
    assert.equal(store.appendSubmission(key, second), false);
    assert.equal(await store.flush(key), true);
    assert.deepEqual(saved.submissions, [first, second]);

    const reopened = createDraftStore({ onRestore() {}, onError(error) {
      throw error;
    }, onSaved() {} });
    reopened.select(key);
    assert.equal(await reopened.ensureLoaded(key), true);
    assert.deepEqual(reopened.submissions(key), [first, second]);
    assert.equal(reopened.updateSubmissionState(key, first.id, "posting"), true);
    assert.equal(await reopened.flush(key), true);
    assert.deepEqual(saved.submissions, [{ ...first, state: "posting" }, second]);
    assert.equal(reopened.clearSubmission(key, first.id), true);
    assert.equal(await reopened.flush(key), true);
    assert.deepEqual(saved.submissions, [second]);
  } finally {
    globalThis.window = originalWindow;
    globalThis.fetch = originalFetch;
  }
});
