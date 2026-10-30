import { setUiText as writeUiText, uiMessage, t } from "../../i18n.js";

import { element, toast, errorMessage, isImeKey } from "../../utils/dom.js";

import { currentPageWriteToken } from "../../api/client.js";
import { targetState } from "../../api/target.js";
import { moveSidebarItem, emptySidebarLayout } from "./sidebar-layout.js";
import { openLayer } from "../../components/layers.js";

const epoch = () => JSON.stringify([targetState().selected, targetState().runtimeChanged, currentPageWriteToken()]);
export function createSidebarOrganizer({ container, store, render }) {
  let drag = null, dialog = null;
  async function save(change) {
    try { return await store.update(change); }
    catch (error) { if (error?.name !== "AbortError") toast(errorMessage(error), "error"); return false; }
  }
  function close() { const previous = dialog; dialog = null; previous?.dispose(); }
  function form(title, description, fields, commit, opener = document.activeElement) {
    close(); if (!store.writable()) return;
    const origin = epoch(), revision = store.get().revision;
    const focusKey = opener?.dataset.sidebarFocus;
    let busy = false;
    let source;
    source = openLayer({ title, icon: "folder", width: "28rem", height: "auto", owner: container, opener,
      beforeClose: () => !busy,
      onClosed({ reason }) {
        if (dialog === source) dialog = null;
        if (reason === "dispose") return;
        const target = opener?.isConnected ? opener : [...container.querySelectorAll("[data-sidebar-focus]")]
          .find(node => node.dataset.sidebarFocus === focusKey) || container.querySelector('[data-sidebar-focus="list-options"]');
        target?.focus({ preventScroll: true });
      } });
    dialog = source; source.element.classList.add("sidebar-organizer-dialog");
    const formId = `${source.element.id}-form`;
    const cancel = element("button", { className: "secondary-button", text: uiMessage("sidebar.cancel"), attrs: { type: "button" } });
    const submit = element("button", { className: "primary-button", text: uiMessage("sidebar.save"), attrs: { type: "submit", form: formId } });
    const error = element("p", { className: "dialog-error", attrs: { role: "alert" } }); error.hidden = true;
    const hint = element("p", { className: "sidebar-organizer-description", text: description, attrs: { id: `${formId}-description` } });
    source.element.setAttribute("aria-describedby", hint.id);
    const card = element("form", { className: "sidebar-organizer-form", attrs: { id: formId } },
      [element("div", { className: "dialog-fields" }, fields), error]);
    source.body.append(hint, card); source.footer.append(cancel, submit);
    const options = { signal: source.signal };
    cancel.addEventListener("click", () => { void source.close("cancel"); }, options);
    card.addEventListener("submit", async event => {
      event.preventDefault(); if (busy || dialog !== source || source.signal.aborted) return;
      if (origin !== epoch() || revision !== store.get().revision || !store.writable()) { writeUiText(error, uiMessage("sidebar.changed")); error.hidden = false; return; }
      busy = true; error.hidden = true; submit.disabled = cancel.disabled = source.closeButton.disabled = true;
      try {
        const saved = await commit();
        if (dialog !== source || source.signal.aborted) return;
        busy = false;
        if (saved) await source.close("saved");
        else { writeUiText(error, uiMessage("sidebar.changed")); error.hidden = false; }
      }
      catch (failure) { if (dialog === source && !source.signal.aborted) { writeUiText(error, errorMessage(failure)); error.hidden = false; } }
      finally { busy = false; if (!source.signal.aborted) submit.disabled = cancel.disabled = source.closeButton.disabled = false; }
    }, options);
    const first = card.querySelector("input,select") || submit; first.focus({ preventScroll: true }); first.select?.();
  }
  function groupForm(group = null) {
    const input = element("input", { attrs: { required: "", maxlength: "80", "aria-label": uiMessage("sidebar.groupName"), autocomplete: "off" } });
    input.value = group?.name || "";
    form(t(group ? "sidebar.renameGroup" : "sidebar.newGroup"), t("sidebar.groupHint"), [input], async () => {
      const name = input.value.trim();
      if (!name || new TextEncoder().encode(name).length > 256 || /[\x00-\x1f\x7f]/.test(name)) throw new Error(t("sidebar.nameInvalid"));
      if (!group && store.get().groups.length >= 32) throw new Error(t("sidebar.limit"));
      const ok = await store.update(value => {
        if (group) { const target = value.groups.find(item => item.id === group.id); if (!target) throw new Error(t("sidebar.changed")); target.name = name; }
        else { const id = [...crypto.getRandomValues(new Uint8Array(16))].map(item => item.toString(16).padStart(2, "0")).join(""); value.groups.push({ id, name }); }
        value.view = "workspace"; return value;
      }); return ok;
    });
  }
  function move(key) {
    const input = element("select", { attrs: { "aria-label": uiMessage("sidebar.destination") } });
    input.append(element("option", { text: uiMessage("sidebar.originalLocation"), attrs: { value: "" } }));
    for (const group of store.get().groups) input.append(element("option", { text: group.name, attrs: { value: group.id } }));
    input.value = store.get().placements.find(item => item.key === key)?.group || "";
    form(t("sidebar.moveToGroup"), t("sidebar.groupHint"), [input], () => store.update(value => {
      const next = moveSidebarItem(value, key, input.value); next.view = "workspace"; return next;
    }));
  }
  function remove(group) {
    form(t("sidebar.deleteGroup"), t("sidebar.deleteHint"), [], () => store.update(value => ({ ...value,
      groups: value.groups.filter(item => item.id !== group.id), placements: value.placements.map(item => item.group === group.id ? { ...item, group: "" } : item),
      collapsed: value.collapsed.filter(key => key !== `section:custom-${group.id}`) })));
  }
  function reset() {
    form(t("sidebar.reset"), t("sidebar.resetHint"), [], () => store.update(value => ({ ...emptySidebarLayout(), revision: value.revision })));
  }
  function clearDrop() { container.querySelectorAll(".sidebar-drop-before,.sidebar-drop-after,.sidebar-drop-inside")
    .forEach(node => node.classList.remove("sidebar-drop-before", "sidebar-drop-after", "sidebar-drop-inside")); }
  function end(commit = false) {
    if (!drag) return;
    const saved = drag; drag = null; clearDrop(); saved.row.classList.remove("sidebar-dragging");
    try { saved.handle.releasePointerCapture(saved.pointer); } catch { /* Pointer already released. */ }
    if (commit && saved.moved && saved.target && saved.origin === epoch() && saved.revision === store.get().revision) {
      const target = saved.target;
      void save(value => {
        if (saved.key.startsWith("g:")) {
          const item = value.groups.find(group => group.id === saved.key.slice(2));
          value.groups = value.groups.filter(group => group !== item);
          const index = value.groups.findIndex(group => group.id === target.before.slice(2));
          value.groups.splice(index < 0 ? value.groups.length : index, 0, item); return value;
        }
        return moveSidebarItem(value, saved.key, target.group, target.before, saved.order);
      });
    }
    render();
  }
  const rows = () => [...container.querySelectorAll("[data-sidebar-key]")];
  function pointerDown(event) {
    const handle = event.target.closest?.("[data-sidebar-drag]");
    if (!handle || handle.disabled || !store.writable() || event.button > 0) return;
    const row = handle.closest("[data-sidebar-key]");
    drag = { row, handle, key: row.dataset.sidebarKey, parent: row.dataset.sidebarParent,
      group: row.dataset.sidebarGroup || "", origin: epoch(), revision: store.get().revision, pointer: event.pointerId,
      start: event.clientY, moved: false, order: rows().map(node => node.dataset.sidebarKey) };
    try { handle.setPointerCapture(event.pointerId); } catch { /* Synthetic pointer. */ }
    event.preventDefault();
  }
  function pointerMove(event) {
    if (!drag || event.pointerId !== drag.pointer) return;
    if (!drag.moved && Math.abs(event.clientY - drag.start) < 6) return;
    drag.moved = true; drag.row.classList.add("sidebar-dragging"); clearDrop(); drag.target = null;
    const hit = document.elementFromPoint(event.clientX, event.clientY);
    let row = hit?.closest("[data-sidebar-key]");
    if (drag.key.startsWith("g:")) row = hit?.closest("[data-sidebar-section-id]");
    if (!row || !container.contains(row)) return;
    const key = row.dataset.sidebarKey;
    if (key === drag.key) return;
    const group = row.dataset.sidebarGroup || "", parent = row.dataset.sidebarParent || "";
    if (drag.key.startsWith("g:") && !key?.startsWith("g:")) return;
    if (drag.parent === "pinned" ? parent !== "pinned" : parent === "pinned") return;
    const compatible = drag.key.startsWith("g:") || Boolean(group) || parent === drag.parent;
    if (!compatible) return;
    const box = row.getBoundingClientRect(), after = event.clientY > box.top + box.height / 2;
    if (row.hasAttribute("data-sidebar-drop-zone") && !drag.key.startsWith("g:")) {
      drag.target = { group, before: "" }; row.classList.add("sidebar-drop-inside");
    } else {
      const siblings = rows().filter(node => node.dataset.sidebarParent === parent && node.dataset.sidebarKey !== drag.key);
      const index = siblings.indexOf(row) + (after ? 1 : 0);
      drag.target = { group: drag.parent === "pinned" ? drag.group : group, before: siblings[index]?.dataset.sidebarKey || "" };
      row.classList.add(after ? "sidebar-drop-after" : "sidebar-drop-before");
    }
    const boxList = container.getBoundingClientRect();
    if (event.clientY < boxList.top + 30) container.scrollTop -= 14;
    else if (event.clientY > boxList.bottom - 30) container.scrollTop += 14;
    event.preventDefault();
  }
  function keyboard(event) {
    if (isImeKey(event) || event.ctrlKey || event.metaKey || event.altKey) return;
    if (event.key === "Escape" && drag) { event.preventDefault(); event.stopPropagation(); end(); return; }
    const handle = event.target.closest?.("[data-sidebar-drag]");
    if (!handle || handle.disabled || !["ArrowUp", "ArrowDown"].includes(event.key)) return;
    event.preventDefault(); event.stopPropagation();
    const row = handle.closest("[data-sidebar-key]"), key = row.dataset.sidebarKey;
    const peers = rows().filter(item => item.dataset.sidebarParent === row.dataset.sidebarParent && item.dataset.sidebarKey.startsWith(key.slice(0, 2)));
    const at = peers.indexOf(row), next = at + (event.key === "ArrowUp" ? -1 : 1);
    if (next < 0 || next >= peers.length) return;
    const before = event.key === "ArrowUp" ? peers[next].dataset.sidebarKey : peers[next + 1]?.dataset.sidebarKey || "";
    void save(value => {
      if (key.startsWith("g:")) {
        const item = value.groups.splice(at, 1)[0]; value.groups.splice(next, 0, item); return value;
      }
      return moveSidebarItem(value, key, row.dataset.sidebarGroup || "", before, peers.map(item => item.dataset.sidebarKey));
    });
  }
  const pointerUp = event => { if (event.pointerId === drag?.pointer) end(true); };
  const cancel = () => end();
  container.addEventListener("pointerdown", pointerDown); container.addEventListener("pointermove", pointerMove);
  container.addEventListener("pointerup", pointerUp); container.addEventListener("pointercancel", cancel);
  container.addEventListener("lostpointercapture", cancel); container.addEventListener("keydown", keyboard);
  return { save, groupForm, move, remove, reset, close, dragging: () => Boolean(drag), cancel,
    destroy() { close(); end(); container.removeEventListener("pointerdown", pointerDown); container.removeEventListener("pointermove", pointerMove);
      container.removeEventListener("pointerup", pointerUp); container.removeEventListener("pointercancel", cancel);
      container.removeEventListener("lostpointercapture", cancel); container.removeEventListener("keydown", keyboard); } };
}
