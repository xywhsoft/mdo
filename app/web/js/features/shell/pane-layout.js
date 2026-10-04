import { api } from "../../api/client.js";
import { errorMessage, toast } from "../../utils/dom.js";
import { t } from "../../i18n.js";

const defaults = Object.freeze({
  sidebar_width: 272, sidebar_open: true,
});

function clamp(value, minimum, maximum) {
  return Math.max(minimum, Math.min(maximum, Math.round(value)));
}

export function createPaneLayout({ shell, mobileLayout, sidebarHandle, onLoaded }) {
  const preference = { ...defaults };
  const edited = new Set();
  let ready = false;
  let loading = null;
  let dirty = false;
  let saveTimer = 0;
  let saving = false;
  let saveAgain = false;

  function bounds() {
    return { min: 264, max: Math.max(264, Math.min(420, window.innerWidth - 640)) };
  }

  function apply() {
    const sidebar = shell.dataset.sidebar === "open" && !mobileLayout.matches
      ? clamp(preference.sidebar_width, 264, bounds().max) : 0;
    shell.style.setProperty("--sidebar-column", `${sidebar}px`);
    sidebarHandle.setAttribute("aria-valuenow", String(sidebar || preference.sidebar_width));
    sidebarHandle.setAttribute("aria-valuemax", String(bounds().max));
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

  function remember(open) {
    if (mobileLayout.matches) {
      apply();
      return;
    }
    const key = "sidebar_open";
    edited.add(key);
    if (preference[key] !== open || !ready) {
      preference[key] = open;
      dirty = true;
      scheduleSave();
    }
    apply();
  }

  function setWidth(width, save) {
    const key = "sidebar_width";
    const range = bounds();
    preference[key] = clamp(width, range.min, range.max);
    edited.add(key);
    keepCurrentPanel();
    apply();
    if (save) { dirty = true; scheduleSave(); }
  }

  function keepCurrentPanel() {
    const key = "sidebar_open";
    edited.add(key);
    preference[key] = shell.dataset.sidebar === "open";
  }

  function wireHandle(handle) {
    let startX = 0;
    let startWidth = 0;
    handle.addEventListener("pointerdown", (event) => {
      if (mobileLayout.matches) return;
      event.preventDefault();
      keepCurrentPanel();
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
      setWidth(startWidth + delta, false);
    });
    handle.addEventListener("pointerup", (event) => {
      if (!handle.hasPointerCapture(event.pointerId)) return;
      const delta = event.clientX - startX;
      setWidth(startWidth + delta, true);
      handle.releasePointerCapture(event.pointerId);
      delete handle.dataset.dragging;
      delete shell.dataset.dragging;
    });
    handle.addEventListener("pointercancel", (event) => {
      if (!handle.hasPointerCapture(event.pointerId)) return;
      setWidth(startWidth, false);
      handle.releasePointerCapture(event.pointerId);
      delete handle.dataset.dragging;
      delete shell.dataset.dragging;
    });
    handle.addEventListener("keydown", (event) => {
      const direction = { ArrowLeft: -1, ArrowRight: 1 }[event.key];
      if (!direction && event.key !== "Home" && event.key !== "End") return;
      event.preventDefault();
      const range = bounds();
      const current = Number(handle.getAttribute("aria-valuenow"));
      const next = event.key === "Home" ? range.min : event.key === "End"
        ? range.max : current + direction * (event.shiftKey ? 24 : 8);
      setWidth(next, true);
    });
  }

  wireHandle(sidebarHandle);
  window.addEventListener("resize", apply);
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
    sidebarOpen: () => preference.sidebar_open });
}
