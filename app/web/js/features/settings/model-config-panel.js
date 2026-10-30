import { t as uiErrorText, setUiText as writeUiText, uiMessage, subscribeLocale, t } from "../../i18n.js";

import { api, ApiError } from "../../api/client.js";
import { loadModels } from "../../state/catalogs.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";

import { createAdvancedModelConfigPanel } from "./model-config-advanced.js";
import { providerPresets, setProviderCatalog, presetVariant, presetProvider, presetModel, suggestedModels } from "./provider-presets.js";
import { sameModelConfig } from "./model-config-state.js";
import { createModelProxyEditor } from "./model-proxy-editor.js";
import { verifyModelSetup } from "./model-verification.js";
import { parameterDraft, parameterIssues, applyParameters, createParameterEditor, parameterSummary } from "./model-parameters.js";

const clone = (value) => structuredClone(value);
const text = (key, fallback, params = {}) => t(`modelSetup.${key}`, params, fallback);
const probeErrorKeys = { authentication_failed: "authFailed", credential_unavailable: "credentialUnavailable",
  connection_failed: "connectionFailed", catalog_invalid: "catalogInvalid", supplier_rate_limited: "rateLimited",
  probe_busy: "probeBusy", model_test_failed: "testFailed", model_tools_test_failed: "toolTestFailed",
  model_config_changed: "verificationChanged", model_verification_unavailable: "verificationUnavailable",
  model_tools_unsupported: "toolsUnsupported" };
const probeError = (cause) => probeErrorKeys[cause.code] ? text(probeErrorKeys[cause.code], errorMessage(cause)) : errorMessage(cause);
const button = (label, action, primary = false) => {
  const node = element("button", { text: label, className: primary ? "primary-button" : "secondary-button", attrs: { type: "button" } });
  node.addEventListener("click", action); return node;
};
function field(label, name, value, onChange, { type = "text", required = false } = {}) {
  const control = element("input", { attrs: { name, type, required: required ? "" : null, autoComplete: type === "password" ? "new-password" : "off", maxLength: type === "password" ? 4096 : 2048 } });
  control.value = value || ""; control.addEventListener("input", () => onChange(control.value));
  return element("label", { className: "model-field" }, [element("span", { text: label }), control]);
}

