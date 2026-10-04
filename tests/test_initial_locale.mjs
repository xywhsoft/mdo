import assert from "node:assert/strict";
import test from "node:test";
import { resolveLocale } from "../app/web/js/i18n.js";
import { loadSettings, settingsStore } from "../app/web/js/state/settings.js";
import { setApiWriteGuard } from "../app/web/js/api/client.js";

test("first-use language follows the primary system language, with English fallback", () => {
  for (const [language, expected] of [
    ["zh-CN", "zh-CN"], ["zh-TW", "zh-CN"], ["zh-Hant-HK", "zh-CN"],
    ["ZH_cn", "zh-CN"], ["ru-RU", "ru-RU"], ["ru-UA", "ru-RU"],
    ["en-GB", "en-US"], ["fr-FR", "en-US"], ["zhx", "en-US"],
    ["", "en-US"],
  ]) assert.equal(resolveLocale("auto", { languages: [language] }), expected, language);
  assert.equal(resolveLocale("auto", { languages: ["fr-FR", "zh-CN"] }), "en-US");
  assert.equal(resolveLocale("auto", { language: "ru-RU" }), "ru-RU");
  assert.equal(resolveLocale("auto", { languages: [], language: "zh-CN" }), "zh-CN");
  assert.equal(resolveLocale("auto", null), "en-US");
  for (const preference of ["zh-CN", "en-US", "ru-RU"])
    assert.equal(resolveLocale(preference, { languages: ["fr-FR"] }), preference);
});

test("native Android language wins over WebView's browser language", () => {
  const bridge = globalThis.XsPlatform;
  try {
    globalThis.XsPlatform = { languages: () => "ru-RU,en-US" };
    assert.equal(resolveLocale(), "ru-RU");
    globalThis.XsPlatform = { languages: () => "zh-Hans-CN,en-US" };
    assert.equal(resolveLocale(), "zh-CN");
    globalThis.XsPlatform = { languages: () => "de-DE,zh-CN" };
    assert.equal(resolveLocale(), "en-US");
    assert.equal(resolveLocale("ru-RU"), "ru-RU");
  } finally {
    if (bridge === undefined) delete globalThis.XsPlatform;
    else globalThis.XsPlatform = bridge;
  }
});

test("an unavailable native bridge falls back to the browser language", async () => {
  const bridge = globalThis.XsPlatform;
  try {
    await withSystem("ru-RU", globalThis.fetch, async () => {
      for (const languages of [() => "", () => null, () => { throw new Error("unavailable"); }]) {
        globalThis.XsPlatform = { languages };
        assert.equal(resolveLocale(), "ru-RU");
      }
    });
  } finally {
    if (bridge === undefined) delete globalThis.XsPlatform;
    else globalThis.XsPlatform = bridge;
  }
});

async function withSystem(language, fetch, operation) {
  const navigator = Object.getOwnPropertyDescriptor(globalThis, "navigator");
  const originalFetch = globalThis.fetch;
  const warn = console.warn;
  Object.defineProperty(globalThis, "navigator", { configurable: true,
    value: { languages: [language] } });
  globalThis.fetch = fetch;
  console.warn = () => {};
  settingsStore.reset();
  try { await operation(); }
  finally {
    settingsStore.reset();
    globalThis.fetch = originalFetch;
    console.warn = warn;
    if (navigator) Object.defineProperty(globalThis, "navigator", navigator);
    else delete globalThis.navigator;
  }
}

function snapshot(locale, etag = '"settings-1"') {
  return Response.json({ ok: true, data: { locale } }, { headers: { ETag: etag } });
}

test("initial language is saved once and survives a later system language change", async () => {
  let stored = "auto";
  const writes = [];
  await withSystem("ru-RU", async (url, options) => {
    if (options.method === "GET") return snapshot(stored);
    assert.equal(url, "/api/v1/settings/settings");
    assert.equal(options.method, "PATCH");
    assert.equal(options.headers.get("If-Match"), '"settings-1"');
    const document = JSON.parse(options.body);
    writes.push(document);
    stored = document.patch.locale;
    return Response.json({ ok: true, data: { changed: true } });
  }, async () => {
    assert.equal((await loadSettings()).data.locale, "ru-RU");
    assert.deepEqual(writes, [{ schema_version: 1, patch: { locale: "ru-RU" } }]);
    globalThis.navigator.languages = ["zh-CN"];
    settingsStore.reset();
    assert.equal((await loadSettings()).data.locale, "ru-RU");
    assert.equal(writes.length, 1);
  });
});

test("upgrading an existing saved language never rewrites it", async () => {
  for (const saved of ["zh-CN", "en-US", "ru-RU"])
    await withSystem("ru-RU", async (_url, options) => {
      assert.equal(options.method, "GET");
      return snapshot(saved);
    }, async () => assert.equal((await loadSettings()).data.locale, saved));
});

test("unsupported system language is persisted as English even with secondary Chinese", async () => {
  let saved = "auto";
  await withSystem("de-DE", async (_url, options) => {
    if (options.method === "GET") return snapshot(saved);
    saved = JSON.parse(options.body).patch.locale;
    return Response.json({ ok: true, data: { changed: true } });
  }, async () => {
    globalThis.navigator.languages.push("zh-CN");
    assert.equal((await loadSettings()).data.locale, "en-US");
    assert.equal(saved, "en-US");
  });
});

test("a concurrent accepted language wins over first-use detection", async () => {
  let saved = "auto", writes = 0;
  await withSystem("zh-CN", async (_url, options) => {
    if (options.method === "GET") return snapshot(saved, '"settings-2"');
    writes++;
    saved = "ru-RU";
    return Response.json({ ok: false, error: { code: "revision_conflict" } }, { status: 409 });
  }, async () => {
    const state = await loadSettings();
    assert.equal(state.status, "ready");
    assert.equal(state.data.locale, "ru-RU");
    assert.equal(state.data.etag, '"settings-2"');
    assert.equal(writes, 1);
  });
});

test("a lost write reply is confirmed by reading, without repeating the write", async () => {
  let saved = "auto", writes = 0;
  await withSystem("ru-RU", async (_url, options) => {
    if (options.method === "GET") return snapshot(saved);
    writes++;
    saved = JSON.parse(options.body).patch.locale;
    throw new Error("reply lost after commit");
  }, async () => {
    assert.equal((await loadSettings()).data.locale, "ru-RU");
    await loadSettings();
    assert.equal(writes, 1);
  });
});

test("an unreadable write confirmation keeps the first-use UI usable", async () => {
  let reads = 0, writes = 0;
  await withSystem("ru-RU", async (_url, options) => {
    if (options.method === "GET") {
      if (++reads === 2) throw new Error("read offline");
      return snapshot("auto");
    }
    writes++;
    return Response.json({ ok: true, data: { changed: true } });
  }, async () => {
    const state = await loadSettings();
    assert.equal(state.status, "ready");
    assert.equal(state.data.locale, "ru-RU");
    assert.equal(writes, 1);
    assert.equal(reads, 2);
  });
});

test("read-only recovery uses the detected language without bypassing the write guard", async () => {
  const releaseGuard = setApiWriteGuard(() => false);
  try {
    await withSystem("zh-Hant", async (_url, options) => {
      assert.equal(options.method, "GET");
      return snapshot("auto");
    }, async () => {
      const state = await loadSettings();
      assert.equal(state.status, "ready");
      assert.equal(state.data.locale, "zh-CN");
    });
  } finally { releaseGuard(); }
});
