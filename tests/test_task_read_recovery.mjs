import assert from "node:assert/strict";
import test from "node:test";
import { tasksStore, taskDetailStore, loadTasks, selectTask,
  clearSelectedTask, refreshSelectedTask } from "../app/web/js/state/tasks.js";

const transient = () => Response.json({ ok: false, error: {
  code: "tasks_unavailable", message: "Temporary read failure" } }, { status: 503 });
const tick = () => new Promise(resolve => setImmediate(resolve));
const empty = { data: "", start: 0, next: 0, dropped: false };

test("task list retains its snapshot and recovers transient reads without a visible error", async () => {
  const original = globalThis.fetch;
  const saved = { total: 1, items: [{ id: 9, state: "running", stop_requested: true }] };
  const final = { total: 1, items: [{ id: 9, state: "cancelled", stop_requested: true }] };
  let reads = 0;
  const signals = [], states = [];
  tasksStore.setData(saved);
  const unsubscribe = tasksStore.subscribe(state => states.push(state.status));
  globalThis.fetch = async (path, options) => {
    assert.equal(path, "/api/v1/tasks"); assert.equal(options.method, "GET");
    signals.push(options.signal);
    return ++reads === 1 ? transient() : Response.json({ ok: true, data: final });
  };
  try {
    const first = await loadTasks();
    assert.equal(first.status, "ready"); assert.equal(first.error, null);
    assert.equal(tasksStore.isPending(), true);
    assert.equal(first.data, saved);
    await loadTasks(); await loadTasks();
    assert.equal(reads, 1, "polling and live events must share the ongoing recovery budget");
    await new Promise(resolve => setTimeout(resolve, 650));
    assert.equal(tasksStore.get().status, "ready");
    assert.deepEqual(tasksStore.get().data, final);
    assert.equal(reads, 2); assert.ok(!states.includes("error"));
    assert.ok(signals.every(signal => signal instanceof AbortSignal));
  } finally { unsubscribe(); tasksStore.reset(); globalThis.fetch = original; }
});

test("failed task detail batches abort sibling reads and append recovered output only once", async () => {
  const original = globalThis.fetch, originalWindow = globalThis.window;
  globalThis.window = { atob: globalThis.atob };
  let outputReads = 0, abortedSibling = 0;
  const paths = [];
  globalThis.fetch = async (path, options) => {
    paths.push(path);
    if (path.includes("/output?")) {
      if (++outputReads === 2) return transient();
      return Response.json({ ok: true, data: { complete: false,
        stdout: outputReads === 1 ? { ...empty, data: "QQ==", next: 1 } :
          { ...empty, data: "Qg==", start: 1, next: 2 }, stderr: empty, result: empty } });
    }
    if (outputReads === 2 && path === "/api/v1/artifacts")
      return new Promise((_resolve, reject) => options.signal?.addEventListener("abort", () => {
        ++abortedSibling; reject(new DOMException("cancelled", "AbortError"));
      }, { once: true }));
    return Response.json({ ok: true, data: path.endsWith("/asks") || path.endsWith("/artifacts")
      ? { items: [] } : { id: 9, state: "running", terminal: false, stop_requested: true } });
  };
  try {
    assert.equal((await selectTask(9)).data.output.streams.stdout.text, "A");
    const failed = await refreshSelectedTask();
    assert.equal(failed.status, "ready"); assert.equal(failed.error, null);
    assert.equal(taskDetailStore.isPending(), true);
    await tick(); assert.equal(abortedSibling, 1);
    await refreshSelectedTask(); await refreshSelectedTask();
    assert.equal(outputReads, 2, "detail polling must not cancel an ongoing retry");
    await new Promise(resolve => setTimeout(resolve, 650));
    assert.equal(taskDetailStore.get().status, "ready");
    assert.equal(taskDetailStore.get().data.output.streams.stdout.text, "AB");
    assert.equal(outputReads, 3);
    assert.equal(paths.filter(path => path.includes("stdout=1&stderr=0&result=0")).length, 2);
  } finally { clearSelectedTask(); globalThis.fetch = original; globalThis.window = originalWindow; }
});

