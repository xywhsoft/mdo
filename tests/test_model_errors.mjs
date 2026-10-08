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
      const expectedTitle = locale === "zh-CN" ? "模型额度不足"
        : locale === "en-US" ? "Model allowance exhausted" : "Лимит модели исчерпан";
      assert.equal(eventsToTimeline(events).find(i => i.kind === "error").role, expectedTitle);
      const duplicate = eventsToTimeline([...events, { ...events[1], event_id: 3 }]);
      assert.equal(duplicate.filter(i => i.kind === "error").length, 1);
      assert.equal(duplicate.find(i => i.kind === "error").role, expectedTitle);
      assert.match(errorMessage({ code: "model_daily_token_limit", message: "raw" }), pattern);
      const limit = errorMessage({ code: "model_output_limit", message: "raw" });
      assert.notEqual(limit, "raw");
      assert.match(limit, locale === "zh-CN" ? /输出上限/ :
        locale === "ru-RU" ? /лимита вывода/ : /output limit/);
      assert.match(errorMessage({ code: "model_service_quota_exceeded", message: "raw" }),
        locale === "zh-CN" ? /提供方/ : locale === "ru-RU" ? /поставщика/ : /upstream/);
      assert.match(errorMessage({ code: "model_service_configuration", message: "raw" }),
        locale === "zh-CN" ? /管理员/ : locale === "ru-RU" ? /администратору/ : /administrator/);
      const compaction = eventsToTimeline([{ kind: "error", event_id: 10,
        run_id: "compact", model_error_kind: "context_compaction",
        text: "compaction summary failed; missing_sections=0x00003f00" }])[0];
      assert.match(compaction.text, locale === "zh-CN" ? /原始对话仍保留/ :
        locale === "ru-RU" ? /Исходный диалог сохранён/ : /original conversation is retained/);
      assert.equal(compaction.role, locale === "zh-CN" ? "上下文整理未完成" :
        locale === "ru-RU" ? "Сводка контекста не завершена" : "Context summary incomplete");
      assert.doesNotMatch(compaction.text, /missing_sections|0x00003f00/);
      assert.match(errorMessage({ code: "task_stop_unconfirmed", message: "raw" }),
        locale === "zh-CN" ? /无法确认任务是否已停止/ :
          locale === "ru-RU" ? /подтвердить остановку/ : /stop could not be confirmed/);
      assert.match(errorMessage({ code: "task_stop_context_changed", message: "raw" }),
        locale === "zh-CN" ? /停止操作没有继续/ :
          locale === "ru-RU" ? /отмена не продолжилась/ : /cancellation did not continue/);
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

test("stream recovery replaces only the interrupted turn's draft", () => {
  const events = [
    { kind: "agent_start", event_id: 1, run_id: "a", text: "request" },
    { kind: "model_start", event_id: 2, run_id: "a", agent_turn: 1, time: 1000000 },
    { kind: "model_text_delta", event_id: 3, run_id: "a", agent_turn: 1, text: "previous work" },
    { kind: "model_done", event_id: 4, run_id: "a", agent_turn: 1, time: 2000000 },
    { kind: "model_start", event_id: 5, run_id: "a", agent_turn: 2, time: 3000000 },
    { kind: "model_reasoning_delta", event_id: 6, run_id: "a", agent_turn: 2, text: "discarded thought" },
    { kind: "model_text_delta", event_id: 7, run_id: "a", agent_turn: 2, text: "discarded reply" },
    { kind: "model_start", event_id: 8, run_id: "a", agent_turn: 2, text: "stream_restart", time: 4000000 },
    { kind: "model_text_delta", event_id: 9, run_id: "a", agent_turn: 2, text: "replacement" },
    { kind: "model_done", event_id: 10, run_id: "a", agent_turn: 2, time: 5000000 },
    { kind: "agent_done", event_id: 11, run_id: "a", success: true },
  ];
  const items = eventsToTimeline(events);
  assert.equal(items.filter(i => i.kind === "error").length, 0);
  assert.equal(items.filter(i => i.kind === "reasoning").length, 0);
  assert.deepEqual(items.filter(i => i.kind === "assistant").map(i => i.text),
    ["previous work", "replacement"]);
});
