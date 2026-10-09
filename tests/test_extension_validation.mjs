import assert from "node:assert/strict";
import test from "node:test";
import { validateExtensionId, requiredExtensionFields, validateExtensionForm, parseExtensionJson } from
  "../app/web/js/features/settings/extension-validation.js";

const rejected = (action, key, field) => assert.throws(action, error =>
  error.extensionValidation === key && error.field === field);
const agent = { name: "Helper", description: "Read files", prompt: "Inspect files." };
const mcp = { name: "Server", description: "Local server", transport: "stdio", program: "python", arguments: "[]", environment: "[]" };

test("missing and whitespace-only SubAgent fields are identified before serialization", () => {
  for (const field of ["name", "description", "prompt"])
    for (const value of ["", " \t\n"])
      rejected(() => validateExtensionForm("subagents", { ...agent, [field]: value }), "fieldRequired", field);
  assert.doesNotThrow(() => validateExtensionForm("subagents", agent));
});
test("default Agent instructions and Skill bodies remain optional; commands require instructions only", () => {
  for (const kind of ["agents", "skills"]) {
    assert.deepEqual(requiredExtensionFields(kind), ["name", "description"]);
    assert.doesNotThrow(() => validateExtensionForm(kind, { ...agent, prompt: "" }));
  }
  assert.doesNotThrow(() => validateExtensionForm("commands", { prompt: "Review $ARGUMENTS" }));
  rejected(() => validateExtensionForm("commands", { prompt: " " }), "fieldRequired", "prompt");
});
test("reserved command names and nonportable IDs cannot reach a write", () => {
  rejected(() => validateExtensionId("commands", "new"), "commandReserved", "extension_id");
  rejected(() => validateExtensionId("agents", "con"), "idInvalid", "extension_id");
  assert.doesNotThrow(() => validateExtensionId("agents", "new"));
  assert.doesNotThrow(() => validateExtensionId("commands", "review"));
});
test("native UTF-8 field limits are measured in bytes rather than character count", () => {
  assert.doesNotThrow(() => validateExtensionForm("agents", { ...agent, name: "中".repeat(85) }));
  rejected(() => validateExtensionForm("agents", { ...agent, name: "中".repeat(86) }), "fieldLength", "name");
  rejected(() => validateExtensionForm("mcp", { ...mcp, description: "a".repeat(2049) }), "fieldLength", "description");
});
test("MCP validation checks only the active transport and accepts either credential representation", () => {
  assert.doesNotThrow(() => validateExtensionForm("mcp", { ...mcp, endpoint: "invalid", headers: "broken" }));
  const http = { ...mcp, transport: "streamable-http", endpoint: "https://example.invalid/mcp", headers: '{"Authorization":"Bearer test"}', program: "", arguments: "broken" };
  assert.doesNotThrow(() => validateExtensionForm("mcp", http));
  assert.doesNotThrow(() => validateExtensionForm("mcp", { ...http, headers: '[{"name":"Authorization","secret_ref":"env:TOKEN"}]' }));
  rejected(() => validateExtensionForm("mcp", { ...mcp, program: " " }), "fieldRequired", "program");
  rejected(() => validateExtensionForm("mcp", { ...http, endpoint: "http://example.invalid/mcp" }), "fieldHttps", "endpoint");
});
test("MCP arguments and credential mistakes identify the field without reflecting its contents", () => {
  for (const args of ['{"x":"y"}', '[1]', '[null]'])
    rejected(() => validateExtensionForm("mcp", { ...mcp, arguments: args }), "fieldStringArray", "arguments");
  for (const environment of ['null', '42', '"secret"', '{"TOKEN":""}', '{"TOKEN":1}'])
    rejected(() => validateExtensionForm("mcp", { ...mcp, environment }), "fieldCredentials", "environment");
  try { parseExtensionJson('{"TOKEN":"sensitive-credential" trailing}', "environment"); assert.fail("expected error"); }
  catch (error) {
    assert.equal(error.extensionValidation, "fieldJson"); assert.equal(error.field, "environment");
    assert.equal(JSON.stringify(error).includes("sensitive-credential"), false);
    assert.equal(error.message.includes("sensitive-credential"), false);
  }
});
