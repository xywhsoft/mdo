import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { loadLocale } from "../app/web/js/i18n.js";
import { TestElement, testDocument, descendants } from "./helpers/extension-dom.mjs";
import { createDownloadRegistry } from "../app/web/js/state/downloads.js";
import { createDownloadCenter } from "../app/web/js/features/downloads/center.js";
const initialFetch = globalThis.fetch;
globalThis.fetch = async url => new Response(readFileSync(new URL(`../app/web${url}`, import.meta.url)), { status: 200 });
await loadLocale("en-US"); globalThis.fetch = initialFetch;
const flush = () => new Promise(r => setImmediate(r));

test("one global dialog combines source types and delegates cancellation, retry and update review", async t => {
  const previous = globalThis.document, entry = new TestElement("button"), doc = testDocument();
  for (const name of ["label", "count"]) { const child = new TestElement("span"); child.setAttribute(`data-downloads-${name}`, ""); entry.append(child); }
  doc.querySelector = name => name === "#open-downloads" ? entry : null; globalThis.document = doc;
  const registry = createDownloadRegistry(), calls = []; let updates = 0;
  registry.observe("/cli-tools", { tasks: [{ id: "active", name: "rg", action: "install", state: "downloading", done: 20, total: 100 }, { id: "retry", name: "curl", action: "install", state: "failed", error: "Checksum mismatch" }] });
  registry.observe("/update", { status: "ready", ready: true, build_id: 9 });
  let complete; const extension = registry.run("local", { id: 4, name: "My Skill" }, () => new Promise(r => { complete = r; }));
  const center = createDownloadCenter({ registry, updatePanel: { open: () => { updates++; } }, transport: { get: async () => ({ data: {} }), post: async (path, data) => { calls.push([path, data]); }, delete: async path => { calls.push([path]); } } });
  t.after(() => { center.destroy(); globalThis.document = previous; });
  center.open(); await flush();
  const dialog = doc.body.querySelector("dialog"); assert(dialog.open); assert.equal(doc.body.querySelectorAll("dialog").length, 1);
  assert(dialog.textContent.includes("My Skill") && dialog.textContent.includes("mdo") && dialog.textContent.includes("Checksum mismatch"));
  assert.equal(entry.querySelector("[data-downloads-count]").textContent, "2");
  const buttons = label => descendants(dialog).filter(n => n.tagName === "BUTTON" && n.textContent === label);
  buttons("Cancel")[0].click(); await flush(); assert.deepEqual(calls.at(-1), ["/cli-tools", { action: "cancel", id: "active" }]);
  buttons("Retry")[0].click(); await flush(); assert.deepEqual(calls.at(-1), ["/cli-tools", { action: "retry", id: "retry" }]);
  buttons("View update")[0].click(); assert.equal(updates, 1); assert(!dialog.open);
  complete(); await extension; center.open(); await flush();
  buttons("Clear finished")[0].click(); await flush(); assert.deepEqual(calls.at(-1), ["/cli-tools", { action: "clear" }]);
  assert(!registry.list("local").some(row => row.name === "My Skill"));
  assert(registry.list("local").some(row => row.state === "ready"));
});
