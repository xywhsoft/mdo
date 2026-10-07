import assert from "node:assert/strict";
import test from "node:test";

import { createDraftStore, projectDraftKey } from
  "../app/web/js/features/chat/draft-store.js";
import { createProjectDraftSelection } from
  "../app/web/js/features/chat/project-draft-selection.js";

function fixture(initial = {}) {
  const oldWindow = globalThis.window;
  const oldFetch = globalThis.fetch;
  const documents = new Map(Object.entries(initial));
  const route = { projectId: "alpha", sessionId: "" };
  let revalidations = 0;
  const failures = [];
  let migrated = 0;
  // Each assertion flushes explicitly; avoid a deferred autosave outliving
  // the test's temporary browser globals.
  globalThis.window = { setTimeout() { return 1; }, clearTimeout() {},
    addEventListener() {} };
  globalThis.fetch = async (url, options) => {
    const current = documents.get(url) ?? { revision: 0, text: "",
      attachments: [], submissions: [], new_task: null };
    if (options.method === "GET")
      return Response.json({ ok: true, data: current });
    const body = JSON.parse(options.body);
    if (body.revision !== current.revision)
      return Response.json({ ok: false, error: { code: "draft_conflict",
        message: "The draft changed in another window" } }, { status: 409 });
    const next = { ...body, revision: current.revision + 1 };
    documents.set(url, next);
    return Response.json({ ok: true, data: next });
  };
  const draftStore = createDraftStore({ onRestore() {}, onSaved() {},
    onError(error) { failures.push(error); } });
  const navigation = { get: () => route, preferredProject: () => "alpha",
    revalidate: () => { revalidations += 1; } };
  const selector = createProjectDraftSelection({ draftStore, navigation,
    onFailure: (error) => failures.push(error), onChange() {},
    onMigrated: () => { migrated += 1; } });
  return { documents, route, failures, draftStore, selector,
    get revalidations() { return revalidations; },
    get migrated() { return migrated; },
    restore() { globalThis.window = oldWindow; globalThis.fetch = oldFetch; } };
}

test("blank tasks keep independent portable drafts for each project", async () => {
  const env = fixture();
  try {
    assert.equal(await env.selector.restoreLegacy(), true);
    const alpha = env.selector.key();
    assert.equal(alpha, projectDraftKey("alpha"));
    env.draftStore.select(alpha);
    env.draftStore.edit(alpha, "alpha input", [], true);
    assert.equal(await env.draftStore.flush(alpha), true);
    env.route.projectId = "beta";
    const beta = env.selector.key();
    assert.equal(beta, projectDraftKey("beta"));
    env.draftStore.select(beta);
    assert.equal(await env.draftStore.ensureLoaded(beta), true);
    assert.equal(env.draftStore.text(beta), "");
    env.draftStore.edit(beta, "beta input", [], true);
    assert.equal(await env.draftStore.flush(beta), true);
    assert.equal(env.documents.get("/api/v1/projects/alpha/draft").text,
      "alpha input");
    assert.equal(env.documents.get("/api/v1/projects/beta/draft").text,
      "beta input");
    assert.equal(env.documents.has("/api/v1/draft"), false);
  } finally { env.restore(); }
});

test("an old global draft moves to its project before the global file clears", async () => {
  const env = fixture({ "/api/v1/draft": { revision: 1,
    text: "legacy input", attachments: [], submissions: [], new_task: null } });
  try {
    assert.equal(env.selector.key(), "");
    assert.equal(await env.selector.restoreLegacy(), true);
    assert.equal(env.documents.get("/api/v1/projects/alpha/draft").text,
      "legacy input");
    assert.equal(env.documents.get("/api/v1/draft").text, "");
    assert.equal(env.selector.key(), projectDraftKey("alpha"));
    assert.equal(env.selector.owner(), "");
    assert.equal(env.migrated, 1);
    assert.ok(env.revalidations > 0);
  } finally { env.restore(); }
});

test("a conflicting project draft leaves both copies and keeps project ownership", async () => {
  const env = fixture({
    "/api/v1/draft": { revision: 1, text: "legacy input",
      attachments: [], submissions: [], new_task: null },
    "/api/v1/projects/alpha/draft": { revision: 1, text: "new input",
      attachments: [], submissions: [] },
  });
  try {
    assert.equal(await env.selector.restoreLegacy(), false);
    assert.equal(env.documents.get("/api/v1/draft").text, "legacy input");
    assert.equal(env.documents.get("/api/v1/projects/alpha/draft").text,
      "new input");
    env.route.projectId = "beta";
    assert.equal(env.selector.owner(), "alpha");
    assert.equal(env.selector.key(), "");
    assert.equal(env.failures.length, 1);
  } finally { env.restore(); }
});

test("a cold global draft edited offline migrates durably after reconnect", async () => {
  const env = fixture(), onlineFetch = globalThis.fetch;
  globalThis.fetch = async () => { throw new TypeError("offline"); };
  try {
    env.draftStore.select("");
    assert.equal(await env.selector.restoreLegacy(), false);
    env.draftStore.edit("", "offline first task");
    globalThis.fetch = onlineFetch;
    env.draftStore.resumeSaves({ retryReads: true });
    assert.equal(await env.selector.restoreLegacy(), true);
    assert.equal(env.documents.get("/api/v1/projects/alpha/draft").text,
      "offline first task");
    assert.equal(env.documents.get("/api/v1/draft").text, "");
    assert.equal(env.selector.key(), projectDraftKey("alpha"));
    assert.equal(env.draftStore.hasUnsaved(), false);
  } finally { env.restore(); }
});
