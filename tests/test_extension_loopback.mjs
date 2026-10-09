import assert from "node:assert/strict";
import test from "node:test";
import { validateExtensionForm } from "../app/web/js/features/settings/extension-validation.js";
import { mcpImportDocuments } from "../app/web/js/features/settings/extension-formats.js";

const accepted = ["HTTP://LOCALHOST:3000/mcp", "http://127.0.0.1:3000/mcp", "http://[::1]:3000/mcp"];
const rejected = ["http://example.com/mcp", "http://192.168.1.1/mcp", "http://127.1/mcp",
  "http://2130706433/mcp", "http://0x7f000001/mcp", "http://localhost.evil/mcp",
  "http://127.0.0.1.evil/mcp", "http://user@localhost/mcp", "http://localhost:0/mcp",
  "http://localhost:65536/mcp", "http://localhost/mcp#fragment", "http://localhost\n/mcp",
  "https:example.com/mcp", "https://@example.com/mcp"];

test("MCP form accepts explicit loopback HTTP and rejects remote or ambiguous authorities", () => {
  const form = { name: "Local", description: "Loopback test", transport: "streamable-http", headers: "[]" };
  for (const endpoint of accepted)
    assert.doesNotThrow(() => validateExtensionForm("mcp", { ...form, endpoint }), endpoint);
  for (const endpoint of rejected)
    assert.throws(() => validateExtensionForm("mcp", { ...form, endpoint }),
      error => error.extensionValidation === "fieldHttps" && error.field === "endpoint" &&
        !error.message.includes(endpoint), endpoint);
});
test("MCP server import uses the same loopback transport rule as the form", () => {
  const source = url => JSON.stringify({ mcpServers: { local: { url } } });
  for (const endpoint of accepted) {
    const [entry] = mcpImportDocuments(source(endpoint));
    assert.equal(JSON.parse(entry.content).transport.endpoint, endpoint);
    assert.equal(JSON.parse(entry.content).transport.type, "streamable-http");
  }
  for (const endpoint of rejected) assert.throws(() => mcpImportDocuments(source(endpoint)), endpoint);
});
