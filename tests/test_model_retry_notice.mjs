import assert from "node:assert/strict";
import test from "node:test";
import { readFileSync } from "node:fs";
import { eventsToTimeline } from "../app/web/js/features/chat/timeline.js";
import { modelRetryCopy, updateModelRetryCountdowns } from "../app/web/js/features/chat/model-retry.js";
import { loadLocale } from "../app/web/js/i18n.js";

const event = (kind, id, fields = {}) => ({ kind, event_id: id, run_id: "a",
  agent_turn: 2, time: id * 1e6, ...fields });
const retry = (id, fields = {}) => event("model_retry", id, {
  model_error_kind: "rate_limited", model_http_status: 429,
  model_attempts: 1, model_retry_after_ms: 8000, ...fields });
const notices = events => eventsToTimeline(events).filter(item => item.kind === "retry");

test("repeated failures update one notice; starting another attempt is not recovery", () => {
  const items = notices([event("agent_start", 1), event("model_start", 2),
    retry(3), retry(4, { model_attempts: 2 }), retry(5, { model_attempts: 2, model_retry_after_ms: 0 })]);
  assert.equal(items.length, 1);
  assert.equal(items[0].attempts, 2);
  assert.equal(items[0].retryWaiting, false);
  assert.equal(items[0].sourceEventId, 3);
  assert.equal(items[0].state, "running");
});

test("only nonempty model output or a completed model call removes its notice", () => {
  for (const kind of ["model_text_delta", "model_reasoning_delta"]) {
    assert.equal(notices([retry(1), event(kind, 2, { text: "" })]).length, 1);
    assert.equal(notices([retry(1), event(kind, 2, { text: "valid output" })]).length, 0);
  }
  assert.equal(notices([retry(1), event("model_done", 2, { success: true, model_tool_call_count: 1 })]).length, 0);
});

test("recovery preserves earlier partial output and another run's pending notice", () => {
  const items = eventsToTimeline([event("model_text_delta", 1, { text: "Existing partial answer" }),
    retry(2), retry(3, { run_id: "b" }), event("model_reasoning_delta", 4, { text: "Recovered" })]);
  assert.equal(items.find(item => item.kind === "assistant").text, "Existing partial answer");
  assert.deepEqual(items.filter(item => item.kind === "retry").map(item => item.runKey), ["b"]);
});

test("a later interruption can show the same notice again without retaining resolved copies", () => {
  const items = eventsToTimeline([retry(1), event("model_text_delta", 2, { text: "A" }),
    retry(3, { model_attempts: 2 }), retry(4, { model_attempts: 3 })]);
  assert.equal(items.filter(item => item.kind === "retry").length, 1);
  assert.equal(items.find(item => item.kind === "retry").attempts, 3);
  assert.equal(items.find(item => item.kind === "assistant").text, "A");
});

test("final failure or cancellation replaces pending progress without erasing partial text", () => {
  for (const kind of ["error", "agent_done"]) {
    const items = eventsToTimeline([event("agent_start", 1),
      event("model_text_delta", 2, { text: "Keep this" }), retry(3),
      event(kind, 4, { success: false, text: kind === "error" ? "Final failure" : "" })]);
    assert.equal(items.filter(item => item.kind === "retry").length, 0);
    assert.equal(items.find(item => item.kind === "assistant").text, "Keep this");
    assert.equal(items.find(item => item.kind === "assistant").state, kind === "error" ? "failed" : "cancelled");
  }
});

test("reused run IDs and late progress do not resurrect completed errors", () => {
  assert.equal(notices([retry(1), event("agent_done", 2, { success: true }), retry(3)]).length, 0);
  assert.equal(notices([event("agent_start", 1), retry(2), event("agent_start", 3)]).length, 0);
  assert.equal(notices([retry(1), event("model_text_delta", 2, { run_id: "b", text: "Unrelated" })]).length, 1);
});

test("a final error before output retains an explicit retry target for the original input", () => {
  const items = eventsToTimeline([event("agent_start", 1, { agent_depth: 0,
    user_message_sequence: 2, text: "Original question" }), retry(2),
    event("error", 3, { text: "Final failure" })]);
  assert.equal(items.find(item => item.kind === "error").retryPrompt.text, "Original question");
  assert.equal(items.find(item => item.kind === "error").retryPrompt.sequence, 2);
  assert.equal(items.some(item => item.kind === "retry"), false);
});

test("countdown updates text in place and does not restart its deadline", () => {
  const detail = { textContent: "" };
  const node = { dataset: { retryAt: "10000", retryWaiting: "true", retryAttempts: "2" },
    querySelector: () => detail };
  const container = { querySelectorAll: () => [node] };
  updateModelRetryCountdowns(container, 2000);
  assert.match(detail.textContent, /8/);
  updateModelRetryCountdowns(container, 5000);
  assert.match(detail.textContent, /5/);
  assert.equal(node.dataset.retryAt, "10000");
  assert.equal(updateModelRetryCountdowns({ querySelectorAll: () => [] }, 5000), 0);
});

test("rate limit notices and countdowns use the UI language, without provider prose", async () => {
  const fetch = globalThis.fetch;
  globalThis.fetch = async path => Response.json(JSON.parse(readFileSync(
    new URL(String(path).replace(/^\//, ""), new URL("../app/web/", import.meta.url)), "utf8")));
  try {
    for (const [locale, pattern] of [["zh-CN", /限流/], ["en-US", /rate limited/]]) {
      await loadLocale(locale);
      const copy = modelRetryCopy({ httpStatus: 429, errorKind: "rate_limited", attempts: 2,
        retryWaiting: true, retryAt: 10000, text: "private-provider-prose" }, 2000);
      assert.match(copy.title, pattern);
      assert.match(copy.title, /429/);
      assert.match(copy.detail, /8/);
      assert.doesNotMatch(JSON.stringify(copy), /private-provider-prose/);
    }
  } finally { globalThis.fetch = fetch; }
});
