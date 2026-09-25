import { boot } from "./app.js";
import { loadLocale } from "./i18n.js";

// The HTML already contains the Chinese fallback. Language loading must not
// delay workspace startup when a bundled asset is unavailable.
void loadLocale("zh-CN").catch((error) => {
  console.warn("Language pack unavailable; using HTML defaults", error);
});

boot().catch((error) => {
  document.body.textContent = "墨斗界面初始化失败。请重新启动应用。";
  console.error(error);
});
