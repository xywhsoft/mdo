import assert from "node:assert/strict";
import test from "node:test";

import { withSessionRuntime } from "../app/web/js/state/session-runtime.js";
import { selectRecovery, loadRecovery } from "../app/web/js/state/recovery.js";
import { startRun } from "../app/web/js/state/runs.js";

function deferred() {
  let resolve;
  const promise = new Promise((done) => { resolve = done; });
  return { promise, resolve };
}

test("a new run waits for navigation's recovery inspection", async () => {
  const inspection = deferred();
  const entered = deferred();
  const calls = [];
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async (path, options) => {
    const method = options.method;
    calls.push(method);
    if (method === "GET") {
      entered.resolve();
      await inspection.promise;
      return Response.json({ ok: true, data: { resume_required: false, total: 0 } });
    }
    assert.equal(method, "POST");
    return Response.json({ ok: true, data: { id: "run-1" } }, { status: 202 });
  };
  try {
    selectRecovery("default", "new-session");
    const loading = loadRecovery();
    await entered.promise;
    const run = startRun("default", "new-session", "hello");
    await Promise.resolve();
    assert.deepEqual(calls, ["GET"]);
    inspection.resolve();
    await Promise.all([loading, run]);
    assert.deepEqual(calls, ["GET", "POST"]);
    assert.equal((await run).id, "run-1");
  } finally {
    inspection.resolve();
    globalThis.fetch = originalFetch;
    selectRecovery("", "");
  }
});

test("a failed operation releases its session without blocking other sessions", async () => {
  const entered = deferred();
  const release = deferred();
  const first = withSessionRuntime("default", "a", async () => {
    entered.resolve();
    await release.promise;
    throw new Error("inspection failed");
  });
  await entered.promise;
  const second = withSessionRuntime("default", "a", async () => "next");
  const independent = withSessionRuntime("default", "b", async () => "other");
  assert.equal(await independent, "other");
  release.resolve();
  await assert.rejects(first, /inspection failed/);
  assert.equal(await second, "next");
});

test("a lost run response is reported as uncertain without replaying the POST", async () => {
  const originalFetch = globalThis.fetch;
  let posts = 0;
  globalThis.fetch = async () => {
    posts += 1;
    throw new TypeError("connection closed after acceptance");
  };
  try {
    await assert.rejects(startRun("default", "lost-response", "hello"),
      (error) => error.code === "network_error" &&
        error.runAdmissionUncertain === true);
    assert.equal(posts, 1);
  } finally { globalThis.fetch = originalFetch; }
});

test("an explicit run rejection keeps its ordinary failure status", async () => {
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async () => Response.json({ ok: false,
    error: { code: "session_busy", message: "Session is busy" } },
  { status: 409 });
  try {
    await assert.rejects(startRun("default", "busy-session", "hello"),
      (error) => error.code === "session_busy" &&
        error.runAdmissionUncertain !== true);
  } finally { globalThis.fetch = originalFetch; }
});
