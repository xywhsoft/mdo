import test from "node:test";
import assert from "node:assert/strict";
import { testDocument } from "./helpers/extension-dom.mjs";
import { createMarkdownRenderer } from "../app/web/js/features/chat/markdown.js";

const tick = () => new Promise(resolve => setTimeout(resolve, 5));
async function until(check) {
  for (let i = 0; i < 100; i++) { if (check()) return; await tick(); }
  assert.fail("Markdown rendering did not make progress");
}
const tree = source => ({ schema_version: 1, engine: "md4c", nodes: [{ t: "p", c: [{ t: "text", s: source }] }] });
function fixture() {
  globalThis.document = testDocument();
  const pending = []; let paints = 0;
  const renderer = createMarkdownRenderer({ delay: 0,
    parse: source => new Promise(resolve => pending.push({ source, resolve })),
    onLayout: () => paints++ });
  return { renderer, pending, get paints() { return paints; } };
}

test("each completed prefix is formatted even while tokens keep arriving", async () => {
  const f = fixture(); let source = "First";
  try {
    const root = f.renderer.render(source, { key: "stream" });
    for (let i = 0; i < 5; i++) {
      await until(() => f.pending.length);
      const previous = f.pending.shift(); source += " continued";
      assert.equal(f.renderer.render(source, { key: "stream" }), root);
      previous.resolve(tree(previous.source));
      await until(() => f.paints === i + 1);
      assert.equal(root.querySelector(".md-blocks").textContent, previous.source);
      assert.equal(root.querySelector(".md-pending").textContent, source.slice(previous.source.length));
    }
    f.renderer.render(source, { key: "stream", final: true });
    await until(() => f.pending.length);
    const last = f.pending.shift(); last.resolve(tree(last.source));
    await until(() => f.paints === 6);
    assert.equal(root.querySelector(".md-blocks").textContent, source);
    assert.equal(root.querySelector(".md-pending").textContent, "");
  } finally { f.renderer.destroy(); }
});

test("replaced text rejects stale parsing and cannot cache an incomplete final reply", async () => {
  const f = fixture();
  try {
    const root = f.renderer.render("Old", { key: "stream" });
    await until(() => f.pending.length);
    const old = f.pending.shift();
    f.renderer.render("New", { key: "stream" });
    old.resolve(tree(old.source)); await tick();
    assert.equal(f.paints, 0);
    assert.equal(root.querySelector(".md-pending").textContent, "New");
    await until(() => f.pending.length);
    const prefix = f.pending.shift();
    f.renderer.render("New complete", { key: "stream", final: true });
    prefix.resolve(tree(prefix.source)); await until(() => f.paints === 1);
    assert.equal(root.querySelector(".md-pending").textContent, " complete");
    // The prefix was not the final source and must be parsed afresh elsewhere.
    f.renderer.render("New", { key: "other", final: true });
    await until(() => f.pending.some(request => request.source === "New"));
    assert.equal(f.paints, 1);
    for (const request of f.pending.splice(0)) request.resolve(tree(request.source));
    await until(() => f.paints === 3);
  } finally { f.renderer.destroy(); }
});
