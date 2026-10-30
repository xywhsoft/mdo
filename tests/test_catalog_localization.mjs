import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { loadLocale } from "../app/web/js/i18n.js";
import { resolveRegisteredLocale } from "../app/web/js/locale-registry.js";
import { catalogText, validateCatalogLanguages } from "../app/web/js/features/settings/catalog-localization.js";
import { providerPresets, setProviderCatalog, presetProvider, presetModel, providerDisplayName } from "../app/web/js/features/settings/provider-presets.js";

const metadata = { default_locale: "zh-CN", locales: [
  { id: "zh-CN", name: "中文", dir: "ltr", enabled: true },
  { id: "en-US", name: "English", dir: "ltr", enabled: true },
  { id: "ru-RU", name: "Русский", dir: "ltr", enabled: true },
  { id: "fr-FR", name: "Français", dir: "ltr", enabled: false, fallback: "en-US" },
] };
const supplier = { id: "test", name: "测试供应商", i18n: {
  "zh-CN": { name: "测试供应商" }, "en-US": { name: "Test supplier" }, "ru-RU": { name: "Тестовый поставщик" },
  "fr-FR": { name: "Unpublished draft" },
}, variants: [{ id: "plan", label: "套餐", key_note: "使用套餐 Key", base: "https://example.test/v1", models: ["wire-model"], protocol: "openai-responses",
  i18n: { "zh-CN": { label: "套餐", key_note: "使用套餐 Key" }, "en-US": { label: "Plan", key_note: "Use a plan key" }, "ru-RU": { label: "Тариф", key_note: "Используйте ключ тарифа" } } }] };

test("catalog text changes live while saved request profiles remain identical", async () => {
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async url => new Response(readFileSync(new URL(`../app/web${url}`, import.meta.url)), { status: 200 });
  try {
    setProviderCatalog([supplier], metadata);
    const preset = providerPresets.find(value => value.id === "test"), choice = preset.variants[0];
    let snapshot;
    for (const [locale, name, label, note] of [["zh-CN", "测试供应商", "套餐", "使用套餐 Key"], ["en-US", "Test supplier", "Plan", "Use a plan key"], ["ru-RU", "Тестовый поставщик", "Тариф", "Используйте ключ тарифа"]]) {
      await loadLocale(locale);
      assert.equal(preset.name, name); assert.equal(choice.label, label); assert.equal(choice.key_note, note);
      const provider = presetProvider(preset, choice), model = presetModel(provider, "wire-model");
      assert.equal(provider.name, supplier.name); assert.equal(providerDisplayName(provider), name);
      assert.equal(providerDisplayName({ ...provider, name: "My custom name" }), "My custom name");
      const current = JSON.stringify({ provider, model });
      if (snapshot) assert.equal(current, snapshot, "locale must not change model verification input");
      snapshot = current;
    }
    assert.equal(supplier.name, "测试供应商", "remote source object stays unchanged");
  } finally { globalThis.fetch = originalFetch; }
});

test("fallbacks are explicit, disabled translations stay private, old catalogs work", () => {
  assert.equal(catalogText(supplier, "name", metadata, "fr-FR"), supplier.name);
  assert.equal(catalogText(supplier, "name", metadata, "xx-XX"), supplier.name);
  assert.equal(catalogText({ id: "p", name: "Legacy" }, "name", undefined, "en-US"), "Legacy");
  const enabled = structuredClone(metadata); enabled.locales.at(-1).enabled = true;
  assert.equal(catalogText(supplier.variants[0], "label", enabled, "fr-FR"), "Plan");
  const chinese = { default_locale: "en-US", locales: [{ id: "zh-TW", enabled: true, fallback: "en-US" }, { id: "en-US", enabled: true }] };
  assert.equal(catalogText(supplier, "name", chinese, "zh-TW"), "Test supplier");
  assert.equal(resolveRegisteredLocale("fr-FR", [{ id: "fr-FR", systemLanguages: ["fr"] }]), "fr-FR");
});

test("invalid registry or translations cannot modify technical config", () => {
  const cyclic = structuredClone(metadata); cyclic.locales[0].fallback = "fr-FR"; cyclic.locales[1].fallback = "zh-CN";
  assert.throws(() => validateCatalogLanguages(cyclic), /Cyclic/);
  const duplicate = structuredClone(metadata); duplicate.locales.push({ ...duplicate.locales[0], id: "ZH-cn" });
  assert.throws(() => validateCatalogLanguages(duplicate), /Invalid/);
  const injected = structuredClone(supplier); injected.i18n["en-US"].endpoints = {};
  assert.throws(() => setProviderCatalog([injected], metadata), /translation field/);
  const oversized = structuredClone(supplier); oversized.i18n["en-US"].name = "Ж".repeat(65);
  assert.throws(() => setProviderCatalog([oversized], metadata), /translation field/);
});
