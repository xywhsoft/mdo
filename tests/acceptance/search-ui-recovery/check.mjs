import assert from "node:assert/strict";
import { readFileSync, writeFileSync } from "node:fs";
import { eventsToTimeline } from "../../../app/web/js/features/chat/timeline.js";

const read = name => JSON.parse(readFileSync(new URL(name, import.meta.url), "utf8"));
const proof = read("native-proof.json");
assert.deepEqual(proof.model_calls, {"search-retry": 2, "search-lost": 2,
  "search-quota": 2, "continue": 2, "search-restart": 2});
assert.deepEqual(proof.search_calls, {"search-retry": 3, "search-lost": 2,
  "search-quota": 1, "search-restart": 2});
assert.equal(proof.provider_dispatches, 3);
assert.equal(proof.quota, 3);
assert.deepEqual(proof.lifecycle.map(item => item.event), ["stopped", "restarted"]);
assert.notEqual(proof.lifecycle[0].pid, proof.lifecycle[1].pid);
for (const query of Object.keys(proof.search_calls)) {
  const calls = proof.search_requests.filter(item => item.query === query);
  assert.equal(calls.length, proof.search_calls[query]);
  assert.equal(new Set(calls.map(item => item.id)).size, 1);
  assert.equal(new Set(calls.map(item => item.body)).size, 1);
}
const originalRequests = proof.model_requests.filter(item => item.prompt === "search-restart");
assert.deepEqual(originalRequests.map(item => item.user_messages), [1, 1]);
assert.deepEqual(originalRequests.map(item => item.tool_results), [0, 1]);

const events = [read("events-0.json"), read("events-1.json")];
assert.ok(events.every(items => items.every(item => item.kind !== "error")));
const quota = events[0].filter(item => item.kind === "tool_done" && !item.success);
assert.equal(quota.length, 1);
assert.match(quota[0].text, /daily search quota is exhausted/);
assert.equal(events[1].filter(item => item.kind === "agent_start" && item.text === "search-restart").length, 1);
assert.equal(events[1].filter(item => item.kind === "tool_start").length, 2);
assert.equal(events[1].filter(item => item.kind === "tool_done").length, 1);
const projected = eventsToTimeline(events[1]);
const tools = projected.filter(item => item.kind === "tool");
assert.equal(tools.length, 1);
assert.equal(tools[0].state, "done");
assert.equal(tools[0].artifactEventId, events[1].find(item => item.kind === "tool_done").event_id);
assert.ok(projected.filter(item => item.kind === "assistant").every(item => item.state === "done"));

const automatic = read("automatic-browser.json");
assert.deepEqual(automatic.rows.filter(row => row.kind === "tool").map(row => row.state), ["done", "done", "failed"]);
const recovery = read("restarted-browser.json");
assert.equal(recovery.rows.filter(row => row.kind === "tool").length, 1);
assert.equal(recovery.rows.find(row => row.kind === "tool").state, "done");
assert.ok(recovery.preview[0].includes("完整预览 · SHA-256 d0bddf96b4c922c51cb9c7c460bc659490fcb22c86b77bd27165c1dc63ccbb8e"));
assert.ok(recovery.rows.some(row => row.text.includes("已处理上次未完成的工具调用。")));
assert.ok(recovery.rows.every(row => !row.text.includes("Pending tool call was retried")));
const continued = read("continued-browser.json");
assert.ok(continued.rows.some(row => row.kind === "assistant" && row.state === "done" && row.text.includes("SEARCH_UI_OK: continue")));
for (const browser of [automatic, recovery, continued]) {
  assert.equal(browser.draft, "");
  assert.ok(browser.rows.every(row => row.kind !== "error"));
  assert.ok(browser.rows.filter(row => row.kind === "tool").every(row => row.state !== "running"));
}
assert.ok(Object.values(read("queues.json")).every(queue => queue.items.length === 0));
const result = {passed: true, website_dispatches: 3, quota_used: 3,
  actual_process_restart: true, original_prompt_once_after_restart: true,
  recovered_tool_cards: 1, recovered_tool_completions: 1,
  recovered_markdown_preview: true, continued_after_quota_and_restart: true,
  final_queues_empty: true,
  final_error_events: 0, final_browser_error_cards: 0,
  scope: "Offline packed UI, native mdo HTTP/xwork and real unified website JWT/SQLite; model and provider HTTP are local fixtures."};
writeFileSync(new URL("checks.json", import.meta.url), JSON.stringify(result, null, 2) + "\n");
console.log(JSON.stringify(result));
