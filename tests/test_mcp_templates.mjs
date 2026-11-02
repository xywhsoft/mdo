import assert from "node:assert/strict";
import test from "node:test";
import { mcpVariantInputs, newMcpTemplate } from "../app/web/js/features/settings/mcp-template.js";

test("MCP connection forms request only inputs used by the selected variant", () => {
  const template = {
    inputs: [{ id: "key", secret: true }, { id: "endpoint" }, { id: "other" }],
    variants: [{ config: { transport: { endpoint: { input: "endpoint" },
      headers: [{ secret_ref: "input:key" }] } } }],
  };
  assert.deepEqual(mcpVariantInputs(template, template.variants[0]).map(i => i.id), ["key", "endpoint"]);
  assert.deepEqual(mcpVariantInputs(template, { config: {} }), []);
});

test("new MCP directories start with a public endpoint input and no device credentials", () => {
  const template = newMcpTemplate("service");
  assert.equal(template.format, "mdo.mcp-template.v1");
  assert.equal(template.id, "service");
  assert.equal(template.variants[0].default, true);
  assert.deepEqual(template.variants[0].platforms, ["any"]);
  assert.deepEqual(mcpVariantInputs(template, template.variants[0]), template.inputs);
  assert.equal(template.inputs[0].secret, false);
  assert.deepEqual(template.variants[0].config.transport.headers, []);
});
