import { t as uiErrorText, setUiText as writeUiText, uiMessage, subscribeLocale, t } from "../../i18n.js";
import { subscribeSettingsPages } from "./settings-page-registry.js";

// Pages are ordinary HTML: one navigation button and one matching panel.
// External links share desktop/mobile navigation but never own a local page.
// The same list drives desktop navigation and the native mobile selector.
// This shell owns presentation only; each page keeps its own data and saves.
export function createSettingsPages({ workspace, form, navigation }) {
  const nav = workspace.querySelector(".settings-navigation");
  const picker = workspace.querySelector("#settings-page-select");
  const pickerRow = workspace.querySelector(".settings-page-picker");
  const layout = workspace.querySelector(".settings-layout");
  const content = workspace.querySelector(".settings-content");
  const pages = new Map();
  const externalLinks = new Map();
  const groups = new Map();
  for (const button of nav.querySelectorAll("[data-settings-section], [data-settings-external]")) {
    const external = button.dataset.settingsExternal;
    const id = external ? `external:${external}` : button.dataset.settingsSection;
    const panel = [...content.querySelectorAll("[data-settings-panel]")]
      .find((panel) => panel.dataset.settingsPanel === id);
    if ((!external && !panel) || pages.has(id) || externalLinks.has(id)) throw new Error(`Invalid settings page: ${id}`);
    const option = document.createElement("option");
    option.value = id;
    const heading = button.closest("[data-settings-group]")
      ?.querySelector("[data-settings-group-label]");
    if (heading && !groups.has(heading)) {
      const group = document.createElement("optgroup");
      groups.set(heading, group);
      picker.append(group);
    }
    (groups.get(heading) ?? picker).append(option);
    if (external) externalLinks.set(id, { button, option });
    else pages.set(id, { button, panel, option, preferences: form.contains(panel) });
  }
  if (!pages.size) throw new Error(uiErrorText("error.detail.settingsNeedsAtLeastOnePage", {}, "Settings needs at least one page"));
  let selected = "";
  const extensionPages = new Map();
  let extensionGroup, extensionHeading, extensionOptions;
  function releasePage(page) {
    page?.abort?.abort();
    if (page?.task) void page.task.then(cleanup => typeof cleanup === "function" && cleanup()).catch(() => {});
    if (page) { page.task = null; page.abort = null; }
  }
  function mountPage(page) {
    if (!page?.extension || page.task) return;
    const abort = page.abort = new AbortController(); page.panel.replaceChildren();
    page.task = Promise.resolve().then(() => !abort.signal.aborted && page.extension.mount(page.panel, { signal: abort.signal }))
      .catch(error => { if (!abort.signal.aborted) writeUiText(page.panel, error.message); });
  }
  const visible = (element) => Boolean(element?.getClientRects().length);
  const usesPreferences = (id) => Boolean(pages.get(id)?.preferences);
  const currentNavigation = () => visible(picker) ? picker : pages.get(selected)?.button;

  function syncLabels() {
    for (const [heading, group] of groups) group.label = heading.textContent;
    for (const { button, option } of pages.values()) writeUiText(option, button.textContent);
    for (const { button, option } of externalLinks.values()) writeUiText(option, `${button.textContent.trim()} ↗`);
    const page = pages.get(selected);
    if (page) writeUiText(workspace.querySelector("#settings-title"), page.preferences
      ? uiMessage("shell.settings.title", {}, "设置") : page.button.textContent);
  }

  function keepFocusedFieldVisible() {
    const field = document.activeElement;
    if (!content.contains(field) ||
        !["INPUT", "TEXTAREA", "SELECT"].includes(field.tagName) ||
        document.querySelector("dialog[open]")?.contains(field) ||
        !visible(pickerRow)) return;
    // The content scrolls between the mobile picker and fixed action row.
    const area = content.getBoundingClientRect();
    const target = field.getBoundingClientRect();
    if (target.bottom > area.bottom - 8)
      content.scrollTop += target.bottom - area.bottom + 8;
    else if (target.top < area.top + 8)
      content.scrollTop += target.top - area.top - 8;
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
      if (id !== selected) releasePage(pages.get(id));
      if (id === selected) button.setAttribute("aria-current", "page");
      else button.removeAttribute("aria-current");
    }
    picker.value = selected;
    form.hidden = !page.preferences;
    workspace.querySelector("#settings-revision").hidden = !page.preferences;
    workspace.querySelector("#settings-actions").hidden = !page.preferences;
    if (changed) { layout.scrollTop = 0; content.scrollTop = 0; }
    syncLabels();
    mountPage(page);
    // Back/forward and programmatic navigation can hide the active editor.
    // Do not move focus when a visible button or the native selector owns it.
    if (content.contains(document.activeElement) && !visible(document.activeElement))
      currentNavigation()?.focus({ preventScroll: true });
    syncResponsive();
    return selected;
  }

  function onClick(event) {
    const button = event.target.closest("[data-settings-section]");
    if (button?.dataset.settingsSection && nav.contains(button)) navigation.openSettings(button.dataset.settingsSection);
  }
  const onChange = () => {
    const external = externalLinks.get(picker.value);
    if (external) { picker.value = selected; external.button.click(); }
    else navigation.openSettings(picker.value);
  };
  nav.addEventListener("click", onClick);
  picker.addEventListener("change", onChange);
  window.addEventListener("resize", syncResponsive);
  const observer = typeof ResizeObserver === "function" ? new ResizeObserver(syncResponsive) : null;
  observer?.observe(nav);
  observer?.observe(pickerRow);
  observer?.observe(layout);
  const unsubscribeLocale = subscribeLocale(syncLabels);
  const unsubscribeExtensions = subscribeSettingsPages(entries => {
    const desired = new Map(entries);
    for (const [id, page] of extensionPages) if (desired.get(id) !== page.extension) {
      releasePage(page); page.button.remove(); page.panel.remove(); page.option.remove();
      extensionPages.delete(id); pages.delete(id);
    }
    for (const [id, extension] of entries) if (!extensionPages.has(id)) {
      if (pages.has(id)) throw Error(`Settings page already exists: ${id}`);
      if (!extensionGroup) {
        extensionGroup = document.createElement("div"); extensionGroup.setAttribute("data-settings-group", "");
        const heading = extensionHeading = document.createElement("p"); heading.setAttribute("data-settings-group-label", "");
        heading.setAttribute("data-i18n", "settings.extensionSettings");
        writeUiText(heading, t("settings.extensionSettings", {}, "扩展设置")); extensionGroup.append(heading); nav.append(extensionGroup);
        extensionOptions = document.createElement("optgroup"); groups.set(heading, extensionOptions); picker.append(extensionOptions);
      }
      const button = document.createElement("button"), panel = document.createElement("section"), option = document.createElement("option");
      button.type = "button"; button.dataset.settingsSection = id; writeUiText(button, extension.title || id);
      panel.dataset.settingsPanel = id; panel.className = "settings-section"; panel.hidden = true;
      option.value = id; extensionGroup.append(button); content.append(panel); extensionOptions.append(option);
      const page = { button, panel, option, preferences: false, extension };
      pages.set(id, page); extensionPages.set(id, page);
    }
    if (!extensionPages.size && extensionGroup) {
      groups.delete(extensionHeading); extensionGroup.remove(); extensionOptions.remove(); extensionGroup = extensionOptions = extensionHeading = null;
    }
    selectSection(navigation.get?.()?.settingsSection || selected || "general");
  });
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
      unsubscribeExtensions(); for (const page of extensionPages.values()) releasePage(page);
    },
  });
}