// Supplier setup and the expert editor share the native config transaction.
// Draft keys exist only in this page's memory, never browser storage or URLs.
export function createModelConfigPanel(container) {
  let config = null, etag = "", selectedId = "", view = "list", busy = false;
  let draft = null, parameterEdit = null, advanced = null, advancedChanged = false, needsRead = false;
  let notice = "", failed = false, disposed = false, loading = null;
  const controller = new AbortController();
  let verificationController = null;
  const verified = new Map();
  const keyDrafts = new Map();

  function provider() { return config?.providers.find((item) => item.id === selectedId); }
  function setBusy(value) {
    busy = value; container.toggleAttribute("aria-busy", value);
    for (const node of container.querySelectorAll("button, input, select")) {
      if (node.dataset.allowBusy === "true") continue;
      if (value) { if (node.dataset.beforeBusy == null) node.dataset.beforeBusy = String(node.disabled); node.disabled = true; }
      else if (node.dataset.beforeBusy != null) { node.disabled = node.dataset.beforeBusy === "true"; delete node.dataset.beforeBusy; }
    }
  }
  function status(message, error = false) {
    notice = message; failed = error;
    const line = container.querySelector(".supplier-notice");
    if (line) { writeUiText(line, message); line.classList.toggle("resource-error", error); line.hidden = !message; }
  }
  function blocked() {
    if (busy) return true;
    if (draft || [...keyDrafts.values()].some(Boolean) || advanced?.hasUnsaved()) { toast(t("modelConfig.unsaved"), "error"); return true; }
    return false;
  }
  async function reload() {
    if (disposed || busy || draft || [...keyDrafts.values()].some(Boolean) || advanced) return;
    if (loading) return loading;
    loading = (async () => {
      setBusy(true);
      try {
        const response = await api.get("/models/config"); if (disposed) return;
        config = response.data; etag = response.etag; needsRead = false; verified.clear();
        if (!provider()) selectedId = config.providers.find((entry) => config.items.some((item) => item.id === config.default_model && item.provider === entry.id))?.id || config.providers[0]?.id || "";
        notice = ""; failed = false; render();
      } catch (cause) { if (!disposed) { notice = errorMessage(cause); failed = true; render(); } }
      finally { loading = null; setBusy(false); }
    })(); return loading;
  }
  async function save(next, keys = []) {
    if (busy || needsRead || config.runtime_override) return false;
    setBusy(true); let committed = false;
    try {
      const latest = await api.get("/models/config", { signal: controller.signal });
      if (!sameModelConfig(config, latest.data)) throw new ApiError(uiErrorText("error.detail.modelSettingsChanged", {}, "Model settings changed"), { status: 412 });
      etag = latest.etag;
      const patch = { default_model: next.default_model, providers: next.providers, items: next.items };
      const saved = keys.length
        ? await api.post("/models/setup", { patch, keys }, { ifMatch: etag, signal: controller.signal })
        : await api.put("/settings/models", { schema_version: 1, patch }, { ifMatch: etag, signal: controller.signal });
      committed = true; config = next; etag = saved.etag;
      for (const key of keys) keyDrafts.delete(key.provider);
      verified.clear();
      if (draft) draft.key = "";
      draft = null; needsRead = true;
      try {
        const current = await api.get("/models/config"); config = current.data; etag = current.etag; needsRead = false;
        notice = text("saved", "配置已保存，模型尚未测试。"); failed = false;
      } catch { notice = text("savedReadFailed", "已保存，但重新读取失败。请刷新后继续编辑。"); failed = true; }
      try { await loadModels(); } catch { /* Native save remains acknowledged. */ }
      view = "detail"; render();
    } catch (cause) {
      status(cause.status === 412 ? t("modelConfig.conflictKept") : errorMessage(cause), true);
    } finally { setBusy(false); }
    return committed;
  }
  function showAdvanced(kind, id) {
    if (blocked()) return;
    advanced?.destroy(); advanced = null; view = "advanced"; advancedChanged = false;
    render(); advanced = createAdvancedModelConfigPanel(container.querySelector(".supplier-advanced-host"), {
      kind, id, onSaved: () => { advancedChanged = true; verified.clear(); },
    }); void advanced.ensureLoaded();
  }
  async function back() {
    if (blocked()) return;
    advanced?.destroy(); advanced = null; view = "list";
    if (advancedChanged) { advancedChanged = false; await reload(); } else render();
  }
  async function add() {
    if (blocked()) return;
    view = "templates"; setBusy(true); status(text("readingSuppliers", "正在读取最新供应商和模型目录…"));
    try {
      const response = await api.get("/models/providers", { signal: controller.signal });
      if (disposed) return;
      setProviderCatalog(response.data.providers);
      notice = response.data.stale ? text("cachedSuppliers", "无法连接目录服务，正在使用上次成功获取的供应商目录。") : "";
      failed = false;
    } catch (cause) { if (!disposed) { notice = text("supplierUnavailable", "供应商目录暂时不可用，可重试或手动添加自定义供应商。"); failed = true; } }
    finally { if (!disposed) render(); setBusy(false); }
  }
  function choose(preset, choice = preset.variants[0]) {
    // New models use the same supplier route as discovery unless overridden.
    draft = { preset, choice, key: "", base: choice.base, name: preset.labelKey ? text(preset.labelKey, preset.name) : preset.name, provider: null,
      items: [...choice.models], selected: new Set(choice.models.slice(0, 2)), queried: false,
      ref: "", query: "", manual: "", default: false, advancedOpen: false,
      parameters: new Map(), parameterBinding: "", proxy: { kind: "global" }, modelProxy: { kind: "provider" } };
    view = "setup"; notice = ""; failed = false; render();
  }
  function connection() {
    if (draft.provider) return clone(draft.provider);
    const entry = presetProvider(draft.preset, draft.choice, config.providers, draft.base);
    entry.name = draft.name.trim() || draft.preset.name;
    if (draft.ref.trim()) entry.credential = { secret_ref: draft.ref.trim() };
    entry.proxy = clone(draft.proxy);
    return entry;
  }
  function selectedIds() {
    return [...new Set([...draft.selected, ...draft.manual.split(/[\n,;]+/).map(id => id.trim()).filter(Boolean)])];
  }
  function setupProtocol(entry) {
    return draft.provider ? (entry.endpoints.responses ? "openai-responses" : entry.endpoints.chat_completions ? "openai-chat-completions" : "anthropic-messages") : draft.choice.protocol;
  }
  async function resolveParameters(ids) {
    const entry = connection(), protocol = setupProtocol(entry);
    const binding = JSON.stringify([entry.endpoints, protocol]);
    if (draft.parameterBinding !== binding) { draft.parameters.clear(); draft.parameterBinding = binding; }
    const missing = ids.filter(id => !draft.parameters.has(id));
    if (!missing.length) return true;
    setBusy(true); status(text("readingParameters", "正在读取模型参数并查询服务器参数表…"));
    try {
      for (let offset = 0; offset < missing.length; offset += 32) {
        const payload = { provider: entry, protocol, models: missing.slice(offset, offset + 32) };
        if (draft.key.trim()) payload.key = draft.key.trim();
        const response = await api.post("/models/parameters", payload, { signal: controller.signal });
        if (disposed) return false;
        for (const record of response.data.items) draft.parameters.set(record.id, parameterDraft(record));
      }
      return true;
    } catch (cause) { if (!disposed) status(probeError(cause), true); return false; }
    finally { setBusy(false); }
  }
  function renderParameters(host) {
    let index = 0;
    for (const id of selectedIds()) {
      const value = draft.parameters.get(id);
      if (value) host.append(createParameterEditor(value, { index: index++ }));
    }
  }
  function editParameters(model) {
    if (busy) return;
    parameterEdit = { model, draft: parameterDraft(model.parameters, model) };
    if (!model.parameters) parameterEdit.draft.record.fields = Object.fromEntries([
      ["context_tokens", model.window.context_tokens], ["max_output_tokens", model.window.max_output_tokens],
      ["reasoning", model.capabilities.includes("reasoning-control") ? { kind: "effort", efforts: model.reasoning_efforts.filter(level => level !== "default") } : { kind: "provider-default" }],
    ].map(([key, value]) => [key, { value, source: "legacy" }]));
    view = "parameters"; render();
  }
  async function identifyParameters() {
    if (busy || !parameterEdit) return;
    const entry = config.providers.find(item => item.id === parameterEdit.model.provider);
    setBusy(true);
    try {
      const response = await api.post("/models/parameters", { provider: entry, protocol: parameterEdit.model.default_protocol,
        models: [parameterEdit.model.wire_model] }, { signal: controller.signal });
      if (disposed) return;
      const detected = parameterDraft(response.data.items[0], parameterEdit.model);
      for (const [key, field] of Object.entries(parameterEdit.draft.record.fields))
        if (field.source === "manual") detected.record.fields[key] = clone(field);
      parameterEdit.draft = detected; render();
    } catch (cause) { if (!disposed) status(probeError(cause), true); }
    finally { setBusy(false); }
  }
  function renderParameterEdit(body) {
    body.append(element("h3", { text: text("editParameters", "模型参数") }),
      element("p", { className: "model-config-status", text: text("applyParametersHelp", "重新识别会保留手动填写的参数。确认并应用后才会改变现有模型配置。") }),
      createParameterEditor(parameterEdit.draft));
    body.append(button(text("identifyParameters", "重新识别参数"), () => void identifyParameters()),
      button(text("cancel", "取消"), () => { parameterEdit = null; view = "detail"; render(); }),
      button(text("applyParameters", "应用参数"), async () => {
        if (busy) return;
        if (parameterIssues(parameterEdit.draft).length) { status(text("parametersMissing", "无法获取模型参数，请补全这些参数。"), true); return; }
        const next = clone(config), index = next.items.findIndex(item => item.id === parameterEdit.model.id);
        next.items[index] = applyParameters(parameterEdit.model, parameterEdit.draft);
        if (await save(next)) parameterEdit = null;
      }, true));
  }
  async function discover() {
    if (busy) return;
    const form = container.querySelector(".supplier-setup-form");
    if (!form.reportValidity()) return;
    if (!draft.preset.local && !draft.provider && !draft.key.trim() && !draft.ref.trim()) {
      status(text("keyRequired", "请输入 API Key，或在高级设置中填写凭据引用。"), true); return;
    }
    setBusy(true); status(text("connecting", "正在读取模型列表…"));
    try {
      const entry = connection();
      const payload = { provider: entry }; if (draft.key.trim()) payload.key = draft.key.trim();
      const result = await api.post("/models/discover", payload);
      if (disposed) return;
      draft.items = [...new Set(result.data.items)];
      draft.selected = new Set(suggestedModels(draft.items, draft.choice.models).filter((id) =>
        !config.items.some((model) => model.provider === draft.provider?.id && model.wire_model === id))); draft.queried = true;
      if (!(await resolveParameters([...draft.selected]))) return;
      notice = text("catalogConnected", "模型列表读取成功；请检查参数，添加后将验证回复和工具调用。"); failed = false; render();
    } catch (cause) {
      if (cause.code === "catalog_unsupported") {
        draft.queried = true; notice = text("catalogFallback", "此服务不提供模型列表。可选用模板模型或手动填写模型 ID；添加后将自动验证。"); failed = false; render();
      } else status(probeError(cause), true);
    } finally { setBusy(false); }
  }
  async function addSelected(completeOnly = false) {
    if (busy) return;
    const form = container.querySelector(".supplier-setup-form");
    if (!completeOnly && !form.reportValidity()) return;
    if (completeOnly) for (const control of form.querySelectorAll("input, select")) {
      if (!control.closest(".model-parameter-editor") && control.checkValidity && !control.checkValidity()) {
        control.reportValidity(); return;
      }
    }
    if (!draft.preset.local && !draft.provider && !draft.key.trim() && !draft.ref.trim()) {
      status(text("keyRequired", "请输入 API Key，或在高级设置中填写凭据引用。"), true); return;
    }
    const entry = connection(), next = clone(config);
    if (!draft.provider) next.providers.push(entry);
    const manual = draft.manual.split(/[\n,;]+/).map((id) => id.trim()).filter(Boolean);
    const ids = [...new Set([...draft.selected, ...manual])];
    if (ids.some(id => id.length > 256)) { status(text("idTooLong", "模型 ID 最多 256 个字符。"), true); return; }
    const unresolved = ids.filter(id => !draft.parameters.has(id));
    if (!(await resolveParameters(ids))) return;
    if (unresolved.length) { render(); status(text("reviewParameters", "已读取参数，请检查并补全后再添加。")); return; }
    const incomplete = ids.filter(id => parameterIssues(draft.parameters.get(id)).length);
    if (incomplete.length && !completeOnly) { render(); status(text("parametersMissing", "无法获取模型参数，请补全这些参数。"), true); return; }
    const pendingDraft = incomplete.length ? draft : null;
    const added = [], makeDefault = draft.default;
    for (const id of ids.filter(id => !incomplete.includes(id))) {
      if (id.length > 256) { status(text("idTooLong", "模型 ID 最多 256 个字符。"), true); return; }
      if (next.items.some((item) => item.provider === entry.id && item.wire_model === id)) continue;
      const model = applyParameters(presetModel(entry, id, next.items), draft.parameters.get(id));
      model.proxy = clone(draft.modelProxy); next.items.push(model); added.push(model.id);
    }
    if (!added.length) { status(text("selectModels", "请至少选择一个尚未添加的模型，或填写模型 ID。"), true); return; }
    selectedId = entry.id;
    const keys = draft.key.trim() ? [{ provider: entry.id, value: draft.key.trim() }] : [];
    if (await save(next, keys) && !disposed && !needsRead) {
      const results = await verifyModels(config.items.filter(model => added.includes(model.id)));
      if (makeDefault && !disposed && results?.get(added[0])?.verified && results?.get(added[0])?.agentReady) {
        const message = notice, error = failed, updated = clone(config);
        updated.default_model = added[0];
        if (await save(updated)) status(message, error);
      }
      if (pendingDraft && !disposed) {
        pendingDraft.provider = config.providers.find(item => item.id === entry.id);
        pendingDraft.key = ""; pendingDraft.selected = new Set(incomplete); pendingDraft.manual = "";
        draft = pendingDraft; view = "setup"; render();
      }
    }
  }
  function renderTemplates(body) {
    body.append(element("h3", { text: text("chooseSupplier", "选择供应商") }), element("p", { className: "model-config-status", text: text("templateHelp", "选择服务并填写 API Key。自己的供应商 Key 无需登录墨斗账号。") }));
    const grid = element("div", { className: "supplier-templates" });
    for (const preset of providerPresets.filter((item) => !item.hidden)) grid.append(button(preset.labelKey ? text(preset.labelKey, preset.name) : preset.name, () => choose(preset)));
    body.append(grid, button(text("refreshSuppliers", "刷新供应商目录"), () => void add()));
  }
  function renderSelection(body) {
    const selectedCount = element("span", { className: "model-config-status", text: text("selected", "已选 {count} 个模型", { count: draft.selected.size }) });
    const list = element("div", { className: "supplier-candidates" });
    function fill(query = "") {
      clear(list);
      const ids = draft.items.filter((id) => id.toLowerCase().includes(query.toLowerCase()));
      for (const id of ids.slice(0, 100)) {
        const enabled = config.items.some((model) => model.provider === draft.provider?.id && model.wire_model === id);
        const checkbox = element("input", { attrs: { type: "checkbox", value: id } });
        checkbox.checked = draft.selected.has(id); checkbox.disabled = enabled;
        checkbox.addEventListener("change", () => {
          if (checkbox.checked) draft.selected.add(id); else draft.selected.delete(id);
          writeUiText(selectedCount, text("selected", "已选 {count} 个模型", { count: draft.selected.size }));
          const parameters = container.querySelector(".supplier-parameters");
          if (parameters) { clear(parameters); renderParameters(parameters); }
        });
        list.append(element("label", {}, [checkbox, element("span", { text: id }), enabled ? element("small", { text: text("alreadyAdded", "已添加") }) : null]));
      }
      if (ids.length > 100) list.append(element("small", { text: text("filterMore", "请搜索缩小范围；当前显示前 100 项。") }));
    }
    const search = field(text("findModel", "搜索模型"), "model-query", draft.query, (value) => { draft.query = value; fill(value); });
    body.append(search, selectedCount, list); fill(draft.query);
  }
  function renderSetup(body) {
    const form = element("form", { className: "supplier-setup-form" });
    form.addEventListener("invalid", (event) => { const details = event.target.closest("details"); if (details) details.open = true; }, true);
    form.addEventListener("submit", (event) => { event.preventDefault(); void (draft.queried ? addSelected() : discover()); });
    form.append(element("h3", { text: draft.provider ? text("addModels", "添加模型") : draft.preset.labelKey ? text(draft.preset.labelKey, draft.preset.name) : draft.preset.name }));
    if (!draft.provider && draft.preset.variants.length > 1) {
      const select = element("select", { attrs: { name: "service-variant" } });
      for (const choice of draft.preset.variants) select.append(element("option", { text: text(choice.label, choice.label), attrs: { value: choice.id } }));
      select.value = draft.choice.id; select.addEventListener("change", () => {
        const previous = draft;
        choose(previous.preset, previous.preset.variants.find((item) => item.id === select.value));
        draft.key = previous.key; draft.name = previous.name;
        draft.proxy = previous.proxy; draft.modelProxy = previous.modelProxy;
        draft.advancedOpen = previous.advancedOpen;
        if (previous.preset.custom) {
          draft.base = previous.base; draft.ref = previous.ref; draft.manual = previous.manual; draft.default = previous.default;
        }
        render();
      });
      form.append(element("label", { className: "model-field" }, [element("span", { text: draft.preset.custom ? text("protocol", "接口协议") : text("apiProduct", "API 产品") }), select]));
    }
    if (!draft.provider && draft.preset.custom) form.append(field(text("baseUrl", "服务地址（含 /v1）"), "base-url", draft.base, (value) => { draft.base = value; draft.queried = false; }, { type: "url", required: true }));
    if (draft.choice.key_note) form.append(element("p", { className: "model-parameter-warning", text: draft.choice.key_note }));
    if (draft.choice.source_url && /^https:\/\//.test(draft.choice.source_url)) form.append(element("a", {
      text: text("productDocs", "接口与套餐说明"), attrs: { href: draft.choice.source_url, target: "_blank", rel: "noopener noreferrer" },
    }));
    if (!draft.provider || draft.provider.credential) {
      form.append(field(draft.provider ? text("replaceKeyOptional", "API Key（留空使用已保存的凭据）") : text("apiKey", "API Key"), "api-key", draft.key, (value) => { draft.key = value; draft.queried = false; }, { type: "password" }));
      if (draft.preset.keyUrl && !draft.provider) form.append(element("a", { className: "supplier-key-link", text: text("getKey", "获取 API Key"), attrs: { href: draft.preset.keyUrl, target: "_blank", rel: "noopener noreferrer" } }));
      form.append(element("p", { className: "model-config-status", text: text("keyPrivacy", "Key 仅在本机加密保存，不写入模型配置。复制到其他设备后需重新填写。") }));
    }
    const advanced = element("details", { className: "supplier-connection-advanced" }, [element("summary", { text: text("advancedConnection", "高级设置") }),
      ...(!draft.provider ? [
        field(text("connectionName", "供应商名称"), "connection-name", draft.name, (value) => { draft.name = value; }),
        !draft.preset.custom ? field(text("baseUrl", "服务地址（含 /v1）"), "base-url", draft.base, (value) => { draft.base = value; draft.queried = false; }, { type: "url", required: true }) : null,
        field(text("credentialReference", "凭据引用（可代替 Key）"), "credential-ref", draft.ref, (value) => { draft.ref = value; draft.queried = false; }),
        createModelProxyEditor(draft.proxy, { collapsible: false, titleKey: "modelProxy.providerTitle",
          namePrefix: "provider_proxy_",
          onChange: value => { draft.proxy = value; draft.queried = false; } }).element,
      ] : []),
      createModelProxyEditor(draft.modelProxy, { allowProvider: true, collapsible: false, titleKey: "modelProxy.newModelsTitle",
        namePrefix: "model_proxy_",
        onChange: value => { draft.modelProxy = value; } }).element,
    ]);
    advanced.open = Boolean(draft.advancedOpen);
    const setupDraft = draft;
    advanced.addEventListener("toggle", () => {
      if (draft === setupDraft && container.contains(advanced)) draft.advancedOpen = advanced.open;
    });
    form.append(advanced);
    if (draft.queried) renderSelection(form);
    const parameters = element("div", { className: "supplier-parameters" });
    renderParameters(parameters); form.append(parameters);
    form.append(field(text("manualIds", "手动添加模型 ID（多个用逗号分隔）"), "manual-models", draft.manual, (value) => { draft.manual = value; }));
    const makeDefault = element("input", { attrs: { type: "checkbox", name: "make-default" } });
    makeDefault.checked = draft.default; makeDefault.addEventListener("change", () => { draft.default = makeDefault.checked; });
    form.append(element("label", { className: "supplier-inline-check" }, [makeDefault, element("span", { text: text("useDefault", "首个新增模型验证通过后设为默认") })]));
    form.append(element("p", { className: "model-config-status", text: text("autoVerifyHelp", "添加后逐个验证流式回复和工具调用，会消耗 Token；工具测试携带本机工具定义，但不会执行工具。缺失的模型参数需要先补全。") }));
    const actions = element("div", { className: "supplier-actions" }, [
      button(text("cancel", "取消"), () => { draft.key = ""; draft = null; view = "detail"; notice = ""; render(); }),
      button(draft.queried ? text("refreshList", "刷新模型列表") : text("connect", "连接并读取模型"), () => void discover()),
      button(draft.queried ? text("addSelected", "添加并验证所选模型") : text("saveAndVerify", "直接添加并验证"), () => void addSelected(), true),
    ]);
    actions.append(button(text("readParameters", "读取所选模型参数"), async () => { if (!busy && await resolveParameters(selectedIds())) render(); }));
    if ([...draft.parameters.values()].some(value => parameterIssues(value).length))
      actions.append(button(text("addComplete", "仅添加参数完整的模型"), () => void addSelected(true)));
    form.append(actions); body.append(form);
  }
  function verificationLabel(record) {
    if (!record) return text("untested", "已配置 · 尚未测试");
    if (record.running) return text(record.running === "tools" ? "verifyingTools" : "verifyingReply", record.running === "tools" ? "正在验证工具调用…" : "正在验证回复…");
    if (record.error) return record.reply?.state === "passed" ? text("toolsFailureDetail", "回复通过 · 工具调用失败：{reason}", { reason: probeError(record.error) }) : probeError(record.error);
    if (record.verified) return record.tools?.state === "passed" ? text("verified", "已验证 · 流式回复和工具调用通过") : text("replyVerified", "已验证 · 流式回复通过（未声明工具能力）");
    if (record.reply?.state === "failed") return text("replyFailed", "回复验证失败，请检查配置后重新验证。");
    if (record.tools?.state === "failed") return text("toolsFailed", "回复通过 · 工具调用验证失败，请检查配置后重新验证。");
    if (record.reply?.state === "passed") return text("replyOnly", "回复通过 · 工具调用尚未通过验证");
    if (record.tools?.state === "passed") return text("toolsOnly", "工具调用通过 · 回复尚未通过验证");
    return text("verificationIncomplete", "验证未完成，请重新验证。");
  }
  async function verifyModels(models) {
    if (busy || disposed || !models.length) return;
    verificationController = new AbortController();
    setBusy(true); let results;
    try {
      results = await verifyModelSetup(models, {
        signal: verificationController.signal,
        request: async (id, tools, signal) => (await api.post("/models/test", { model_id: id, tools }, { signal })).data,
        onProgress({ model, index, total, phase, state, result }) {
          verified.set(model.id, { ...result, running: state === "running" ? phase : "" });
          notice = text("verificationProgress", "验证 {index}/{total} · {model} · {stage}", {
            index: index + 1, total, model: model.name, stage: verificationLabel(verified.get(model.id)),
          }); failed = false; render();
        },
      });
      // Native records are authoritative across page reloads and restarts.
      const current = await api.get("/models/config", { signal: controller.signal });
      if (!sameModelConfig(config, current.data)) {
        verified.clear(); throw new ApiError(text("verificationChanged", "配置已变化，请重新验证。"));
      }
      config = current.data; etag = current.etag;
      for (const model of models) {
        const result = results.get(model.id), evidence = config.verification?.[model.id];
        if (result.verified && !evidence?.verified) {
          result.verified = false; result.agentReady = false; result.error = new ApiError(text("verificationChanged", "配置已变化，请重新验证。"));
          verified.set(model.id, result);
        }
      }
      const passed = [...results.values()].filter(result => result.verified).length;
      notice = text("verificationComplete", "验证完成 · {passed}/{total} 个模型通过；未通过的模型可查看原因并重新验证。", { passed, total: models.length });
      failed = passed !== models.length; render();
    } catch (cause) {
      results = undefined;
      for (const model of models) if (verified.get(model.id)?.running) verified.set(model.id, {});
      if (!disposed) {
        notice = cause.name === "AbortError" ? text("verificationStopped", "已停止后续验证。配置已保存，可稍后重新验证。") : errorMessage(cause);
        failed = cause.name !== "AbortError"; render();
      }
    }
    finally { verificationController = null; render(); setBusy(false); }
    return results;
  }
  async function testModel(model, node, line, tools = false) {
    if (busy) return;
    setBusy(true); line.classList.remove("resource-error"); writeUiText(line, text("testing", "正在请求模型…"));
    try {
      const result = await api.post("/models/test", { model_id: model.id, tools }, { signal: controller.signal });
      if (disposed) return;
      const record = result.data.verification || { [tools ? "tools" : "reply"]: { state: "passed" } };
      verified.set(model.id, record); writeUiText(line, verificationLabel(record));
    } catch (cause) { if (!disposed) { verified.set(model.id, { error: cause }); writeUiText(line, probeError(cause)); line.classList.add("resource-error"); } }
    finally { setBusy(false); if (!disposed) node.focus({ preventScroll: true }); }
  }
  function renderDetail(body) {
    const entry = provider(); if (!entry) return;
    body.append(element("header", { className: "supplier-detail-heading" }, [element("h3", { text: entry.name }),
      entry.builtin ? element("span", { text: uiMessage("modelConfig.builtinTag") }) : button(text("advancedEditor", "高级编辑"), () => showAdvanced("provider", entry.id))]));
    if (entry.builtin) body.append(element("p", { className: "model-config-status", text: uiMessage("modelConfig.builtinModelNote") }));
    else {
      body.append(element("p", { className: "model-config-status supplier-address", text: Object.values(entry.endpoints)[0] || "" }));
      const keyForm = element("form", { className: "supplier-key-form" }); let key = keyDrafts.get(entry.id) || "";
      const keySave = button(text("saveKey", "保存 Key"), async () => { if (!key.trim()) return; if (await save(clone(config), [{ provider: entry.id, value: key.trim() }])) key = ""; });
      keySave.disabled = !key.trim();
      const keyField = field(text("replaceKeyOptional", "API Key（留空使用已保存的凭据）"), "replacement-key", key, (value) => { key = value; keyDrafts.set(entry.id, value); keySave.disabled = !value.trim(); }, { type: "password" });
      keyForm.append(keyField, keySave,
        button(text("cancel", "取消"), () => { keyDrafts.delete(entry.id); render(); }));
      keyForm.addEventListener("submit", (event) => { event.preventDefault(); keyForm.querySelector("button").click(); });
      const credentials = element("details", { className: "supplier-connection-advanced", attrs: { open: key ? "" : null } }, [
        element("summary", { text: entry.credential?.secret_ref ? text("keyConfigured", "凭据已配置 · 更换 API Key") : text("configureKey", "设置 API Key") }), keyForm]);
      body.append(credentials, button(text("addModels", "添加模型"), () => {
        if (blocked()) return;
        const preset = providerPresets.find((item) => item.id === entry.template_id) || providerPresets.find((item) => item.id === "custom");
        choose(preset, presetVariant(preset.id, entry.template_variant)); draft.provider = entry; render();
      }));
    }
    const models = config.items.filter((item) => item.provider === entry.id);
    body.append(element("p", { className: "model-config-status", text: text("testHelp", "测试会消耗 Token；工具测试携带本机已注册工具定义，使用指定探针，不执行任何工具，也不读取会话历史。") }));
    const list = element("div", { className: "supplier-models" });
    for (const model of models) {
      const record = verified.get(model.id) || config.verification?.[model.id];
      const line = element("p", { className: `supplier-model-status ${record?.error || record?.reply?.state === "failed" || record?.tools?.state === "failed" ? "resource-error" : ""}`, text: record ? verificationLabel(record) : (model.builtin ? text("builtinAvailable", "内置服务") : text("untested", "已配置 · 尚未测试")), attrs: { role: "status" } });
      const row = element("article", { className: "supplier-model" }, [element("div", { className: "supplier-model-title" }, [element("strong", { text: model.name }), model.id === config.default_model ? element("span", { className: "model-default-badge", text: uiMessage("modelConfig.defaultTag") }) : null]), element("small", { text: model.wire_model }), line]);
      const actions = element("div", { className: "supplier-actions" });
      const verify = button(text("verifyAgain", "完整验证 / 重试"), () => void verifyModels([model])); actions.append(verify);
      verify.disabled = model.enabled === false;
      const test = button(text("testReply", "测试回复"), () => void testModel(model, test, line)); actions.append(test);
      if (model.enabled !== false && model.capabilities.includes("tool-call-output")) {
        const toolTest = button(text("testTools", "测试工具调用"), () => void testModel(model, toolTest, line, true)); actions.append(toolTest);
      }
      if (!model.builtin) actions.append(button(text("advancedEditor", "高级编辑"), () => showAdvanced("model", model.id)));
      if (model.id !== config.default_model && model.enabled !== false && record?.tools?.state === "passed") actions.append(button(uiMessage("modelConfig.setDefault"), () => { const next = clone(config); next.default_model = model.id; void save(next); }));
      if (!model.builtin && model.id !== config.default_model) {
        actions.append(button(model.enabled === false ? text("enable", "启用") : text("disable", "停用"), () => {
          const next = clone(config); next.items.find((item) => item.id === model.id).enabled = model.enabled === false; void save(next);
        }));
        const confirm = element("div", { className: "model-delete-confirm", attrs: { hidden: "" } }, [element("span", { text: uiMessage("modelConfig.removePrompt", { name: model.name }) })]);
        confirm.append(button(text("cancel", "取消"), () => { confirm.hidden = true; }), button(uiMessage("modelConfig.confirmRemove"), () => {
          const next = clone(config); next.items = next.items.filter((item) => item.id !== model.id); void save(next);
        }));
        actions.append(button(uiMessage("modelConfig.remove"), () => { confirm.hidden = false; })); row.append(confirm);
      }
      if (model.enabled === false) { writeUiText(line, text("disabled", "已停用；启用后可用于会话。")); test.disabled = true; }
      if (!model.builtin) {
        row.append(element("p", { className: "model-config-status", text: parameterSummary(model) }));
        actions.append(button(text("editParameters", "模型参数"), () => editParameters(model)));
      }
      row.append(actions); list.append(row);
    }
    body.append(list);
  }
  function render() {
    if (disposed) return;
    const active = document.activeElement;
    const hadFocus = container.contains(active), focusName = active?.name;
    const selection = hadFocus && typeof active.selectionStart === "number" ? [active.selectionStart, active.selectionEnd] : null;
    clear(container);
    const header = element("div", { className: "supplier-toolbar" }, [element("p", { className: "model-config-status", text: config ? text("default", "默认模型：{model}", { model: config.items.find((item) => item.id === config.default_model)?.name || config.default_model }) : uiMessage("resource.loading") }),
      button(uiMessage("modelConfig.refresh"), () => void reload()), button(text("addSupplier", "添加供应商"), add, true)]);
    container.append(header, element("p", { className: `supplier-notice ${failed ? "resource-error" : "model-config-status"}`, text: notice, attrs: { role: failed ? "alert" : "status", hidden: notice ? null : "" } }));
    if (verificationController) {
      const stop = button(text("stopVerification", "停止后续验证"), () => verificationController?.abort());
      stop.dataset.allowBusy = "true"; header.append(stop);
    }
    if (!config) return;
    if (config.runtime_override || needsRead) {
      const line = element("p", { className: "resource-error", text: config.runtime_override ? uiMessage("modelConfig.runtimeOverride") : notice }); container.append(line);
    }
    const layout = element("div", { className: `supplier-layout supplier-view-${view}` });
    const list = element("nav", { className: "supplier-list", attrs: { "aria-label": uiMessage("modelConfig.providerList") } });
    for (const entry of config.providers) {
      const item = button(entry.name, () => { if (blocked()) return; selectedId = entry.id; view = "detail"; notice = ""; render(); });
      item.className = "model-config-item"; item.setAttribute("aria-current", entry.id === selectedId && view === "detail" ? "true" : "false");
      item.append(element("small", { text: text("modelCount", "{count} 个模型", { count: config.items.filter((model) => model.provider === entry.id).length }) })); list.append(item);
    }
    const body = element("div", { className: "supplier-body" });
    if (view !== "list") body.append(button(view === "setup" ? text("cancel", "取消") : view === "advanced" ? text("backSuppliers", "返回供应商") : text("back", "返回"), () => {
      if (view === "setup") { draft.key = ""; draft = null; }
      void back();
    }));
    if (view === "templates") renderTemplates(body);
    else if (view === "setup") renderSetup(body);
    else if (view === "parameters") renderParameterEdit(body);
    else if (view === "advanced") body.append(element("div", { className: "supplier-advanced-host" }));
    else renderDetail(body);
    layout.append(list, body); container.append(layout);
    if (config.runtime_override || needsRead) for (const node of container.querySelectorAll("button"))
      if (node !== header.children[1]) node.disabled = true;
    // The expert editor owns its revision and drafts until the user returns.
    // Keep the outer navigation from detaching a still-live editor.
    if (view === "advanced") { header.children[1].disabled = true; header.children[2].disabled = true; }
    if (busy) setBusy(true);
    if (hadFocus) {
      const field = focusName && [...container.querySelectorAll("input, select")].find((node) => node.name === focusName);
      const heading = body.querySelector("h3");
      const target = field || (view === "list" ? [...list.querySelectorAll("button")].find((node) => node.textContent.startsWith(provider()?.name || "")) : heading);
      if (target) { const details = target.closest("details"); if (details) details.open = true; if (target === heading) target.tabIndex = -1; target.focus(); if (field && selection) field.setSelectionRange?.(...selection); }
    }
  }
  const unsubscribe = subscribeLocale(() => { if (!advanced && !busy) render(); });
  return Object.freeze({ ensureLoaded: () => config ? Promise.resolve() : reload(), reload,
    destroy() { disposed = true; controller.abort(); verificationController?.abort(); if (draft) draft.key = ""; keyDrafts.clear(); advanced?.destroy(); unsubscribe(); } });
}
