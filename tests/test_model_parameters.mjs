import assert from "node:assert/strict";
import test from "node:test";
import { parameterDraft, parameterIssues, applyParameters, availableEfforts } from "../app/web/js/features/settings/model-parameters.js";
const field = value => ({ value, source: "server", checked_at: "2026-10-30" });
const record = (reasoning = { kind: "effort", efforts: ["low", "high", "max"], default: "high" }) => ({
  id: "exact-id", protocol: "openai-responses", fields: {
    context_tokens: field(1050000), max_output_tokens: field(128000), reasoning: field(reasoning),
  }, registry_version: "server-1",
});
const model = { protocols: ["openai-responses"], capabilities: ["text-input", "text-output", "streaming", "tool-call-output"] };
test("capacity, request budget and declared effort values remain separate", () => {
  const draft = parameterDraft(record());
  assert.deepEqual(parameterIssues(draft), []);
  const profile = applyParameters(model, draft);
  assert.equal(profile.window.context_tokens, 1050000);
  assert.equal(profile.window.max_output_tokens, 8192);
  assert.equal(profile.parameters.fields.max_output_tokens.value, 128000);
  assert.deepEqual(profile.reasoning_efforts, ["default", "low", "high", "max"]);
  assert.equal(profile.default_reasoning_effort, "high");
  assert.equal(model.window, undefined);
});
test("unknown is incomplete; explicit provider default is a deliberate manual choice", () => {
  const draft = parameterDraft({ protocol: "openai-responses", fields: {} });
  assert.deepEqual(parameterIssues(draft), ["context_tokens", "max_output_tokens", "reasoning"]);
  assert.throws(() => applyParameters(model, draft));
  draft.record.fields = { context_tokens: field(10000), max_output_tokens: field(9000), reasoning: field({ kind: "provider-default" }) };
  assert.deepEqual(parameterIssues(draft), []);
  assert.deepEqual(applyParameters(model, draft).reasoning_efforts, ["default"]);
});
test("native adaptive/budget/toggle capabilities do not invent SDK effort controls", () => {
  for (const kind of ["toggle", "budget", "adaptive", "none"]) assert.deepEqual(availableEfforts(record({ kind })), ["default"]);
  assert.deepEqual(availableEfforts({ ...record(), protocol: "anthropic-messages" }), ["default"]);
  assert(!applyParameters(model, parameterDraft(record({ kind: "toggle" }))).capabilities.includes("reasoning-control"));
});
test("invalid budgets, unsupported effort names and explicit no-tools are respected", () => {
  const draft = parameterDraft(record()); draft.output = 128001;
  assert(parameterIssues(draft).includes("output_budget"));
  draft.output = 8192; draft.record.fields.reasoning.value.efforts.push("unlimited");
  assert(parameterIssues(draft).includes("reasoning"));
  const noTools = record({ kind: "none" }); noTools.fields.tools = field(false);
  assert(!applyParameters(model, parameterDraft(noTools)).capabilities.includes("tool-call-output"));
});
test("an existing request budget survives re-identification and a split input limit stays distinct", () => {
  const r = record(); r.fields.window_mode = field("split-input-output"); r.fields.max_input_tokens = field(1000000);
  const old = { ...model, window: { max_output_tokens: 2048 }, default_reasoning_effort: "low" };
  const profile = applyParameters(old, parameterDraft(r, old));
  assert.equal(profile.window.max_output_tokens, 2048);
  assert.equal(profile.window.max_input_tokens, 1000000);
  assert.equal(profile.default_reasoning_effort, "low");
});
