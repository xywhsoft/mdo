import test from "node:test";
import assert from "node:assert/strict";
import { normalizedSettingsValues, rebaseSettingsDraft } from "../app/web/js/features/settings/extension-settings-form.js";
import { localizedSetting } from "../app/web/js/features/settings/extension-settings-text.js";
import { extensionSettingsDescriptor } from "../app/web/js/features/settings/extension-settings.js";

const form = { groups: [{ fields: [{ name: "count", type: "integer", default: 10 }, { name: "enabled", type: "switch" },
  { name: "name", type: "text" }, { name: "key", type: "secret" }] }] };
test("settings use explicit zero/false and do not include secret controls in ordinary values", () => {
  assert.deepEqual(normalizedSettingsValues({ form, values: { count: 0, enabled: false, name: "" } }), { count: 0, enabled: false, name: "" });
  assert.deepEqual(normalizedSettingsValues({ form, values: {} }), { count: 10, enabled: false, name: "" });
});
test("rebase keeps actual local edits and new server defaults, without treating credentials as ordinary fields", () => {
  const before = { values: { count: 0, name: "old", removed: "before" } };
  const latest = { form, values: { count: 100, name: "server", enabled: false } };
  const draft = { values: { count: 0, name: "mine", removed: "edited" }, secrets: { key: "private", removed: "obsolete" } };
  assert.deepEqual(rebaseSettingsDraft(before, draft, latest), { values: { count: 100, name: "mine", enabled: false }, secrets: { key: "private" } });
  assert.equal(before.values.name, "old"); assert.equal(latest.values.name, "server");
});
test("localized schema text accepts future languages and configured fallbacks", () => {
  assert.equal(localizedSetting({ "de-DE": "Name", "en-US": "Character" }, "", "en-US", "de-DE"), "Name");
  assert.equal(localizedSetting({ "en-US": "Character" }, "", "en-US", "ja-JP"), "Character");
  assert.equal(localizedSetting("Direct", "", "en-US", "ru-RU"), "Direct");
});
test("only declared forms expose a configuration action and directory identity remains distinct", () => {
  assert.equal(extensionSettingsDescriptor({ id: "a", settings: false }, "skills"), null);
  assert.deepEqual(extensionSettingsDescriptor({ id: "skill:qa.a", directory_id: "installed", settings: { form: "settings.form.json" } }, "skills"), { kind: "skills", id: "installed", title: "skill:qa.a" });
});
