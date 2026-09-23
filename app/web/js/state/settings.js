import { api } from "../api/client.js";
import { createResourceStore } from "./store.js";

export const settingsStore = createResourceStore();

export function loadSettings() {
  return settingsStore.load(async () => {
    const response = await api.get("/settings");
    return { ...response.data, etag: response.etag };
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
  await loadSettings();
  return result;
}

export async function restoreSettings(etag) {
  const result = (await api.delete("/settings/settings", { ifMatch: etag })).data;
  await loadSettings();
  return result;
}
