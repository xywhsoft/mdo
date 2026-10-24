import { element } from "../utils/dom.js";
import { mountIcons } from "./icons.js";
import { openLayer } from "./layers.js";
import { t } from "../i18n.js";

export const browserText = (key, fallback, args = {}) => t(`extensionBrowser.${key}`, args, fallback);
const el = (tag, cls, text) => element(tag, { className: cls, ...(text === undefined ? {} : { text }) });
const icons = { knowledge: "folder", agents: "brain", subagents: "branch", tools: "code", skills: "spark", mcp: "globe", commands: "terminal", modules: "code", plugins: "settings", toolpacks: "terminal" };
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

// One persistent browser per view. Details use the shared top-layer window, so
// opening them never changes the list layout, pagination or scroll position.
export function createExtensionBrowser({ kind, onClose } = {}) {
  const root = el("div", "eb-browser"), list = el("div", "eb-list"), pager = el("nav", "eb-pagination");
  pager.setAttribute("aria-label", browserText("pagination", "列表翻页")); pager.hidden = true;
  root.append(list, pager);
  let rows = [], selected = null, detailLayer = null, rememberedFocus = null, rememberedPager = null, page = 0, pageSize = 10, pageKey = null, options = {}, awaitingNext = null;
  const closeDetail = (focus = true) => {
    const old = selected, layer = detailLayer; selected = null; detailLayer = null;
    layer?.dispose();
    for (const button of list.querySelectorAll(".eb-open")) button.setAttribute("aria-expanded", "false");
    for (const row of list.children) row.classList.remove("is-selected");
    if (old !== null) onClose?.();
    if (focus) [...list.querySelectorAll(".eb-open")].find(b => b.dataset.key === old)?.focus({ preventScroll: true });
  };
  function showDetail(key, node, { focus = false } = {}) {
    const row = rows.find(r => r.key === key); if (!row || selected !== key) return false;
    if (!detailLayer) {
      detailLayer = openLayer({ owner: root, opener: [...list.querySelectorAll(".eb-open")].find(b => b.dataset.key === key),
        title: row.title, icon: icons[row.kind || kind] || "folder", width: "52rem", height: "80%",
        onClosed({ reason }) { if (detailLayer) closeDetail(reason !== "dispose"); } });
      detailLayer.body.classList.add("eb-detail");
      detailLayer.body.setAttribute("aria-label", browserText("details", "扩展详情"));
    }
    const pane = detailLayer.body;
    const previousTab = pane.querySelector(".eb-detail-tabs")?.dataset.tab, scroll = pane.scrollTop, hadFocus = pane.contains(document.activeElement);
    detailLayer.update({ title: row.title, icon: icons[row.kind || kind] || "folder" });
    pane.replaceChildren(node);
    if (previousTab && !focus) (node.classList.contains("eb-detail-tabs") ? node : node.querySelector?.(".eb-detail-tabs"))?.selectTab?.(previousTab);
    pane.scrollTop = focus ? 0 : scroll;
    if (focus || hadFocus) detailLayer.closeButton.focus({ preventScroll: true }); return true;
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
  const browser = {
    root, open, showDetail, closeDetail,
    rememberFocus() {
      rememberedFocus = list.contains(document.activeElement) ? document.activeElement?.dataset.ebFocus : null;
      const control = pager.contains(document.activeElement) ? document.activeElement : null;
      rememberedPager = control ? { key: control.dataset.pageControl, value: control.value } : null;
    },
    isSelected: key => selected === key,
    setItems(next, settings = {}) {
      options = settings; const { empty = browserText("empty", "没有匹配的扩展"), busy = false } = settings;
      const sameKey = settings.pageKey === pageKey;
      if (!sameKey) { page = settings.initialPage === "last" ? Math.max(0, Math.ceil(next.length / pageSize) - 1) : 0; pageKey = settings.pageKey; awaitingNext = null; }
      else if (awaitingNext && !busy) {
        // A cursor page may fill the last partial display page before adding
        // another one. Show those new rows instead of skipping over them.
        if (next.length > awaitingNext.length) page = Math.floor(awaitingNext.length / pageSize);
        awaitingNext = null;
      }
      page = Math.min(page, Math.max(0, Math.ceil(next.length / pageSize) - 1));
      const scroll = list.scrollTop, focused = list.contains(document.activeElement) ? document.activeElement?.dataset.ebFocus : rememberedFocus;
      rememberedFocus = null;
      rows = next; list.replaceChildren(); list.setAttribute("aria-busy", String(busy));
      for (const row of rows.slice(page * pageSize, (page + 1) * pageSize)) {
        const entry = el("article", "eb-row"); entry.dataset.key = row.key;
        entry.classList.toggle("is-selected", selected === row.key);
        const openButton = element("button", { className: "eb-open", attrs: { type: "button", "aria-expanded": String(selected === row.key) } });
        openButton.dataset.key = row.key; openButton.dataset.ebFocus = `${row.key}:open`;
        const info = el("span", "eb-info"), title = el("span", "eb-title"), name = el("span", "eb-name", row.title);
        title.append(name);
        if (row.byline) title.append(el("small", "eb-byline", row.byline));
        info.append(title, el("span", "eb-description", row.description || browserText("noDescription", "暂无说明")), el("span", "eb-meta", (row.meta || []).filter(Boolean).join(" · ")));
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
      list.scrollTop = scroll; renderPager(sameKey);
      if (selected && !rows.slice(page * pageSize, (page + 1) * pageSize).some(r => r.key === selected)) closeDetail(false);
      else if (selected && rows.find(r => r.key === selected)?.detail) showDetail(selected, rows.find(r => r.key === selected).detail());
      if (focused) {
        const restore = () => [...list.querySelectorAll("[data-eb-focus]")].find(b => b.dataset.ebFocus === focused)?.focus({ preventScroll: true });
        if (root.isConnected) restore();
        else queueMicrotask(() => { if (root.isConnected && document.activeElement === document.body) restore(); });
      }
    },
    reset() { closeDetail(false); rows = []; page = 0; pageKey = null; awaitingNext = rememberedPager = null; list.replaceChildren(); list.scrollTop = 0; pager.replaceChildren(); pager.hidden = true; },
    destroy() { closeDetail(false); root.remove(); rows = []; rememberedFocus = rememberedPager = null; },
  };
  function focusPageControl(key) {
    const controls = [...pager.querySelectorAll("button,input,select")];
    (controls.find(control => control.dataset.pageControl === key && !control.disabled)
      || controls.find(control => control.getAttribute("aria-current") === "page"))?.focus({ preventScroll: true });
  }
  function turn(next, focus = "current") {
    if (options.busy) return;
    closeDetail(false); page = Math.max(0, Math.min(next, Math.ceil(rows.length / pageSize) - 1));
    if (pager.contains(document.activeElement) && document.activeElement?.dataset.pageControl === "jump") document.activeElement.value = String((options.pageOffset || 0) + page + 1);
    browser.setItems(rows, options);
    focusPageControl(focus === "current" ? `page:${(options.pageOffset || 0) + page + 1}` : focus);
    root.scrollIntoView?.({ block: "start" });
  }
  function renderPager(preserveFocus = false) {
    const count = Math.max(1, Math.ceil(rows.length / pageSize));
    const focused = preserveFocus && pager.contains(document.activeElement) ? document.activeElement : null;
    const controlKey = focused?.dataset.pageControl || (preserveFocus ? rememberedPager?.key : null);
    const draft = controlKey === "jump" ? (focused?.value ?? rememberedPager?.value) : null;
    rememberedPager = null;
    pager.replaceChildren(); pager.hidden = rows.length <= 10 && !options.onNext && !options.onPrevious;
    if (pager.hidden) return;
    const offset = options.pageOffset || 0, first = offset + 1, last = offset + count, current = offset + page + 1;
    const summary = el("span", "eb-page-status", offset
      ? browserText("pageOpen", "第 {page} 页", { page: current })
      : browserText(options.onNext ? "loadedItems" : "totalItems", options.onNext ? "已加载 {count} 条" : "共 {count} 条", { count: rows.length }));
    summary.setAttribute("aria-live", "polite");
    const links = el("div", "eb-page-links");
    const previous = element("button", { text: browserText("previousPage", "上一页"), className: "secondary-button eb-page-previous", attrs: { type: "button" } });
    const next = element("button", { text: browserText("nextPage", "下一页"), className: "secondary-button eb-page-next", attrs: { type: "button" } });
    previous.dataset.pageControl = "previous"; next.dataset.pageControl = "next";
    previous.setAttribute("aria-label", previous.textContent); next.setAttribute("aria-label", next.textContent);
    previous.disabled = options.busy || (page === 0 && !options.onPrevious);
    next.disabled = options.busy || (page + 1 >= count && !options.onNext);
    previous.addEventListener("click", () => { if (!previous.disabled) page ? turn(page - 1, "previous") : options.onPrevious?.(); });
    next.addEventListener("click", () => {
      if (next.disabled) return;
      if (page + 1 < count) turn(page + 1, "next");
      else { awaitingNext = { length: rows.length }; options.onNext?.(count); }
    });
    const compact = el("span", "eb-page-compact", options.onNext
      ? browserText("pageOpen", "第 {page} 页", { page: current })
      : browserText("pageCount", "第 {page} / {pages} 页", { page: current, pages: last }));
    links.append(previous, compact);
    const start = Math.max(first, Math.min(current - 2, last - 4)), end = Math.min(last, start + 4);
    const numbers = new Set([first, last]);
    for (let value = start; value <= end; value++) numbers.add(value);
    let previousNumber = first - 1;
    for (const value of [...numbers].sort((a, b) => a - b)) {
      if (value - previousNumber > 1) { const gap = el("span", "eb-page-gap", "…"); gap.setAttribute("aria-hidden", "true"); links.append(gap); }
      const button = element("button", { text: value, className: "secondary-button eb-page-number", attrs: { type: "button", "aria-label": browserText("goToPage", "第 {page} 页", { page: value }) } });
      button.dataset.pageControl = `page:${value}`; button.disabled = Boolean(options.busy);
      if (value === current) button.setAttribute("aria-current", "page");
      button.addEventListener("click", () => { if (!button.disabled && value !== current) turn(value - first); });
      links.append(button); previousNumber = value;
    }
    links.append(next);
    const size = element("select", { attrs: { "aria-label": browserText("pageSize", "每页条数") } });
    for (const value of [10, 20, 50]) { const option = el("option", "", browserText("perPage", "每页 {count} 条", { count: value })); option.value = String(value); size.append(option); }
    size.dataset.pageControl = "size"; size.value = String(pageSize); size.disabled = options.busy || Boolean(offset);
    size.addEventListener("change", () => { const value = Number(size.value); if (!size.disabled && [10, 20, 50].includes(value)) { pageSize = value; turn(0, "size"); } });
    const jump = el("form", "eb-page-jump"), label = el("label", "", browserText("jumpTo", "到第"));
    const input = element("input", { attrs: { type: "text", inputmode: "numeric", maxlength: "10", "aria-label": browserText("jumpPage", "跳转页码"), title: browserText("jumpRange", "可跳转到第 {first}–{last} 页", { first, last }) } });
    input.dataset.pageControl = "jump"; input.value = String(current); input.disabled = Boolean(options.busy);
    const confirm = element("button", { text: browserText("confirmPage", "确定"), className: "secondary-button", attrs: { type: "submit" } });
    confirm.disabled = Boolean(options.busy);
    const error = el("span", "eb-page-error"); error.hidden = true; error.setAttribute("role", "status");
    input.addEventListener("input", () => { input.removeAttribute("aria-invalid"); error.hidden = true; });
    label.append(input); jump.append(label, el("span", "", browserText("pageUnit", "页")), confirm, error);
    jump.addEventListener("submit", event => {
      event.preventDefault(); if (options.busy) return;
      const value = input.value.trim(), target = Number(value);
      if (!/^\d+$/.test(value) || !Number.isSafeInteger(target)) {
        input.setAttribute("aria-invalid", "true"); error.textContent = browserText("invalidPage", "请输入整数页码"); error.hidden = false; input.focus(); return;
      }
      const bounded = Math.max(first, Math.min(last, target)); input.value = String(bounded); turn(bounded - first, "jump");
    });
    pager.append(summary, links, size, jump);
    if (draft !== null) input.value = draft;
    if (controlKey) {
      if (root.isConnected) focusPageControl(controlKey);
      else queueMicrotask(() => { if (root.isConnected && document.activeElement === document.body) focusPageControl(controlKey); });
    }
  }
  return browser;
}
