import { element } from "../../utils/dom.js";
import { t } from "../../i18n.js";

const text = (key, fallback, params = {}) => t(`modelParameters.${key}`, params, fallback);
export const effortNames = ["none", "minimal", "low", "medium", "high", "xhigh", "max"];
export const parameterValue = (record, key) => record?.fields?.[key]?.value;
const positive = value => Number.isSafeInteger(value) && value > 0 && value <= 0xffffffff;

export function parameterDraft(record, model) {
  const result = structuredClone(record || { id: model?.wire_model, protocol: model?.default_protocol, fields: {} });
  result.fields ||= {};
  return { record: result, output: model?.window?.max_output_tokens || Math.min(8192, parameterValue(result, "max_output_tokens") || 8192),
    effort: model?.default_reasoning_effort || parameterValue(result, "reasoning")?.default || "default" };
}

export function availableEfforts(record) {
  const reasoning = parameterValue(record, "reasoning");
  // This SDK encodes OpenAI effort strings. Anthropic's budget/adaptive and
  // vendor-specific toggles are declared metadata, not fabricated effort tiers.
  if (record.protocol === "anthropic-messages" || reasoning?.kind !== "effort") return ["default"];
  return ["default", ...new Set((reasoning.efforts || []).filter(level => effortNames.includes(level)))];
}

export function parameterIssues(draft) {
  const fields = draft.record.fields, issues = [];
  if (!positive(fields.context_tokens?.value)) issues.push("context_tokens");
  if (!positive(fields.max_output_tokens?.value)) issues.push("max_output_tokens");
  const reasoning = fields.reasoning?.value;
  if (!reasoning || !["none", "effort", "toggle", "budget", "adaptive", "provider-default"].includes(reasoning.kind) ||
      (reasoning.kind === "effort" && (!reasoning.efforts?.length || reasoning.efforts.length > 7 ||
        !reasoning.efforts.every(level => effortNames.includes(level))))) issues.push("reasoning");
  if (!positive(draft.output) || draft.output > (fields.max_output_tokens?.value || Infinity) ||
      draft.output >= (fields.context_tokens?.value || Infinity)) issues.push("output_budget");
  return issues;
}

export function applyParameters(model, draft) {
  if (parameterIssues(draft).length) throw new Error(text("incomplete", "无法获取模型参数，请补全这些参数。"));
  const result = structuredClone(model), record = structuredClone(draft.record);
  if (!result.protocols.includes(record.protocol)) result.protocols = [record.protocol];
  result.default_protocol = record.protocol;
  const context = parameterValue(record, "context_tokens"), output = draft.output;
  const mode = parameterValue(record, "window_mode") || "shared-context";
  result.window = { mode, context_tokens: context,
    max_input_tokens: Math.min(parameterValue(record, "max_input_tokens") || context, mode === "shared-context" ? context - output : context),
    max_output_tokens: output, output_reserve_tokens: Math.floor(output / 2), summary_tokens: Math.min(2048, Math.floor(output / 4)) };
  result.reasoning_efforts = availableEfforts(record);
  result.default_reasoning_effort = result.reasoning_efforts.includes(draft.effort) ? draft.effort : "default";
  result.capabilities = result.capabilities.filter(cap => !["reasoning-control", "reasoning-output"].includes(cap));
  if (result.reasoning_efforts.length > 1) result.capabilities.push("reasoning-control");
  if (!["none", "provider-default"].includes(parameterValue(record, "reasoning").kind)) result.capabilities.push("reasoning-output");
  if (parameterValue(record, "tools") === false)
    result.capabilities = result.capabilities.filter(cap => !["tool-call-output", "parallel-tool-calls"].includes(cap));
  result.parameters = { schema_version: 1, ...record };
  return result;
}

export function parameterSummary(model) {
  return model.parameters ? text("identified", "参数已识别 · 上下文 {context} · 每次输出预算 {output}", {
    context: model.window.context_tokens.toLocaleString(), output: model.window.max_output_tokens.toLocaleString(),
  }) : text("legacy", "旧配置，来源未知；可重新识别参数后选择应用。");
}

