import assert from "node:assert/strict";
import test from "node:test";
import { loadSessionTranscript, readCompleteSessionEventText } from "../app/web/js/state/sessions.js";
import { eventsToTimeline, resolveTimelineCopyText, resolveTimelineActionText } from "../app/web/js/features/chat/timeline.js";

const session = { project_id: "default", id: "snapshot" };
const owner = { projectId: session.project_id, sessionId: session.id };
const original = "a".repeat(64), replacement = "b".repeat(64);
const reply = data => Response.json({ ok: true, data });
const changed = () => Response.json({ ok: false,
  error: { code: "conversation_changed", message: "Conversation history changed" } }, { status: 409 });
const event = (id, text, truncated = false, kind = "model_text_delta") => ({
  event_id: id, kind, text, text_truncated: truncated, run_id: 1, agent_depth: 0 });
const page = (epoch, items, latest = items.at(-1)?.event_id ?? 0) => reply({ epoch,
  items, latest_event_id: latest, next_cursor: items.at(-1)?.event_id ?? 0, history_lost: false });

test("export quietly restarts once if history is replaced between pages", async context => {
  const paths = []; let first = true;
  context.mock.method(globalThis, "fetch", async path => {
    const url = new URL(path, "http://localhost"); paths.push(url.search);
    if (url.searchParams.get("after") === "0") {
      if (first) { first = false; return page(original, [event(1, "discarded turn")], 2); }
      return page(replacement, [event(1, "replacement turn"), event(2, "new reply")]);
    }
    return url.searchParams.get("epoch") === original ? changed()
      : page(replacement, [event(2, "new reply")]);
  });
  const transcript = await loadSessionTranscript(session);
  assert.deepEqual(transcript.events.map(row => row.text), ["replacement turn", "new reply"]);
  assert.deepEqual(paths, ["?after=0&limit=32", `?after=1&limit=32&epoch=${original}`, "?after=0&limit=32"]);
});

test("full-body history replacement cannot upgrade an old preview into another reply", async context => {
  let replaced = false;
  context.mock.method(globalThis, "fetch", async path => {
    const url = new URL(path, "http://localhost");
    if (url.searchParams.has("full_text")) {
      replaced = true;
      return url.searchParams.get("epoch") === original ? changed()
        : page(replacement, [event(1, "old prefix but another reply")]);
    }
    return replaced ? page(replacement, [event(1, "replacement only")])
      : page(original, [event(1, "old prefix", true)]);
  });
  const transcript = await loadSessionTranscript(session);
  assert.equal(transcript.textTruncated, false);
  assert.deepEqual(transcript.events.map(row => row.text), ["replacement only"]);
});

test("an inconsistent full-text response preserves the original export preview", async context => {
  context.mock.method(globalThis, "fetch", async path => new URL(path, "http://localhost").searchParams.has("full_text")
    ? page(original, [event(1, "unrelated complete body")])
    : page(original, [event(1, "original preview", true)]));
  const transcript = await loadSessionTranscript(session);
  assert.equal(transcript.textTruncated, true);
  assert.equal(transcript.events[0].text, "original preview");
});

test("repeated history replacement ends with one accurate failure and no transcript", async context => {
  let reads = 0;
  context.mock.method(globalThis, "fetch", async path => {
    reads++;
    return new URL(path, "http://localhost").searchParams.get("after") === "0"
      ? page(original, [event(1, "old")], 2) : changed();
  });
  await assert.rejects(loadSessionTranscript(session), error =>
    error.code === "conversation_changed" && error.status === 409 && /刷新/.test(error.message));
  assert.equal(reads, 4);
});

test("single-event full text verifies the supplied history epoch too", async context => {
  context.mock.method(globalThis, "fetch", async () => page(replacement, [event(1, "preview other reply")]));
  await assert.rejects(readCompleteSessionEventText("default", "snapshot", 1,
    "model_text_delta", { epoch: original }), { code: "conversation_changed" });
});

test("projected user, assistant and final messages keep their epoch for copy and edit", async context => {
  const rows = [
    { ...event(1, "preview", true, "agent_start"), user_message_sequence: 1 },
    event(2, "preview", true),
  ].map(row => ({ ...row, projection_epoch: original }));
  const items = eventsToTimeline(rows).filter(row => row.copySpans?.length);
  const final = eventsToTimeline([{ ...event(3, "preview", true, "agent_done"),
    projection_epoch: original, success: true }]).find(row => row.kind === "assistant");
  items.push(final);
  assert.equal(items.length, 3);
  context.mock.method(globalThis, "fetch", async path => {
    assert.equal(new URL(path, "http://localhost").searchParams.get("epoch"), original);
    return changed();
  });
  for (const item of items) {
    assert.equal(item.copySpans[0].epoch, original);
    assert.deepEqual(await resolveTimelineCopyText(item, owner), { text: "preview", complete: false });
    await assert.rejects(resolveTimelineActionText(item, owner), /完整消息/);
  }
});

test("export rejects missing or malformed history identities", async context => {
  for (const epoch of [undefined, "", "not-an-epoch"]) {
    context.mock.method(globalThis, "fetch", async () => page(epoch, [event(1, "body")]));
    await assert.rejects(loadSessionTranscript(session), { code: "invalid_response" });
    context.mock.restoreAll();
  }
});

test("append within the same history keeps the original export boundary", async context => {
  const paths = [];
  context.mock.method(globalThis, "fetch", async path => {
    const url = new URL(path, "http://localhost"); paths.push(url.search);
    return url.searchParams.get("after") === "0" ? page(original, [event(1, "first")], 2)
      : page(original, [event(2, "second"), event(3, "later input")], 3);
  });
  const transcript = await loadSessionTranscript(session);
  assert.deepEqual(transcript.events.map(row => row.text), ["first", "second"]);
  assert.equal(paths[1], `?after=1&limit=32&epoch=${original}`);
});
