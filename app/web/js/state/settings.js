import { api } from "../api/client.js";
import { createResourceStore } from "./store.js";
import { resolveLocale } from "../i18n.js";

export const settingsStore = createResourceStore();

async function readSettings() {
  const response = await api.get("/settings");
  return { ...response.data, etag: response.etag };
}

export function loadSettings() {
  return settingsStore.load(async () => {
    let settings = await readSettings();
    if (settings.locale !== "auto") return settings;
    const locale = resolveLocale();
    // Initialize only the language field, through the ordinary guarded,
    // revisioned API. Headless startup and existing preferences do not write.
    if (settings.etag) {
      try {
        await api.patch("/settings/settings", documentFor({ locale }), { ifMatch: settings.etag });
      } catch (error) {
        if (error?.status !== 409)
          console.warn("Initial language preference could not be saved", error);
      }
      // Re-read even after a lost reply or revision conflict. Another page's
      // accepted preference wins; never repeat a possibly committed write.
      try { settings = await readSettings(); }
      catch (error) { console.warn("Initial language preference could not be read back", error); }
    }
    // Read-only recovery or an unavailable disk must not block the UI.
    return { ...settings, locale: resolveLocale(settings.locale) };
  });
}

function documentFor(patch) {
  return { schema_version: 1, patch };
}

export async function previewSettings(patch) {
  return (await api.patch("/settings/settings/preview", documentFor(patch))).data;
}

export async function applySettings(patch, etag) {
  const result = (await api.patch("/settings/settings", documentFor(patch), { ifMatch: etag })).data;
  const loaded = await loadSettings();
  if (loaded.status !== "ready") throw loaded.error ?? new Error("Settings could not be read after saving");
  return result;
}

export async function restoreSettings(etag) {
  const result = (await api.delete("/settings/settings", { ifMatch: etag })).data;
  const loaded = await loadSettings();
  if (loaded.status !== "ready") throw loaded.error ?? new Error("Settings could not be read after restoring");
  return result;
}
