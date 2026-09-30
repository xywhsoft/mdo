import assert from "node:assert/strict";
import test from "node:test";
import { answerTaskAsk, selectTask, clearSelectedTask, taskDetailStore } from "../app/web/js/state/tasks.js";

test("task answer validates UTF-8 bytes and returns its acknowledgement before refresh", async () => {
  const original = globalThis.fetch;
  const calls = [];
  globalThis.fetch = async (path, options) => {
    calls.push({ path, options });
    return Response.json({ ok: true, data: { id: 7, answer: "回答" } });
  };
  try {
    await assert.rejects(answerTaskAsk(1, 7, "中".repeat(342)), /1024/);
    await assert.rejects(answerTaskAsk(1, 7, "  "), /./);
    await assert.rejects(answerTaskAsk(1, "0", "a"), /./);
    await assert.rejects(answerTaskAsk("../2", 7, "a"), /./);
    assert.equal(calls.length, 0);
    const acknowledged = await answerTaskAsk(1, 7, " 回答 ");
    assert.deepEqual(acknowledged, { id: 7, answer: "回答" });
    assert.equal(calls.length, 1);
    assert.equal(calls[0].path, "/api/v1/tasks/1/asks/7");
    assert.equal(calls[0].options.method, "PUT");
    assert.deepEqual(JSON.parse(calls[0].options.body), { answer: "回答" });
  } finally { globalThis.fetch = original; }
});

test("a slow question read for an earlier task cannot replace the newly selected task", async () => {
  const original = globalThis.fetch;
  const originalWindow = globalThis.window;
  globalThis.window = { atob: globalThis.atob };
  let release;
  const delayed = new Promise((resolve) => { release = resolve; });
  globalThis.fetch = async (path) => {
    if (path === "/api/v1/tasks/1/asks") return delayed;
    let data;
    if (path.endsWith("/asks")) data = { total: 1, items: [{ id: 22, question: "Task 2" }] };
    else if (path.includes("/output?")) {
      const chunk = { data: "", start: 0, next: 0, dropped: false };
      data = { stdout: chunk, stderr: chunk, result: chunk };
    } else if (path.includes("/events?") || path.endsWith("/artifacts")) data = { items: [] };
    else data = { id: path.endsWith("/1") ? 1 : 2, state: "running" };
    return Response.json({ ok: true, data });
  };
  try {
    const old = selectTask(1);
    await selectTask(2);
    assert.equal(taskDetailStore.get().status, "ready");
    release(Response.json({ ok: true, data: { total: 1, items: [{ id: 11, question: "Task 1" }] } }));
    await old;
    assert.equal(taskDetailStore.get().data.id, "2");
    assert.equal(taskDetailStore.get().data.asks.items[0].id, 22);
  } finally { globalThis.fetch = original; globalThis.window = originalWindow; clearSelectedTask(); }
});
