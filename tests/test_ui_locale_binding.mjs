import assert from "node:assert/strict";
import { readFileSync, readdirSync } from "node:fs";
import test from "node:test";
import { supportedLocales, loadLocale, t, uiMessage, setUiText, setUiAttribute, translateStatic } from "../app/web/js/i18n.js";
import { element } from "../app/web/js/utils/dom.js";
import { openLayer } from "../app/web/js/components/layers.js";
import { createSingleResourceStore } from "../app/web/js/features/settings/single-resource-store.js";
import { createExternalToolsPanel } from "../app/web/js/features/notifications/external-directory-tools.js";
import { TestElement, testDocument } from "./helpers/extension-dom.mjs";

const packs = Object.fromEntries(supportedLocales.map(locale => [locale,
  JSON.parse(readFileSync(new URL(`../app/web/lang/${locale}.json`, import.meta.url), "utf8"))]));
const flush = async () => { for (let i = 0; i < 10; i++) await new Promise(resolve => setImmediate(resolve)); };

test("literal UI keys exist in every pack and new English/Russian copy has no Chinese fallback", () => {
  const sources = directory => readdirSync(directory, { withFileTypes: true }).flatMap(entry => {
    const file = new URL(entry.name, directory);
    return entry.isDirectory() ? sources(new URL(`${entry.name}/`, directory)) : entry.name.endsWith(".js") ? [file] : [];
  });
  for (const file of sources(new URL("../app/web/js/", import.meta.url))) {
    const source = readFileSync(file, "utf8");
    for (const [, key] of source.matchAll(/\b(?:t|uiText|uiMessage|uiErrorText)\(\s*["']([A-Za-z][\w]*\.[\w.]+)["']/g))
      for (const locale of supportedLocales) assert.equal(typeof packs[locale][key], "string", `${file.pathname}:${locale}:${key}`);
  }
  for (const locale of ["en-US", "ru-RU"])
    for (const [key, value] of Object.entries(packs[locale]))
      if (key.startsWith("ui.")) assert.doesNotMatch(value, /[\u4e00-\u9fff]/u, `${locale}:${key}`);
});
function environment(context) {
  const saved = { document: globalThis.document, window: globalThis.window, fetch: globalThis.fetch };
  const document = testDocument();
  document.querySelectorAll = selector => document.body.querySelectorAll(selector);
  document.querySelector = selector => document.body.querySelector(selector);
  Object.assign(globalThis, { document, window: new EventTarget(), fetch: async (url, options) => {
    if (url.startsWith("/lang/")) return Response.json(packs[url.slice(6, -5)]);
    const body = options?.body && JSON.parse(options.body);
    return Response.json({ ok: true, data: body?.action === "catalog" ? { items: [], total: 0 } : {} });
  } });
  context.after(() => Object.assign(globalThis, saved));
  return document;
}

test("explicit UI bindings change copy and nested parameters without changing edits or author text", async context => {
  const document = environment(context);
  await loadLocale("zh-CN");
  const save = element("button", { text: uiMessage("sessionAction.save"), attrs: { "data-i18n": "sessionAction.close" } });
  const title = uiMessage("settings.knowledge");
  const search = element("input", { attrs: { placeholder: uiMessage("ui.singleResourceStore.searchValue1", { value1: title }), "data-i18n-placeholder": "sessionAction.close" } });
  const author = element("p", { text: "保存 <script>author</script>" });
  search.value = "保存 my unsent draft"; search.selectionStart = 3; search.scrollTop = 19;
  document.body.append(save, search, author); search.focus();
  for (const locale of ["en-US", "ru-RU", "zh-CN"]) {
    await loadLocale(locale);
    assert.equal(save.textContent, packs[locale]["sessionAction.save"]);
    assert.equal(search.getAttribute("placeholder"), t("ui.singleResourceStore.searchValue1", { value1: t("settings.knowledge") }));
    assert.equal(search.value, "保存 my unsent draft");
    assert.equal(search.selectionStart, 3); assert.equal(search.scrollTop, 19);
    assert.equal(document.activeElement, search);
    assert.equal(author.textContent, "保存 <script>author</script>");
    assert.equal(author.querySelectorAll("script").length, 0);
  }
  setUiText(save, "Author supplied label"); setUiAttribute(search, "placeholder", "user/path");
  await loadLocale("ru-RU");
  assert.equal(save.textContent, "Author supplied label"); assert.equal(search.getAttribute("placeholder"), "user/path");
  assert.equal(JSON.stringify({ title }), JSON.stringify({ title: t("settings.knowledge") }));
});

test("changing language updates layer title and window controls while retaining form identity", async context => {
  const document = environment(context); await loadLocale("zh-CN");
  const layer = openLayer({ title: uiMessage("settings.knowledge") });
  const input = element("textarea"); input.value = "unsaved 文本"; layer.body.append(input); input.focus();
  try {
    for (const locale of ["en-US", "ru-RU", "zh-CN"]) {
      await loadLocale(locale);
      assert.equal(layer.element.querySelector("h2").textContent, t("settings.knowledge"));
      assert.equal(layer.closeButton.getAttribute("title"), t("layers.close"));
      assert.equal(layer.closeButton.getAttribute("aria-label"), t("layers.close"));
      const maximize = layer.element.querySelector(".mdo-layer-controls").children[0];
      assert.equal(maximize.getAttribute("title"), t("layers.maximize"));
      assert.equal(input.value, "unsaved 文本"); assert.equal(document.activeElement, input);
      assert.equal(layer.body.children[0], input);
    }
  } finally { layer.dispose(); }
});

test("all nine store pages and local external tools use the selected language", async context => {
  const document = environment(context), stores = [];
  const panel = createExternalToolsPanel(), root = new TestElement("div");
  try {
    await loadLocale("zh-CN");
    for (const kind of ["agents", "subagents", "tools", "mcp", "skills", "commands", "external-tools", "knowledge", "plugins"]) {
      const store = createSingleResourceStore(kind, () => {}); stores.push(store);
      document.body.append(store.root); store.activate(true);
    }
    document.body.append(root); panel.render(root, { platform: "windows-x86_64", guide: "accepted", items: [] });
    await flush();
    for (const locale of ["en-US", "ru-RU", "zh-CN"]) {
      await loadLocale(locale);
      for (const store of stores) {
        assert.equal(store.root.querySelector("button").textContent, t("store.searchButton"));
        if (locale !== "zh-CN") {
          assert.doesNotMatch(store.root.textContent, /[\u4e00-\u9fff]/u);
          assert.doesNotMatch(store.root.querySelector("input").getAttribute("placeholder"), /[\u4e00-\u9fff]/u);
        }
      }
      if (locale !== "zh-CN") assert.doesNotMatch(root.textContent, /[\u4e00-\u9fff]/u);
    }
  } finally { panel.destroy(); for (const store of stores) store.destroy(); }
});

test("translated text updates preserve appended icons and yield to another content renderer", async context => {
  environment(context); await loadLocale("zh-CN");
  // A small native-text-node surface covers ownership absent from the list fixture.
  const node = { ownerDocument: {}, attributes: {}, child: null,
    setAttribute(k, v) { this.attributes[k] = v; }, removeAttribute(k) { delete this.attributes[k]; },
    set textContent(value) { this.child = { nodeType: 3, nodeValue: String(value), parentNode: this }; },
    get firstChild() { return this.child; } };
  setUiText(node, uiMessage("sessionAction.save")); const anchor = node.firstChild;
  const icon = {}; node.icon = icon;
  await loadLocale("en-US"); translateStatic({ querySelectorAll: () => [node] });
  assert.equal(anchor.nodeValue, t("sessionAction.save")); assert.equal(node.icon, icon);
  anchor.parentNode = null; node.child = { nodeType: 3, nodeValue: "other renderer", parentNode: node };
  await loadLocale("ru-RU"); translateStatic({ querySelectorAll: () => [node] });
  assert.equal(node.firstChild.nodeValue, "other renderer");
});
