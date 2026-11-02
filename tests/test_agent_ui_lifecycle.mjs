import assert from "node:assert/strict";
import test from "node:test";
import { createAgentLifecycle } from "../app/web/js/features/shell/agent-lifecycle.js";
const tick = () => new Promise(setImmediate);

test("late Agent registration opens the selected conversation once and ignores polling", async () => {
  const errors = [], calls = [], cleanups = [];
  const lifecycle = createAgentLifecycle(e => errors.push(e));
  const context = { agent: "agent.demo", project: "p", session: "one" };
  lifecycle.select(context);
  const unregister = lifecycle.register("agent.demo", (scope, signal) => {
    calls.push({ scope, signal }); return () => cleanups.push(scope.session);
  });
  await tick(); lifecycle.select({ ...context }); await tick(); assert.equal(calls.length, 1);
  lifecycle.select({ ...context, session: "two" }); await tick();
  assert.equal(calls[0].scope.session, "one"); assert.equal(calls[0].signal.aborted, true);
  assert.deepEqual(cleanups, ["one"]); assert.equal(calls[1].scope.session, "two");
  unregister(); await tick(); assert.deepEqual(cleanups, ["one", "two"]); assert.deepEqual(errors, []);
  lifecycle.destroy();
});

test("deselection aborts an unfinished callback and disposes its late result", async () => {
  const lifecycle = createAgentLifecycle(); let finish, signal, cleaned = 0;
  lifecycle.register("agent.demo", (_scope, s) => { signal = s; return new Promise(r => finish = r); });
  lifecycle.select({ agent: "agent.demo", session: "one" }); await tick();
  lifecycle.select(null); assert.equal(signal.aborted, true);
  finish(() => cleaned++); await tick(); assert.equal(cleaned, 1); lifecycle.destroy();
});

test("Agent callback may refuse switching without selecting or starting another Agent", async () => {
  const lifecycle = createAgentLifecycle(); let called = 0;
  const descriptor = () => { called++; };
  descriptor.beforeDeselected = async context => { assert.equal(context.session, "one"); throw Error("Finish the current team first"); };
  lifecycle.register("agent.demo", descriptor); lifecycle.select({ agent: "agent.demo", session: "one" }); await tick();
  await assert.rejects(lifecycle.beforeSelect("mdo.default"), /Finish/);
  assert.equal(called, 1); lifecycle.destroy();
});
