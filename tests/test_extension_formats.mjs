import assert from "node:assert/strict";
import test from "node:test";
import { editPrompt, promptFields, splitPrompt, portableId } from "../app/web/js/features/settings/extension-formats.js";

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
