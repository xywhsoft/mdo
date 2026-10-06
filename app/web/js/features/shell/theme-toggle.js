import { mountIcons } from "../../components/icons.js";
import { subscribeLocale, t } from "../../i18n.js";

// Reflect the effective appearance, including system changes and unsaved
// settings previews. Persistence stays in the ordinary settings transaction.
export function createThemeToggle({ button, root = document.documentElement }) {
  const systemTheme = window.matchMedia("(prefers-color-scheme: dark)");
  const isDark = () => root.dataset.theme === "dark" ||
    (!root.dataset.theme && systemTheme.matches);
  function render() {
    const dark = isDark();
    const label = dark ? t("shell.themeLight", {}, "切换为浅色主题")
      : t("shell.themeDark", {}, "切换为深色主题");
    button.setAttribute("aria-label", label);
    button.title = label;
    const icon = button.querySelector("[data-icon]");
    const name = dark ? "sun" : "theme";
    if (icon.dataset.icon !== name || !icon.firstElementChild) {
      icon.dataset.icon = name;
      mountIcons(button);
    }
  }
  const observer = new MutationObserver(render);
  observer.observe(root, { attributes: true, attributeFilter: ["data-theme"] });
  systemTheme.addEventListener("change", render);
  const unsubscribe = subscribeLocale(render);
  render();
  return Object.freeze({ isDark,
    destroy() {
      observer.disconnect();
      systemTheme.removeEventListener("change", render);
      unsubscribe();
    },
  });
}
