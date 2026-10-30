import { t as uiText } from "../../i18n.js";
// The renderer is bundled and integrity-pinned; no CDN or Node runtime.
let loading;
export function loadTerminalRenderer() {
  return loading ||= (async () => {
    const css = document.createElement("link"); css.rel = "stylesheet"; css.href = "/assets/terminal/xterm.css";
    const ready = new Promise((resolve, reject) => { css.onload = resolve; css.onerror = () => reject(new Error(uiText("ui.terminalRenderer.terminalStylesCouldNotLoadReopenThePage", {}, "终端样式未能加载，请重新打开页面。"))); });
    document.head.append(css);
    try {
      const [terminal, fit, search, unicode] = await Promise.all([
        import("../../vendor/terminal/xterm.js"), import("../../vendor/terminal/addon-fit.js"),
        import("../../vendor/terminal/addon-search.js"), import("../../vendor/terminal/addon-unicode11.js"), ready,
      ]);
      return { ...terminal, ...fit, ...search, ...unicode };
    } catch (error) { css.remove(); loading = null; throw error; }
  })();
}
