import { api, ApiError } from "../../api/client.js";
import { loadModels } from "../../state/catalogs.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";
import { subscribeLocale, t } from "../../i18n.js";
import { createAdvancedModelConfigPanel } from "./model-config-advanced.js";
import { providerPresets, presetVariant, presetProvider, presetModel, suggestedModels } from "./provider-presets.js";
import { sameModelConfig } from "./model-config-state.js";

const clone = (value) => structuredClone(value);
const text = (key, fallback, params = {}) => t(`modelSetup.${key}`, params, fallback);
const probeErrorKeys = { authentication_failed: "authFailed", credential_unavailable: "credentialUnavailable",
  connection_failed: "connectionFailed", catalog_invalid: "catalogInvalid", supplier_rate_limited: "rateLimited",
  probe_busy: "probeBusy", model_test_failed: "testFailed", model_tools_test_failed: "toolTestFailed" };
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
  let draft = null, advanced = null, advancedChanged = false, needsRead = false;
  let notice = "", failed = false, disposed = false, loading = null;
  const verified = new Map();
  const keyDrafts = new Map();

  function provider() { return config?.providers.find((item) => item.id === selectedId); }
  function setBusy(value) {
    busy = value; container.toggleAttribute("aria-busy", value);
    for (const node of container.querySelectorAll("button, input, select")) {
      if (value) { node.dataset.beforeBusy = String(node.disabled); node.disabled = true; }
      else if (node.dataset.beforeBusy != null) { node.disabled = node.dataset.beforeBusy === "true"; delete node.dataset.beforeBusy; }
    }
  }
  function status(message, error = false) {
    notice = message; failed = error;
    const line = container.querySelector(".supplier-notice");
    if (line) { line.textContent = message; line.classList.toggle("resource-error", error); line.hidden = !message; }
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
        config = response.data; etag = response.etag; needsRead = false;
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
      const latest = await api.get("/models/config");
      if (!sameModelConfig(config, latest.data)) throw new ApiError("Model settings changed", { status: 412 });
      etag = latest.etag;
      const patch = { default_model: next.default_model, providers: next.providers, items: next.items };
      const saved = keys.length
        ? await api.post("/models/setup", { patch, keys }, { ifMatch: etag })
        : await api.put("/settings/models", { schema_version: 1, patch }, { ifMatch: etag });
      committed = true; config = next; etag = saved.etag;
      for (const key of keys) { keyDrafts.delete(key.provider); verified.clear(); }
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
  function add() {
    if (blocked()) return;
    view = "templates"; render();
  }
  function choose(preset, choice = preset.variants[0]) {
    draft = { preset, choice, key: "", base: choice.base, name: preset.name, provider: null,
      items: [...choice.models], selected: new Set(choice.models.slice(0, 2)), queried: false,
      ref: "", query: "", manual: "", default: false };
    view = "setup"; notice = ""; failed = false; render();
  }
  function connection() {
    if (draft.provider) return clone(draft.provider);
    const entry = presetProvider(draft.preset, draft.choice, config.providers, draft.base);
    entry.name = draft.name.trim() || draft.preset.name;
    if (draft.ref.trim()) entry.credential = { secret_ref: draft.ref.trim() };
    return entry;
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
      notice = text("catalogConnected", "模型列表连接成功；选择模型后添加，实际可用性需单独测试。"); failed = false; render();
    } catch (cause) {
      if (cause.code === "catalog_unsupported") {
        draft.queried = true; notice = text("catalogFallback", "此服务不提供模型列表。可选用模板模型或手动填写模型 ID；保存后请测试。"); failed = false; render();
      } else status(probeError(cause), true);
    } finally { setBusy(false); }
  }
  async function addSelected() {
    if (busy) return;
    const form = container.querySelector(".supplier-setup-form"); if (!form.reportValidity()) return;
    if (!draft.preset.local && !draft.provider && !draft.key.trim() && !draft.ref.trim()) {
      status(text("keyRequired", "请输入 API Key，或在高级设置中填写凭据引用。"), true); return;
    }
    const entry = connection(), next = clone(config);
    if (!draft.provider) next.providers.push(entry);
    const manual = draft.manual.split(/[\n,;]+/).map((id) => id.trim()).filter(Boolean);
    const ids = [...new Set([...draft.selected, ...manual])];
    let added = 0;
    for (const id of ids) {
      if (id.length > 256) { status(text("idTooLong", "模型 ID 最多 256 个字符。"), true); return; }
      if (next.items.some((item) => item.provider === entry.id && item.wire_model === id)) continue;
      const model = presetModel(entry, id, next.items); next.items.push(model); added++;
      if (draft.default && added === 1) next.default_model = model.id;
    }
    if (!added) { status(text("selectModels", "请至少选择一个尚未添加的模型，或填写模型 ID。"), true); return; }
    selectedId = entry.id;
    const keys = draft.key.trim() ? [{ provider: entry.id, value: draft.key.trim() }] : [];
    await save(next, keys);
  }
  function renderTemplates(body) {
    body.append(element("h3", { text: text("chooseSupplier", "选择供应商") }), element("p", { className: "model-config-status", text: text("templateHelp", "选择服务并填写 API Key。自己的供应商 Key 无需登录墨斗账号。") }));
    const grid = element("div", { className: "supplier-templates" });
    for (const preset of providerPresets) grid.append(button(preset.id === "openai-compatible" ? text("compatibleOpenai", "OpenAI 兼容服务") : preset.id === "anthropic-compatible" ? text("compatibleAnthropic", "Anthropic 兼容服务") : preset.name, () => choose(preset)));
    body.append(grid);
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
        checkbox.addEventListener("change", () => { if (checkbox.checked) draft.selected.add(id); else draft.selected.delete(id); selectedCount.textContent = text("selected", "已选 {count} 个模型", { count: draft.selected.size }); });
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
    form.append(element("h3", { text: draft.provider ? text("addModels", "添加模型") : draft.preset.name }));
    if (!draft.provider && draft.preset.variants.length > 1) {
      const select = element("select", { attrs: { name: "service-variant" } });
      for (const choice of draft.preset.variants) select.append(element("option", { text: text(choice.label, choice.label), attrs: { value: choice.id } }));
      select.value = draft.choice.id; select.addEventListener("change", () => {
        const key = draft.key, name = draft.name; choose(draft.preset, draft.preset.variants.find((item) => item.id === select.value)); draft.key = key; draft.name = name; render();
      });
      form.append(element("label", { className: "model-field" }, [element("span", { text: text("apiProduct", "API 产品") }), select]));
    }
    if (!draft.provider && draft.preset.custom) form.append(field(text("baseUrl", "服务地址（含 /v1）"), "base-url", draft.base, (value) => { draft.base = value; draft.queried = false; }, { type: "url", required: true }));
    if (!draft.provider || draft.provider.credential) {
      form.append(field(draft.provider ? text("replaceKeyOptional", "API Key（留空使用已保存的凭据）") : text("apiKey", "API Key"), "api-key", draft.key, (value) => { draft.key = value; draft.queried = false; }, { type: "password" }));
      if (draft.preset.keyUrl && !draft.provider) form.append(element("a", { className: "supplier-key-link", text: text("getKey", "获取 API Key"), attrs: { href: draft.preset.keyUrl, target: "_blank", rel: "noopener noreferrer" } }));
      form.append(element("p", { className: "model-config-status", text: text("keyPrivacy", "Key 仅在本机加密保存，不写入模型配置。复制到其他设备后需重新填写。") }));
    }
    if (!draft.provider) form.append(element("details", { className: "supplier-connection-advanced" }, [element("summary", { text: text("advancedConnection", "高级连接设置") }),
      field(text("connectionName", "供应商名称"), "connection-name", draft.name, (value) => { draft.name = value; }),
      !draft.preset.custom ? field(text("baseUrl", "服务地址（含 /v1）"), "base-url", draft.base, (value) => { draft.base = value; draft.queried = false; }, { type: "url", required: true }) : null,
      field(text("credentialReference", "凭据引用（可代替 Key）"), "credential-ref", draft.ref, (value) => { draft.ref = value; draft.queried = false; })]));
    if (draft.queried) renderSelection(form);
    form.append(field(text("manualIds", "手动添加模型 ID（多个用逗号分隔）"), "manual-models", draft.manual, (value) => { draft.manual = value; }));
    const makeDefault = element("input", { attrs: { type: "checkbox", name: "make-default" } });
    makeDefault.checked = draft.default; makeDefault.addEventListener("change", () => { draft.default = makeDefault.checked; });
    form.append(element("label", { className: "supplier-inline-check" }, [makeDefault, element("span", { text: text("useDefault", "将首个新增模型设为默认") })]));
    const actions = element("div", { className: "supplier-actions" }, [
      button(text("cancel", "取消"), () => { draft.key = ""; draft = null; view = "detail"; notice = ""; render(); }),
      button(draft.queried ? text("refreshList", "刷新模型列表") : text("connect", "连接并读取模型"), () => void discover()),
      button(draft.queried ? text("addSelected", "添加所选模型") : text("saveUnverified", "直接保存，稍后测试"), () => void addSelected(), true),
    ]);
    form.append(actions); body.append(form);
  }
  async function testModel(model, node, line, tools = false) {
    if (busy) return;
    setBusy(true); line.classList.remove("resource-error"); line.textContent = text("testing", "正在请求模型…");
    try {
      const result = await api.post("/models/test", { model_id: model.id, tools });
      const message = tools ? text("toolsTested", "工具调用测试通过 · {ms} ms", { ms: result.data.latency_ms }) : text("tested", "回复测试通过 · {ms} ms", { ms: result.data.latency_ms });
      verified.set(model.id, message); line.textContent = message;
    } catch (cause) { verified.delete(model.id); line.textContent = probeError(cause); line.classList.add("resource-error"); }
    finally { setBusy(false); node.focus({ preventScroll: true }); }
  }
  function renderDetail(body) {
    const entry = provider(); if (!entry) return;
    body.append(element("header", { className: "supplier-detail-heading" }, [element("h3", { text: entry.name }),
      entry.builtin ? element("span", { text: t("modelConfig.builtinTag") }) : button(text("advancedEditor", "高级编辑"), () => showAdvanced("provider", entry.id))]));
    if (entry.builtin) body.append(element("p", { className: "model-config-status", text: t("modelConfig.builtinModelNote") }));
    else {
      body.append(element("p", { className: "model-config-status supplier-address", text: Object.values(entry.endpoints)[0] || "" }));
      const keyForm = element("form", { className: "supplier-key-form" }); let key = keyDrafts.get(entry.id) || "";
      const keyField = field(text("replaceKeyOptional", "API Key（留空使用已保存的凭据）"), "replacement-key", key, (value) => { key = value; keyDrafts.set(entry.id, value); }, { type: "password" });
      keyForm.append(keyField, button(text("saveKey", "保存 Key"), async () => { if (!key.trim()) return; if (await save(clone(config), [{ provider: entry.id, value: key.trim() }])) key = ""; }),
        button(text("cancel", "取消"), () => { keyDrafts.delete(entry.id); render(); }));
      keyForm.addEventListener("submit", (event) => { event.preventDefault(); keyForm.querySelector("button").click(); });
      body.append(keyForm, button(text("addModels", "添加模型"), () => {
        if (blocked()) return;
        const preset = providerPresets.find((item) => item.id === entry.template_id) || providerPresets[10];
        choose(preset, presetVariant(preset.id, entry.template_variant)); draft.provider = entry; render();
      }));
    }
    const models = config.items.filter((item) => item.provider === entry.id);
    body.append(element("p", { className: "model-config-status", text: text("testHelp", "测试会发送简短请求，可能消耗少量 Token；工具调用测试不会执行任何工具。") }));
    const list = element("div", { className: "supplier-models" });
    for (const model of models) {
      const line = element("p", { className: "supplier-model-status", text: verified.get(model.id) || (model.builtin ? text("builtinAvailable", "内置服务") : text("untested", "已配置 · 尚未测试")), attrs: { role: "status" } });
      const row = element("article", { className: "supplier-model" }, [element("div", { className: "supplier-model-title" }, [element("strong", { text: model.name }), model.id === config.default_model ? element("span", { className: "model-default-badge", text: t("modelConfig.defaultTag") }) : null]), element("small", { text: model.wire_model }), line]);
      const actions = element("div", { className: "supplier-actions" });
      const test = button(text("testReply", "测试回复"), () => void testModel(model, test, line)); actions.append(test);
      if (model.enabled !== false && model.capabilities.includes("tool-call-output")) {
        const toolTest = button(text("testTools", "测试工具调用"), () => void testModel(model, toolTest, line, true)); actions.append(toolTest);
      }
      if (!model.builtin) actions.append(button(text("advancedEditor", "高级编辑"), () => showAdvanced("model", model.id)));
      if (model.id !== config.default_model && model.enabled !== false) actions.append(button(t("modelConfig.setDefault"), () => { const next = clone(config); next.default_model = model.id; void save(next); }));
      if (!model.builtin && model.id !== config.default_model) {
        actions.append(button(model.enabled === false ? text("enable", "启用") : text("disable", "停用"), () => {
          const next = clone(config); next.items.find((item) => item.id === model.id).enabled = model.enabled === false; void save(next);
        }));
        const confirm = element("div", { className: "model-delete-confirm", attrs: { hidden: "" } }, [element("span", { text: t("modelConfig.removePrompt", { name: model.name }) })]);
        confirm.append(button(text("cancel", "取消"), () => { confirm.hidden = true; }), button(t("modelConfig.confirmRemove"), () => {
          const next = clone(config); next.items = next.items.filter((item) => item.id !== model.id); void save(next);
        }));
        actions.append(button(t("modelConfig.remove"), () => { confirm.hidden = false; })); row.append(confirm);
      }
      if (model.enabled === false) { line.textContent = text("disabled", "已停用；启用后可用于会话。"); test.disabled = true; }
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
    const header = element("div", { className: "supplier-toolbar" }, [element("p", { className: "model-config-status", text: config ? text("default", "默认模型：{model}", { model: config.items.find((item) => item.id === config.default_model)?.name || config.default_model }) : t("resource.loading") }),
      button(t("modelConfig.refresh"), () => void reload()), button(text("addSupplier", "添加供应商"), add, true)]);
    container.append(header, element("p", { className: `supplier-notice ${failed ? "resource-error" : "model-config-status"}`, text: notice, attrs: { role: failed ? "alert" : "status", hidden: notice ? null : "" } }));
    if (!config) return;
    if (config.runtime_override || needsRead) {
      const line = element("p", { className: "resource-error", text: config.runtime_override ? t("modelConfig.runtimeOverride") : notice }); container.append(line);
    }
    const layout = element("div", { className: `supplier-layout supplier-view-${view}` });
    const list = element("nav", { className: "supplier-list", attrs: { "aria-label": t("modelConfig.providerList") } });
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
    destroy() { disposed = true; if (draft) draft.key = ""; keyDrafts.clear(); advanced?.destroy(); unsubscribe(); } });
}
