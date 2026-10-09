import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { loadLocale, supportedLocales } from "../app/web/js/i18n.js";
import { scheduleHistoryText } from "../app/web/js/features/schedules/schedule-panel.js";

test("failed schedule history reuses typed model explanations in all UI languages", async () => {
  const original = globalThis.fetch;
  const root = new URL("../app/web/", import.meta.url);
  globalThis.fetch = async path => Response.json(JSON.parse(readFileSync(
    new URL(String(path).replace(/^\//, ""), root), "utf8")));
  try {
    for (const locale of supportedLocales) {
      await loadLocale(locale);
      for (const [kind, result] of [["daily_token_limit", "failed"], ["insufficient_balance", "failed"],
        ["timeout", "timed_out"], ["output_limit", "limit"]]) {
        const value = scheduleHistoryText({ result, model_error_kind: kind, text: "private provider detail", text_truncated: true });
        assert.notEqual(value, "private provider detail…");
        assert.notEqual(value, "");
        const pack = JSON.parse(readFileSync(new URL(`lang/${locale}.json`, root), "utf8"));
        assert.equal(value, pack[`model.error.${kind}`]);
      }
      assert.notEqual(scheduleHistoryText({ result: "failed", model_error_kind: "daily_token_limit", text: "" }), "");
    }
  } finally { globalThis.fetch = original; }
});

test("success, cancellation and legacy or unknown failures preserve actual history text", () => {
  for (const result of ["succeeded", "cancelled"])
    assert.equal(scheduleHistoryText({ result, model_error_kind: "daily_token_limit", text: "original result" }), "original result");
  for (const kind of [undefined, "future_failure", "toString", "__proto__"])
    assert.equal(scheduleHistoryText({ result: "failed", model_error_kind: kind, text: "old detail", text_truncated: true }), "old detail…");
  assert.equal(scheduleHistoryText({ result: "failed", text: null }), "");
});
