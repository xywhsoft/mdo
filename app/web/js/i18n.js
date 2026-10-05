// The bundled Chinese pack is the source of truth for UI keys. Dynamic views
// can call t() while static, text-only nodes use data-i18n attributes.
export const supportedLocales = Object.freeze(["zh-CN", "en-US", "ru-RU"]);

const packs = new Map();
const listeners = new Set();
let locale = "en-US";
let requestVersion = 0;

// Only the primary system language chooses the initial UI. A secondary
// supported language must not override an unsupported primary language.
// Explicit preferences remain independent of later system language changes.
export function resolveLocale(preference = "auto", system = systemLanguages()) {
  if (supportedLocales.includes(preference)) return preference;
  if (preference !== "auto") return "en-US";
  const tag = system?.languages?.[0] || system?.language || "";
  const language = typeof tag === "string" ? tag.trim().toLowerCase().split(/[-_]/)[0] : "";
  if (language === "zh") return "zh-CN";
  if (language === "ru") return "ru-RU";
  return "en-US";
}

function systemLanguages() {
  try {
    // Android WebView may report English on a Chinese or Russian device.
    // The xs bridge returns the actual native locale list as BCP 47 tags.
    const languages = globalThis.XsPlatform?.languages();
    if (typeof languages === "string" && languages.trim())
      return { languages: languages.split(",") };
  } catch { /* Browsers and older native shells use navigator below. */ }
  return globalThis.navigator;
}

function validatePack(value) {
  if (!value || Array.isArray(value) || typeof value !== "object")
    throw new Error("Invalid language pack");
  for (const [key, text] of Object.entries(value)) {
    if (!key || typeof text !== "string") throw new Error("Invalid language pack entry");
  }
  return Object.freeze(value);
}

async function readPack(name) {
  if (packs.has(name)) return packs.get(name);
  const response = await fetch(`/lang/${name}.json`);
  if (!response.ok) throw new Error(`Cannot load language pack: ${name}`);
  const pack = validatePack(await response.json());
  packs.set(name, pack);
  return pack;
}

export function currentLocale() { return locale; }

export function t(key, params = {}, fallback = key) {
  const text = packs.get(locale)?.[key] ?? packs.get("zh-CN")?.[key] ?? fallback;
  return text.replace(/\{([a-zA-Z_][\w]*)\}/g,
    (match, name) => Object.hasOwn(params, name) ? String(params[name]) : match);
}

export function translateStatic(root = document) {
  const selector = "[data-i18n], [data-i18n-title], [data-i18n-placeholder], [data-i18n-aria-label]";
  const nodes = [root, ...root.querySelectorAll(selector)];
  for (const node of nodes) {
    if (node.dataset?.i18n) node.textContent = t(node.dataset.i18n, {}, node.textContent);
    for (const [attribute, key] of [
      ["title", node.dataset?.i18nTitle],
      ["placeholder", node.dataset?.i18nPlaceholder],
      ["aria-label", node.dataset?.i18nAriaLabel],
    ]) {
      if (key) node.setAttribute(attribute, t(key, {}, node.getAttribute(attribute) ?? ""));
    }
  }
}

export function subscribeLocale(listener) {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

// An older, slower request must never revert a newer locale choice.
export async function loadLocale(nextLocale) {
  if (!supportedLocales.includes(nextLocale)) throw new Error(`Unsupported locale: ${nextLocale}`);
  const version = ++requestVersion;
  await readPack("zh-CN");
  if (nextLocale !== "zh-CN") await readPack(nextLocale);
  if (version !== requestVersion) return false;
  locale = nextLocale;
  if (typeof document !== "undefined") {
    document.documentElement.lang = locale;
    document.title = t("app.title", {}, document.title);
    translateStatic(document);
  }
  for (const listener of listeners) listener(locale);
  return true;
}
