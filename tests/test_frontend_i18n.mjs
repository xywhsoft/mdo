import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

import { currentLocale, loadLocale, subscribeLocale, supportedLocales, t } from
  "../app/web/js/i18n.js";
import { sessionActionItems } from "../app/web/js/features/sessions/session-actions.js";

const root = new URL("../app/web/", import.meta.url);
const packs = Object.fromEntries(supportedLocales.map((name) => [
  name, JSON.parse(readFileSync(new URL(`lang/${name}.json`, root), "utf8")),
]));

test("bundled language packs cover the annotated shell and switch without stale responses", async () => {
  const referenceKeys = Object.keys(packs["zh-CN"]).sort();
  for (const name of supportedLocales) {
    assert.deepEqual(Object.keys(packs[name]).sort(), referenceKeys, name);
    for (const key of referenceKeys) {
      const parameters = (value) => [...value.matchAll(/\{([a-zA-Z_][\w]*)\}/g)]
        .map((match) => match[1]).sort();
      assert.deepEqual(parameters(packs[name][key]), parameters(packs["zh-CN"][key]),
        `${name}:${key} parameters`);
    }
  }
  const html = readFileSync(new URL("index.html", root), "utf8");
  for (const [, key] of html.matchAll(/data-i18n(?:-title|-placeholder|-aria-label)?="([^"]+)"/g))
    assert.ok(referenceKeys.includes(key), `missing static key: ${key}`);
  for (const [tag, key] of html.matchAll(/<button class="starter"[^>]*data-prompt-key="([^"]+)"[^>]*>/g)) {
    assert.ok(referenceKeys.includes(key), `missing starter prompt: ${key}`);
    assert.match(tag, /data-prompt="[^"]+"/, `missing bundled starter fallback: ${key}`);
  }
  for (const path of [
    "js/features/sessions/session-actions.js",
    "js/features/sessions/session-list.js",
    "js/features/chat/composer-profile.js",
    "js/features/chat/composer-project.js",
    "js/features/chat/token-meter.js",
    "js/features/chat/timeline.js",
    "js/features/chat/markdown.js",
    "js/features/chat/prompt-queue.js",
    "js/features/chat/conversation-docks.js",
    "js/features/chat/conversation-search.js",
    "js/features/chat/composer-images.js",
    "js/features/chat/message-edit-dialog.js",
    "js/features/chat/message-replacement.js",
    "js/features/chat/slash-commands.js",
    "js/features/chat/file-mentions.js",
    "js/features/settings/settings-view.js",
    "js/features/settings/resource-panels.js",
    "js/features/settings/schedule-panel.js",
    "js/features/settings/feedback-panel.js",
    "js/features/settings/project-panel.js",
    "js/features/settings/memory-panel.js",
    "js/features/settings/model-config-panel.js",
    "js/features/sessions/project-dialog.js",
    "js/app.js",
  ]) {
    const source = readFileSync(new URL(path, root), "utf8");
    for (const [, key] of source.matchAll(/\bt\("([^"]+)"/g))
      assert.ok(referenceKeys.includes(key), `missing dynamic key: ${key}`);
    if (path.endsWith("conversation-docks.js"))
      for (const [, key] of source.matchAll(/"(dock\.[^"]+)"/g))
        assert.ok(referenceKeys.includes(key), `missing dock key: ${key}`);
    if (path.endsWith("slash-commands.js"))
      for (const [, key] of source.matchAll(/descriptionKey: "([^"]+)"/g))
        assert.ok(referenceKeys.includes(key), `missing command key: ${key}`);
    if (path.endsWith("session-actions.js"))
      for (const [, key] of source.matchAll(/\bitem\("[^"]+",\s*"([^"]+)"/g))
        assert.ok(referenceKeys.includes(key), `missing action key: ${key}`);
    if (path.endsWith("resource-panels.js"))
      for (const [, key] of source.matchAll(/:\s*"(resource\.[^"]+)"/g))
        assert.ok(referenceKeys.includes(key), `missing resource code key: ${key}`);
    if (path.endsWith("schedule-panel.js"))
      for (const [, key] of source.matchAll(/"(schedule\.[^"]+)"/g))
        assert.ok(referenceKeys.includes(key), `missing schedule key: ${key}`);
    if (path.endsWith("feedback-panel.js"))
      for (const [, key] of source.matchAll(/"(feedback\.[^"]+)"/g))
        assert.ok(referenceKeys.includes(key), `missing feedback key: ${key}`);
    if (path.endsWith("project-panel.js") || path.endsWith("project-dialog.js"))
      for (const [, key] of source.matchAll(/"(project\.[^"]+)"/g))
        assert.ok(referenceKeys.includes(key), `missing project key: ${key}`);
    if (path.endsWith("memory-panel.js"))
      for (const [, key] of source.matchAll(/"(memory\.[^"]+)"/g))
        assert.ok(referenceKeys.includes(key), `missing memory key: ${key}`);
    if (path.endsWith("model-config-panel.js"))
      for (const [, key] of source.matchAll(/"(modelConfig\.[^"]+)"/g))
        assert.ok(referenceKeys.includes(key), `missing model configuration key: ${key}`);
  }

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
  const changes = [];
  assert.equal(t("welcome.project.prompt", {}, "bundled fallback"), "bundled fallback");
  const unsubscribe = subscribeLocale((name) => changes.push(name));
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
    assert.equal(t("nav.actionsFor", { title: "Тест" }), "Действия с сеансом Тест");
    assert.equal(sessionActionItems({ status: "active", pinned: true })[1].label, "Открепить");
    assert.equal(t("missing.key", {}, "中文回退"), "中文回退");
    assert.equal(await loadLocale("en-US"), true);
    assert.equal(node.textContent, "New task");
    assert.equal(await loadLocale("zh-CN"), true);
    assert.equal(node.textContent, "新建任务");
    assert.deepEqual(changes, ["zh-CN", "ru-RU", "en-US", "zh-CN"]);
    await assert.rejects(loadLocale("fr"), /Unsupported locale/);
  } finally {
    unsubscribe();
    releaseEnglish();
    globalThis.document = originalDocument;
    globalThis.fetch = originalFetch;
  }
});
