import assert from "node:assert/strict";
import test from "node:test";

import { loadSessionTranscript } from "../app/web/js/state/sessions.js";
import { formatSessionMarkdown } from
  "../app/web/js/features/sessions/session-export.js";

const session = { project_id: "default", id: "S1", title: "Long transcript" };
const epoch = "a".repeat(64);

function envelope(data) { return Response.json({ ok: true, data: { epoch, ...data } }); }

test("Markdown export restores long user and assistant events before formatting", async () => {
  const previousFetch = globalThis.fetch;
  const requests = [];
  const visibleUser = "u".repeat(4096);
  const visibleAnswer = "a".repeat(4096);
  globalThis.fetch = async (path) => {
    const url = new URL(path, "http://localhost");
    requests.push(url.search);
    if (url.searchParams.get("full_text") === "1") {
      const after = Number(url.searchParams.get("after"));
      const event = after === 0
        ? { event_id: 1, kind: "agent_start", text: `${visibleUser} full user`,
          text_truncated: false }
        : { event_id: 2, kind: "model_text_delta",
          text: `${visibleAnswer} full answer`, text_truncated: false };
      return envelope({ items: [event] });
    }
    return envelope({ next_cursor: 2, latest_event_id: 2,
      history_lost: false, items: [
        { event_id: 1, kind: "agent_start", run_id: 7, agent_depth: 0,
          user_message_sequence: 1, text: visibleUser, text_truncated: true },
        { event_id: 2, kind: "model_text_delta", run_id: 7,
          text: visibleAnswer, text_truncated: true },
      ] });
  };
  try {
    const transcript = await loadSessionTranscript(session);
    assert.equal(transcript.textTruncated, false);
    assert.deepEqual(requests, ["?after=0&limit=32",
      `?after=0&limit=1&full_text=1&epoch=${epoch}`, `?after=1&limit=1&full_text=1&epoch=${epoch}`]);
    const markdown = formatSessionMarkdown(session, transcript);
    assert.match(markdown, /full user/);
    assert.match(markdown, /full answer/);
    assert.doesNotMatch(markdown, /部分事件正文未能完整导出/);
  } finally { globalThis.fetch = previousFetch; }
});

test("Markdown export keeps an honest warning when an exact event is unavailable", async () => {
  const previousFetch = globalThis.fetch;
  globalThis.fetch = async (path) => {
    const url = new URL(path, "http://localhost");
    if (url.searchParams.get("full_text") === "1")
      return envelope({ items: [{ event_id: 2, kind: "agent_start",
        text: "wrong neighbour", text_truncated: false }] });
    return envelope({ next_cursor: 1, latest_event_id: 1,
      history_lost: false, items: [{ event_id: 1, kind: "agent_start",
        run_id: 7, agent_depth: 0, user_message_sequence: 1,
        text: "visible prefix", text_truncated: true }] });
  };
  try {
    const transcript = await loadSessionTranscript(session);
    assert.equal(transcript.textTruncated, true);
    assert.equal(transcript.events[0].text, "visible prefix");
    const markdown = formatSessionMarkdown(session, transcript);
    assert.match(markdown, /visible prefix/);
    assert.doesNotMatch(markdown, /wrong neighbour/);
    assert.match(markdown, /部分事件正文未能完整导出/);
  } finally { globalThis.fetch = previousFetch; }
});
