import { t } from "../../i18n.js";
import { element, errorMessage } from "../../utils/dom.js";
import { createApplicationClient } from "../../api/application.js";

export function promptPolicy(options) {
  const spec = options.context?.["plugins.instructions"];
  return spec === false ? { enabled: false } : spec && typeof spec === "object" ? spec : {};
}
const match = (patterns, value) => Array.isArray(patterns) && patterns.some(pattern =>
  typeof pattern === "string" && (pattern.endsWith("*") ? value.startsWith(pattern.slice(0, -1)) : value === pattern));
export function promptChosen(spec, item) {
  return spec.enabled !== false && (!Array.isArray(spec.include) || match(spec.include, item.plugin) || match(spec.include, item.key)) &&
    !match(spec.exclude, item.plugin) && !match(spec.exclude, item.key) &&
    (item.activation !== "explicit" || Array.isArray(spec.include));
}
export function updatePromptPolicy(options, action, value) {
  const copy = structuredClone(options), spec = { ...promptPolicy(copy) };
  if (action === "mode") {
    spec.enabled = value !== "off";
    if (value === "automatic") delete spec.include;
    if (value === "selected" && !Array.isArray(spec.include)) spec.include = [];
  } else {
    const { item, checked } = value;
    const exclude = new Set(spec.exclude || []);
    if (checked) exclude.delete(item.key); else exclude.add(item.key);
    spec.exclude = [...exclude];
    if (Array.isArray(spec.include)) {
      const include = new Set(spec.include);
      if (checked) include.add(item.key); else include.delete(item.key);
      spec.include = [...include];
    }
  }
  copy.context = { ...copy.context, "plugins.instructions": spec };
  return copy;
}

// The JSON field remains authoritative, including unknown or uninstalled
// selections. Opening/loading the picker never rewrites saved configuration.
export function createPluginPromptPicker({ input, signal, isCurrent = () => true }) {
  const root = element("fieldset", { className: "agent-plugin-prompts" });
  root.append(element("legend", { text: t("ui.pluginPrompts.title", {}, "插件提示词") }));
  const mode = element("select", { attrs: { "aria-label": t("ui.pluginPrompts.mode", {}, "插件提示词选择方式") } });
  for (const [value, text] of [["automatic", t("ui.pluginPrompts.automatic", {}, "跟随当前角色可用工具")],
    ["selected", t("ui.pluginPrompts.selected", {}, "仅使用选择的说明")], ["off", t("ui.pluginPrompts.off", {}, "关闭插件说明")]]) {
    const option = element("option", { text, attrs: { value } }); option.value = value; mode.append(option);
  }
  const status = element("p", { attrs: { role: "status" }, text: t("ui.pluginPrompts.loading", {}, "正在读取插件说明…") });
  const list = element("div", { className: "agent-tool-list" });
  root.append(mode, status, list, element("small", { text: t("ui.pluginPrompts.help", {}, "仅选择能力说明，不会启用工具或授予权限。SubAgent 按自己的配置独立选择。") }));
  let items = [], loaded = false, failure = "";
  const read = () => {
    const options = JSON.parse(input.value || "{}");
    if (!options || typeof options !== "object" || Array.isArray(options)) throw Error(t("ui.pluginPrompts.invalidOptions", {}, "请先修正高级配置 JSON。"));
    if (options.context !== undefined && (!options.context || typeof options.context !== "object" || Array.isArray(options.context))) throw Error(t("ui.pluginPrompts.invalidContext", {}, "context 必须是对象，请先修正高级配置。"));
    const spec = options.context?.["plugins.instructions"];
    if (spec !== undefined && typeof spec !== "boolean" && (!spec || typeof spec !== "object" || Array.isArray(spec))) throw Error(t("ui.pluginPrompts.invalidSpec", {}, "插件说明配置必须是布尔值或对象。"));
    for (const key of ["include", "exclude"]) if (spec?.[key] !== undefined && (!Array.isArray(spec[key]) || spec[key].some(value => typeof value !== "string"))) throw Error(t("ui.pluginPrompts.invalidSelection", {}, "插件说明的 include 和 exclude 必须是字符串数组。"));
    return options;
  };
  const render = () => {
    if (signal?.aborted || !isCurrent()) return;
    let spec;
    try { spec = promptPolicy(read()); }
    catch (error) { mode.disabled = true; status.textContent = errorMessage(error); list.replaceChildren(); return; }
    mode.disabled = false; mode.value = spec.enabled === false ? "off" : Array.isArray(spec.include) ? "selected" : "automatic";
    status.textContent = failure || (loaded ? items.length ? t("ui.pluginPrompts.available", { count: items.length }, "{count} 项说明；最终采用情况请查看上下文预览或运行检查。") : t("ui.pluginPrompts.empty", {}, "当前没有已生效的插件提示词。") : t("ui.pluginPrompts.loading", {}, "正在读取插件说明…"));
    list.replaceChildren();
    for (const item of items) {
      const check = element("input", { attrs: { type: "checkbox" } }); check.checked = promptChosen(spec, item);
      check.disabled = mode.value === "off" || (mode.value === "automatic" && item.activation === "explicit");
      check.addEventListener("change", () => write("item", { item, checked: check.checked }));
      list.append(element("label", { className: "agent-tool-option", attrs: { title: item.text || "" } }, [check,
        element("span", { text: `${item.alias || item.plugin} · ${item.title || item.id}` }),
        element("small", { text: `${item.version || ""} · ${item.bytes} B` })]));
    }
  };
  const write = (action, value) => {
    try { input.value = JSON.stringify(updatePromptPolicy(read(), action, value), null, 2); input.dispatchEvent(new Event("input", { bubbles: true })); }
    catch (error) { status.textContent = errorMessage(error); }
    render();
  };
  mode.addEventListener("change", () => write("mode", mode.value)); input.addEventListener("input", render, { signal });
  const sdk = createApplicationClient(() => ({ extension: "mdo.role-editor" }));
  void sdk.call("plugin.prompts", {}, { signal }).then(data => { items = data.items || []; loaded = true; render(); })
    .catch(error => { if (error.name !== "AbortError") { failure = errorMessage(error); render(); } });
  render(); return root;
}

const reasons = {
  selected: "已采用", runtime_pending: "候选，等待运行时工具筛选", source_disabled: "来源已关闭",
  agent_disabled: "角色程序已关闭", role_target: "不适用于当前角色身份", role_selection: "角色配置已过滤",
  explicit_selection_required: "需要角色明确选择", tools_unavailable: "当前请求没有所需工具",
  budget: "超出角色说明预算", plugin_budget: "超出单插件说明预算",
};
export function promptContributionDetails(source) {
  const list = element("div", { className: "agent-plugin-prompt-details" });
  for (const item of source.items || []) list.append(element("details", {}, [
    element("summary", { text: `${item.alias || item.plugin} · ${item.title || item.id} · ${reasons[item.reason] || item.reason || ""}${item.override ? " · Agent 已改写" : ""}` }),
    element("p", { text: `${item.key} · ${item.version} · ${item.bytes} B` }),
    element("code", { text: `${item.revision} / ${item.effective_hash || item.hash}` }),
    element("pre", { className: "store-source", text: item.text }),
  ]));
  return list;
}
