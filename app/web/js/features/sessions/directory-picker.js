import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { uiMessage } from "../../i18n.js";
import { openLayer } from "../../components/layers.js";
import { createDirectoryBrowser } from "./directory-browser.js";
import { directoryPlaces } from "./directory-navigation.js";
export { directoryChild } from "./directory-navigation.js";

export function directoryDeviceScope() {
  const selected = targetState().selected;
  return selected ? `remote:${selected.owner}:${selected.id}` : "local";
}

export function createDirectoryPicker({
  read = (path, signal, query = {}) => api.get(`/workspace/directories?${Object.entries({ path, ...query })
    .map(([key, value]) => `${key}=${encodeURIComponent(value)}`).join("&")}`, { signal }).then(result => result.data),
  locations = signal => api.get("/workspace/locations", { signal }).then(result => result.data),
  create = (path, name, signal) => api.post("/workspace/directories", { path, name }, { signal }).then(result => result.data),
  scope = directoryDeviceScope, windowFactory = openLayer,
} = {}) {
  let active = null;
  const current = own => active === own && !own.layer.signal.aborted && own.scope === scope();
  async function loadLocations(own) {
    const controller = new AbortController(), abort = () => controller.abort();
    own.layer.signal.addEventListener("abort", abort, { once: true });
    const timer = setTimeout(abort, 8000);
    try {
      const value = await locations(controller.signal);
      if (current(own)) own.browser.setLocations(value);
      return value;
    } catch {
      if (current(own)) own.browser.setLocations(null, true);
      return null;
    } finally { clearTimeout(timer); own.layer.signal.removeEventListener("abort", abort); }
  }
  const unsubscribe = subscribeTarget(() => { if (active && active.scope !== scope()) active.layer.dispose(); });
  return {
    open(initialPath, onSelect, options = {}) {
      active?.layer.dispose();
      const selected = targetState().selected;
      let storage; try { storage = globalThis.localStorage; } catch { /* Optional. */ }
      const own = { scope: scope(), places: directoryPlaces(storage, scope()), chosen: "", layer: null, browser: null };
      own.layer = windowFactory({ title: uiMessage("directory.title"), icon: "folder", width: "64rem", height: "42rem",
        mobile: "fullscreen", beforeClose: () => !own.browser?.busy(),
        onClosed(result) {
          if (active === own) active = null;
          if (result.reason === "select" && own.chosen && own.scope === scope()) {
            own.places.confirm(own.chosen); onSelect?.(own.chosen);
          }
          options.onClosed?.(result);
        } });
      active = own; own.layer.element.classList.add("directory-window");
      own.browser = createDirectoryBrowser({ layer: own.layer, read, create, places: own.places,
        onReloadLocations: () => { void loadLocations(own); },
        device: selected?.name || uiMessage("directory.local"), writable: selected?.mode !== "view",
        storageAccess: !selected && typeof globalThis.XsPlatform?.requestStorageAccess === "function"
          ? () => globalThis.XsPlatform.requestStorageAccess() : null,
        onConfirm(path) { if (current(own)) { own.chosen = path; void own.layer.close("select"); } },
        onCancel() { void own.layer.close("cancel"); } });
      const metadata = loadLocations(own);
      own.ready = (async () => {
        let path = initialPath || own.places.get().recent[0];
        if (!path) path = (await metadata)?.locations?.find(row => row.kind === "home")?.path || "";
        if (current(own)) await own.browser.navigate(path);
      })();
      return own.ready;
    },
    close() { active?.layer.dispose(); },
    destroy() { this.close(); unsubscribe(); },
  };
}
