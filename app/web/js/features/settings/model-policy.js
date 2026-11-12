// Model limits are facts; an output budget is a user/role request policy.
export const outputLimit = model => model?.output_budget ? model.window?.max_output_tokens : model?.parameters?.fields?.max_output_tokens?.value || model?.window?.max_output_tokens;
export function normalizeMedia(model) {
  const result = structuredClone(model);
  const caps = new Set(result.capabilities || []), attachments = new Set(result.attachments || []);
  // Old configurations used two independent flags for the same image input.
  if (attachments.has("image") || caps.has("media-input")) {
    attachments.add("image"); caps.add("media-input");
  }
  result.capabilities = [...caps]; result.attachments = [...attachments];
  return result;
}

export function outputPolicy(model) {
  if (model?.output_budget) return structuredClone(model.output_budget);
  return { mode: "legacy", tokens: model?.window?.max_output_tokens || 8192 };
}

export function applyInputModalities(model, record) {
  const modalities = record?.fields?.input_modalities?.value;
  if (!Array.isArray(modalities)) return normalizeMedia(model); // unknown, not false
  const result = structuredClone(model), image = modalities.includes("image");
  result.capabilities = (result.capabilities || []).filter(cap => cap !== "media-input");
  result.attachments = (result.attachments || []).filter(kind => kind !== "image");
  if (image) { result.capabilities.push("media-input"); result.attachments.push("image"); }
  return normalizeMedia(result);
}

// A fresh public recommendation may replace only advisory fields. An API
// declaration or an explicit user edit wins; legacy request limits stay intact.
export function mergeParameterRecord(previous, incoming) {
  const result = structuredClone(incoming);
  result.fields ||= {};
  for (const [key, field] of Object.entries(previous?.fields || {}))
    if (["manual", "api"].includes(field.source) || !result.fields[key]) result.fields[key] = structuredClone(field);
  return result;
}
