// Small data-only catalog. API product variants have independent addresses;
// an ordinary API key must never silently use a Coding Plan endpoint.
const chat = "openai-chat-completions", messages = "anthropic-messages";
const variant = (id, label, base, models, protocol = chat) => ({ id, label, base, models, protocol });
export const providerPresets = Object.freeze([
  { id: "bigmodel", name: "智谱", keyUrl: "https://open.bigmodel.cn/usercenter/proj-mgmt/apikeys", variants: [
    variant("api", "regular", "https://open.bigmodel.cn/api/paas/v4", ["glm-5.3", "glm-5.3-flash"]),
    variant("coding", "coding", "https://open.bigmodel.cn/api/coding/paas/v4", ["glm-5.3", "glm-5.1"])] },
  { id: "zai", name: "Z.ai", keyUrl: "https://z.ai/manage-apikey/apikey-list", variants: [
    variant("api", "regular", "https://api.z.ai/api/paas/v4", ["glm-5.3", "glm-5.1"]),
    variant("coding", "coding", "https://api.z.ai/api/coding/paas/v4", ["glm-5.3", "glm-5.1"])] },
  { id: "deepseek", name: "DeepSeek", keyUrl: "https://platform.deepseek.com/api_keys", variants: [
    variant("api", "regular", "https://api.deepseek.com/v1", ["deepseek-chat", "deepseek-reasoner"])] },
  { id: "kimi", name: "Kimi", keyUrl: "https://platform.moonshot.cn/console/api-keys", variants: [
    variant("cn", "domestic", "https://api.moonshot.cn/v1", ["kimi-k2.5"]),
    variant("global", "international", "https://api.moonshot.ai/v1", ["kimi-k2.5"])] },
  { id: "minimax", name: "MiniMax", keyUrl: "https://platform.minimaxi.com/user-center/basic-information/interface-key", variants: [
    variant("cn", "domestic", "https://api.minimaxi.com/anthropic/v1", ["MiniMax-M2.5"], messages),
    variant("global", "international", "https://api.minimax.io/anthropic/v1", ["MiniMax-M2.5"], messages)] },
  { id: "dashscope", name: "阿里云百炼", keyUrl: "https://bailian.console.aliyun.com/", variants: [
    variant("cn", "domestic", "https://dashscope.aliyuncs.com/compatible-mode/v1", ["qwen3.6-plus", "qwen3-coder-plus"]),
    variant("global", "international", "https://dashscope-intl.aliyuncs.com/compatible-mode/v1", ["qwen3.6-plus"]),
    variant("coding", "coding", "https://coding.dashscope.aliyuncs.com/v1", ["qwen3.6-plus", "kimi-k2.5"])] },
  { id: "siliconflow", name: "硅基流动", keyUrl: "https://cloud.siliconflow.cn/account/ak", variants: [
    variant("cn", "domestic", "https://api.siliconflow.cn/v1", ["deepseek-ai/DeepSeek-V3.2", "Pro/zai-org/GLM-5.1"])] },
  { id: "openai", name: "OpenAI", keyUrl: "https://platform.openai.com/api-keys", variants: [
    variant("api", "regular", "https://api.openai.com/v1", ["gpt-5.4", "gpt-4.1-mini"], "openai-responses")] },
  { id: "anthropic", name: "Anthropic", keyUrl: "https://platform.claude.com/settings/keys", variants: [
    variant("api", "regular", "https://api.anthropic.com/v1", ["claude-sonnet-4-6"], messages)] },
  { id: "openrouter", name: "OpenRouter", keyUrl: "https://openrouter.ai/settings/keys", variants: [
    variant("api", "regular", "https://openrouter.ai/api/v1", ["anthropic/claude-sonnet-4.6", "openai/gpt-5.4"])] },
  { id: "openai-compatible", name: "OpenAI Compatible", custom: true, variants: [variant("api", "custom", "", [])] },
  { id: "anthropic-compatible", name: "Anthropic Compatible", custom: true, variants: [variant("api", "custom", "", [], messages)] },
  { id: "ollama", name: "Ollama", local: true, custom: true, variants: [variant("local", "local", "http://127.0.0.1:11434/v1", [])] },
  { id: "llamacpp", name: "llama.cpp", local: true, custom: true, variants: [variant("local", "local", "http://127.0.0.1:8080/v1", [])] },
]);

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
    : selected.protocol === "openai-responses" ? { responses: `${base}/responses`, chat_completions: `${base}/chat/completions` }
      : { chat_completions: `${base}/chat/completions` };
  return { id: uniqueId(preset.id, entries), name: preset.name, builtin: false,
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
  const reasoning = /^(glm-5|gpt-5)/.test(wireModel) ? ["low", "medium", "high"] : ["none"];
  const capabilities = ["text-input", "tool-result-input", "text-output", "tool-call-output", "streaming"];
  if (reasoning[0] !== "none") capabilities.push("reasoning-output", "reasoning-control");
  return { id: uniqueId(`${provider.id}-${wireModel}`, entries), name: wireModel,
    provider: provider.id, wire_model: wireModel, builtin: false, free: false,
    editable: true, removable: true, protocols: [protocol], default_protocol: protocol,
    capabilities, reasoning_efforts: reasoning, default_reasoning_effort: reasoning[0] === "none" ? "none" : "medium", attachments: [],
    window: { mode: "shared-context", context_tokens: context, max_input_tokens: context - output,
      max_output_tokens: output, output_reserve_tokens: output / 2, summary_tokens: Math.min(2048, output / 4) } };
}

export function suggestedModels(ids, recommendations = []) {
  const available = new Set(ids);
  const preferred = recommendations.filter((id) => available.has(id));
  const others = ids.filter((id) => !/(embed|whisper|tts|audio|image|rerank|moderation|dall-e|sora)/i.test(id));
  return [...new Set([...preferred, ...others])].slice(0, 2);
}
