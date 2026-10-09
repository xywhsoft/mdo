import { api, ApiError, allowsApiWrite } from "../api/client.js";
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
  canInitializeLocale = () => allowsApiWrite("/settings/settings", { method: "PATCH" }),
} = {}) {
  let localePending = false, localeGeneration = 0;
  async function readSettings(signal) {
    const response = await client.get("/settings", { signal });
    if (signal?.aborted) throw new DOMException("Settings read cancelled", "AbortError");
    return { ...response.data, etag: response.etag };
  }
  async function initializeLanguage(settings, signal) {
    let accepted = false;
    try {
      await client.patch("/settings/settings", documentFor({ locale: resolveLanguage() }),
        { ifMatch: settings.etag, signal });
      accepted = true;
    } catch (error) {
      if (![409, 412].includes(error?.status) && error?.name !== "AbortError" &&
          !isTransientReadError(error))
        console.warn("Initial language preference could not be saved", error);
    }
    try { return await readSettings(signal); }
    catch (error) {
      if (signal?.aborted || !accepted || !isTransientReadError(error)) throw error;
      // An accepted initialization can use the readable snapshot while its
      // echo is offline. An uncertain write still needs read-only recovery.
      return settings;
    }
  }
  async function load({ waitForRecovery = false, initializeLocale = true } = {}) {
    let initializationAttempted = false;
    const generation = localeGeneration;
    await store.load(async signal => {
      let settings = await readSettings(signal);
      if (initializeLocale && !initializationAttempted && generation === localeGeneration &&
          settings.locale === "auto" && settings.etag) {
        // One optional, revisioned initialization per load. If its reply or
        // the following read is lost, subsequent attempts only read settings.
        if (!canInitializeLocale()) localePending = true;
        else {
          localePending = false;
          initializationAttempted = true;
          settings = await initializeLanguage(settings, signal);
        }
      }
      if (settings.locale !== "auto") localePending = false;
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
    resumeLocaleInitialization() {
      // Wait for a confirmed, writable snapshot; a fresh read supplies its
      // current revision and preserves a language chosen on another page.
      if (!localePending || !canInitializeLocale() || store.isPending() ||
          store.get().status !== "ready") return Promise.resolve(store.get());
      localePending = false;
      return load();
    },
    recover() {
      const state = store.get();
      return state.status === "error" && isTransientReadError(state.error)
        ? load({ initializeLocale: false }) : Promise.resolve(state);
    },
    async preview(patch) {
      return (await client.patch("/settings/settings/preview", documentFor(patch))).data;
    },
    async apply(patch, etag) {
      localePending = false; localeGeneration++;
      const result = (await client.patch("/settings/settings", documentFor(patch), { ifMatch: etag })).data;
      await confirmRead();
      return result;
    },
    async restore(etag) {
      localePending = false; localeGeneration++;
      const result = (await client.delete("/settings/settings", { ifMatch: etag })).data;
      await confirmRead();
      return result;
    },
  });
}

const settings = createSettingsState();
export const settingsStore = settings.store;
export const loadSettings = settings.load;
export const resumeSettingsLocaleInitialization = settings.resumeLocaleInitialization;
export const recoverSettings = settings.recover;
export const previewSettings = settings.preview;
export const applySettings = settings.apply;
export const restoreSettings = settings.restore;
