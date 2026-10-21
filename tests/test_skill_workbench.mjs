import assert from "node:assert/strict";
import test from "node:test";
import { createSkillWorkbench } from "../app/web/js/features/settings/skill-workbench.js";

class Node extends EventTarget {
  constructor(tag) { super(); this.tagName = tag.toUpperCase(); this.children = []; this.attributes = {}; this.value = ""; this.isConnected = true; }
  setAttribute(key, value) { this.attributes[key] = value; if (key === "value") this.value = value; }
  append(...nodes) { for (const node of nodes) { this.children.push(node); if (node && typeof node === "object") node.parent = this; }
    if (this.tagName === "SELECT" && !this.value) this.value = this.children[0]?.value || ""; }
  replaceChildren(...nodes) { this.children = []; this.append(...nodes); }
  querySelectorAll(selector) { return this.children.flatMap(node => typeof node === "object" ? [
    ...(selector.split(",").some(tag => node.tagName === tag.toUpperCase()) ? [node] : []), ...node.querySelectorAll(selector)] : []); }
  showModal() { this.open = true; } close() { this.open = false; }
  remove() { if (this.parent) this.parent.children = this.parent.children.filter(node => node !== this); this.isConnected = false; }
  focus() { document.activeElement = this; }
}
const flush = () => new Promise(resolve => setImmediate(resolve));
const descendants = node => [node, ...node.children.flatMap(child => typeof child === "object" ? descendants(child) : [])];

test("Skill workbench uses project-scoped reads, protects edits and emits an unsent authoring intent", async context => {
  const original = { document: globalThis.document, window: globalThis.window };
  const body = new Node("body"), focus = new Node("button");
  globalThis.document = { body, activeElement: focus, createElement: tag => new Node(tag) };
  const page = new EventTarget(); let discard = false;
  page.confirm = () => discard; globalThis.window = page;
  const requests = [], intents = [];
  page.addEventListener("mdo-create-skill", event => intents.push(event.detail));
  context.mock.method(globalThis, "fetch", async (url, options) => {
    requests.push(url); assert.equal(options.method, "GET");
    const data = url.endsWith("/projects") ? { items: [{ id: "work", name: "Work" }] } :
      url.includes("/skills/sources") ? { items: [], diagnostics: [], user_directory: "D:/home/skills", project_directory: "D:/work/.mdo/skills" } : { items: [] };
    return Response.json({ ok: true, data });
  });
  const workbench = createSkillWorkbench({ onChange() {} });
  try {
    await workbench.open();
    assert.ok(requests.includes("/api/v1/skills/sources?project=default"));
    assert.ok(requests.includes("/api/v1/skills/project?project=default"));
    const dialog = body.children[0];
    const buttons = () => descendants(dialog).filter(node => node.tagName === "BUTTON");
    buttons().find(node => node.textContent === "新建项目 Skill").dispatchEvent(new Event("click")); await flush();
    const editor = descendants(dialog).find(node => node.tagName === "TEXTAREA"); editor.value = "Unsent edits";
    const project = descendants(dialog).find(node => node.attributes["aria-label"] === "项目");
    project.value = "work"; project.dispatchEvent(new Event("change")); await flush();
    assert.equal(project.value, "default"); assert.equal(editor.value, "Unsent edits");
    assert.equal(workbench.hasPendingChanges(), true);
    buttons().find(node => node.textContent === "用 AI 编写 Skill").dispatchEvent(new Event("click")); await flush();
    assert.equal(intents.length, 0); discard = true;
    buttons().find(node => node.textContent === "用 AI 编写 Skill").dispatchEvent(new Event("click")); await flush();
    assert.equal(intents.length, 1); assert.equal(intents[0].projectId, "default");
    assert.ok(intents[0].text.includes("D:/home/skills")); assert.equal(body.children.length, 0);
  } finally { workbench.destroy(); globalThis.document = original.document; globalThis.window = original.window; }
});
