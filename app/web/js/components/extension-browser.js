import { element } from "../utils/dom.js";
import { mountIcons } from "./icons.js";
import { t } from "../i18n.js";

export const browserText = (key, fallback) => t(`extensionBrowser.${key}`, {}, fallback);
const el = (tag, cls, text) => element(tag, { className: cls, ...(text === undefined ? {} : { text }) });
const icons = { agents: "brain", subagents: "branch", tools: "code", skills: "spark", mcp: "globe", commands: "terminal", modules: "code", plugins: "settings", toolpacks: "terminal" };
let sequence = 0;
export function extensionIcon(kind) {
  const icon = element("span", { className: "icon", attrs: { "data-icon": icons[kind] || "folder", "aria-hidden": "true" } });
  const wrap = el("span", "eb-icon"); wrap.append(icon); mountIcons(wrap); return wrap;
}

// Keep the existing operation buttons and their guards. A menu changes their
// presentation, never their permissions, confirmation or native transaction.
export function actionMenu(actions, label = browserText("more", "更多操作")) {
  const menu = el("details", "eb-menu"), summary = el("summary", "secondary-button", label);
  summary.setAttribute("aria-label", label);
  const body = el("div", "eb-menu-items"); body.setAttribute("role", "group"); body.setAttribute("aria-label", label);
  body.append(...actions); menu.append(summary, body);
  menu.addEventListener("toggle", () => {
    if (!menu.open) return;
    const rect = summary.getBoundingClientRect(), width = Math.min(260, globalThis.innerWidth - 24 || 260);
    Object.assign(body.style, { width: `${width}px`, left: `${Math.max(12, Math.min(rect.right - width, (globalThis.innerWidth || 600) - width - 12))}px`, top: `${Math.min(rect.bottom + 5, Math.max(12, (globalThis.innerHeight || 800) - 240))}px`, maxHeight: "min(55dvh, 340px)", overflowY: "auto" });
  });
  body.addEventListener("click", event => { if (event.target.closest?.("button,a")) menu.open = false; });
  menu.addEventListener("keydown", event => {
    if (event.key === "Escape") { menu.open = false; summary.focus(); event.stopPropagation(); }
    if (!["ArrowDown", "ArrowUp", "Home", "End"].includes(event.key)) return;
    const controls = [...body.querySelectorAll("button,a")].filter(b => !b.disabled);
    if (!controls.length) return;
    event.preventDefault(); menu.open = true;
    const index = controls.indexOf(document.activeElement);
    controls[event.key === "Home" ? 0 : event.key === "End" ? controls.length - 1 : (index + (event.key === "ArrowDown" ? 1 : -1) + controls.length) % controls.length].focus();
  });
  menu.addEventListener("focusout", event => { if (event.relatedTarget && !menu.contains(event.relatedTarget)) menu.open = false; });
  return menu;
}

export function extensionToggle(control, enabled, name) {
  control.className = "eb-switch";
  control.textContent = "";
  control.setAttribute("role", "switch"); control.setAttribute("aria-checked", String(Boolean(enabled)));
  control.setAttribute("aria-label", `${browserText("enabled", "启用")} ${name}`);
  control.title = enabled ? browserText("disable", "停用") : browserText("enabled", "启用");
  return control;
}

export function detailTabs(sections) {
  const root = el("div", "eb-detail-tabs"), tabs = el("div", "eb-tabs"), panels = el("div", "eb-panels");
  tabs.setAttribute("role", "tablist"); tabs.setAttribute("aria-label", browserText("details", "扩展详情"));
  const entries = sections.filter(s => s.content), prefix = `extension-detail-${++sequence}`;
  const select = key => {
    if (!entries.some(section => section.id === key)) return;
    root.dataset.tab = key;
    for (const [i, section] of entries.entries()) {
      const active = section.id === key, tab = tabs.children[i];
      tab.setAttribute("aria-selected", String(active)); tab.tabIndex = active ? 0 : -1; panels.children[i].hidden = !active;
      if (active) section.onSelect?.();
    }
  };
  for (const [index, section] of entries.entries()) {
    const button = element("button", { text: section.label, attrs: { type: "button", role: "tab", id: `${prefix}-tab-${index}`, "aria-controls": `${prefix}-panel-${index}` } });
    const panel = element("section", { attrs: { role: "tabpanel", id: `${prefix}-panel-${index}`, "aria-labelledby": `${prefix}-tab-${index}` } });
    panel.append(section.content); button.addEventListener("click", () => select(section.id));
    button.addEventListener("keydown", event => {
      if (!["ArrowLeft", "ArrowRight", "Home", "End"].includes(event.key)) return;
      event.preventDefault(); const next = event.key === "Home" ? 0 : event.key === "End" ? entries.length - 1 : (index + (event.key === "ArrowRight" ? 1 : -1) + entries.length) % entries.length;
      select(entries[next].id); tabs.children[next].focus();
    });
    tabs.append(button); panels.append(panel);
  }
  root.append(tabs, panels); root.selectTab = select;
  if (entries.length) select(entries[0].id);
  return root;
}

export function detailFacts(values) {
  const list = el("dl", "eb-facts");
  for (const [name, value] of values) if (value !== null && value !== undefined && value !== "") {
    const row = el("div"); row.append(el("dt", "", name), el("dd", "", value)); list.append(row);
  }
  return list;
}

