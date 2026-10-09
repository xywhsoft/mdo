import assert from "node:assert/strict";
import nodeTest from "node:test";
import { refreshMcp, reloadCatalog } from "../app/web/js/state/resources.js";
import { awaitResourceOperation } from "../app/web/js/state/resource-operation.js";

const reply = data => Response.json({ ok: true, data });
const running = { id: "job-1", kind: "mcp_refresh", target: "local", state: "running", terminal: false, message: "" };
const succeeded = { ...running, state: "succeeded", terminal: true };
const requestedId = "op-c-0123456789abcdef0123456789abcdef";
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
    options: { createId: () => requestedId, recoveryOptions: { now: () => time, random: () => 0, setTimer, clearTimer, eventTarget: page } },
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
        calls.push({ url, method: options.method, at: time, signal: options.signal, body: options.body });
        const result = await handler(url, options);
        // Existing observer cases accept the requested identity from the host.
        if (options.method === "POST" && options.body && result instanceof Response && result.ok) {
          const value = await result.clone().json();
          if (value.data?.id === running.id) return reply({ ...value.data, id: requestedId });
        }
        if (options.method === "GET" && result instanceof Response && result.ok) {
          const value = await result.clone().json();
          if (value.data?.id === running.id) return reply({ ...value.data, id: requestedId });
        }
        return result;
      });
    },
  };
}

test("accepted MCP operation quietly survives two 503 reads without another POST", async context => {
  const env = environment(context); let reads = 0;
  env.fetch((_url, options) => options.method === "POST" ? reply(running) : ++reads < 3 ? failed() : reply(succeeded));
  assert.deepEqual(await env.settle(refreshMcp("local", env.options)), { ...succeeded, id: requestedId });
  assert.equal(env.calls.filter(row => row.method === "POST").length, 1);
  const rows = env.calls.filter(row => row.method === "GET");
  assert.equal(rows.length, 3); assert.ok(rows.every(row => row.url === `/api/v1/operations/${requestedId}`));
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
  assert.deepEqual(await env.settle(reloadCatalog("modules", env.options)), { ...final, id: requestedId });
});
test("normal pending states use progressively slower polls and preserve one accepted ID", async context => {
  const env = environment(context); let reads = 0;
  env.fetch((_url, options) => reply(options.method === "POST" || ++reads < 3 ? running : succeeded));
  assert.deepEqual(await env.settle(refreshMcp("local", env.options)), { ...succeeded, id: requestedId });
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
    error.code === "resource_operation_unconfirmed" && error.operation?.id === requestedId);
  assert.equal(env.elapsed(), 60000); assert.ok(env.calls.filter(row=>row.method === "GET").length <= 6);
  assert.ok(env.calls.filter(row=>row.method === "GET").every(row => row.signal.aborted));
});
test("an unaccepted failing POST is never automatically replayed", async context => {
  const env = environment(context); env.fetch(() => Response.json({ ok: false,
    error: { code: "operation_unavailable", message: "Not queued" } }, { status: 503 }));
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

test("a lost startup reply is recovered by its requested ID with no repeated POST", async context => {
  const env = environment(context); let reads = 0;
  env.fetch((_url, options) => {
    if (options.method === "POST") throw new TypeError("connection reset after acceptance");
    if (++reads === 1) return Response.json({ ok: false, error: { code: "operation_not_found" } }, { status: 404 });
    if (reads === 2) return failed();
    return reply({ ...succeeded, id: requestedId });
  });
  assert.equal((await env.settle(refreshMcp("local", env.options))).id, requestedId);
  assert.equal(env.calls.filter(row => row.method === "POST").length, 1);
  assert.deepEqual(JSON.parse(env.calls[0].body), { operation_id: requestedId });
  assert.ok(env.calls.slice(1).every(row => row.url === `/api/v1/operations/${requestedId}`));
});

test("a hanging startup body is bounded and its late result cannot replace confirmation", async context => {
  const env = environment(context); let resolveBody;
  env.fetch((_url, options) => options.method === "POST" ? {
    ok: true, status: 202, json: () => new Promise(resolve => { resolveBody = resolve; })
  } : reply({ ...succeeded, id: requestedId }));
  assert.equal((await env.settle(refreshMcp("local", env.options))).id, requestedId);
  assert.ok(env.elapsed() >= 8000 && env.elapsed() < 60000);
  resolveBody({ ok: true, data: { ...succeeded, id: "late-wrong-id" } }); await env.flush();
  assert.deepEqual(env.calls.map(row => row.method), ["POST", "GET"]);
});

test("unknown acceptance ends accurately and retains a GET-only recheck identity", async context => {
  const env = environment(context);
  env.fetch((_url, options) => options.method === "POST" ? failed() :
    Response.json({ ok: false, error: { code: "operation_not_found" } }, { status: 404 }));
  await assert.rejects(env.settle(refreshMcp("local", env.options)), error =>
    error.code === "resource_operation_unconfirmed" && error.operation?.id === requestedId &&
    error.operation.acceptance_unknown === true);
  assert.equal(env.calls.filter(row => row.method === "POST").length, 1);
  assert.equal(env.calls.filter(row => row.method === "GET").length, 6);
  assert.ok(env.elapsed() < 60000);
});

test("startup and subsequent observation share one minute", async context => {
  const env = environment(context);
  env.fetch((_url, options) => ({ ok: true, status: options.method === "POST" ? 202 : 200,
    json: () => new Promise(() => {}) }));
  await assert.rejects(env.settle(refreshMcp("local", env.options)), { code: "resource_operation_unconfirmed" });
  assert.ok(env.elapsed() >= 50000 && env.elapsed() <= 60000);
  assert.equal(env.calls.filter(row => row.method === "POST").length, 1);
});

test("leaving during an uncertain startup preserves the identity for GET-only recheck", async context => {
  const env = environment(context), controller = new AbortController(); let received;
  env.fetch((_url, options) => options.method === "POST" ? {
    ok: true, status: 202, json: () => new Promise(() => {})
  } : reply({ ...succeeded, id: requestedId }));
  const pending = refreshMcp("local", { ...env.options, signal: controller.signal });
  const cancelled = assert.rejects(pending, error => {
    received = error.operation; return error.name === "AbortError" && received?.id === requestedId;
  });
  await env.flush(); controller.abort(); await cancelled;
  assert.equal(env.timers.size, 0);
  assert.equal((await env.settle(awaitResourceOperation(received, env.options))).id, requestedId);
  assert.deepEqual(env.calls.map(row => row.method), ["POST", "GET"]);
});

test("malformed acknowledgement recovers module reload through the same identity", async context => {
  const env = environment(context);
  const final = { ...succeeded, id: requestedId, kind: "module_reload", target: "" };
  env.fetch((_url, options) => options.method === "POST" ? new Response("{bad", { status: 202 }) : reply(final));
  assert.deepEqual(await env.settle(reloadCatalog("modules", env.options)), final);
  assert.deepEqual(env.calls.map(row => row.method), ["POST", "GET"]);
});

test("unrelated 404 after acceptance is final rather than evidence of a late submission", async context => {
  const env = environment(context);
  env.fetch((_url, options) => options.method === "POST" ? reply(running) :
    Response.json({ ok: false, error: { code: "operation_not_found" } }, { status: 404 }));
  await assert.rejects(env.settle(refreshMcp("local", env.options)), { status: 404 });
  assert.deepEqual(env.calls.map(row => row.method), ["POST", "GET"]);
});
