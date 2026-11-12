import assert from "node:assert/strict";
import test from "node:test";
import { parameterDraft, parameterIssues, applyParameters, availableEfforts, recommendedOutputBudget } from "../app/web/js/features/settings/model-parameters.js";
const field = value => ({ value, source: "server", checked_at: "2026-10-30" });
const record = (reasoning = { kind: "effort", efforts: ["low", "high", "max"], default: "high" }) => ({
  id: "exact-id", protocol: "openai-responses", fields: {
    context_tokens: field(1050000), max_output_tokens: field(128000), reasoning: field(reasoning),
  }, registry_version: "server-1",
});
const model = { protocols: ["openai-responses"], capabilities: ["text-input", "text-output", "streaming", "tool-call-output"] };
test("sampling defaults persist independently of advisory metadata and allow zero or omission",()=>{
  const r=record();r.fields.temperature=field(0.35);
  const draft=parameterDraft(r);assert.equal(draft.temperature,0.35);assert.equal(draft.effort,"high");
  const result=applyParameters(model,draft);assert.equal(result.temperature,0.35);
  assert.equal(parameterDraft(r,{...result,temperature:0}).temperature,0);
  draft.temperature=null;assert(!Object.hasOwn(applyParameters(model,draft),"temperature"));
  for(const value of [-1,2.1,NaN,Infinity]){draft.temperature=value;assert(parameterIssues(draft).includes("temperature"));}
});
test("capacity, request budget and declared effort values remain separate", () => {
  const draft = parameterDraft(record());
  assert.deepEqual(parameterIssues(draft), []);
  const profile = applyParameters(model, draft);
  assert.equal(profile.window.context_tokens, 1050000);
  assert.equal(profile.window.max_output_tokens, 128000);
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
  assert.equal(profile.window.max_output_tokens, 128000);
  assert.deepEqual(profile.output_budget,{mode:"legacy",tokens:2048});
  assert.equal(profile.window.max_input_tokens, 1000000);
  assert.equal(profile.default_reasoning_effort, "low");
});

test("official per-effort defaults are distinct from model capacity and respect API limits", () => {
  const r=record();r.request_defaults={basis:"official-default",output_tokens:65536,
    output_tokens_by_effort:{none:8192,low:65536,high:65536,max:131072}};
  r.fields.max_output_tokens=field(393216);r.fields.context_tokens=field(1048576);
  assert.equal(parameterDraft(r).output,65536);
  assert.equal(recommendedOutputBudget(r,"max"),131072);
  assert.equal(recommendedOutputBudget(r,"none"),8192);
  assert.equal(parameterDraft(r,{window:{max_output_tokens:4096}}).output,4096);
  r.fields.max_output_tokens=field(32768);
  assert.equal(recommendedOutputBudget(r,"max"),32768);
});

import { normalizeMedia, mergeParameterRecord } from "../app/web/js/features/settings/model-policy.js";
test("vision metadata derives both native image capability and attachment support",()=>{
  const r=record();r.fields.input_modalities=field(["text","image","video"]);
  const result=applyParameters({...model,attachments:[]},parameterDraft(r));
  assert(result.capabilities.includes("media-input"));assert.deepEqual(result.attachments,["image"]);
  r.fields.input_modalities=field(["text"]);
  const textOnly=applyParameters(result,parameterDraft(r,result));
  assert(!textOnly.capabilities.includes("media-input"));assert.deepEqual(textOnly.attachments,[]);
  delete r.fields.input_modalities;
  assert(applyParameters(result,parameterDraft(r,result)).capabilities.includes("media-input"));
  for(const partial of [{capabilities:[],attachments:["image"]},{capabilities:["media-input"],attachments:[]}]) {
    const normalized=normalizeMedia(partial);assert(normalized.capabilities.includes("media-input"));assert(normalized.attachments.includes("image"));
  }
});
test("recommended defaults follow refreshed parameters while custom and legacy budgets survive",()=>{
  const r=record();r.request_defaults={output_tokens:65536};
  const first=applyParameters({...model,attachments:[]},parameterDraft(r));
  assert.equal(first.window.max_output_tokens,128000);assert.deepEqual(first.output_budget,{mode:"recommended",tokens:65536});
  r.request_defaults.output_tokens=96000;
  assert.equal(parameterDraft(r,first).output,96000);
  assert.equal(parameterDraft(r,{...first,output_budget:{mode:"custom",tokens:4096}}).output,4096);
  assert.equal(parameterDraft(r,{...model,window:{max_output_tokens:8192}}).output,8192);
  const updated=mergeParameterRecord({fields:{temperature:{value:0.3,source:"manual"},max_output_tokens:{value:100000,source:"api"}}},r);
  assert.equal(updated.fields.temperature.value,0.3);assert.equal(updated.fields.max_output_tokens.value,100000);
});
test("profiles outside the current runtime context limit are rejected before saving",()=>{
  const r=record();r.fields.context_tokens=field(64000);r.fields.window_mode=field("split-input-output");
  const draft=parameterDraft(r);assert(parameterIssues(draft).includes("max_output_tokens"));
});
