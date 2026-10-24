import test from "node:test";
import assert from "node:assert/strict";
import { TestElement, testDocument } from "./helpers/extension-dom.mjs";
import { createExtensionBrowser, detailTabs, actionMenu, extensionToggle } from "../app/web/js/components/extension-browser.js";

test("pagination bounds rendered rows, preserves a live page and resets filters without nested scrolling", () => {
  const previous = globalThis.document; globalThis.document = testDocument();
  try {
    const browser = createExtensionBrowser({ kind: "toolpacks" });
    const rows = Array.from({ length: 25 }, (_, i) => ({ key: String(i), title: `Tool ${i}` }));
    browser.setItems(rows, { pageKey: "all" });
    assert.equal(browser.root.querySelectorAll(".eb-row").length, 10);
    const button = label => browser.root.querySelector(".eb-pagination").querySelectorAll("button").find(b => b.textContent === label);
    assert(button("上一页").disabled); button("下一页").click();
    assert.equal(browser.root.querySelector(".eb-name").textContent, "Tool 10");
    browser.setItems(rows.map(row => ({ ...row })), { pageKey: "all" });
    assert.equal(browser.root.querySelector(".eb-name").textContent, "Tool 10", "live refresh retains current page");
    button("下一页").click(); assert.equal(browser.root.querySelectorAll(".eb-row").length, 5); assert(button("下一页").disabled);
    browser.setItems(rows.slice(0, 11), { pageKey: "all" }); assert.equal(browser.root.querySelector(".eb-name").textContent, "Tool 10", "deletion clamps to last valid page");
    browser.setItems(rows, { pageKey: "new-filter" }); assert.equal(browser.root.querySelector(".eb-name").textContent, "Tool 0");
    const size = browser.root.querySelector(".eb-pagination").querySelector("select"); size.value = "20"; size.dispatchEvent(new Event("change"));
    assert.equal(browser.root.querySelectorAll(".eb-row").length, 20); browser.destroy();
  } finally { globalThis.document = previous; }
});

test("cursor batches page forward and return to the previous batch's last page", () => {
  const previous = globalThis.document; globalThis.document = testDocument();
  try {
    const browser = createExtensionBrowser();
    const rows = Array.from({ length: 15 }, (_, i) => ({ key: String(i), title: `Tool ${i}` }));
    const first = last => browser.setItems(rows, { pageKey: "first", initialPage: last ? "last" : 0, onNext: pages => {
      assert.equal(pages, 2); browser.setItems([{ key: "last", title: "Last" }], { pageKey: "second", pageOffset: pages, onPrevious: () => first(true) });
    } });
    first(false);
    const button = label => browser.root.querySelector(".eb-pagination").querySelectorAll("button").find(b => b.textContent === label);
    button("下一页").click(); button("下一页").click(); assert.equal(browser.root.querySelector(".eb-name").textContent, "Last");
    button("上一页").click(); assert.equal(browser.root.querySelector(".eb-name").textContent, "Tool 10"); browser.destroy();
  } finally { globalThis.document = previous; }
});

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
