import assert from "node:assert/strict";
import test from "node:test";
import { readFile } from "node:fs/promises";
import { refreshMcp } from "../app/web/js/state/resources.js";
import { loadLocale } from "../app/web/js/i18n.js";

const secret = "qa-private-credential-must-not-appear";
const operation = { id: "job-1", kind: "mcp_refresh", target: "local",
  state: "failed", terminal: true, message: `MCP protocol error: ${secret}` };
const packs = Object.fromEntries(await Promise.all(["zh-CN", "en-US", "ru-RU"].map(async locale =>
  [locale, JSON.parse(await readFile(new URL(`../app/web/lang/${locale}.json`, import.meta.url), "utf8"))])));
function environment(context) {
  let result = operation;
  const requests = [];
  context.mock.method(globalThis, "fetch", async (url, options = {}) => {
    if (url.startsWith("/lang/")) return Response.json(packs[url.split("/").at(-1).slice(0, -5)]);
    requests.push({ url, method: options.method });
    return Response.json({ ok: true, data: result });
  });
  return { requests, result(value) { result = { ...operation, ...value }; } };
}

test("terminal MCP failure reasons are localized and never reflect provider text", async context => {
  const env = environment(context);
  const reasons = { mcp_start_failed: "mcpStartFailed", mcp_timeout: "mcpTimeout", mcp_io: "mcpIo",
    mcp_busy: "mcpBusy", mcp_disabled: "mcpDisabled", mcp_protocol_version: "mcpProtocolVersion",
    mcp_invalid_response: "mcpInvalidResponse", mcp_remote_error: "mcpRemoteError", mcp_limit: "mcpLimit",
    mcp_memory: "mcpMemory", mcp_configuration: "mcpConfiguration", mcp_session_expired: "mcpSessionExpired",
    mcp_not_found: "mcpNotFound", mcp_status_unavailable: "mcpStatusUnavailable", mcp_failed: "connectionFailed" };
  for (const locale of Object.keys(packs)) {
    await loadLocale(locale);
    for (const [code, key] of Object.entries(reasons)) {
      env.result({ error_code: code, http_status: 0 });
      await assert.rejects(refreshMcp("local"), error => {
        assert.equal(error.code, "resource_operation_failed");
        assert.equal(error.message, packs[locale][`resource.${key}`]);
        assert.ok(!error.message.includes(secret)); return true;
      });
    }
  }
  assert.ok(env.requests.every(row => row.method === "POST"));
});

test("HTTP authentication, permission, limit and service failures have distinct final advice", async context => {
  const env = environment(context); await loadLocale("zh-CN");
  for (const [status, expected] of [[401, /凭据/], [403, /权限/], [429, /限流/],
      [503, /暂不可用/], [404, /地址/], [418, /418/]]) {
    env.result({ error_code: "mcp_http_error", http_status: status });
    await assert.rejects(refreshMcp("local"), error => {
      assert.match(error.message, expected); assert.ok(!error.message.includes(secret)); return true;
    });
  }
  assert.equal(env.requests.length, 6);
});

test("legacy or unknown error codes remain safe and do not start a retry", async context => {
  const env = environment(context); await loadLocale("zh-CN");
  for (const code of [undefined, "future_reason", "__proto__"]) {
    env.result({ error_code: code });
    await assert.rejects(refreshMcp("local"), error => {
      assert.equal(error.message, packs["zh-CN"]["resource.connectionFailed"]);
      assert.ok(!error.message.includes(secret)); return true;
    });
  }
  assert.equal(env.requests.length, 3);
});

test("malformed optional diagnostic metadata cannot become final advice", async context => {
  const env = environment(context); await loadLocale("zh-CN");
  for (const metadata of [{ error_code: { value: "mcp_start_failed" } },
      { error_code: "x".repeat(65) }, { http_status: "401" }, { http_status: 601 }, { http_status: -1 }]) {
    env.result(metadata);
    await assert.rejects(refreshMcp("local"), { code: "resource_operation_invalid" });
  }
});
