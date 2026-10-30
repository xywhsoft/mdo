import { t as uiErrorText, setUiText as writeUiText, uiMessage, subscribeLocale, t } from "../../i18n.js";

import { api, ApiError } from "../../api/client.js";
import { loadModels } from "../../state/catalogs.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";
import { openLayer } from "../../components/layers.js";
import { mountIcons } from "../../components/icons.js";

import { createAdvancedModelConfigPanel } from "./model-config-advanced.js";
import { providerPresets, setProviderCatalog, presetVariant, providerDisplayName } from "./provider-presets.js";
import { sameModelConfig } from "./model-config-state.js";
import { createModelSetupWizard } from "./model-setup-wizard.js";
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
  let draft = null, setup = null, parameterEdit = null, advanced = null, advancedChanged = false, needsRead = false;
  let notice = "", failed = false, disposed = false, loading = null;
  const controller = new AbortController();
  let verificationController = null;
  let removal = null;
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
    if (busy || removal) return true;
    if (setup || [...keyDrafts.values()].some(Boolean) || advanced?.hasUnsaved()) { toast(t("modelConfig.unsaved"), "error"); return true; }
    return false;
  }
  async function reload() {
    if (disposed || busy || setup || removal || [...keyDrafts.values()].some(Boolean) || advanced) return;
    if (loading) return loading;
    loading = (async () => {
      setBusy(true);
      try {
        const response = await api.get("/models/config"); if (disposed) return;
        config = response.data; etag = response.etag; needsRead = false; verified.clear();
        if (!provider()) selectedId = config.providers.find((entry) => config.items.some((item) => item.id === config.default_model && item.provider === entry.id))?.id || config.providers[0]?.id || "";
        notice = ""; failed = false; render();
        if (config.providers.some(item => item.template_id)) void api.get("/models/providers", { signal: controller.signal }).then(response => { if (!disposed) { setProviderCatalog(response.data.providers, response.data); if (!draft && !advanced && !busy) render(); } }).catch(() => {});
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
      draft?.destroy();
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
  function canRemoveProvider(entry) {
    return !entry.builtin && entry.removable === true && !config.items.some(model =>
      model.provider === entry.id && (model.builtin || model.removable === false));
  }
  function removeProvider(entry, opener) {
    if (blocked() || needsRead || config.runtime_override || !canRemoveProvider(entry)) return;
    const next = clone(config), models = next.items.filter(model => model.provider === entry.id);
    next.providers = next.providers.filter(item => item.id !== entry.id);
    next.items = next.items.filter(model => model.provider !== entry.id);
    const fallback = next.items.find(model => model.id === next.default_model) ||
      next.items.find(model => model.builtin && model.enabled !== false) || next.items.find(model => model.enabled !== false);
    if (!fallback) { status(t("modelConfig.defaultInUse"), true); return; }
    const changedDefault = next.default_model !== fallback.id;
    next.default_model = fallback.id;
    const own = { pending: false, layer: null };
    own.layer = openLayer({ title: uiMessage("modelSetup.removeSupplier", {}, "删除供应商"), icon: "close", width: "32rem", height: "auto",
      mobile: "fit", maximizable: false, owner: container, opener, beforeClose: () => !own.pending,
      onClosed() { if (removal === own) removal = null; },
    });
    removal = own; own.layer.element.classList.add("supplier-remove-layer");
    own.layer.body.append(element("p", { text: uiMessage("modelSetup.removeSupplierPrompt", { name: providerDisplayName(entry), count: models.length }, "确定删除供应商“{name}”吗？对应的 {count} 个模型也会一并删除，已有会话会保留。") }));
    if (changedDefault) own.layer.body.append(element("p", { text: uiMessage("modelSetup.removeSupplierDefault", { model: fallback.name }, "默认模型将切换为 {model}。") }));
    const error = element("p", { className: "resource-error", attrs: { role: "alert", hidden: "" } });
    own.layer.body.append(error);
    const cancel = button(uiMessage("modelSetup.cancel", {}, "取消"), () => void own.layer.close());
    const apply = button(uiMessage("modelConfig.confirmRemove", {}, "确认删除"), async () => {
      if (own.pending || disposed || removal !== own) return;
      own.pending = true; error.hidden = true;
      for (const node of own.layer.element.querySelectorAll("button")) node.disabled = true;
      try {
        if (await save(next)) {
          keyDrafts.delete(entry.id);
          if (!provider()) selectedId = config.items.find(model => model.id === config.default_model)?.provider || config.providers[0]?.id || "";
          own.layer.dispose();
          if (!needsRead) { notice = text("supplierRemoved", "供应商及其模型已删除。"); failed = false; }
          render(); container.querySelector('.model-config-item[aria-current="true"]')?.focus();
        } else { writeUiText(error, notice); error.hidden = false; }
      } finally {
        own.pending = false;
        if (removal === own) for (const node of own.layer.element.querySelectorAll("button")) node.disabled = false;
      }
    });
    apply.className = "danger-button"; own.layer.footer.append(cancel, apply); cancel.focus();
  }
  function closeSetup(refresh = true) {
    const own = setup; if (!own) return;
    setup = null; own.catalogController?.abort(); draft?.destroy(); draft = null;
    own.layer.dispose({ restoreFocus: !disposed });
    if (refresh && !disposed) void reload();
  }
  function openSetup(existing = false) {
    if (setup) return setup;
    const own = { loading: false, notice: "", catalogController: null, layer: null };
    own.layer = openLayer({ title: uiMessage(existing ? "modelSetup.addModels" : "modelSetup.addSupplier"),
      icon: "spark", width: "60rem", height: "85%", owner: container,
      onClosed() { if (setup === own) closeSetup(); },
    });
    own.layer.element.classList.add("model-setup-layer"); setup = own;
    return own;
  }
  async function readSuppliers(own) {
    own.catalogController?.abort(); const control = new AbortController(); own.catalogController = control;
    own.loading = true; own.notice = ""; renderTemplatesWindow(own);
    try {
      const response = await api.get("/models/providers", { signal: control.signal });
      if (disposed || setup !== own || draft || control.signal.aborted) return;
      setProviderCatalog(response.data.providers, response.data);
      own.notice = response.data.stale ? text("cachedSuppliers", "无法连接目录服务，正在使用上次成功获取的供应商目录。") : "";
    } catch (cause) { if (!control.signal.aborted) own.notice = text("supplierUnavailable", "供应商目录暂时不可用，可重试或手动添加自定义供应商。"); }
    finally { if (setup === own && !draft && own.catalogController === control) { own.loading = false; renderTemplatesWindow(own); } }
  }
  function add() {
    if (blocked()) return;
    void readSuppliers(openSetup());
  }
  function choose(preset, choice = preset.variants[0], existingProvider = null) {
    const own = openSetup(Boolean(existingProvider)); own.catalogController?.abort();
    draft?.destroy();
    draft = createModelSetupWizard({ preset, choice, provider: existingProvider, config, etag,
      onCommit(response) {
        if (disposed || setup !== own) return;
        config = response.data; etag = response.etag;
        void loadModels().catch(() => {}); render();
      },
      onDone(result) {
        if (disposed || setup !== own) return;
        selectedId = result.providerId; closeSetup(false); view = "detail";
        notice = text("modelsAdded", "已添加 {count} 个模型。", { count: result.added }) + (result.notice ? " " + result.notice : "");
        failed = false; render();
      },
      onCancel() { closeSetup(); },
    });
    clear(own.layer.body); clear(own.layer.footer); draft.mount(own.layer.body, own.layer.footer);
    own.layer.body.scrollTop = 0;
    own.layer.body.querySelector('input:not([type="checkbox"])')?.focus();
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
  function renderTemplatesWindow(own) {
    if (disposed || setup !== own || draft) return;
    const body = own.layer.body; clear(body); clear(own.layer.footer);
    body.append(element("h3", { text: text("chooseSupplier", "选择供应商") }), element("p", { className: "model-config-status", text: text("templateHelp", "选择服务并填写 API Key。自己的供应商 Key 无需登录墨斗账号。") }));
    const grid = element("div", { className: "supplier-templates" });
    if (own.loading || own.notice) body.append(element("p", { className: "model-config-status", attrs: { role: "status" }, text: own.loading ? text("readingSuppliers", "正在读取最新供应商和模型目录…") : own.notice }));
    for (const preset of providerPresets.filter((item) => !item.hidden)) grid.append(button(preset.labelKey ? text(preset.labelKey, preset.name) : preset.name, () => choose(preset)));
    body.append(grid);
    const refresh = button(text("refreshSuppliers", "刷新供应商目录"), () => void readSuppliers(own)); refresh.disabled = own.loading;
    own.layer.footer.append(refresh, button(text("cancel", "取消"), () => closeSetup()));
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
  function renderDetail(body) {
    const entry = provider(); if (!entry) return;
    body.append(element("header", { className: "supplier-detail-heading" }, [element("h3", { text: providerDisplayName(entry) }),
      entry.builtin ? element("span", { text: uiMessage("modelConfig.builtinTag") }) : button(text("advancedEditor", "编辑"), () => showAdvanced("provider", entry.id))]));
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
        choose(preset, presetVariant(preset.id, entry.template_variant), entry);
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
      const verify = button(text("verifyAgain", "验证"), () => void verifyModels([model])); actions.append(verify);
      verify.disabled = model.enabled === false;
      if (!model.builtin) actions.append(button(text("advancedEditor", "编辑"), () => showAdvanced("model", model.id)));
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
      if (model.enabled === false) { writeUiText(line, text("disabled", "已停用；启用后可用于会话。")); verify.disabled = true; }
      if (!model.builtin) {
        row.append(element("p", { className: "model-config-status", text: parameterSummary(model) }));
        const parameters = button(text("editParameters", "模型参数"), () => editParameters(model));
        parameters.className = "model-parameter-link"; row.append(parameters);
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
      const item = button(providerDisplayName(entry), () => { if (blocked()) return; selectedId = entry.id; view = "detail"; notice = ""; render(); });
      item.className = "model-config-item"; item.setAttribute("aria-current", entry.id === selectedId && view === "detail" ? "true" : "false");
      item.append(element("small", { text: text("modelCount", "{count} 个模型", { count: config.items.filter((model) => model.provider === entry.id).length }) }));
      const remove = button("", () => removeProvider(entry, remove)); remove.className = "supplier-remove";
      remove.setAttribute("aria-label", text("removeSupplierNamed", "删除供应商 {name}", { name: providerDisplayName(entry) }));
      remove.setAttribute("title", canRemoveProvider(entry) ? text("removeSupplier", "删除供应商") : text("supplierProtected", "内置或受保护的供应商不可删除。"));
      remove.disabled = !canRemoveProvider(entry);
      remove.append(element("span", { className: "icon", attrs: { "data-icon": "close", "aria-hidden": "true" } }));
      list.append(element("div", { className: "supplier-list-row", attrs: { "data-provider-id": entry.id } }, [item, remove]));
    }
    mountIcons(list);
    const body = element("div", { className: "supplier-body" });
    if (view !== "list") body.append(button(view === "advanced" ? text("backSuppliers", "返回供应商") : text("back", "返回"), () => {
      void back();
    }));
    if (view === "parameters") renderParameterEdit(body);
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
      const target = field || (view === "list" ? [...list.querySelectorAll("button")].find((node) => node.textContent.startsWith(provider() ? providerDisplayName(provider()) : "")) : heading);
      if (target) { const details = target.closest("details"); if (details) details.open = true; if (target === heading) target.tabIndex = -1; target.focus(); if (field && selection) field.setSelectionRange?.(...selection); }
    }
  }
  const unsubscribe = subscribeLocale(() => { if (!advanced && !busy) render(); if (setup && !draft) renderTemplatesWindow(setup); });
  return Object.freeze({ ensureLoaded: () => config ? Promise.resolve() : reload(), reload,
    destroy() { disposed = true; controller.abort(); verificationController?.abort(); removal?.layer.dispose(); closeSetup(false); keyDrafts.clear(); advanced?.destroy(); unsubscribe(); } });
}
