import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { TestElement, testDocument, descendants } from "./helpers/extension-dom.mjs";
const flush = () => new Promise(r => setImmediate(r));
test("external store follows remote cursors, preserves previous pages and exposes no local download section", async t => {
  const previous = globalThis.document; globalThis.document = testDocument();
  const tools = Array.from({ length: 50 }, (_, i) => ({ id: 100 - i, tool_id: `tool.${i}`, name: `Tool ${i}`, version: "1.0.0", platform: "windows-x86_64" }));
  let snapshot = { writable: true, guide: "accepted", platform: "windows-x86_64", items: [], tasks: [], store: { items: [], request_id: "initial" } }, calls = [], serial = 0;
  globalThis.__externalPages = { api: { post: async (_, body) => {
    calls.push(body); snapshot = { ...snapshot, tasks: [...snapshot.tasks, { id: `task${++serial}`, action: "catalog", state: "queued", request: body }] }; return { data: snapshot };
  } } };
  const url = new URL("../app/web/js/features/notifications/external-tools.js", import.meta.url);
  const source = readFileSync(url, "utf8").replace('import { api } from "../../api/client.js";', "const { api } = globalThis.__externalPages;")
    .replace(/from "(\.[^"]+)"/g, (_, path) => `from ${JSON.stringify(new URL(path, url).href)}`);
  const { createExternalToolsPanel } = await import("data:text/javascript;base64," + Buffer.from(source).toString("base64"));
  const panel = createExternalToolsPanel({ refresh: async () => {}, openFull: async () => {}, openTools: () => {}, signedIn: () => true }), body = new TestElement("div");
  t.after(() => { panel.destroy(); globalThis.document = previous; delete globalThis.__externalPages; });
  const finish = (id, before, items, next) => { snapshot = { ...snapshot, tasks: [], store: { request_id: id, query: "", before, items, next_before: next } }; panel.render(body, snapshot); };
  panel.render(body, snapshot); descendants(body).find(n => n.attributes.role === "tab" && n.textContent === "Tool store").click(); await flush();
  finish("first", 0, tools, 50);
  const button = label => body.querySelector(".eb-pagination").querySelectorAll("button").find(b => b.textContent === label);
  for (let i = 0; i < 4; i++) button("下一页").click(); assert.equal(body.querySelector(".eb-name").textContent, "Tool 40");
  button("下一页").click(); await flush(); assert.equal(calls.at(-1).before, 50);
  finish("second", 50, [{ ...tools[0], id: 1, name: "Tail" }], 0); assert.equal(body.querySelector(".eb-name").textContent, "Tail");
  const requests=calls.length;button("上一页").click(); await flush();
  assert.equal(calls.length,requests,"The numbered pager reuses earlier loaded batches");
  assert.equal(body.querySelector(".eb-name").textContent, "Tool 40");
  assert.equal(body.querySelectorAll(".cli-tasks").length, 0); assert.equal(document.body.querySelectorAll(".cli-download-entry").length, 0);
  assert(!descendants(body).some(n => n.tagName === "BUTTON" && /^Downloads/.test(n.textContent)));
});
