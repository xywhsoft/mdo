import { validateCatalogLanguages, validateCatalogText, localizedCatalogEntity, canonicalCatalogText } from "./catalog-localization.js";
// Small data-only catalog. API product variants have independent addresses;
// an ordinary API key must never silently use a Coding Plan endpoint.
const chat = "openai-chat-completions", messages = "anthropic-messages";
const variant = (id, label, base, models, protocol = chat) => ({ id, label, base, models, protocol });
// Only a manual escape hatch is bundled. Supplier/catalog data is maintained
// by the public service and loaded on every Add Supplier action.
export let providerPresets = [{ id: "custom", name: "Custom supplier", labelKey: "customSupplier", custom: true,
  variants: [variant("chat", "chatCompletions", "", []), variant("responses", "responses", "", [], "openai-responses"),
    variant("messages", "anthropicMessages", "", [], messages)] }];
export function setProviderCatalog(items, metadata = {}) {
  const languages = validateCatalogLanguages(metadata);
  const valid = items.filter(item => item && /^[A-Za-z0-9._-]{1,90}$/.test(item.id) && item.id !== "custom" &&
    typeof item.name === "string" && item.name.length <= 128 && Array.isArray(item.variants) && item.variants.length && item.variants.length <= 16 &&
    item.variants.every(v => typeof v.id === "string" && typeof v.base === "string" &&
      [chat, "openai-responses", messages].includes(v.protocol) && Array.isArray(v.models) && v.models.every(id => typeof id === "string" && id.length <= 256)));
  if (valid.length !== items.length) throw new Error("Invalid supplier catalog");
  for (const provider of valid) {
    validateCatalogText(provider, ["name", "description"], languages);
    for (const variant of provider.variants) validateCatalogText(variant, ["label", "description", "key_note"], languages);
  }
  providerPresets = [...valid.map(provider => {
    const value = localizedCatalogEntity(provider, ["name", "description"], languages);
    value.variants = provider.variants.map(variant => localizedCatalogEntity(variant, ["label", "description", "key_note"], languages));
    return value;
  }), providerPresets.find(item => item.id === "custom")];
}

export function presetVariant(id, variantId) {
  const preset = providerPresets.find((item) => item.id === id);
  return preset?.variants.find((item) => item.id === variantId) || preset?.variants[0];
}

export function uniqueId(hint, entries) {
  const base = hint.replace(/[^a-zA-Z0-9._-]+/g, "-").replace(/^-+|-+$/g, "").slice(0, 90) || "custom";
  let id = base, index = 2;
  while (entries.some((item) => item.id === id)) id = `${base}-${index++}`;
  return id;
}

export function presetProvider(preset, selected, entries = [], base = selected.base) {
  base = base.trim().replace(/\/+$/, "");
  const endpoints = selected.protocol === messages ? { anthropic_messages: `${base}/messages` }
    : selected.protocol === "openai-responses" ? { responses: `${base}/responses`, ...(!preset.custom ? { chat_completions: `${base}/chat/completions` } : {}) }
      : { chat_completions: `${base}/chat/completions` };
  return { id: uniqueId(preset.id, entries), name: canonicalCatalogText(preset, "name"), builtin: false,
    editable: true, removable: true, verify_peer: true, timeout_ms: 120000,
    template_id: preset.id, template_variant: selected.id, endpoints };
}

export function presetModel(provider, wireModel, entries = []) {
  const selected = presetVariant(provider.template_id, provider.template_variant);
  const protocol = selected?.protocol || (provider.endpoints.responses ? "openai-responses" :
    provider.endpoints.chat_completions ? chat : messages);
  // Conservative limits for unknown catalog IDs; never infer vision or tool
  // parallelism from a model name. Advanced edits survive future discovery.
  const local = providerPresets.find((item) => item.id === provider.template_id)?.local;
  const context = local ? 8192 : 65536, output = local ? 2048 : 8192;
  const reasoning = ["default"];
  const capabilities = ["text-input", "tool-result-input", "text-output", "tool-call-output", "streaming"];

  return { id: uniqueId(`${provider.id}-${wireModel}`, entries), name: wireModel,
    provider: provider.id, wire_model: wireModel, builtin: false, free: false,
    editable: true, removable: true, protocols: [protocol], default_protocol: protocol,
    capabilities, reasoning_efforts: reasoning, default_reasoning_effort: "default", attachments: [],
    window: { mode: "shared-context", context_tokens: context, max_input_tokens: context - output,
      max_output_tokens: output, output_reserve_tokens: output / 2, summary_tokens: Math.min(2048, output / 4) } };
}

export function suggestedModels(ids, recommendations = []) {
  const available = new Set(ids);
  const preferred = recommendations.filter((id) => available.has(id));
  const others = ids.filter((id) => !/(embed|whisper|tts|audio|image|rerank|moderation|dall-e|sora)/i.test(id));
  return [...new Set([...preferred, ...others])].slice(0, 2);
}

// Rendering never edits the saved name. A custom name always wins.
export function providerDisplayName(provider) {
  const preset = providerPresets.find(value => value.id === provider.template_id);
  return preset && provider.name === canonicalCatalogText(preset, "name") ? preset.name : provider.name;
}
