import assert from "node:assert/strict";
import test from "node:test";
import { createSettingsPages } from "../app/web/js/features/settings/settings-pages.js";
import { loadLocale } from "../app/web/js/i18n.js";

// Minimal DOM adapter for page ownership and navigation. Real layout, native
// selectors and keyboard visibility are checked against the packed app.
class Element extends EventTarget {
  constructor(properties = {}) {
    super();
    Object.assign(this, { dataset: {}, children: [], attributes: {}, hidden: false,
      shown: true, scrollTop: 0, textContent: "", tagName: "DIV" }, properties);
  }
  append(child) { this.children.push(child); child.parent = this; }
  contains(child) { return child === this || this.children.some((node) => node.contains(child)); }
  querySelectorAll(selector) { return selector === "[data-settings-section]"
    ? this.children : selector === "[data-settings-panel]" ? this.panels : []; }
  querySelector(selector) { return this.nodes?.[selector] ?? null; }
  getClientRects() {
    return !this.shown || this.hidden || (this.parent && !this.parent.getClientRects().length)
      ? [] : [this.getBoundingClientRect()];
  }
  getBoundingClientRect() { return { top: 0, bottom: 200 }; }
  setAttribute(key, value) { this.attributes[key] = value; }
  removeAttribute(key) { delete this.attributes[key]; }
  focus() { document.activeElement = this; }
  closest(selector) { return selector === "[data-settings-group]" ? this.group ?? null : this; }
}

function fixture({ grouped = false } = {}) {
  const original = { document: globalThis.document, window: globalThis.window,
    getComputedStyle: globalThis.getComputedStyle, fetch: globalThis.fetch };
  const document = new Element({ title: "墨斗", documentElement: new Element() });
  document.createElement = () => new Element();
  document.activeElement = new Element();
  globalThis.document = document;
  globalThis.window = new EventTarget();
  globalThis.getComputedStyle = () => ({ display: "grid" });
  const form = new Element(), nav = new Element(), content = new Element({ panels: [] });
  const groups = ["基础设置", "扩展能力"].map((textContent) => {
    const heading = new Element({ textContent });
    return new Element({ nodes: { "[data-settings-group-label]": heading } });
  });
  const picker = new Element({ shown: false }), pickerRow = new Element({ shown: false });
  const workspace = new Element({ nodes: {
    ".settings-navigation": nav, "#settings-page-select": picker,
    ".settings-page-picker": pickerRow, ".settings-layout": new Element(),
    ".settings-content": content, "#settings-title": new Element(),
    "#settings-revision": new Element(), "#settings-actions": new Element(),
  } });
  // A new preference page needs no hardcoded list in the shell or app router.
  for (const [id, label, preferences] of [["general", "常规", true],
    ["future", "未来配置页", true], ["models", "模型", false]]) {
    nav.append(new Element({ dataset: { settingsSection: id }, textContent: label,
      group: grouped ? groups[id === "models" ? 1 : 0] : null }));
    const panel = new Element({ dataset: { settingsPanel: id } });
    content.panels.push(panel);
    if (preferences) form.append(panel);
    else content.append(panel);
  }
  content.append(form);
  const calls = [];
  const pages = createSettingsPages({ workspace, form,
    navigation: { openSettings(section) { calls.push(section); pages.selectSection(section); } } });
  return { pages, form, nav, picker, pickerRow, workspace, content, calls, groups,
    mobile() { nav.shown = false; picker.shown = pickerRow.shown = true; },
    close() { pages.destroy(); Object.assign(globalThis, original); } };
}

