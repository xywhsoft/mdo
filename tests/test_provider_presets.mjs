import assert from "node:assert/strict";
import { providerPresets, presetProvider, presetModel, suggestedModels, uniqueId } from "../app/web/js/features/settings/provider-presets.js";
import { sameModelConfig } from "../app/web/js/features/settings/model-config-state.js";

for (const preset of providerPresets) for (const variant of preset.variants) {
  const provider = presetProvider(preset, variant, [], variant.base || "https://example.test/v1");
  assert.equal(provider.builtin, false);
  for (const url of Object.values(provider.endpoints)) assert.ok(new URL(url));
  for (const id of variant.models) {
    const model = presetModel(provider, id);
    assert.match(model.id, /^[A-Za-z0-9_-][A-Za-z0-9._-]*$/);
    assert.ok(model.reasoning_efforts.includes(model.default_reasoning_effort));
    assert.ok(model.window.max_input_tokens + model.window.output_reserve_tokens <= model.window.context_tokens);
    assert.ok(model.capabilities.includes("tool-call-output"));
    assert.deepEqual(model.attachments, []);
  }
}
const products = providerPresets.filter((item) => ["bigmodel", "zai", "dashscope"].includes(item.id));
for (const preset of products) {
  const regular = presetProvider(preset, preset.variants[0]);
  const coding = presetProvider(preset, preset.variants.find((item) => item.id === "coding"));
  assert.notDeepEqual(regular.endpoints, coding.endpoints);
}
assert.equal(uniqueId("my supplier", [{ id: "my-supplier" }, { id: "my-supplier-2" }]), "my-supplier-3");
assert.deepEqual(suggestedModels(["text-embedding", "new-agent", "recommended", "whisper"], ["recommended", "gone"]), ["recommended", "new-agent"]);
const baseline = { providers: [{ id: "p", name: "P" }], items: [], default_model: "ornith", runtime_override: false };
assert.equal(sameModelConfig(baseline, { ...baseline, revision: 20 }), true);
assert.equal(sameModelConfig(baseline, { ...baseline, providers: [{ name: "P", id: "p" }] }), true);
assert.equal(sameModelConfig(baseline, { ...baseline, default_model: "other" }), false);
assert.equal(sameModelConfig(baseline, { ...baseline, items: [{ id: "new" }] }), false);
console.log("PASS supplier products, unique identities, conservative profiles and recommended model selection");
