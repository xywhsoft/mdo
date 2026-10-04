import { subscribeLocale, t } from "../../i18n.js";

// Pages are ordinary HTML: one navigation button and one matching panel.
// The same list drives desktop navigation and the native mobile selector.
// This shell owns presentation only; each page keeps its own data and saves.
export function createSettingsPages({ workspace, form, navigation }) {
  const nav = workspace.querySelector(".settings-navigation");
  const picker = workspace.querySelector("#settings-page-select");
  const pickerRow = workspace.querySelector(".settings-page-picker");
  const layout = workspace.querySelector(".settings-layout");
  const content = workspace.querySelector(".settings-content");
  const pages = new Map();
  for (const button of nav.querySelectorAll("[data-settings-section]")) {
    const id = button.dataset.settingsSection;
    const panel = [...content.querySelectorAll("[data-settings-panel]")]
      .find((panel) => panel.dataset.settingsPanel === id);
    if (!panel || pages.has(id)) throw new Error(`Invalid settings page: ${id}`);
    const option = document.createElement("option");
    option.value = id;
    picker.append(option);
    pages.set(id, { button, panel, option, preferences: form.contains(panel) });
  }
  if (!pages.size) throw new Error("Settings needs at least one page");
  let selected = "";
  const visible = (element) => Boolean(element?.getClientRects().length);
  const usesPreferences = (id) => Boolean(pages.get(id)?.preferences);
  const currentNavigation = () => visible(picker) ? picker : pages.get(selected)?.button;

  function syncLabels() {
    for (const { button, option } of pages.values()) option.textContent = button.textContent;
    const page = pages.get(selected);
    if (page) workspace.querySelector("#settings-title").textContent = page.preferences
      ? t("shell.settings.title", {}, "设置") : page.button.textContent;
  }

  function keepFocusedFieldVisible() {
    const field = document.activeElement;
    if (!content.contains(field) ||
        !["INPUT", "TEXTAREA", "SELECT"].includes(field.tagName) ||
        document.querySelector("dialog[open]")?.contains(field) ||
        getComputedStyle(layout).display !== "block") return;
    const area = layout.getBoundingClientRect();
    const pickerBounds = pickerRow.getBoundingClientRect();
    const top = visible(pickerRow) && pickerBounds.top < area.bottom &&
      pickerBounds.bottom > area.top ? Math.max(area.top, pickerBounds.bottom) : area.top;
    const target = field.getBoundingClientRect();
    if (target.bottom > area.bottom - 8)
      layout.scrollTop += target.bottom - area.bottom + 8;
    else if (target.top < top + 8)
      layout.scrollTop += target.top - top - 8;
  }

  function syncResponsive() {
    if (!visible(workspace)) return;
    // Resizing must not leave keyboard focus in a navigation control that
    // has just disappeared at the mobile breakpoint.
    const focused = document.activeElement;
    if ((nav.contains(focused) || focused === picker) && !visible(focused))
      currentNavigation()?.focus({ preventScroll: true });
    const button = pages.get(selected)?.button;
    if (visible(nav) && button) {
      const area = nav.getBoundingClientRect();
      const target = button.getBoundingClientRect();
      if (target.top < area.top + 8) nav.scrollTop += target.top - area.top - 8;
      else if (target.bottom > area.bottom - 8) nav.scrollTop += target.bottom - area.bottom + 8;
    }
    keepFocusedFieldVisible();
  }

  function selectSection(requested) {
    const next = pages.has(requested) ? requested :
      pages.has("general") ? "general" : pages.keys().next().value;
    const changed = selected !== next;
    selected = next;
    const page = pages.get(selected);
    for (const [id, { panel, button }] of pages) {
      panel.hidden = id !== selected;
      if (id === selected) button.setAttribute("aria-current", "page");
      else button.removeAttribute("aria-current");
    }
    picker.value = selected;
    form.hidden = !page.preferences;
    workspace.querySelector("#settings-revision").hidden = !page.preferences;
    workspace.querySelector("#settings-actions").hidden = !page.preferences;
    if (changed) { layout.scrollTop = 0; content.scrollTop = 0; }
    syncLabels();
    // Back/forward and programmatic navigation can hide the active editor.
    // Do not move focus when a visible button or the native selector owns it.
    if (content.contains(document.activeElement) && !visible(document.activeElement))
      currentNavigation()?.focus({ preventScroll: true });
    syncResponsive();
    return selected;
  }

  function onClick(event) {
    const button = event.target.closest("[data-settings-section]");
    if (button && nav.contains(button)) navigation.openSettings(button.dataset.settingsSection);
  }
  const onChange = () => navigation.openSettings(picker.value);
  nav.addEventListener("click", onClick);
  picker.addEventListener("change", onChange);
  window.addEventListener("resize", syncResponsive);
  const observer = typeof ResizeObserver === "function" ? new ResizeObserver(syncResponsive) : null;
  observer?.observe(nav);
  observer?.observe(pickerRow);
  observer?.observe(layout);
  const unsubscribeLocale = subscribeLocale(syncLabels);
  syncLabels();

  return Object.freeze({ selectSection, usesPreferences, keepFocusedFieldVisible,
    navigationTarget: currentNavigation,
    focusNavigation() { currentNavigation()?.focus({ preventScroll: true }); },
    destroy() {
      nav.removeEventListener("click", onClick);
      picker.removeEventListener("change", onChange);
      window.removeEventListener("resize", syncResponsive);
      observer?.disconnect();
      unsubscribeLocale();
    },
  });
}