test("clearing the selected task cancels its queued read recovery", async () => {
  const original = globalThis.fetch;
  let calls = 0;
  globalThis.fetch = async () => { ++calls; return transient(); };
  try {
    await selectTask(9); clearSelectedTask();
    await new Promise(resolve => setTimeout(resolve, 650));
    assert.equal(calls, 4); assert.equal(taskDetailStore.get().status, "idle");
  } finally { clearSelectedTask(); globalThis.fetch = original; }
});

test("permanent task read refusal stays precise and has no automatic retry", async () => {
  const original = globalThis.fetch;
  let reads = 0;
  tasksStore.setData({ total: 1, items: [{ id: 9, state: "running" }] });
  globalThis.fetch = async () => { ++reads; return Response.json({ ok: false, error: {
    code: "permission_denied", message: "Task access denied" } }, { status: 403 }); };
  try {
    const result = await loadTasks();
    assert.equal(result.status, "error"); assert.equal(result.error.code, "permission_denied");
    assert.deepEqual(result.data.items, [], "denied reads must not retain a usable task snapshot");
    assert.equal(result.updatedAt, 0, "the discarded snapshot must not appear freshly read");
    await new Promise(resolve => setTimeout(resolve, 650));
    assert.equal(reads, 1);
  } finally { tasksStore.reset(); globalThis.fetch = original; }
});

test("an unreadable detail loses stale questions until a fresh read succeeds", async () => {
  const original = globalThis.fetch, originalWindow = globalThis.window;
  globalThis.window = { atob: globalThis.atob };
  let refusal = 0, release;
  globalThis.fetch = async path => {
    if (path === "/api/v1/tasks/9" && refusal) {
      if (refusal === 1) return Response.json({ ok: false,
        error: { code: "permission_denied", message: "denied" } }, { status: 403 });
      await new Promise(resolve => { release = resolve; });
    }
    return Response.json({ ok: true, data: path.includes("/output?")
      ? { stdout: empty, stderr: empty, result: empty }
      : path.endsWith("/asks") ? { items: [{ id: 11, question: "Continue?" }] }
      : path.endsWith("/artifacts") ? { items: [] }
      : { id: 9, state: "running", pending_questions: 1 } });
  };
  try {
    await selectTask(9); assert.equal(taskDetailStore.get().data.asks.items.length, 1);
    refusal = 1; const denied = await refreshSelectedTask();
    assert.equal(denied.status, "error"); assert.equal(denied.data, null);
    refusal = 2; const retry = refreshSelectedTask({ retry: true });
    assert.equal(taskDetailStore.get().status, "loading");
    assert.equal(taskDetailStore.get().data, null, "retry must not restore a denied snapshot before confirmation");
    release(); await retry;
    assert.equal(taskDetailStore.get().status, "ready");
    assert.equal(taskDetailStore.get().data.asks.items.length, 1);
  } finally { clearSelectedTask(); globalThis.fetch = original; globalThis.window = originalWindow; }
});

test("ordinary task refreshes keep final notices while explicit retry reads immediately", async () => {
  const original = globalThis.fetch, originalWindow = globalThis.window;
  globalThis.window = { atob: globalThis.atob };
  const error = Object.assign(new Error("temporarily unavailable"), { status: 503, code: "tasks_unavailable" });
  const paths = [];
  globalThis.fetch = async path => {
    paths.push(path);
    return Response.json({ ok: true, data: path.includes("/output?")
      ? { stdout: empty, stderr: empty, result: empty }
      : path.endsWith("/asks") || path.endsWith("/artifacts") || path === "/api/v1/tasks"
        ? { items: [], total: 0 } : { id: 9, state: "running", pending_questions: 0 } });
  };
  try {
    await selectTask(9); paths.length = 0;
    tasksStore.setError(error); taskDetailStore.setError(error);
    await loadTasks(); await refreshSelectedTask(); await loadTasks();
    assert.equal(paths.length, 0, "ordinary polling/live refreshes must respect the cooldown");
    assert.equal(tasksStore.get().error, error); assert.equal(taskDetailStore.get().error, error);
    await loadTasks({ retry: true }); await refreshSelectedTask({ retry: true });
    assert.equal(paths.length, 5); assert.equal(tasksStore.get().status, "ready");
    assert.equal(taskDetailStore.get().status, "ready");
    assert(paths.every(path => path.startsWith("/api/v1/")));
  } finally { clearSelectedTask(); tasksStore.reset(); globalThis.fetch = original; globalThis.window = originalWindow; }
});
