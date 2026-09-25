import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

import { currentLocale, loadLocale, supportedLocales, t } from
  "../app/web/js/i18n.js";

const root = new URL("../app/web/", import.meta.url);
const packs = Object.fromEntries(supportedLocales.map((name) => [
  name, JSON.parse(readFileSync(new URL(`lang/${name}.json`, root), "utf8")),
]));

test("bundled language packs cover the annotated shell and switch without stale responses", async () => {
  const referenceKeys = Object.keys(packs["zh-CN"]).sort();
  for (const name of supportedLocales)
    assert.deepEqual(Object.keys(packs[name]).sort(), referenceKeys, name);
  const html = readFileSync(new URL("index.html", root), "utf8");
  for (const [, key] of html.matchAll(/data-i18n(?:-title|-placeholder|-aria-label)?="([^"]+)"/g))
    assert.ok(referenceKeys.includes(key), `missing static key: ${key}`);

  const node = {
    dataset: { i18n: "shell.newTask", i18nAriaLabel: "shell.newTask.configure" },
    textContent: "新建任务",
    attributes: new Map([["aria-label", "配置后创建任务"]]),
    getAttribute(name) { return this.attributes.get(name); },
    setAttribute(name, value) { this.attributes.set(name, value); },
  };
  const originalDocument = globalThis.document;
  const originalFetch = globalThis.fetch;
  let releaseEnglish;
  const englishGate = new Promise((resolve) => { releaseEnglish = resolve; });
  globalThis.document = {
    documentElement: { lang: "zh-CN" },
    title: "墨斗",
    querySelectorAll() { return [node]; },
  };
  globalThis.fetch = async (path) => {
    const name = path.slice("/lang/".length, -".json".length);
    if (name === "en-US") await englishGate;
    return Response.json(packs[name]);
  };
  try {
    assert.equal(await loadLocale("zh-CN"), true);
    assert.equal(node.textContent, "新建任务");
    const stale = loadLocale("en-US");
    assert.equal(await loadLocale("ru-RU"), true);
    releaseEnglish();
    assert.equal(await stale, false);
    assert.equal(currentLocale(), "ru-RU");
    assert.equal(globalThis.document.documentElement.lang, "ru-RU");
    assert.equal(node.textContent, "Новая задача");
    assert.equal(node.getAttribute("aria-label"), "Настроить и создать задачу");
    assert.equal(t("missing.key", {}, "中文回退"), "中文回退");
    assert.equal(await loadLocale("en-US"), true);
    assert.equal(node.textContent, "New task");
    assert.equal(await loadLocale("zh-CN"), true);
    assert.equal(node.textContent, "新建任务");
    await assert.rejects(loadLocale("fr"), /Unsupported locale/);
  } finally {
    releaseEnglish();
    globalThis.document = originalDocument;
    globalThis.fetch = originalFetch;
  }
});
