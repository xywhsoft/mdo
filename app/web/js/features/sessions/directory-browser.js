import { uiMessage, setUiText, setUiAttribute, subscribeLocale } from "../../i18n.js";
import { element, errorMessage, isImeKey } from "../../utils/dom.js";
import { mountIcons } from "../../components/icons.js";
import { createDirectoryNavigation, directoryChild, directoryBreadcrumbs, directoryCompletion } from "./directory-navigation.js";

const copy = (key, params = {}) => uiMessage(`directory.${key}`, params);
const icon = name => element("span", { className: "icon", attrs: { "data-icon": name, "aria-hidden": "true" } });

export function createDirectoryBrowser({ layer, read, create, places, device, writable = true,
  storageAccess, onConfirm, onCancel, onReloadLocations }) {
  const root = element("section", { className: "directory-browser" });
  const options = { signal: layer.signal };
  const on = (node, name, fn) => node.addEventListener(name, fn, options);
  const button = (key, symbol, action, className = "secondary-button", caption = true) => {
    const node = element("button", { className, attrs: { type: "button", "aria-label": copy(key), title: copy(key) } },
      [...(symbol ? [icon(symbol)] : []), ...(caption ? [element("span", { text: copy(key) })] : [])]);
    if (action) on(node, "click", action); return node;
  };
  const deviceLabel = element("p", { className: "directory-device", text: copy("device", { name: device }) });
  const toolbar = element("div", { className: "directory-toolbar" });
  const back = button("back", "arrow-left", () => void historyMove(-1), "icon-button", false);
  const forward = button("forward", "arrow-right", () => void historyMove(1), "icon-button", false);
  const up = button("parent", "arrow-up", () => { const path = navigation.get().current?.parent; if (path) void go(path); }, "icon-button", false);
  const refresh = button("refresh", "refresh", () => { const state = navigation.get(); if (state.current) void go(state.current.path, { replace: true, filter: filter.value }); }, "icon-button", false);
  const pathArea = element("div", { className: "directory-address" });
  const crumbs = element("nav", { className: "directory-breadcrumbs", attrs: { "aria-label": copy("path") } });
  const addressForm = element("form", { className: "directory-path-form", attrs: { hidden: "" } });
  const pathInput = element("input", { attrs: { maxlength: 2048, autocomplete: "off", spellcheck: "false", "aria-label": copy("path"), role: "combobox", "aria-autocomplete": "list", "aria-expanded": "false" } });
  const goButton = element("button", { className: "secondary-button", text: copy("go"), attrs: { type: "submit" } });
  const suggestions = element("div", { className: "directory-suggestions", attrs: { role: "listbox", hidden: "" } });
  const suggestionsId = `${layer.element.id}-suggestions`; suggestions.id = suggestionsId; pathInput.setAttribute("aria-controls", suggestionsId);
  addressForm.append(pathInput, goButton); pathArea.append(crumbs, addressForm, suggestions);
  const editPath = button("editPath", "compose", () => editAddress(), "icon-button directory-edit-path", false);
  toolbar.append(back, forward, up, refresh, pathArea, editPath);
  const controls = element("div", { className: "directory-tools" });
  const locationsToggle = button("locations", "folder", () => {
    const show = !root.classList.contains("show-locations");
    root.classList.toggle("show-locations", show); locationsToggle.setAttribute("aria-expanded", String(show));
  }, "secondary-button directory-locations-toggle");
  const filter = element("input", { attrs: { type: "search", placeholder: copy("filter"), "aria-label": copy("filter"), maxlength: 1024 } });
  const favorite = button("favorite", "pin", () => { const path = navigation.get().current?.path; if (path) { places.favorite(path); renderPlaces(); renderState(); } });
  const newFolder = button("newFolder", "plus", () => { createForm.hidden = false; folderName.focus(); });
  newFolder.hidden = !writable;
  controls.append(locationsToggle, filter, favorite, newFolder);
  const createForm = element("form", { className: "directory-create", attrs: { hidden: "" } });
  const folderName = element("input", { attrs: { maxlength: 255, required: "", autocomplete: "off", placeholder: copy("folderName"), "aria-label": copy("folderName") } });
  const createButton = element("button", { className: "primary-button", text: copy("create"), attrs: { type: "submit" } });
  const cancelCreate = button("cancel", "close", () => { createForm.hidden = true; newFolder.focus(); }, "icon-button", false);
  createForm.append(folderName, createButton, cancelCreate);
  const area = element("div", { className: "directory-main" });
  const sidebar = element("aside", { className: "directory-places", attrs: { "aria-label": copy("locations") } });
  const files = element("div", { className: "directory-results" });
  const list = element("div", { className: "directory-list", attrs: { role: "listbox", "aria-label": copy("children") } });
  const more = button("more", "chevron-down", () => void navigation.more(), "secondary-button directory-more");
  const status = element("p", { className: "directory-status", attrs: { role: "status" } });
  const retry = button("retry", "refresh", () => void navigation.retry()); retry.hidden = true;
  const progress = element("div", { className: "directory-progress" }, [status, retry]);
  files.append(list, more); area.append(sidebar, files); root.append(deviceLabel, toolbar, controls, createForm, progress, area);
  layer.body.append(root);
  const selectedPath = element("p", { className: "directory-selected-path" });
  const cancel = button("cancel", null, () => onCancel());
  const choose = button("choose", null, async () => {
    if (editing && pathInput.value !== navigation.get().current?.path) { await go(pathInput.value); return; }
    const path = await navigation.confirm(); if (path && !layer.signal.aborted) onConfirm(path);
  }, "primary-button");
  layer.footer.classList.add("directory-footer");
  const actions = element("div", { className: "directory-footer-actions" }, [cancel, choose]);
  layer.footer.append(selectedPath, actions);
  let editing = false, addressRevision = 0, completion = null, completionTimer = 0, completionGeneration = 0, completionIndex = -1, filterTimer = 0;
  let locations = [], locationError = false, renderedRows = null, renderedPath = "", renderedFilter = "", selectedRow = "";
  const rowNodes = new Map();
  const isMobile = () => globalThis.matchMedia?.("(max-width: 600px), (pointer: coarse)").matches;
  const navigation = createDirectoryNavigation({ read, create, onChange: renderState });
  function closeSuggestions() {
    ++completionGeneration; clearTimeout(completionTimer); completion?.abort(); completionIndex = -1;
    suggestions.hidden = true; pathInput.setAttribute("aria-expanded", "false"); pathInput.removeAttribute("aria-activedescendant");
  }
  function editAddress() {
    ++addressRevision;
    editing = true; root.dataset.editingPath = "true"; crumbs.hidden = true; addressForm.hidden = false;
    pathInput.value = navigation.get().requestedPath || navigation.get().current?.path || "";
    pathInput.focus(); pathInput.select(); renderState();
  }
  function endAddress() {
    editing = false; delete root.dataset.editingPath; crumbs.hidden = false; addressForm.hidden = true; closeSuggestions();
  }
  async function go(path, settings = {}) {
    if (navigation.get().creating) return;
    clearTimeout(filterTimer);
    const revision = addressRevision;
    closeSuggestions(); root.classList.remove("show-locations"); locationsToggle.setAttribute("aria-expanded", "false");
    if (!settings.filter) filter.value = "";
    await navigation.navigate(path, settings);
    const state = navigation.get();
    if (!state.failure && !state.loading && !layer.signal.aborted && revision === addressRevision) { endAddress(); renderState(); }
  }
  function historyMove(step) {
    const state = navigation.get(), index = state.index + step;
    if (index >= 0 && index < state.history.length) return go(state.history[index], { historyIndex: index });
  }
  function renderPlaces() {
    sidebar.replaceChildren();
    const group = (key, rows) => {
      if (!rows.length) return;
      sidebar.append(element("h3", { text: copy(key) }));
      for (const row of rows) {
        const item = element("button", { className: "directory-place", attrs: { type: "button", title: row.path } }, [icon(row.icon || "folder"),
          element("span", { text: row.name || copy(row.kind) })]);
        on(item, "click", () => void go(row.path)); sidebar.append(item);
      }
    };
    group("quickLocations", locations.filter(row => row.kind !== "root"));
    group("roots", locations.filter(row => row.kind === "root").map(row => ({ ...row, name: row.path, icon: "monitor" })));
    group("favorites", places.get().favorites.map(path => ({ path, name: path, icon: "pin" })));
    group("recent", places.get().recent.map(path => ({ path, name: path, icon: "clock" })));
    if (locationError) sidebar.append(element("p", { className: "directory-location-error", text: copy("locationsFailed") }), button("retryLocations", "refresh", onReloadLocations));
    if (storageAccess) sidebar.append(button("storageAccess", "shield", () => storageAccess()));
    mountIcons(sidebar);
  }
  function renderState() {
    const state = navigation.get(), data = state.current;
    const blocked = state.loading || Boolean(state.failure) || !data || (editing && pathInput.value !== data?.path);
    root.setAttribute("aria-busy", String(state.loading));
    for (const node of [back, forward, up, refresh, newFolder, favorite, goButton, filter, folderName, createButton]) node.disabled = state.creating;
    back.disabled ||= state.index <= 0; forward.disabled ||= state.index + 1 >= state.history.length;
    up.disabled ||= !data?.parent; refresh.disabled ||= !data;
    newFolder.disabled ||= blocked; favorite.disabled ||= blocked;
    pathInput.disabled = state.creating; editPath.disabled = state.creating;
    cancel.disabled = state.creating; createButton.disabled ||= blocked;
    choose.disabled = blocked || (editing && pathInput.value !== data?.path);
    more.hidden = !data?.has_more; more.disabled = state.loading;
    retry.hidden = !state.failure;
    progress.classList.toggle("has-error", Boolean(state.failure));
    setUiText(status, state.failure ? state.failure.code === "directory_read_timeout" ? copy("readTimeout") : errorMessage(state.failure)
      : state.loading || !data ? copy(state.creating ? "creating" : "loading")
      : data.truncated ? copy("truncated") : !data.directories.length ? copy(state.filter ? "noMatches" : "empty")
      : copy("count", { shown: data.directories.length, total: data.total ?? data.directories.length }));
    refresh.classList.toggle("is-loading", state.loading);
    const chosen = data ? state.selected ? directoryChild(data, state.selected) : data.path : "";
    setUiText(selectedPath, chosen ? copy("selectedPath", { path: chosen }) : ""); selectedPath.title = chosen;
    setUiText(choose, state.selected ? copy("chooseNamed", { name: state.selected }) : copy("choose"));
    favorite.setAttribute("aria-pressed", String(places.get().favorites.includes(data?.path)));
    setUiAttribute(favorite, "title", copy(places.get().favorites.includes(data?.path) ? "unfavorite" : "favorite"));
    if (!data) return;
    if (renderedPath !== data.path) {
      crumbs.replaceChildren(...directoryBreadcrumbs(data.path, data.separator).flatMap((row, index) => {
        const node = element("button", { attrs: { type: "button", title: row.path }, text: row.name });
        on(node, "click", () => void go(row.path)); return index ? [icon("chevron-right"), node] : [node];
      }));
      mountIcons(crumbs);
      if (!editing) pathInput.value = data.path;
    }
    if (renderedRows !== data.directories) {
      const oldFocus = list.contains(document.activeElement) ? document.activeElement?.dataset.directoryName : "";
      const nodes = [];
      for (const name of data.directories) {
        const key = `${data.path}\n${name}`;
        let row = rowNodes.get(key);
        if (!row) {
          row = element("div", { className: "directory-item" });
          const main = element("button", { className: "directory-item-name", attrs: { type: "button", role: "option", title: name, "data-directory-name": name } }, [icon("folder"), element("span", { text: name })]);
          const enter = button("enter", "chevron-right", () => void go(directoryChild(data, name)), "icon-button directory-enter", false);
          setUiAttribute(enter, "aria-label", copy("enterNamed", { name }));
          on(main, "click", () => { if (isMobile()) void go(directoryChild(data, name)); else navigation.select(name); });
          on(main, "dblclick", () => { if (!isMobile()) void go(directoryChild(data, name)); });
          row.append(main, enter); rowNodes.set(key, row);
        }
        nodes.push(row);
      }
      list.replaceChildren(...nodes); mountIcons(list);
      for (const [key, node] of rowNodes) if (!nodes.includes(node)) rowNodes.delete(key);
      if (renderedPath !== data.path || renderedFilter !== state.filter) list.scrollTop = 0;
      if (oldFocus) [...list.querySelectorAll("[data-directory-name]")].find(node => node.dataset.directoryName === oldFocus)?.focus({ preventScroll: true });
      renderedRows = data.directories;
    }
    for (const node of list.querySelectorAll("[role=option]")) {
      node.setAttribute("aria-selected", String(node.dataset.directoryName === state.selected)); node.disabled = state.creating;
    }
    for (const node of root.querySelectorAll(".directory-enter, .directory-place")) node.disabled = state.creating;
    if (selectedRow !== state.selected && state.selected) [...list.querySelectorAll("[aria-selected=true]")][0]?.scrollIntoView({ block: "nearest" });
    selectedRow = state.selected; renderedPath = data.path; renderedFilter = state.filter;
    if (filter.value !== state.filter && document.activeElement !== filter) filter.value = state.filter;
  }
  on(crumbs, "click", event => { if (event.target === crumbs) editAddress(); });
  on(addressForm, "submit", event => { event.preventDefault(); if (!navigation.get().creating) void go(pathInput.value); });
  on(pathInput, "input", () => {
    ++addressRevision;
    closeSuggestions(); renderState(); const own = completionGeneration;
    completionTimer = setTimeout(async () => {
      const parent = directoryCompletion(pathInput.value, navigation.get().current?.separator || (pathInput.value.includes("\\") ? "\\" : "/"));
      if (!parent || !editing) return;
      const typed = pathInput.value; completion = new AbortController();
      try {
        const result = await read(parent.path, completion.signal, { prefix: parent.prefix, limit: 20 });
        if (layer.signal.aborted || own !== completionGeneration || typed !== pathInput.value || !editing) return;
        suggestions.replaceChildren(...result.directories.map((name, index) => {
          const item = element("button", { text: name, attrs: { type: "button", role: "option", id: `${suggestionsId}-${index}` } });
          on(item, "click", () => { void go(directoryChild(result, name)); }); return item;
        }));
        suggestions.hidden = !result.directories.length; pathInput.setAttribute("aria-expanded", String(!suggestions.hidden));
      } catch { /* Completion is optional; explicit navigation reports errors. */ }
    }, 180);
  });
  on(pathInput, "keydown", event => {
    if (isImeKey(event)) { if (event.key === "Enter") event.preventDefault(); return; }
    if (event.key === "Escape") { event.preventDefault(); event.stopPropagation(); if (!suggestions.hidden) closeSuggestions(); else { endAddress(); editPath.focus(); renderState(); } }
    else if (["ArrowDown", "ArrowUp"].includes(event.key) && !suggestions.hidden) {
      event.preventDefault(); const rows = [...suggestions.children];
      completionIndex = (completionIndex + (event.key === "ArrowDown" ? 1 : -1) + rows.length) % rows.length;
      for (const [index, node] of rows.entries()) node.setAttribute("aria-selected", String(index === completionIndex));
      pathInput.setAttribute("aria-activedescendant", rows[completionIndex].id);
    } else if ((event.key === "Enter" || event.key === "Tab") && completionIndex >= 0 && !suggestions.hidden) {
      event.preventDefault(); suggestions.children[completionIndex].click();
    }
  });
  on(list, "keydown", event => {
    if (isImeKey(event) || !event.target.matches("[role=option]")) return;
    const state = navigation.get(), rows = [...list.querySelectorAll("[role=option]")], index = rows.indexOf(event.target);
    if (event.key === "Enter") { event.preventDefault(); void go(directoryChild(state.current, event.target.dataset.directoryName)); }
    else if (["ArrowDown", "ArrowUp", "Home", "End"].includes(event.key)) {
      event.preventDefault(); const next = event.key === "Home" ? 0 : event.key === "End" ? rows.length - 1 : Math.max(0, Math.min(rows.length - 1, index + (event.key === "ArrowDown" ? 1 : -1)));
      navigation.select(rows[next].dataset.directoryName); rows[next].focus();
    }
  });
  on(layer.element, "keydown", event => {
    if (isImeKey(event)) return;
    if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "l") { event.preventDefault(); editAddress(); }
    if (event.altKey && event.key === "ArrowLeft") { event.preventDefault(); void historyMove(-1); }
    if (event.altKey && event.key === "ArrowRight") { event.preventDefault(); void historyMove(1); }
    if (event.altKey && event.key === "ArrowUp") { event.preventDefault(); up.click(); }
  });
  on(filter, "input", () => { clearTimeout(filterTimer); filterTimer = setTimeout(() => {
    const path = navigation.get().current?.path; if (path) void navigation.navigate(path, { replace: true, filter: filter.value });
  }, 200); });
  on(createForm, "submit", async event => {
    event.preventDefault(); if (!createForm.reportValidity()) return;
    if (await navigation.mkdir(folderName.value)) { createForm.hidden = true; folderName.value = ""; filter.value = navigation.get().filter; }
  });
  on(folderName, "keydown", event => { if (event.key === "Enter" && isImeKey(event)) event.preventDefault(); });
  const unsubscribe = subscribeLocale(() => { renderPlaces(); renderState(); });
  layer.signal.addEventListener("abort", () => { closeSuggestions(); clearTimeout(filterTimer); navigation.dispose(); unsubscribe(); }, { once: true });
  renderPlaces(); renderState(); mountIcons(root);
  return { navigate: go, busy: () => navigation.get().creating,
    setLocations(value, failed = false) { locations = value?.locations || []; locationError = failed; renderPlaces(); },
    focusPath: editAddress,
  };
}
