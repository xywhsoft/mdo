import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { eventsToTimeline } from "../app/web/js/features/chat/timeline.js";
import { loadLocale, supportedLocales } from "../app/web/js/i18n.js";

const packs = Object.fromEntries(supportedLocales.map((locale) => [locale,
  JSON.parse(readFileSync(new URL(`../app/web/lang/${locale}.json`, import.meta.url), "utf8"))]));
const expected = {
  "zh-CN": ["会话历史已截断", "会话历史已清空"],
  "en-US": ["Session history truncated", "Session history cleared"],
  "ru-RU": ["История сеанса обрезана", "История сеанса очищена"],
};

function languageFetch(context) {
  context.mock.method(globalThis, "fetch", async (path) => {
    const locale = /^\/lang\/(.+)\.json$/.exec(path)?.[1];
    assert.ok(packs[locale], `unexpected language request ${path}`);
    return { ok: true, async json() { return packs[locale]; } };
  });
}

test("durable truncate and clear markers follow the current language without changing events", async (context) => {
  languageFetch(context);
  const events = [
    { kind: "history_truncated", event_id: 4, text: "会话历史已截断", time: 1000000 },
    { kind: "history_truncated", event_id: 5, text: "会话历史已清空", time: 2000000 },
    { kind: "history_truncated", event_id: 6, text: "", time: 3000000 },
    { kind: "history_truncated", event_id: 7, time: 4000000 },
  ];
  const original = structuredClone(events);
  for (const locale of [...supportedLocales, "en-US", "zh-CN"]) {
    await loadLocale(locale);
    const items = eventsToTimeline(events, true);
    assert.deepEqual(items.map((item) => item.text),
      [expected[locale][0], expected[locale][1], expected[locale][0], expected[locale][0]]);
    assert.deepEqual(items.map((item) => item.key),
      ["history-4", "history-5", "history-6", "history-7"]);
    assert.ok(items.every((item) => item.role === packs[locale]["timeline.history"]));
    assert.deepEqual(events, original);
  }
});

test("custom history notes and canonical-looking user, model, tool, and error text remain literal", async (context) => {
  languageFetch(context);
  const bodies = ["作者的历史说明 <script> stays literal", " 会话历史已截断 ",
    "会话历史已清空", "会话历史已截断", "会话历史已清空", "会话历史已截断", "会话历史已清空"];
  const events = [
    { kind: "history_truncated", event_id: 1, text: bodies[0] },
    { kind: "history_truncated", event_id: 2, text: bodies[1] },
    { kind: "agent_start", event_id: 3, run_id: 1, agent_depth: 0,
      schema_version: 3, user_message_sequence: 1, text: bodies[2] },
    { kind: "model_text_delta", event_id: 4, run_id: 1, text: bodies[3] },
    { kind: "tool_done", event_id: 5, run_id: 1, success: true, text: bodies[4] },
    { kind: "error", event_id: 6, run_id: 1, text: bodies[5] },
    { kind: "custom_event", event_id: 7, text: bodies[6] },
  ];
  for (const locale of supportedLocales) {
    await loadLocale(locale);
    assert.deepEqual(eventsToTimeline(events).map((item) => item.text), bodies);
  }
});

test("translated history boundaries retain the existing distinction from unexplained gaps", async (context) => {
  languageFetch(context);
  await loadLocale("en-US");
  const marker = { kind: "history_truncated", event_id: 8, text: "会话历史已清空" };
  assert.deepEqual(eventsToTimeline([marker], true).map((item) => item.text),
    ["Session history cleared"]);
  const items = eventsToTimeline([
    { kind: "agent_start", event_id: 7, text: "retained prompt" }, marker,
  ], true);
  assert.equal(items[0].key, "history-gap");
  assert.equal(items[0].text, "Earlier events are no longer in this record.");
  assert.equal(items.at(-1).text, "Session history cleared");
});
