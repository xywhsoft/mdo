import assert from "node:assert/strict";
import test from "node:test";
import { editPrompt, promptFields, splitPrompt, portableId, newMcp, prepareMcpCredentials, mcpImportDocuments, expandCommand } from "../app/web/js/features/settings/extension-formats.js";

test("editing common fields preserves unrelated Skill metadata and references", () => {
  const source = '---\nname: research\ndescription: >\n  Inspect before\n  editing.\nmetadata:\n  author: someone\nallowed-tools: Read Bash\n---\nRead references/guide.md.\n';
  const fields = promptFields(source);
  assert.equal(fields.description, "Inspect before editing.");
  const edited = editPrompt(source, { name: "research", description: "Changed", prompt: fields.prompt });
  assert.match(edited, /metadata:\n  author: someone/);
  assert.match(edited, /allowed-tools: Read Bash/);
  assert.equal(promptFields(edited).prompt, "Read references/guide.md.\n");
  assert.equal(splitPrompt('literal prompt').body, 'literal prompt');
  assert.throws(() => splitPrompt('---\nname: broken'));
});
test("portable IDs prevent cross-platform special path names", () => {
  for (const value of ["research", "my-tool", "review.v2", "a_1"]) assert.equal(portableId(value), true, value);
  for (const value of ["../a", "x/y", "CON", "con", "con.txt", "lpt1.md", "a..b", ".hidden", "x.", "foo ", "x:y", "a".repeat(65)]) assert.equal(portableId(value), false, value);
});
test("standard MCP imports isolate credentials and reject incompatible transports", () => {
  const [stdio, http] = mcpImportDocuments(JSON.stringify({ mcpServers: {
    local: { command: "npx", args: ["-y", "package"], env: { TOKEN: "test-token" } },
    hosted: { url: "https://example.invalid/mcp", headers: { Authorization: "Bearer test" } },
  } }));
  assert.equal(JSON.parse(stdio.content).transport.program, "npx");
  assert.deepEqual(stdio.secrets, ["test-token"]);
  assert.equal(stdio.content.includes("test-token"), false);
  assert.equal(JSON.parse(stdio.content).transport.environment[0].secret_ref, "input:0");
  assert.deepEqual(http.secrets, ["Bearer test"]);
  assert.equal(JSON.parse(http.content).transport.type, "streamable-http");
  assert.throws(() => mcpImportDocuments('{"mcpServers":{"x":{"url":"http://example.invalid"}}}'));
  assert.throws(() => mcpImportDocuments('{"mcpServers":{"x":{"type":"sse","url":"https://example.invalid"}}}'));
});
test("command arguments are literal prompt text, including dollar replacement sequences", () => {
  assert.equal(expandCommand("Review $ARGUMENTS twice: $ARGUMENTS", "$& $1 `cmd`"), "Review $& $1 `cmd` twice: $& $1 `cmd`");
  assert.equal(expandCommand("Review changes", "app.c"), "Review changes\n\napp.c");
  assert.equal(expandCommand("Review $ARGUMENTS", ""), "Review ");
});
test("MCP form credentials seal at save time without replacing existing references", () => {
  const doc = newMcp('server');
  doc.transport.environment = { TOKEN: 'new key 中文' };
  const first = prepareMcpCredentials(doc, ['imported key']);
  assert.deepEqual(first.secrets, ['imported key', 'new key 中文']);
  assert.equal(JSON.parse(first.content).transport.environment[0].secret_ref, 'input:1');
  assert.deepEqual(doc.transport.environment, { TOKEN: 'new key 中文' });
  doc.transport = { type: 'streamable-http', endpoint: 'https://example.invalid/mcp',
    headers: { Authorization: 'Bearer new key' } };
  const next = prepareMcpCredentials(doc);
  assert.equal(next.content.includes('Bearer new key'), false);
  assert.deepEqual(next.secrets, ['Bearer new key']);
  doc.transport.headers = [{ name: 'Authorization', secret_ref: 'vault:'+'a'.repeat(64) }];
  assert.deepEqual(JSON.parse(prepareMcpCredentials(doc).content).transport.headers, doc.transport.headers);
});
