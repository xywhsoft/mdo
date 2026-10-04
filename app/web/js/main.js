import { boot } from "./app.js";
import { loadLocale, resolveLocale } from "./i18n.js";

export function start() {
  // Use the system language while reading saved settings. Loading a bundled
  // pack must not delay workspace startup if an asset is unavailable.
  void loadLocale(resolveLocale()).catch((error) => {
    console.warn("Language pack unavailable; using HTML defaults", error);
  });
  return boot();
}
