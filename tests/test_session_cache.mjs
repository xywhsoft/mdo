import assert from "node:assert/strict";
import test from "node:test";
import { createSessionCache } from "../app/web/js/features/chat/session-cache.js";

const data = id => ({ sessionId: id, events: [{ text: "你好" }], cursor: 4 });
test("LRU is bounded and revisiting promotes a session", () => {
  const cache = createSessionCache({ maxEntries: 2 });
  cache.put("device/a", data("a")); cache.put("device/b", data("b"));
  assert.equal(cache.get("device/a").cursor, 4);
  cache.put("device/c", data("c"));
  assert.equal(cache.get("device/b"), null);
  assert.equal(cache.usage().entries, 2);
  cache.clear(); assert.deepEqual(cache.usage(), { bytes: 0, entries: 0 });
});
test("incomplete or oversized reads never enter the cache", () => {
  const cache = createSessionCache({ maxEntryBytes: 512, maxBytes: 600 });
  cache.put("a", { ...data("a"), initializing: true });
  cache.put("b", { ...data("b"), syncing: true });
  cache.put("c", { ...data("c"), events: [{ text: "x".repeat(1000) }] });
  assert.equal(cache.usage().entries, 0);
  cache.put("a", data("a")); cache.put("b", data("b")); cache.put("c", data("c"));
  assert.ok(cache.usage().bytes <= 600);
  assert.notEqual(cache.get("c"), null);
});
