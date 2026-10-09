import assert from "node:assert/strict";
import test from "node:test";
import { createHistoryWindow } from "../app/web/js/features/chat/history-window.js";

const flush = () => new Promise(resolve => setImmediate(resolve));
function fixture({ height = 150, viewport = 400, increment = 300 } = {}) {
  const state = { enabled: true, followTail: true, data: { projectId: "qa", sessionId: "one",
    epoch: "a", firstLoadedTurn: 100, hasOlder: true } };
  const geometry = { height, viewport, top: 900 };
  let calls = 0, active = 0, concurrent = 0;
  const window = createHistoryWindow({ read: () => state, measure: () => geometry,
    async load() {
      ++calls; ++active; concurrent = Math.max(concurrent, active);
      state.data.loadingHistory = true;
      await flush();
      state.data.firstLoadedTurn -= 1; geometry.height += increment; geometry.top += increment;
      state.data.loadingHistory = false; --active;
    }, afterPaint: flush });
  return { state, geometry, window, calls: () => calls, concurrent: () => concurrent };
}

test("initial history fills 2.5 viewport heights with complete pages", async () => {
  const f = fixture(); f.window.consider(); await f.window.load();
  assert.equal(f.calls(), 3); assert.equal(f.geometry.height, 1050);
  assert.equal(f.concurrent(), 1); f.window.destroy();
});
test("one screen of headroom starts a single batch before the top is reached", async () => {
  const f = fixture({ height: 1400 });
  f.state.followTail = false; f.geometry.top = 450;
  f.window.consider(); await flush(); assert.equal(f.calls(), 0);
  f.geometry.top = 380; f.window.consider(); f.window.consider();
  await f.window.load();
  assert.equal(f.calls(), 4); assert.equal(f.geometry.height, 2600);
  assert.equal(f.concurrent(), 1); f.window.destroy();
});
test("long replies remain whole even when one page exceeds the height target", async () => {
  const f = fixture({ increment: 1800 });
  f.window.consider(); await f.window.load(); assert.equal(f.calls(), 1);
  assert.equal(f.geometry.height, 1950); f.window.destroy();
});
test("many short groups also fill the viewport buffer", async () => {
  const f = fixture({ height: 50, increment: 50 });
  f.window.consider(); await f.window.load();
  assert.equal(f.geometry.height, 1000); assert.equal(f.calls(), 19);
  f.window.destroy();
});
test("search, exhaustion and zero-height windows suppress automatic requests", async () => {
  for (const alter of [f => f.state.enabled = false, f => f.state.data.hasOlder = false,
    f => f.geometry.viewport = 0]) {
    const f = fixture(); alter(f); f.window.consider(); await flush();
    assert.equal(f.calls(), 0); f.window.destroy();
  }
});
test("a session change or view destruction ends the old batch", async () => {
  for (const alter of [f => f.state.data.sessionId = "two", f => f.window.destroy()]) {
    const f = fixture(); f.window.consider(); await Promise.resolve();
    alter(f); await f.window.load(); assert.equal(f.calls(), 1); f.window.destroy();
  }
});
test("non-advancing or invisible pages do not cause a fetch loop", async () => {
  const f = fixture({ increment: 0 });
  f.window.consider(); await f.window.load(); f.window.consider(); await flush();
  assert.equal(f.calls(), 1); f.window.destroy();
});
test("a taller viewport gets additional buffer without loading all history", async () => {
  const f = fixture(); f.window.consider(); await f.window.load();
  f.geometry.viewport = 650; f.window.resize(); await f.window.load();
  assert.equal(f.calls(), 5); assert.equal(f.geometry.height, 1650); f.window.destroy();
});
