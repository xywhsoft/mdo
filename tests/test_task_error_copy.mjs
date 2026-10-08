import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { loadLocale } from "../app/web/js/i18n.js";
import { errorMessage } from "../app/web/js/utils/dom.js";
import { taskReadErrorMessage } from "../app/web/js/features/tasks/read-errors.js";

test("task failures describe the resource or action in all UI languages", async () => {
  const original = globalThis.fetch;
  const root = new URL("../app/web/", import.meta.url);
  globalThis.fetch = async path => Response.json(JSON.parse(readFileSync(
    new URL(String(path).replace(/^\//, ""), root), "utf8")));
  try {
    for (const locale of ["zh-CN", "en-US", "ru-RU"]) {
      await loadLocale(locale);
      for (const code of ["task_not_found", "task_unavailable", "tasks_unavailable",
        "task_output_unavailable", "task_cancel_failed", "runtime_unavailable"]) {
        const text = errorMessage({ code, message: "Internal task diagnostic" });
        assert.notEqual(text, "Internal task diagnostic", `${locale}: ${code}`);
        assert.ok(text.length > 10);
      }
      assert.equal(errorMessage({ code: "future_task_code", message: "Specific new cause" }),
        "Specific new cause", "unknown permanent causes remain explainable");
      const questions = { code: "asks_unavailable", status: 503, message: "Task questions could not be read" };
      const read = taskReadErrorMessage(questions);
      assert.match(read, locale === "zh-CN" ? /读取任务询问/ :
        locale === "en-US" ? /questions cannot be read/ : /прочитать вопросы/);
      assert.notEqual(read, errorMessage(questions), "answer-write failures keep their own meaning");
      assert.notEqual(taskReadErrorMessage({ status: 503, code: "unknown_transport", message: "Internal proxy prose" }),
        "Internal proxy prose", "exhausted transient reads have useful localized copy");
      assert.equal(taskReadErrorMessage({ status: 403, code: "permission_denied", message: "raw" }),
        errorMessage({ code: "permission_denied", message: "raw" }));
      assert.equal(taskReadErrorMessage({ status: 400, code: "future_task_code", message: "Specific new cause" }),
        "Specific new cause");
    }
  } finally { globalThis.fetch = original; }
});
