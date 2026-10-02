import { api } from "../../api/client.js";
import { errorMessage, toast } from "../../utils/dom.js";
import { t } from "../../i18n.js";

const defaults = Object.freeze({
  sidebar_width: 272, inspector_width: 336,
  sidebar_open: true, inspector_open: false,
});

function clamp(value, minimum, maximum) {
  return Math.max(minimum, Math.min(maximum, Math.round(value)));
}

export function createPaneLayout({ shell, mobileLayout, wideLayout,
  sidebarHandle, inspectorHandle, onLoaded }) {
  const preference = { ...defaults };
  const edited = new Set();
  let ready = false;
  let loading = null;
  let dirty = false;
  let saveTimer = 0;
  let saving = false;
  let saveAgain = false;

  function bounds(side) {
    const docked = wideLayout.matches && shell.dataset.inspector === "open";
    if (side === "sidebar") return {
      min: 264,
      max: Math.max(264, Math.min(420, window.innerWidth -
        (docked ? 300 : 0) - 640)),
    };
    const sidebar = shell.dataset.sidebar === "open"
      ? Number(sidebarHandle.getAttribute("aria-valuenow")) || 264 : 0;
    return {
      min: 300,
      max: Math.max(300, Math.min(520, window.innerWidth - sidebar - 640)),
    };
  }

  function apply() {
    const sidebar = shell.dataset.sidebar === "open" && !mobileLayout.matches
      ? clamp(preference.sidebar_width, 264, bounds("sidebar").max) : 0;
    shell.style.setProperty("--sidebar-column", `${sidebar}px`);
    shell.style.setProperty("--inspector-width", `${preference.inspector_width}px`);
    sidebarHandle.setAttribute("aria-valuenow", String(sidebar || preference.sidebar_width));
    sidebarHandle.setAttribute("aria-valuemax", String(bounds("sidebar").max));
    const inspector = wideLayout.matches && shell.dataset.inspector === "open"
      ? clamp(preference.inspector_width, 300,
        Math.max(300, Math.min(520, window.innerWidth - sidebar - 640))) : 0;
    shell.style.setProperty("--inspector-column", `${inspector}px`);
    inspectorHandle.setAttribute("aria-valuenow", String(inspector || preference.inspector_width));
    inspectorHandle.setAttribute("aria-valuemax", String(bounds("inspector").max));
  }

  async function flush() {
    if (saving) { saveAgain = true; return; }
    saving = true;
    // Do not send default values for untouched fields before their first read.
    // A resize/toggle can happen while startup settings are still in flight.
    if (!ready) await load();
    if (!ready) { saving = false; return; }
    do {
      saveAgain = false;
      const snapshot = { ...preference };
      dirty = false;
      try { await api.put("/pane-layout", snapshot); }
      catch (cause) {
        dirty = true;
        const error = errorMessage(cause);
        toast(t("pane.saveFailed", { error }, `无法保存分栏布局：${error}`), "error");
        break;
      }
    } while (saveAgain || dirty);
    saving = false;
  }

  function scheduleSave() {
    window.clearTimeout(saveTimer);
    saveTimer = window.setTimeout(() => { void flush(); }, 250);
  }

  function remember(side, open) {
    if ((side === "sidebar" && mobileLayout.matches) ||
        (side === "inspector" && !wideLayout.matches)) {
      apply();
      return;
    }
    const key = side === "sidebar" ? "sidebar_open" : "inspector_open";
    edited.add(key);
    if (preference[key] !== open || !ready) {
      preference[key] = open;
      dirty = true;
      scheduleSave();
    }
    apply();
  }

  function setWidth(side, width, save) {
    const key = side === "sidebar" ? "sidebar_width" : "inspector_width";
    const range = bounds(side);
    preference[key] = clamp(width, range.min, range.max);
    edited.add(key);
    keepCurrentPanel(side);
    apply();
    if (save) { dirty = true; scheduleSave(); }
  }

  function keepCurrentPanel(side) {
    const key = side === "sidebar" ? "sidebar_open" : "inspector_open";
    edited.add(key);
    preference[key] = shell.dataset[side] === "open";
  }

  function wireHandle(handle, side) {
    let startX = 0;
    let startWidth = 0;
    handle.addEventListener("pointerdown", (event) => {
      if (mobileLayout.matches || (side === "inspector" && !wideLayout.matches)) return;
      event.preventDefault();
      keepCurrentPanel(side);
      handle.focus({ preventScroll: true });
      startX = event.clientX;
      startWidth = Number(handle.getAttribute("aria-valuenow"));
      handle.setPointerCapture(event.pointerId);
      handle.dataset.dragging = "true";
      shell.dataset.dragging = "true";
    });
    handle.addEventListener("pointermove", (event) => {
      if (!handle.hasPointerCapture(event.pointerId)) return;
      const delta = event.clientX - startX;
      setWidth(side, startWidth + (side === "sidebar" ? delta : -delta), false);
    });
    handle.addEventListener("pointerup", (event) => {
      if (!handle.hasPointerCapture(event.pointerId)) return;
      const delta = event.clientX - startX;
      setWidth(side, startWidth + (side === "sidebar" ? delta : -delta), true);
      handle.releasePointerCapture(event.pointerId);
      delete handle.dataset.dragging;
      delete shell.dataset.dragging;
    });
    handle.addEventListener("pointercancel", (event) => {
      if (!handle.hasPointerCapture(event.pointerId)) return;
      setWidth(side, startWidth, false);
      handle.releasePointerCapture(event.pointerId);
      delete handle.dataset.dragging;
      delete shell.dataset.dragging;
    });
    handle.addEventListener("keydown", (event) => {
      const direction = { ArrowLeft: -1, ArrowRight: 1 }[event.key];
      if (!direction && event.key !== "Home" && event.key !== "End") return;
      event.preventDefault();
      const range = bounds(side);
      const current = Number(handle.getAttribute("aria-valuenow"));
      const next = event.key === "Home" ? range.min : event.key === "End"
        ? range.max : current + direction * (event.shiftKey ? 24 : 8) *
          (side === "sidebar" ? 1 : -1);
      setWidth(side, next, true);
    });
  }

  wireHandle(sidebarHandle, "sidebar");
  wireHandle(inspectorHandle, "inspector");
  window.addEventListener("resize", apply);
  wideLayout.addEventListener("change", apply);
  mobileLayout.addEventListener("change", apply);
  apply();

  function load() {
    if (loading) return loading;
    if (ready) return Promise.resolve();
    loading = (async () => {
      try {
        const saved = (await api.get("/pane-layout")).data;
        for (const key of Object.keys(defaults))
          if (!edited.has(key)) preference[key] = saved[key];
        ready = true;
        onLoaded({ ...preference });
        apply();
        if (dirty && !saving) scheduleSave();
      } catch (cause) {
        const error = errorMessage(cause);
        toast(t("pane.loadFailed", { error }, `无法读取分栏布局：${error}`), "error");
      }
    })().finally(() => { loading = null; });
    return loading;
  }

  return Object.freeze({ apply, load, remember,
    sidebarOpen: () => preference.sidebar_open,
    inspectorOpen: () => preference.inspector_open });
}