// One persistent browser per view preserves the selected item, list scroll and
// keyboard focus across refreshes. CSS switches the same detail pane to a
// full-width view on phones; no cloned controls or hidden modal are involved.
export function createExtensionBrowser({ kind, onClose } = {}) {
  const root = el("div", "eb-browser"), list = el("div", "eb-list"), pane = el("section", "eb-detail");
  pane.setAttribute("aria-label", browserText("details", "扩展详情")); pane.hidden = true;
  root.append(list, pane);
  let rows = [], selected = null, rememberedFocus = null;
  const closeDetail = (focus = true) => {
    const old = selected; selected = null; pane.hidden = true; pane.replaceChildren(); root.classList.remove("has-detail");
    for (const button of list.querySelectorAll(".eb-open")) button.setAttribute("aria-expanded", "false");
    for (const row of list.children) row.classList.remove("is-selected");
    onClose?.(); if (focus) [...list.querySelectorAll(".eb-open")].find(b => b.dataset.key === old)?.focus({ preventScroll: true });
  };
  function showDetail(key, node, { focus = false } = {}) {
    const row = rows.find(r => r.key === key); if (!row || selected !== key) return false;
    const previousTab = pane.querySelector(".eb-detail-tabs")?.dataset.tab, scroll = pane.scrollTop, hadFocus = pane.contains(document.activeElement);
    const back = element("button", { text: browserText("back", "返回列表"), className: "secondary-button eb-back", attrs: { type: "button" } });
    back.addEventListener("click", () => closeDetail());
    const heading = el("div", "eb-detail-heading"); heading.append(extensionIcon(row.kind || kind), el("h3", "", row.title));
    pane.replaceChildren(back, heading, node); pane.hidden = false; root.classList.add("has-detail");
    if (previousTab && !focus) (node.classList.contains("eb-detail-tabs") ? node : node.querySelector?.(".eb-detail-tabs"))?.selectTab?.(previousTab);
    pane.scrollTop = focus ? 0 : scroll; if (focus || hadFocus) back.focus({ preventScroll: true }); return true;
  }
  function open(key, focus = true) {
    const row = rows.find(r => r.key === key); if (!row) return;
    selected = key;
    for (const entry of list.children) entry.classList.toggle("is-selected", entry.dataset.key === key);
    for (const button of list.querySelectorAll(".eb-open")) button.setAttribute("aria-expanded", String(button.dataset.key === key));
    const loading = el("p", "eb-empty", browserText("loading", "正在读取详情…")); loading.setAttribute("role", "status");
    showDetail(key, row.detail ? row.detail() : loading, { focus }); row.onSelect?.();
  }
  root.addEventListener("click", event => { for (const menu of root.querySelectorAll(".eb-menu[open]")) if (!menu.contains(event.target)) menu.open = false; });
  root.addEventListener("scroll", event => { for (const menu of root.querySelectorAll(".eb-menu[open]")) if (!menu.contains(event.target)) menu.open = false; }, true);
  return {
    root, open, showDetail, closeDetail,
    rememberFocus() { rememberedFocus = list.contains(document.activeElement) ? document.activeElement?.dataset.ebFocus : null; },
    isSelected: key => selected === key,
    setItems(next, { empty = browserText("empty", "没有匹配的扩展"), busy = false } = {}) {
      const scroll = list.scrollTop, focused = list.contains(document.activeElement) ? document.activeElement?.dataset.ebFocus : rememberedFocus;
      rememberedFocus = null;
      rows = next; list.replaceChildren(); list.setAttribute("aria-busy", String(busy));
      for (const row of rows) {
        const entry = el("article", "eb-row"); entry.dataset.key = row.key;
        entry.classList.toggle("is-selected", selected === row.key);
        const openButton = element("button", { className: "eb-open", attrs: { type: "button", "aria-expanded": String(selected === row.key) } });
        openButton.dataset.key = row.key; openButton.dataset.ebFocus = `${row.key}:open`;
        const info = el("span", "eb-info"), name = el("span", "eb-name", row.title);
        info.append(name, el("span", "eb-description", row.description || browserText("noDescription", "暂无说明")), el("span", "eb-meta", (row.meta || []).filter(Boolean).join(" · ")));
        openButton.append(extensionIcon(row.kind || kind), info); openButton.addEventListener("click", () => open(row.key));
        const controls = el("div", "eb-actions");
        if (row.badge?.text) controls.append(el("span", `eb-badge ${row.badge.tone === "error" ? "eb-error" : ""}`, row.badge.text));
        if (row.primary) controls.append(row.primary);
        if (row.toggle) controls.append(row.toggle);
        if (row.actions?.length) controls.append(actionMenu(row.actions));
        entry.append(openButton, controls);
        [...controls.querySelectorAll("button,summary,a")].forEach((b, index) => { b.dataset.ebFocus = `${row.key}:${index}`; });
        list.append(entry);
      }
      if (!rows.length) list.append(el("p", "eb-empty", busy ? browserText("loading", "正在载入…") : empty));
      list.scrollTop = scroll;
      if (selected && !rows.some(r => r.key === selected)) closeDetail(false);
      else if (selected && rows.find(r => r.key === selected)?.detail) showDetail(selected, rows.find(r => r.key === selected).detail());
      if (focused) {
        const restore = () => [...list.querySelectorAll("[data-eb-focus]")].find(b => b.dataset.ebFocus === focused)?.focus({ preventScroll: true });
        if (root.isConnected) restore();
        else queueMicrotask(() => { if (root.isConnected && document.activeElement === document.body) restore(); });
      }
    },
    reset() { closeDetail(false); rows = []; list.replaceChildren(); list.scrollTop = 0; },
    destroy() { closeDetail(false); root.remove(); rows = []; rememberedFocus = null; },
  };
}
