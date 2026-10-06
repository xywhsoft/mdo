function canonical(value) {
  if (Array.isArray(value)) return value.map(canonical);
  if (value && typeof value === "object") return Object.fromEntries(
    Object.keys(value).sort().map((key) => [key, canonical(value[key])]));
  return value;
}

// Config revisions also change when unrelated settings autosave. A fresh
// revision may be adopted only while this complete model domain is unchanged.
export function sameModelConfig(left, right) {
  const domain = (value) => ({ providers: value.providers, items: value.items,
    default_model: value.default_model, runtime_override: value.runtime_override });
  return JSON.stringify(canonical(domain(left))) === JSON.stringify(canonical(domain(right)));
}
