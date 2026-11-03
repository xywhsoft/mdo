import assert from "node:assert/strict";
import test from "node:test";
import { createFileImporter, decodeTextFile, uploadLeaf, MAX_FILE_BYTES } from "../app/web/js/features/chat/composer-files.js";
import { createComposerContext } from "../app/web/js/features/chat/composer-context.js";
import { newPromptContext, serializePromptContext } from "../app/web/js/features/chat/prompt-context.js";
import { TestElement, testDocument } from "./helpers/extension-dom.mjs";

function fixture(options = {}) {
  let scope = "session-a";
  const input = { disabled: false, readOnly: false, focus() {} }, added = [], errors = [], stored = [];
  const importer = createFileImporter({ input, owner: () => scope,
    add: item => { added.push(item); return true; },
    store: async file => { stored.push(file); return { path: "@uploads/" + "a".repeat(32) + ".bin" }; },
    onError: error => errors.push(error), ...options });
  return { importer, input, added, stored, errors, navigate(value) { scope = value; } };
}
const text = (body, name = "settings.json") => new File([body], name, { type: "application/json" });

test("JSON, Unicode and code files are exact contexts, never sent to image upload", async () => {
  const f = fixture(), body = '{"项目":"墨斗","enabled":true}\n';
  assert.equal(await f.importer.importFiles([text(body), new File(["int main() {}"], "main.c")] ), true);
  assert.equal(f.added[0].content, body); assert.equal(f.added[1].kind, "text");
  assert.equal(f.stored.length, 0); assert.equal(f.errors.length, 0);
});
test("background status refreshes preserve attachment buttons and their focus", () => {
  globalThis.document = testDocument();
  const input = new TestElement("textarea"), strip = new TestElement("div"); input.value = "";
  document.body.append(input, strip);
  const context = createComposerContext({ input, strip, owner: () => "same-editor" });
  const value = serializePromptContext("", [newPromptContext("text", "test.json", { content: "{}" })]);
  context.restore(value);
  const button = strip.querySelector(".prompt-context-open"); button.focus();
  for (let i = 0; i < 30; ++i) { context.render(); context.restore(value); }
  assert.equal(strip.querySelector(".prompt-context-open"), button);
  assert.equal(document.activeElement, button);
});
test("BOM UTF-16 is decoded while binary NUL is never inlined", () => {
  assert.equal(decodeTextFile(new Uint8Array([255,254,65,0,66,0]).buffer), "AB");
  assert.throws(() => decodeTextFile(new Uint8Array([65,0]).buffer));
  assert.equal(uploadLeaf({ name: "../../文档.XLSX" }, "b".repeat(32)), "b".repeat(32) + ".xlsx");
  assert.equal(uploadLeaf({ name: "no-extension" }, "b".repeat(32)), "b".repeat(32) + ".bin");
});
test("binary, invalid UTF-8 and oversized text retain bytes in session storage", async () => {
  const f = fixture();
  const files = [new File([new Uint8Array([0,255,1])], "工作表.xlsx"), text("x".repeat(50000)),
    new File([new Uint8Array([255,250])], "legacy.txt", { type: "text/plain" })];
  await f.importer.importFiles(files);
  assert.deepEqual(f.stored, files); assert(f.added.every(item => item.kind === "file" && item.path.startsWith("@uploads/")));
});
test("context byte overflow falls back to a file reference without truncation", async () => {
  const f = fixture({ canAdd: item => item.kind === "file" });
  await f.importer.importFiles([text('"'.repeat(40000))]);
  assert.equal(f.stored.length, 1); assert.equal(f.added[0].kind, "file"); assert.equal(f.errors.length, 0);
});
test("changing conversation during a file read never attaches into the next editor", async () => {
  let release;
  const f = fixture(), file = { name: "one.txt", size: 1, type: "text/plain",
    arrayBuffer: () => new Promise(resolve => { release = resolve; }) };
  const importing = f.importer.importFiles([file]); assert.equal(f.importer.isBusy(), true);
  f.navigate("session-b"); release(new TextEncoder().encode("x").buffer);
  assert.equal(await importing, false); assert.equal(f.added.length, 0); assert.equal(f.importer.hasInFlight(), false);
});
test("a bad file does not suppress other files and read-only input cannot upload", async () => {
  const f = fixture(); await f.importer.importFiles([{ name: "huge", size: MAX_FILE_BYTES + 1 }, text("{}")]);
  assert.equal(f.errors.length, 1); assert.equal(f.added.length, 1);
  f.input.readOnly = true; assert.equal(await f.importer.importFiles([text("{}")]), false);
  assert.equal(f.added.length, 1);
});
test("an invalid storage locator is rejected and a late upload ACK cannot cross owners", async () => {
  const f = fixture({ store: async () => ({ path: "C:/external/file.exe" }) });
  await f.importer.importFiles([new File(["raw"], "binary.zip")]);
  assert.equal(f.added.length, 0); assert.equal(f.errors.length, 1);
  let finish;
  const late = fixture({ store: () => new Promise(resolve => { finish = resolve; }) });
  const uploading = late.importer.importFiles([new File(["raw"], "binary.zip")]);
  late.navigate("session-b"); finish({ path: "@uploads/" + "a".repeat(32) + ".zip" });
  assert.equal(await uploading, false); assert.equal(late.added.length, 0);
});
