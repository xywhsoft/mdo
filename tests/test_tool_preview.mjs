import assert from "node:assert/strict";
import test from "node:test";
import { toolCallPreview } from "../app/web/js/features/approvals/tool-preview.js";

test("native argv previews preserve argument boundaries and the working directory", () => {
  const preview = toolCallPreview({ tool: "exec", effects: ["process"],
    arguments_json: JSON.stringify({ argv: ["python", "a b.py", "", "line\nbreak"], cwd: "D:/my project" }) });
  assert.equal(preview.command, 'python "a b.py" "" "line\\nbreak"');
  assert.equal(preview.cwd, "D:/my project");
  assert.equal(preview.kind, "command");
  assert.equal(toolCallPreview({ tool: "exec", workspace_root: "D:/workspace" }).cwd, "D:/workspace");
});

test("custom tools and malformed arguments do not invent shell commands", () => {
  const custom = toolCallPreview({ tool: "plugin", arguments_json: '{"command":"delete","path":"a"}' });
  assert.equal(custom.command, ""); assert.equal(custom.path, "a");
  assert.equal(custom.kind, "tool");
  for (const arguments_json of ["bad", "null", "[]", "123"]) {
    const result = toolCallPreview({ tool: "exec", arguments_json });
    assert.equal(result.command, "");
  }
});
