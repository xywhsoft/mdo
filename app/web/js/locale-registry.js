// Add a language pack and one definition here; business views need no locale cases.
export const localeDefinitions = Object.freeze([
  { id: "zh-CN", name: "简体中文", dir: "ltr", systemLanguages: ["zh"] },
  { id: "en-US", name: "English", dir: "ltr", systemLanguages: ["en"] },
  { id: "ru-RU", name: "Русский", dir: "ltr", systemLanguages: ["ru"] },
].map(value => Object.freeze(value)));

export const supportedLocales = Object.freeze(localeDefinitions.map(value => value.id));
export function resolveRegisteredLocale(tag, definitions = localeDefinitions, fallback = "en-US") {
  const normalized = String(tag || "").trim().replaceAll("_", "-").toLowerCase();
  return definitions.find(value => value.id.toLowerCase() === normalized)?.id
    || definitions.find(value => value.systemLanguages?.includes(normalized.split("-")[0]))?.id
    || fallback;
}