test("new HTML pages appear in both navigations and share preference actions", () => {
  const f = fixture();
  try {
    assert.deepEqual(f.picker.children.map((option) => option.value), ["general", "future", "models"]);
    f.pages.selectSection("future");
    assert.equal(f.picker.value, "future");
    assert.equal(f.nav.children[1].attributes["aria-current"], "page");
    assert.equal(f.form.hidden, false);
    assert.equal(f.workspace.nodes["#settings-actions"].hidden, false);
    f.pages.selectSection("models");
    assert.equal(f.form.hidden, true);
    assert.equal(f.workspace.nodes["#settings-actions"].hidden, true);
    assert.deepEqual(f.content.panels.map((panel) => panel.hidden), [true, true, false]);
    assert.equal(f.pages.selectSection("constructor"), "general");
    assert.equal(f.form.hidden, false);
  } finally { f.close(); }
});

test("native dropdown and desktop clicks use the same route without losing form state", () => {
  const f = fixture();
  try {
    f.pages.selectSection("general");
    const editor = new Element({ value: "unsaved", tagName: "INPUT" });
    f.content.panels[0].append(editor);
    f.mobile();
    f.picker.focus();
    f.picker.value = "models";
    f.picker.dispatchEvent(new Event("change"));
    assert.deepEqual(f.calls, ["models"]);
    assert.equal(document.activeElement, f.picker);
    assert.equal(editor.value, "unsaved");
    const click = new Event("click");
    Object.defineProperty(click, "target", { value: f.nav.children[1] });
    f.nav.dispatchEvent(click);
    assert.deepEqual(f.calls, ["models", "future"]);
    f.pages.destroy();
    f.picker.dispatchEvent(new Event("change"));
    assert.equal(f.calls.length, 2);
  } finally { f.close(); }
});

test("back navigation resets page scroll and focuses the visible mobile control", () => {
  const f = fixture();
  try {
    f.pages.selectSection("future");
    const editor = new Element(); f.content.panels[1].append(editor);
    f.workspace.nodes[".settings-layout"].scrollTop = 125;
    f.content.scrollTop = 60;
    f.mobile(); editor.focus();
    f.pages.selectSection("general");
    assert.equal(document.activeElement, f.picker);
    assert.equal(f.content.scrollTop, 0);
    assert.equal(f.workspace.nodes[".settings-layout"].scrollTop, 0);
    f.picker.focus();
    f.nav.shown = true; f.picker.shown = f.pickerRow.shown = false;
    window.dispatchEvent(new Event("resize"));
    assert.equal(document.activeElement, f.nav.children[0]);
  } finally { f.close(); }
});

test("locale changes refresh dropdown labels without resetting the selected page", async () => {
  const f = fixture();
  try {
    globalThis.fetch = async () => Response.json({ "shell.settings.title": "Settings" });
    f.pages.selectSection("models");
    f.nav.children[2].textContent = "Models";
    await loadLocale("en-US");
    assert.equal(f.picker.value, "models");
    assert.equal(f.picker.children[2].textContent, "Models");
    assert.equal(f.workspace.nodes["#settings-title"].textContent, "Models");
  } finally { f.close(); }
});

test("mobile groups follow desktop labels without changing the selected page or route", async () => {
  const f = fixture({ grouped: true });
  try {
    assert.deepEqual(f.picker.children.map((group) => group.label), ["基础设置", "扩展能力"]);
    assert.deepEqual(f.picker.children.map((group) => group.children.map((option) => option.value)),
      [["general", "future"], ["models"]]);
    f.mobile();
    f.picker.value = "models";
    f.picker.dispatchEvent(new Event("change"));
    assert.deepEqual(f.calls, ["models"]);
    globalThis.fetch = async () => Response.json({ "shell.settings.title": "Settings" });
    f.groups[0].nodes["[data-settings-group-label]"].textContent = "Basic settings";
    f.groups[1].nodes["[data-settings-group-label]"].textContent = "Extended capabilities";
    f.nav.children[2].textContent = "Models";
    await loadLocale("en-US");
    assert.deepEqual(f.picker.children.map((group) => group.label), ["Basic settings", "Extended capabilities"]);
    assert.equal(f.picker.children[1].children[0].textContent, "Models");
    assert.equal(f.picker.value, "models");
    assert.deepEqual(f.calls, ["models"]);
  } finally { f.close(); }
});
