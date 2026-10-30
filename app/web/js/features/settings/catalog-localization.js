import { currentLocale, t } from "../../i18n.js";

const legacyLabels = new Set(["regular", "domestic", "international", "coding", "chatCompletions", "responses", "anthropicMessages"]);
const sourceObjects = new WeakMap();
export const legacyCatalogLocale = Object.freeze({ default_locale: "zh-CN", locales: [] });

export function validateCatalogLanguages(metadata = {}) {
  if (metadata.locales == null) return legacyCatalogLocale;
  const locales = metadata.locales;
  if (!Array.isArray(locales) || !locales.length || locales.length > 16) throw Error("Invalid catalog languages");
  const ids = new Set();
  for (const language of locales) {
    if (!language || typeof language.id !== "string" || language.id.length > 35 ||
        !/^[a-zA-Z]{2,8}(?:-[a-zA-Z0-9]{1,8})*$/.test(language.id) || ids.has(language.id.toLowerCase()) ||
        typeof language.name !== "string" || !language.name || !["ltr", "rtl"].includes(language.dir) || typeof language.enabled !== "boolean")
      throw Error("Invalid catalog language");
    ids.add(language.id.toLowerCase());
    const seen = new Set(); let next = language;
    while (next.fallback) {
      if (seen.has(next.id)) throw Error("Cyclic catalog language fallback");
      seen.add(next.id); next = locales.find(item => item?.id === next.fallback);
      if (!next) throw Error("Unknown catalog fallback language");
    }
  }
  if (!locales.some(item => item.id === metadata.default_locale && item.enabled)) throw Error("Invalid catalog default language");
  return structuredClone({ default_locale: metadata.default_locale, locales });
}

export function validateCatalogText(entity, fields, metadata) {
  if (entity.i18n == null) return;
  if (!entity.i18n || Array.isArray(entity.i18n) || typeof entity.i18n !== "object" || Object.keys(entity.i18n).length > 16)
    throw Error("Invalid catalog translations");
  for (const [locale, values] of Object.entries(entity.i18n)) {
    if (!metadata.locales.some(item => item.id === locale) || !values || Array.isArray(values) || typeof values !== "object")
      throw Error("Unknown catalog translation language");
    for (const [field, value] of Object.entries(values)) if (!fields.includes(field) || typeof value !== "string" ||
        new TextEncoder().encode(value).length > (field === fields[0] ? 128 : 2048)) throw Error("Invalid catalog translation field");
  }
}

export function catalogText(entity, field, metadata = legacyCatalogLocale, requested = currentLocale()) {
  const source = sourceObjects.get(entity) || entity, seen = new Set(), languages = metadata.locales || [];
  // A disabled translation is an admin draft; it must not be displayed to clients.
  let next = languages.some(item => item.id === requested && item.enabled) ? requested : null;
  while (next && !seen.has(next)) {
    seen.add(next);
    const value = source.i18n?.[next]?.[field];
    if (typeof value === "string" && value.trim()) return value;
    const language = languages.find(item => item.id === next);
    next = languages.some(item => item.id === language?.fallback && item.enabled) ? language.fallback : null;
  }
  const value = source.i18n?.[metadata.default_locale]?.[field] || source[field] || "";
  return field === "label" && !source.i18n && legacyLabels.has(value) ? t(`modelSetup.${value}`, {}, value) : value;
}

// Getters localize each render, including already-open setup wizards. The original
// strings stay separate so language switches cannot change saved provider config.
export function localizedCatalogEntity(entity, fields, metadata) {
  const source = structuredClone(entity), result = structuredClone(entity);
  sourceObjects.set(result, source);
  for (const field of fields) if (field in source || source.i18n && Object.values(source.i18n).some(value => field in value))
    Object.defineProperty(result, field, { enumerable: true, configurable: true, get: () => catalogText(result, field, metadata) });
  return result;
}

export function canonicalCatalogText(entity, field) {
  return (sourceObjects.get(entity) || entity)?.[field] || "";
}
