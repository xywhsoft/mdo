import test from "node:test";
import assert from "node:assert/strict";
import { TestElement, testDocument } from "./helpers/extension-dom.mjs";
import { createExtensionBrowser, detailTabs, actionMenu, extensionToggle } from "../app/web/js/components/extension-browser.js";

test("selection, tabs, scroll and action focus survive a list refresh; late details are rejected", () => {
  const previous = globalThis.document; globalThis.document = testDocument();
  try {
    const browser = createExtensionBrowser({ kind: "skills" });
    const content = () => { const wrapper = new TestElement("div"); wrapper.append(detailTabs([{ id: "about", label: "About", content: new TestElement("p") }, { id: "files", label: "Files", content: new TestElement("pre") }])); return wrapper; };
    const rows = () => [{ key: "one", title: "One", description: "First", detail: content }, { key: "two", title: "Two", description: "Second", detail: content }];
    browser.setItems(rows()); browser.open("one");
    const list = browser.root.children[0], pane = browser.root.children[1]; list.scrollTop = 130; pane.scrollTop = 42;
    pane.querySelector(".eb-detail-tabs").selectTab("files");
    const first = list.querySelector(".eb-open"); first.focus(); browser.setItems(rows());
    assert.equal(list.scrollTop, 130); assert.equal(pane.scrollTop, 42);
    assert.equal(pane.querySelector(".eb-detail-tabs").dataset.tab, "files");
    assert.equal(document.activeElement.dataset.key, "one");
    browser.open("two"); assert.equal(browser.showDetail("one", new TestElement("p")), false);
    browser.closeDetail(); assert(pane.hidden); assert.equal(document.activeElement.dataset.key, "two");
    browser.open("one"); browser.setItems(rows().slice(1)); assert(pane.hidden, "filtered-out selection must not leave an orphan detail");
    browser.reset(); assert.equal(list.children.length, 0); browser.destroy();
  } finally { globalThis.document = previous; }
});

test("menus preserve actual actions and skip disabled entries; builtin rows expose no invented mutation", () => {
  const previous = globalThis.document; globalThis.document = testDocument();
  try {
    let calls = 0; const edit = new TestElement("button"), blocked = new TestElement("button"); blocked.disabled = true;
    edit.addEventListener("click", () => calls++);
    const menu = actionMenu([blocked, edit]);
    const event = new Event("keydown", { cancelable: true }); Object.defineProperty(event, "key", { value: "ArrowDown" }); menu.dispatchEvent(event);
    assert.equal(document.activeElement, edit); edit.click(); assert.equal(calls, 1);
    const escape = new Event("keydown"); Object.defineProperty(escape, "key", { value: "Escape" }); menu.dispatchEvent(escape); assert.equal(menu.open, false);
    const toggle = extensionToggle(edit, false, "My tool"); assert.equal(toggle.getAttribute("aria-checked"), "false"); assert.equal(toggle.getAttribute("role"), "switch");
    const browser = createExtensionBrowser({ kind: "tools" }); browser.setItems([{ key: "read", title: "read", description: "Builtin" }]);
    assert.equal(browser.root.querySelectorAll("button").length, 1, "builtin tool only opens its details"); browser.destroy();
  } finally { globalThis.document = previous; }
});
