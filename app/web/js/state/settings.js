import { api, allowsApiWrite } from "../api/client.js";
import { createResourceStore } from "./store.js";
import { resolveLocale } from "../i18n.js";
import { isTransientReadError } from "../api/read-recovery.js";
import { createSettingsWriter } from "./settings-write.js";

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
  writeOptions = {},
} = {}) {
  let localePending = false, localeGeneration = 0;
  const write = createSettingsWriter({ client, ...writeOptions });
  let cancelWrite = null;
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
  async function save(patch, etag, { restore = false, signal } = {}) {
    localePending = false; localeGeneration++;
    cancelWrite?.();
    const controller = new AbortController(), cancel = () => controller.abort();
    cancelWrite = cancel;
    signal?.addEventListener("abort", cancel, { once: true });
    if (signal?.aborted) cancel();
    let initial = true;
    const unsubscribe = store.subscribe(state => {
      if (!initial && state.status === "idle") cancel();
      initial = false;
    });
    try {
      const completed = await write(patch, etag, { restore, signal: controller.signal });
      if (controller.signal.aborted) throw new DOMException("Settings write cancelled", "AbortError");
      // A newer read may have completed on another view while saving. Never
      // replace that snapshot with an older echo, or initialize locale here.
      if (!(store.get().data?.revision > completed.snapshot.revision)) {
        const fresh = completed.snapshot;
        store.setData(fresh.locale === "auto" ? { ...fresh, locale: resolveLanguage() } : fresh);
      }
      return completed.result;
    } catch (error) {
      if (error?.code === "settings_confirmation_failed") store.setError(error.cause);
      throw error;
    } finally {
      unsubscribe(); signal?.removeEventListener("abort", cancel);
      if (cancelWrite === cancel) cancelWrite = null;
    }
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
    apply: save,
    restore: (etag, options = {}) => save(null, etag, { ...options, restore: true }),
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
