import { api, ApiError } from "../api/client.js";
import { createResourceStore } from "./store.js";
import { resolveLocale } from "../i18n.js";
import { isTransientReadError } from "../api/read-recovery.js";

function documentFor(patch) {
  return { schema_version: 1, patch };
}

// Startup settles its first read promptly. An accepted save instead waits for
// the final confirmed snapshot; background recovery never repeats that write.
function waitForSettingsRead(store) {
  return new Promise((resolve, reject) => {
    let unsubscribe;
    let finished = false;
    const observe = state => {
      if (!["ready", "error", "idle"].includes(state.status)) return;
      finished = true;
      unsubscribe?.();
      if (state.status === "idle") reject(new DOMException("Settings read cancelled", "AbortError"));
      else resolve(state);
    };
    unsubscribe = store.subscribe(observe);
    if (finished) unsubscribe();
  });
}

export function createSettingsState({
  store = createResourceStore(null, { recoverRead: isTransientReadError }),
  client = api, resolveLanguage = resolveLocale,
} = {}) {
  async function readSettings(signal) {
    const response = await client.get("/settings", { signal });
    if (signal?.aborted) throw new DOMException("Settings read cancelled", "AbortError");
    return { ...response.data, etag: response.etag };
  }
  async function load({ waitForRecovery = false, initializeLocale = true } = {}) {
    let initializationAttempted = false;
    await store.load(async signal => {
      let settings = await readSettings(signal);
      if (initializeLocale && !initializationAttempted &&
          settings.locale === "auto" && settings.etag) {
        // One optional, revisioned initialization per load. If its reply or
        // the following read is lost, subsequent attempts only read settings.
        initializationAttempted = true;
        let initializationAccepted = false;
        try {
          await client.patch("/settings/settings", documentFor({ locale: resolveLanguage() }),
            { ifMatch: settings.etag, signal });
          initializationAccepted = true;
        } catch (error) {
          if (![409, 412].includes(error?.status) && error?.name !== "AbortError" &&
              !isTransientReadError(error))
            console.warn("Initial language preference could not be saved", error);
        }
        try {
          settings = await readSettings(signal);
        } catch (error) {
          if (signal?.aborted || !initializationAccepted || !isTransientReadError(error)) throw error;
          // An accepted language initialization can use the readable snapshot
          // while its echo is offline. An uncertain write still needs recovery.
        }
      }
      return settings.locale === "auto"
        ? { ...settings, locale: resolveLanguage() } : settings;
    });
    return waitForRecovery ? waitForSettingsRead(store) : store.get();
  }
  async function confirmRead() {
    const loaded = await load({ waitForRecovery: true, initializeLocale: false });
    if (loaded.status !== "ready")
      throw new ApiError("Settings were saved, but the latest snapshot could not be read", {
        code: "settings_confirmation_failed", status: loaded.error?.status,
        details: { read_error: loaded.error?.code ?? "request_failed" },
      });
  }
  return Object.freeze({
    store, load,
    recover() {
      const state = store.get();
      return state.status === "error" && isTransientReadError(state.error)
        ? load({ initializeLocale: false }) : Promise.resolve(state);
    },
    async preview(patch) {
      return (await client.patch("/settings/settings/preview", documentFor(patch))).data;
    },
    async apply(patch, etag) {
      const result = (await client.patch("/settings/settings", documentFor(patch), { ifMatch: etag })).data;
      await confirmRead();
      return result;
    },
    async restore(etag) {
      const result = (await client.delete("/settings/settings", { ifMatch: etag })).data;
      await confirmRead();
      return result;
    },
  });
}

const settings = createSettingsState();
export const settingsStore = settings.store;
export const loadSettings = settings.load;
export const recoverSettings = settings.recover;
export const previewSettings = settings.preview;
export const applySettings = settings.apply;
export const restoreSettings = settings.restore;
