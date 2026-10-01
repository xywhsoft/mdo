import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

import { loadLocale } from "../app/web/js/i18n.js";
import { formatSessionMarkdown, sessionMarkdownFilename } from
  "../app/web/js/features/sessions/session-export.js";

test("Markdown session export follows the selected language and keeps transcript data", async () => {
  const previousFetch = globalThis.fetch;
  const root = new URL("../app/web/", import.meta.url);
  globalThis.fetch = async (path) => Response.json(JSON.parse(readFileSync(
    new URL(String(path).replace(/^\//, ""), root), "utf8")));
  const time = 1_700_000_000_000_000;
  const session = { title: "", project_id: "default", id: "S1" };
  const transcript = {
    historyLost: true, limitReached: true, textTruncated: true,
    events: [
      { kind: "agent_start", event_id: 1, run_id: "r1", agent_depth: 0,
        user_message_sequence: 1, text: "See https://example.com",
        attachments: ["image-1"], time },
      { kind: "model_text_delta", event_id: 2, run_id: "r1", model: "Ling",
        text: "Done", time: time + 1000 },
      { kind: "model_done", event_id: 3, run_id: "r1", success: true,
        time: time + 2000 },
      { kind: "tool_start", event_id: 4, run_id: "r1", tool_call_id: "read-1",
        tool_name: "read", text: "```source```", time: time + 3000 },
      { kind: "tool_done", event_id: 5, run_id: "r1", tool_call_id: "read-1",
        success: true, text: "file body", time: time + 4000 },
    ],
  };
  const exportedAt = new Date("2024-01-02T03:04:05Z");
  try {
    await loadLocale("en-US");
    const english = formatSessionMarkdown(session, transcript, exportedAt);
    assert.equal(sessionMarkdownFilename(session), "mdo-Untitled task.md");
    assert.match(english, /Exported on .* · mdo · default\/S1/);
    assert.match(english, /## User · /);
    assert.match(english, /## Assistant · /);
    assert.match(english, /## Tool · read · /);
    assert.match(english, /Record notice: older events/);
    assert.match(english, /Record notice: the 4,096-event export limit/);
    assert.match(english, /Call:\n\n````\n```source```\n````/);
    assert.match(english, /Result:\n\n```\nfile body\n```/);
    assert.match(english, /Image attachments:\n\n- image-1/);
    assert.match(english, /Record notice: some images were omitted/);
    assert.match(english, /See https:\/\/example.com/);
    assert.doesNotMatch(english, /导出于|用户|图片附件/);
    assert.match(formatSessionMarkdown(session, { events: [] }, exportedAt),
      /_No conversation messages yet\._/);

    await loadLocale("ru-RU");
    const russian = formatSessionMarkdown(session, transcript, exportedAt);
    assert.equal(sessionMarkdownFilename(session), "mdo-Задача без названия.md");
    assert.match(russian, /Экспортировано .* · mdo · default\/S1/);
    assert.match(russian, /## Пользователь · /);
    assert.match(russian, /## Ассистент · /);
    assert.match(russian, /## Инструмент · read · /);
    assert.match(russian, /Вызов:\n\n````/);
    assert.match(russian, /Результат:\n\n```/);
    assert.match(russian, /Вложения с изображениями:\n\n- image-1/);
    assert.match(russian, /Примечание: некоторые изображения не включены/);
  } finally {
    await loadLocale("zh-CN");
    globalThis.fetch = previousFetch;
  }
});
