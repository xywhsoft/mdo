import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { loadLocale } from "../app/web/js/i18n.js";
import { errorMessage } from "../app/web/js/utils/dom.js";
import { eventsToTimeline } from "../app/web/js/features/chat/timeline.js";

test("final model errors use the UI language and preserve unknown explanations", async () => {
  const original = globalThis.fetch;
  const root = new URL("../app/web/", import.meta.url);
  globalThis.fetch = async path => Response.json(JSON.parse(readFileSync(
    new URL(String(path).replace(/^\//, ""), root), "utf8")));
  try {
    for (const [locale, pattern] of [["zh-CN", /今日模型额度/],
      ["en-US", /Today's model allowance/], ["ru-RU", /Дневной лимит/]]) {
      await loadLocale(locale);
      const events = [{ kind: "agent_start", event_id: 1, run_id: "a", text: "user input" },
        { kind: "error", event_id: 2, run_id: "a", text: "raw provider prose",
          model_error_kind: "daily_token_limit", model_http_status: 429, model_attempts: 1 }];
      assert.match(eventsToTimeline(events).find(i => i.kind === "error").text, pattern);
      assert.match(errorMessage({ code: "model_daily_token_limit", message: "raw" }), pattern);
      assert.equal(eventsToTimeline([{ kind: "error", event_id: 9,
        run_id: "b", model_error_kind: "future_kind", text: "Specific new cause" }])[0].text,
      "Specific new cause");
    }
  } finally { globalThis.fetch = original; }
});

test("one execution has one final error, retains partial output, and survives cleanup", () => {
  const items = eventsToTimeline([
    { kind: "agent_start", event_id: 1, run_id: "a", text: "original input" },
    { kind: "model_text_delta", event_id: 2, run_id: "a", agent_turn: 1, text: "partial" },
    { kind: "error", event_id: 3, run_id: "a", text: "transport failed" },
    { kind: "error", event_id: 4, run_id: "a", text: "final actionable reason" },
    { kind: "agent_done", event_id: 5, run_id: "a", success: false },
  ]);
  assert.equal(items.filter(i => i.kind === "error").length, 1);
  assert.equal(items.find(i => i.kind === "error").text, "final actionable reason");
  assert.equal(items.filter(i => i.kind === "assistant").length, 1);
  assert.equal(items.find(i => i.kind === "assistant").text, "partial");
  assert.equal(items.find(i => i.kind === "assistant").state, "failed");
});

test("a child failure stays in task details while its parent continues", () => {
  const items = eventsToTimeline([
    { kind: "agent_start", event_id: 1, run_id: "parent", text: "delegate" },
    { kind: "agent_start", event_id: 2, run_id: "child", agent_depth: 1, text: "child task" },
    { kind: "error", event_id: 3, run_id: "child", agent_depth: 1, text: "child unavailable" },
    { kind: "model_text_delta", event_id: 4, run_id: "parent", agent_turn: 1, text: "parent recovered" },
    { kind: "agent_done", event_id: 5, run_id: "parent", success: true },
  ]);
  assert.equal(items.filter(i => i.kind === "error").length, 0);
  assert.equal(items.find(i => i.kind === "task" && i.state === "failed").text, "child unavailable");
  assert.equal(items.find(i => i.kind === "assistant").state, "done");
});
