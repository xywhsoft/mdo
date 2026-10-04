import assert from "node:assert/strict";
import test from "node:test";
import { selectTask, clearSelectedTask, refreshSelectedTask } from "../app/web/js/state/tasks.js";

test("task detail reads operational data without a debug event request", async () => {
  const originalFetch = globalThis.fetch;
  const originalWindow = globalThis.window;
  globalThis.window = { atob: globalThis.atob };
  const paths = [];
  let page = 0;
  const chunk = (text, start) => ({ data: Buffer.from(text).toString("base64"),
    start, next: start + Buffer.byteLength(text), dropped: false });
  globalThis.fetch = async (path) => {
    paths.push(path);
    let data;
    if (path === "/api/v1/tasks/42") data = { id: 42, state: "running", terminal: false };
    else if (path.includes("/output?")) {
      const empty = chunk("", 0);
      data = { complete: false, stdout: page ? chunk("完成", 6) : chunk("开始", 0),
        stderr: empty, result: empty };
    } else if (path === "/api/v1/artifacts") data = { items: [
      { id: 1, task_id: 42 }, { id: 2, task_id: 7 }] };
    else if (path === "/api/v1/tasks/42/asks") data = { items: [{ id: 3, question: "继续？" }] };
    else throw new Error(`unexpected request: ${path}`);
    return Response.json({ ok: true, data });
  };
  try {
    const first = await selectTask(42);
    assert.equal(first.status, "ready");
    assert.equal(first.data.output.streams.stdout.text, "开始");
    assert.equal(first.data.asks.items.length, 1);
    assert.deepEqual(first.data.artifacts, [{ id: 1, task_id: 42 }]);
    assert.equal("events" in first.data, false);
    page = 1;
    const second = await refreshSelectedTask();
    assert.equal(second.data.output.streams.stdout.text, "开始完成");
    assert.equal(paths.length, 8);
    assert(paths.some((path) => path.includes("stdout=6&stderr=0&result=0")));
    assert(paths.every((path) => !path.includes("/events")));
  } finally {
    clearSelectedTask();
    globalThis.fetch = originalFetch;
    globalThis.window = originalWindow;
  }
});
