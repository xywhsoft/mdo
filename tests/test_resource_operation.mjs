import assert from "node:assert/strict";
import nodeTest from "node:test";
import { refreshMcp, reloadCatalog } from "../app/web/js/state/resources.js";

const reply = data => Response.json({ ok: true, data });
const running = { id: "job-1", kind: "mcp_refresh", target: "local", state: "running", terminal: false, message: "" };
const succeeded = { ...running, state: "succeeded", terminal: true };
const failed = (status = 503) => Response.json({ ok: false, error: { code: "service_unavailable", message: "temporary" } }, { status });
const flush = () => new Promise(resolve => setImmediate(resolve));
// Cancellation itself is under test; keep a real guard around the fake clock
// so the broken implementation cannot leave the test process waiting forever.
const test = (name, run) => nodeTest(name, { timeout: 1500 }, run);
function environment(context) {
  let time = 0, sequence = 0;
  const timers = new Map(), calls = [], page = new EventTarget();
  const setTimer = (fn, delay) => { timers.set(++sequence, { fn, delay, at: time + delay }); return sequence; };
  const clearTimer = id => timers.delete(id);
  const before = globalThis.window;
  page.setTimeout = setTimer; globalThis.window = page;
  context.after(() => { globalThis.window = before; });
  return { timers, calls, page, elapsed: () => time, flush,
    options: { recoveryOptions: { now: () => time, random: () => 0, setTimer, clearTimer, eventTarget: page } },
    async settle(promise) {
      let done = false, result, failure;
      promise.then(value => { done = true; result = value; }, error => { done = true; failure = error; });
      for (let count = 0; !done && count < 1000; count++) {
        await flush(); if (done) break;
        assert.ok(timers.size, "pending observations must own a finite deadline");
        const [id, timer] = [...timers].sort((a, b) => a[1].at - b[1].at)[0];
        timers.delete(id); time = timer.at; timer.fn();
      }
      assert.ok(done, "observation must settle"); assert.equal(timers.size, 0);
      if (failure) throw failure; return result;
    },
    fetch(handler) {
      context.mock.method(globalThis, "fetch", async (url, options) => {
        calls.push({ url, method: options.method, at: time, signal: options.signal });
        return handler(url, options);
      });
    },
  };
}