export function createParameterEditor(draft, { onChange = () => {}, index = 0 } = {}) {
  const record = draft.record;
  const root = element("section", { className: "model-parameter-editor" });
  root.append(element("strong", { text: record.id }));
  if (record.registry_version && record.registry_version !== "unavailable") root.append(element("small", {
    text: text("catalogVersion", "服务器目录版本：{version}", { version: record.registry_version.slice(0, 16) }),
  }));
  if (record.catalog_stale || record.catalog_unavailable) root.append(element("p", { className: "model-parameter-warning",
    text: text("catalogOffline", "服务器参数目录暂时不可用；缓存中的推荐值可能已过期，未知项请手动补全。"),
  }));
  const warning = element("p", { className: "model-parameter-warning", attrs: { role: "status" } });
  root.append(warning);
  const labels = { context_tokens: text("context", "上下文长度（tokens）"), max_output_tokens: text("maxOutput", "模型最大输出（tokens）"), reasoning: text("reasoning", "思考控制"), output_budget: text("budget", "每次请求输出预算（tokens）") };
  function update() {
    const issues = parameterIssues(draft);
    warning.hidden = !issues.length;
    warning.textContent = issues.length ? text("missing", "无法获取模型参数，请补全这些参数：{fields}", { fields: issues.map(key => labels[key]).join("、") }) : "";
    onChange();
  }
  function manual(key, value) {
    record.fields[key] = { value, source: "manual", checked_at: new Date().toISOString() };
  }
  const sourceLabels = { api: text("api", "供应商 API"), server: text("registry", "服务器推荐参数"),
    manual: text("manual", "用户填写"), "upstream-recommendation": text("upstream", "上游推荐值，请确认中转服务限制") };
  function provenance(key, host) {
    const f = record.fields[key];
    host.textContent = f ? `${sourceLabels[f.source] || text("unknown", "来源未知")} · ${typeof f.checked_at === "number" ? new Date(f.checked_at).toLocaleString() : f.checked_at || ""}` : text("unknown", "来源未知");
    if (f?.source !== "manual" && /^https:\/\//.test(f?.url || "")) host.append(element("a", { text: text("source", "查看来源"), attrs: { href: f.url, target: "_blank", rel: "noopener noreferrer" } }));
  }
  for (const key of ["context_tokens", "max_output_tokens"]) {
    const input = element("input", { attrs: { type: "number", name: `parameter-${index}-${key}`, min: "1", max: "4294967295", step: "1", inputmode: "numeric" } });
    input.value = parameterValue(record, key) || "";
    const source = element("small"); provenance(key, source);
    input.addEventListener("input", () => { manual(key, Number(input.value)); provenance(key, source); update(); });
    root.append(element("label", { className: "model-field" }, [element("span", { text: labels[key] }), input, source]));
  }
  const reasoning = parameterValue(record, "reasoning");
  const kind = element("select", { attrs: { name: `parameter-${index}-reasoning` } });
  for (const [value, label] of [["", text("choose", "请选择思考控制方式")], ["provider-default", text("providerDefault", "使用供应商默认（不发送思考控制参数）")],
    ["none", text("noReasoning", "模型不支持思考控制")], ["effort", text("effort", "支持明确的思考档位")],
    ["toggle", text("toggle", "仅开关控制")], ["budget", text("tokenBudget", "Token 预算控制")], ["adaptive", text("adaptive", "自适应思考")]])
    kind.append(element("option", { text: label, attrs: { value } }));
  kind.value = reasoning?.kind || "";
  const levels = element("input", { attrs: { type: "text", name: `parameter-${index}-efforts`, placeholder: "low, medium, high" } });
  levels.value = reasoning?.efforts?.join(", ") || "";
  const source = element("small"); provenance("reasoning", source);
  const effortSelect = element("select", { attrs: { name: `parameter-${index}-default-effort` } });
  const adapter = element("p", { className: "model-config-status" });
  function refreshEfforts() {
    const values = availableEfforts(record);
    effortSelect.replaceChildren();
    for (const value of values) effortSelect.append(element("option", { text: value === "default" ? text("default", "供应商默认") : value, attrs: { value } }));
    effortSelect.value = values.includes(draft.effort) ? draft.effort : "default"; draft.effort = effortSelect.value;
    levels.hidden = kind.value !== "effort";
    adapter.textContent = (kind.value === "effort" && record.protocol === "anthropic-messages") || ["toggle", "budget", "adaptive"].includes(kind.value)
      ? text("adapter", "已记录模型能力；当前适配器尚不支持此控制方式，本次使用供应商默认。") : "";
  }
  function changeReasoning() {
    const values = levels.value.split(/[,，\s]+/).filter(Boolean);
    const invalid = kind.value === "effort" ? values.filter(value => !effortNames.includes(value)) : [];
    levels.setCustomValidity?.(invalid.length ? text("invalidEffort", "不支持的思考档位：{levels}", { levels: invalid.join(", ") }) : "");
    manual("reasoning", { kind: invalid.length ? "invalid" : kind.value, ...(kind.value === "effort" ? { efforts: values } : {}) });
    provenance("reasoning", source); refreshEfforts(); update();
  }
  kind.addEventListener("change", changeReasoning); levels.addEventListener("input", changeReasoning);
  effortSelect.addEventListener("change", () => { draft.effort = effortSelect.value; update(); });
  root.append(element("label", { className: "model-field" }, [element("span", { text: labels.reasoning }), kind, levels, source]), adapter,
    element("label", { className: "model-field" }, [element("span", { text: text("defaultEffort", "默认思考档位") }), effortSelect]));
  const budget = element("input", { attrs: { type: "number", name: `parameter-${index}-output-budget`, min: "1", step: "1" } });
  budget.value = draft.output; budget.addEventListener("input", () => { draft.output = Number(budget.value); update(); });
  root.append(element("label", { className: "model-field" }, [element("span", { text: labels.output_budget }), budget]),
    element("small", { text: text("budgetHelp", "输出预算与模型最大输出分开保存，不会自动请求模型的全部输出额度。") }));
  if (record.lookup_state && !["ok", "not_exposed"].includes(record.lookup_state)) root.append(element("p", { className: "model-parameter-warning", text: text("unavailable", "本次未能读取供应商参数，已尝试服务器参数表；请确认网络、接口权限或手动补全。") }));
  refreshEfforts(); update(); return root;
}
