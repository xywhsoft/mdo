import { boot } from "./app.js";
import { loadLocale } from "./i18n.js";
import { initializeTarget } from "./api/target.js";
import { createDevices } from "./features/shell/devices.js";

export async function start() {
  // The HTML already contains the Chinese fallback. Language loading must not
  // delay workspace startup when a bundled asset is unavailable.
  void loadLocale("zh-CN").catch((error) => {
    console.warn("Language pack unavailable; using HTML defaults", error);
  });
  createDevices();
  await initializeTarget();
  return boot();
}