test("accepted MCP operation quietly survives two 503 reads without another POST", async context => {
  const env = environment(context); let reads = 0;
  env.fetch((_url, options) => options.method === "POST" ? reply(running) : ++reads < 3 ? failed() : reply(succeeded));
  assert.deepEqual(await env.settle(refreshMcp("local", env.options)), succeeded);
  assert.equal(env.calls.filter(row => row.method === "POST").length, 1);
  const rows = env.calls.filter(row => row.method === "GET");
  assert.equal(rows.length, 3); assert.ok(rows.every(row => row.url === "/api/v1/operations/job-1"));
  assert.ok(rows[1].at - rows[0].at >= 500 && rows[2].at - rows[1].at >= 1000);
});
test("already terminal failures and cancellations are never reported as success", async context => {
  const env = environment(context); let state = "failed";
  env.fetch(() => reply({ ...running, terminal: true, state, message: "fixture failed" }));
  await assert.rejects(env.settle(refreshMcp("local", env.options)), { code: "resource_operation_failed" });
  state = "cancelled";
  await assert.rejects(env.settle(refreshMcp("local", env.options)), { code: "resource_operation_cancelled" });
  assert.equal(env.calls.filter(row => row.method === "GET").length, 0);
});
test("a different operation ID or server cannot satisfy the original request", async context => {
  const env = environment(context); let wrong = { ...succeeded, id: "job-2" };
  env.fetch((_url, options) => reply(options.method === "POST" ? running : wrong));
  await assert.rejects(env.settle(refreshMcp("local", env.options)), { code: "resource_operation_invalid" });
  wrong = { ...succeeded, target: "other-server" };
  await assert.rejects(env.settle(refreshMcp("local", env.options)), { code: "resource_operation_invalid" });
});
test("catalog reload returns the confirmed terminal result rather than its queued snapshot", async context => {
  const env = environment(context), initial = { ...running, kind: "module_reload", target: "" };
  const final = { ...initial, state: "succeeded", terminal: true };
  env.fetch((_url, options) => reply(options.method === "POST" ? initial : final));
  assert.deepEqual(await env.settle(reloadCatalog("modules", env.options)), final);
});
test("normal pending states use progressively slower polls and preserve one accepted ID", async context => {
  const env = environment(context); let reads = 0;
  env.fetch((_url, options) => reply(options.method === "POST" || ++reads < 3 ? running : succeeded));
  assert.deepEqual(await env.settle(refreshMcp("local", env.options)), succeeded);
  const rows = env.calls.filter(row => row.method === "GET");
  assert.deepEqual(rows.map(row => row.at), [250, 750, 1750]);
});
test("cancellation releases an abort-ignoring body without cancelling the native operation", async context => {
  const env = environment(context), controller = new AbortController(); let resolveBody;
  env.fetch((_url, options) => options.method === "POST" ? reply(running) :
    { ok: true, status: 200, json: () => new Promise(resolve => { resolveBody = resolve; }) });
  const pending = refreshMcp("local", { ...env.options, signal: controller.signal });
  const cancelled = assert.rejects(pending, { name: "AbortError" });
  await env.flush(); const [id, timer] = [...env.timers].sort((a,b)=>a[1].at-b[1].at)[0];
  env.timers.delete(id); timer.fn(); await env.flush();
  controller.abort(); await cancelled; assert.equal(env.timers.size, 0);
  resolveBody({ ok: true, data: succeeded }); await env.flush();
  assert.deepEqual(env.calls.map(row => row.method), ["POST", "GET"]);
});
test("page exit cancels a retry wait without another request", async context => {
  const env = environment(context);
  env.fetch((_url, options) => options.method === "POST" ? reply(running) : failed());
  const pending = refreshMcp("local", env.options), cancelled = assert.rejects(pending, { name: "AbortError" });
  await env.flush(); const [id, timer] = [...env.timers].sort((a,b)=>a[1].at-b[1].at)[0];
  env.timers.delete(id); timer.fn(); await env.flush();
  env.page.dispatchEvent(new Event("pagehide")); await cancelled;
  assert.equal(env.timers.size, 0); assert.deepEqual(env.calls.map(row => row.method), ["POST", "GET"]);
});
test("hanging status bodies end within the shared minute as unconfirmed, not failed", async context => {
  const env = environment(context);
  env.fetch((_url, options) => options.method === "POST" ? reply(running) :
    { ok: true, status: 200, json: () => new Promise(() => {}) });
  await assert.rejects(env.settle(refreshMcp("local", env.options)), error =>
    error.code === "resource_operation_unconfirmed" && error.operation?.id === "job-1");
  assert.equal(env.elapsed(), 60000); assert.ok(env.calls.filter(row=>row.method === "GET").length <= 6);
  assert.ok(env.calls.filter(row=>row.method === "GET").every(row => row.signal.aborted));
});
test("an unaccepted failing POST is never automatically replayed", async context => {
  const env = environment(context); env.fetch(() => failed());
  await assert.rejects(env.settle(refreshMcp("local", env.options)), { status: 503 });
  assert.deepEqual(env.calls.map(row => row.method), ["POST"]);
});
test("target changes while accepting an operation cannot read a different device", async context => {
  const env = environment(context); let owner = "old";
  env.fetch((_url, options) => { if (options.method === "POST") owner = "new"; return reply(succeeded); });
  await assert.rejects(env.settle(refreshMcp("local", { ...env.options, epoch: () => owner })), { name: "AbortError" });
  assert.deepEqual(env.calls.map(row => row.method), ["POST"]);
});
test("an accepted reply must also identify the server that was actually requested", async context => {
  const env = environment(context); env.fetch(() => reply({ ...succeeded, target: "other-server" }));
  await assert.rejects(env.settle(refreshMcp("local", env.options)), error =>
    error.code === "resource_operation_invalid" && !error.operation);
  assert.deepEqual(env.calls.map(row => row.method), ["POST"]);
});
test("a permanent status refusal is final after one GET", async context => {
  const env = environment(context); env.fetch((_url, options) => options.method === "POST" ? reply(running) : failed(403));
  await assert.rejects(env.settle(refreshMcp("local", env.options)), { status: 403 });
  assert.deepEqual(env.calls.map(row => row.method), ["POST", "GET"]);
});
