import { boot } from "./app.js";
import { loadLocale } from "./i18n.js";

export function start() {
  // The HTML already contains the Chinese fallback. Language loading must not
  // delay workspace startup when a bundled asset is unavailable.
  void loadLocale("zh-CN").catch((error) => {
    console.warn("Language pack unavailable; using HTML defaults", error);
  });
  return boot();
}
