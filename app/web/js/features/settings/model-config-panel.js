import { api } from "../../api/client.js";
import { loadModels } from "../../state/catalogs.js";
import { clear, element, errorMessage, isImeKey, toast } from "../../utils/dom.js";
import { subscribeLocale, t } from "../../i18n.js";

const protocols = [
  ["openai-chat-completions", "Chat Completions", "chat_completions"],
  ["openai-responses", "Responses", "responses"],
  ["anthropic-messages", "Anthropic Messages", "anthropic_messages"],
];
const capabilities = [
  ["text-input", "文本输入"], ["tool-result-input", "工具结果"],
  ["text-output", "文本输出"], ["json-output", "JSON 输出"],
  ["tool-call-output", "工具调用"], ["reasoning-output", "思考输出"],
  ["streaming", "流式输出"], ["reasoning-control", "思考强度"],
  ["parallel-tool-calls", "并行工具"], ["max-completion-tokens", "max_completion_tokens"],
  ["developer-role", "Developer 角色"], ["media-input", "媒体输入"],
];
const efforts = ["none", "minimal", "low", "medium", "high", "xhigh", "max"];
const attachments = [["image", "图片"], ["audio", "音频"], ["file", "文件"]];
const clone = (value) => structuredClone(value);

// Translate by stable form names and option values, never by display copy.
// The latter changes with the locale and makes new fields easy to miss.
const fieldKeys = Object.freeze({
  id: "modelConfig.id", name: "modelConfig.name",
  chat_completions: "modelConfig.chatUrl",
  responses: "modelConfig.responsesUrl",
  anthropic_messages: "modelConfig.anthropicUrl",
  secret_ref: "modelConfig.secretRef", timeout_ms: "modelConfig.timeout",
  verify_peer: "modelConfig.verifyTls", provider: "modelConfig.provider",
  wire_model: "modelConfig.wireModel",
  default_protocol: "modelConfig.defaultProtocol",
  default_reasoning_effort: "modelConfig.defaultEffort",
  billable: "modelConfig.billable", protocol: "modelConfig.protocols",
  capability: "modelConfig.capabilities", effort: "modelConfig.efforts",
  attachment: "modelConfig.attachments", window_mode: "modelConfig.windowMode",
  context_tokens: "modelConfig.contextTokens",
  max_input_tokens: "modelConfig.maxInputTokens",
  max_output_tokens: "modelConfig.maxOutputTokens",
  output_reserve_tokens: "modelConfig.outputReserveTokens",
  summary_tokens: "modelConfig.summaryTokens",
});
const optionKeys = Object.freeze({
  "shared-context": "modelConfig.sharedContext",
  "split-input-output": "modelConfig.splitWindow",
  "text-input": "modelConfig.textInput",
  "tool-result-input": "modelConfig.toolResultInput",
  "text-output": "modelConfig.textOutput",
  "json-output": "modelConfig.jsonOutput",
  "tool-call-output": "modelConfig.toolCallOutput",
  "reasoning-output": "modelConfig.reasoningOutput",
  "reasoning-control": "modelConfig.reasoningControl",
  streaming: "modelConfig.streaming",
  "parallel-tool-calls": "modelConfig.parallelTools",
  "max-completion-tokens": "modelConfig.maxCompletionTokens",
  "developer-role": "modelConfig.developerRole",
  "media-input": "modelConfig.mediaInput",
  image: "modelConfig.image", audio: "modelConfig.audio",
  file: "modelConfig.file",
  none: "settings.reasoningNone", minimal: "settings.reasoningMinimal",
  low: "settings.reasoningLow", medium: "settings.reasoningMedium",
  high: "settings.reasoningHigh", xhigh: "settings.reasoningXhigh",
  max: "settings.reasoningMax",
});

function copy(tag, key, fallback, params = {}, options = {}) {
  return element(tag, { ...options, text: t(key, params, fallback), attrs: {
    ...options.attrs, "data-model-copy-key": key,
    "data-model-copy-params": JSON.stringify(params),
    "data-model-copy-fallback": fallback,
  } });
}

function translateCopy(container) {
  for (const node of container.querySelectorAll("[data-model-copy-key]"))
    node.textContent = t(node.dataset.modelCopyKey,
      JSON.parse(node.dataset.modelCopyParams), node.dataset.modelCopyFallback);
  for (const node of container.querySelectorAll("[data-model-attr-key]"))
    node.setAttribute(node.dataset.modelAttrName,
      t(node.dataset.modelAttrKey, {}, node.dataset.modelAttrFallback));
}

function translatedAttribute(node, name, key, fallback) {
  node.setAttribute(name, t(key, {}, fallback));
  node.dataset.modelAttrName = name;
  node.dataset.modelAttrKey = key;
  node.dataset.modelAttrFallback = fallback;
  return node;
}

function input(label, name, value, options = {}) {
  const field = element("label", { className: "model-field" }, [
    fieldKeys[name] ? copy("span", fieldKeys[name], label) : element("span", { text: label }),
  ]);
  const control = element(options.kind === "select" ? "select" : "input", {
    attrs: { name, type: options.type || "text", required: options.required ? "" : null,
      min: options.min, max: options.max, maxLength: options.maxLength,
      pattern: options.pattern,
      readOnly: options.readOnly ? "" : null, placeholder: options.placeholder },
  });
  if (options.kind === "select") {
    for (const [key, text] of options.choices ?? []) {
      const optionKey = name === "default_reasoning_effort" || name === "window_mode"
        ? optionKeys[key] : null;
      control.append(optionKey
        ? copy("option", optionKey, text, {}, { attrs: { value: key } })
        : element("option", { text, attrs: { value: key } }));
    }
  }
  if (options.type === "checkbox") control.checked = Boolean(value);
  else control.value = value ?? "";
  field.append(control);
  return field;
}

function checks(title, name, values, choices) {
  const group = element("fieldset", { className: "model-checks" });
  group.append(fieldKeys[name] ? copy("legend", fieldKeys[name], title)
    : element("legend", { text: title }));
  for (const [key, label] of choices) {
    const line = element("label");
    const box = element("input", { attrs: { type: "checkbox", name, value: key } });
    box.checked = values?.includes(key) ?? false;
    line.append(box, optionKeys[key] ? copy("span", optionKeys[key], label)
      : element("span", { text: label }));
    group.append(line);
  }
  return group;
}

function selected(form, name) {
  return [...form.querySelectorAll(`input[name="${name}"]:checked`)]
    .map((control) => control.value);
}

function providerDefault() {
  return { id: "", name: "", builtin: false, editable: true,
    removable: true, verify_peer: true, timeout_ms: 120000,
    endpoints: { chat_completions: "" }, credential: { secret_ref: "" } };
}

function modelDefault(provider) {
  const protocol = protocols.find(([, , endpoint]) => provider?.endpoints?.[endpoint])?.[0]
    || "openai-chat-completions";
  return { id: "", name: "", provider: provider?.id || "", wire_model: "",
    builtin: false, free: false, editable: true, removable: true,
    protocols: [protocol], default_protocol: protocol,
    capabilities: ["text-input", "tool-result-input", "text-output",
      "tool-call-output", "streaming"],
    window: { mode: "shared-context", context_tokens: 128000,
      max_input_tokens: 112000, max_output_tokens: 16000,
      output_reserve_tokens: 8000, summary_tokens: 4000 },
    reasoning_efforts: ["none", "low", "medium", "high"],
    default_reasoning_effort: "medium", attachments: [] };
}

export function createModelConfigPanel(container) {
  let config = null;
  let etag = "";
  let kind = "model";
  let selectedId = "";
  let busy = false;
  let writing = false;
  let dirty = false;
  let editVersion = 0;
  const rebaseForm = new WeakMap();

  function renderReadState(cause = null, keepForm = false) {
    if (keepForm) container.querySelector("[data-model-read-status]")?.remove();
    else clear(container);
    const notice = element("div", { className: cause ? "resource-error" : "empty-state",
      attrs: { role: cause ? "alert" : "status", "data-model-read-status": "" } }, [cause
      ? element("p", { text: errorMessage(cause) })
      : copy("p", "resource.loading", "正在读取…")]);
    if (cause) {
      const retry = copy("button", "resource.retryLoad", "重新读取", {},
        { className: "secondary-button", attrs: { type: "button" } });
      retry.addEventListener("click", async () => {
        if (busy || !allowChange()) return;
        const applied = await load();
        if (applied && container.isConnected && container.getClientRects().length &&
            globalThis.document.activeElement === globalThis.document.body)
          container.querySelector("button")?.focus({ preventScroll: true });
      });
      notice.append(retry);
    }
    container.prepend(notice);
  }

  async function load(preferredKind = kind, preferredId = selectedId) {
    if (busy) return false;
    const formAtRead = container.querySelector(".model-config-form");
    const versionAtRead = editVersion;
    const focusAtRead = globalThis.document.activeElement;
    busy = true;
    container.querySelector("[data-model-read-status]")?.remove();
    container.setAttribute("aria-busy", "true");
    if (!config) renderReadState();
    try {
      const response = await api.get("/models/config");
      // A refresh is not permission to discard later edits or a newly selected
      // editor. Retain its nodes, values, selection and original config/ETag.
      if (versionAtRead !== editVersion ||
          formAtRead !== container.querySelector(".model-config-form") ||
          (formAtRead?.contains(globalThis.document.activeElement) &&
            globalThis.document.activeElement !== focusAtRead)) {
        container.prepend(copy("p", "modelConfig.refreshKept",
          "刷新期间的新输入已保留；保存或放弃修改后可再次刷新。", {}, {
            className: "model-config-status", attrs: {
              role: "status", "data-model-read-status": "",
            },
          }));
        return false;
      }
      config = response.data;
      etag = response.etag;
      kind = preferredKind;
      selectedId = preferredId;
      if (selectedId && !collection().some((item) => item.id === selectedId))
        selectedId = "";
      if (!selectedId) selectedId = kind === "model" ? config.default_model :
        config.providers[0]?.id ?? "";
      dirty = false;
      render();
      return true;
    } catch (cause) {
      renderReadState(cause, Boolean(config));
      return false;
    } finally {
      busy = false;
      container.removeAttribute("aria-busy");
    }
  }

  function collection() { return kind === "model" ? config.items : config.providers; }
  function current() { return collection().find((item) => item.id === selectedId) ?? null; }
  function allowChange() {
    if (!dirty) return true;
    toast(t("modelConfig.unsaved", {}, "当前表单有未保存的修改，请先保存或放弃。"), "error");
    return false;
  }

  async function transact(next, messageKey, messageFallback,
    focusKind = kind, focusId = selectedId, editableWhileSaving = false) {
    if (busy || !config || config.runtime_override) return;
    const form = container.querySelector(".model-config-form");
    const versionAtSave = editVersion;
    const focusAtSave = globalThis.document.activeElement;
    // Keep fields usable during Save, but serialize resource actions. Identity
    // stays fixed until creation is acknowledged so a later Save updates it.
    const locked = [...container.querySelectorAll(editableWhileSaving
      ? "button" : "button, input, select")].map((node) => ({
        node, disabled: node.disabled,
      }));
    const identity = form?.querySelector('input[name="id"]');
    const identityReadOnly = identity?.readOnly;
    for (const { node } of locked) node.disabled = true;
    if (identity) identity.readOnly = true;
    writing = true;
    busy = true;
    container.setAttribute("aria-busy", "true");
    let committed = false;
    try {
      const patch = { default_model: next.default_model,
        providers: next.providers, items: next.items };
      const body = { schema_version: 1, patch };
      await api.post("/settings/models/preview", body);
      const saved = await api.put("/settings/models", body, { ifMatch: etag });
      committed = true;
      // The acknowledged patch and revision are already a valid baseline even
      // if its follow-up read fails. Do not turn that failure into a failed Save.
      config = next;
      etag = saved.etag;
      let readError = null;
      try {
        const response = await api.get("/models/config");
        // Rebase on this acknowledged write, not another client's newer edit.
        // A later Save must still hit the server's revision precondition.
        if (response.etag === saved.etag) config = response.data;
      } catch (cause) { readError = cause; }
      await loadModels();
      const keepForm = editableWhileSaving && versionAtSave !== editVersion &&
        form === container.querySelector(".model-config-form");
      const focused = globalThis.document.activeElement;
      const keepFocus = form?.contains(focused) && focused.matches("input, select");
      const selection = keepFocus && typeof focused.selectionStart === "number"
        ? [focused.selectionStart, focused.selectionEnd, focused.selectionDirection] : null;
      kind = focusKind;
      selectedId = focusId;
      if (!collection().some((item) => item.id === selectedId))
        selectedId = kind === "model" ? config.default_model : config.providers[0]?.id ?? "";
      dirty = keepForm;
      if (keepForm) rebaseForm.get(form)?.();
      render(keepForm ? form : null);
      if (keepFocus && container.isConnected && container.getClientRects().length) {
        const target = keepForm ? focused :
          [...container.querySelectorAll(".model-config-form input, .model-config-form select")]
            .find((node) => node.name === focused.name && node.type === focused.type &&
              (node.type !== "checkbox" || node.value === focused.value));
        target?.focus({ preventScroll: true });
        if (selection) target?.setSelectionRange(...selection);
      } else if (container.isConnected && container.getClientRects().length &&
          globalThis.document.activeElement === globalThis.document.body) {
        container.querySelector('.model-config-item[aria-current="true"]')?.focus();
      }
      if (keepForm) container.prepend(copy("p", "modelConfig.savedKept",
        "本次保存已完成；后续输入仍未保存。", {}, {
          className: "model-config-status", attrs: { role: "status" },
        }));
      if (readError) renderReadState(readError, true);
      toast(t(messageKey, {}, messageFallback));
    } catch (cause) {
      toast(cause?.status === 412 ? t("modelConfig.conflictKept", {},
        "配置已在其他位置更新，当前输入已保留。放弃修改并刷新后可重新编辑。")
        : errorMessage(cause), "error");
    } finally {
      writing = false;
      busy = false;
      for (const { node, disabled } of locked) node.disabled = disabled;
      if (identity && !committed) identity.readOnly = identityReadOnly;
      container.removeAttribute("aria-busy");
      if (!committed && focusAtSave?.isConnected && container.getClientRects().length &&
          globalThis.document.activeElement === globalThis.document.body)
        focusAtSave.focus({ preventScroll: true });
    }
  }

  function renderProvider(form, item) {
    const builtin = item?.builtin;
    form.append(item?.name ? element("h3", { text: item.name })
      : copy("h3", "modelConfig.newProvider", "新增 Provider"),
    builtin ? copy("p", "modelConfig.builtinProviderNote",
      "内置 Provider 由 mdo 提供，配置不可更改。")
      : copy("p", "modelConfig.secretNote",
        "使用凭据引用，不在页面或配置中保存 API Key 明文。"));
    if (builtin) return;
    form.append(element("div", { className: "model-form-grid" }, [
      input("标识", "id", item.id, { required: true, readOnly: Boolean(selectedId), maxLength: 128,
        pattern: "[A-Za-z0-9_-][A-Za-z0-9._-]*" }),
      input("名称", "name", item.name, { required: true, maxLength: 256 }),
      input("Chat Completions URL", "chat_completions", item.endpoints?.chat_completions || "", { maxLength: 2048 }),
      input("Responses URL", "responses", item.endpoints?.responses || "", { maxLength: 2048 }),
      input("Anthropic Messages URL", "anthropic_messages", item.endpoints?.anthropic_messages || "", { maxLength: 2048 }),
      input("凭据引用", "secret_ref", item.credential?.secret_ref || "",
        { placeholder: "env:MY_MODEL_API_KEY", maxLength: 2048 }),
      input("超时（毫秒）", "timeout_ms", item.timeout_ms,
        { type: "number", required: true, min: 1, max: 600000 }),
      input("验证 TLS 证书", "verify_peer", item.verify_peer, { type: "checkbox" }),
    ]));
  }

  function renderModel(form, item) {
    const builtin = item?.builtin;
    form.append(item?.name ? element("h3", { text: item.name })
      : copy("h3", "modelConfig.newModel", "新增模型"),
    builtin ? copy("p", "modelConfig.builtinModelNote",
      "ornith-1.5-35b 是内置免费模型，参数不可编辑。")
      : copy("p", "modelConfig.providerNote",
        "模型引用 Provider；协议必须有对应的 Provider URL。"));
    if (builtin) return;
    form.append(element("div", { className: "model-form-grid" }, [
      input("标识", "id", item.id, { required: true, readOnly: Boolean(selectedId), maxLength: 128,
        pattern: "[A-Za-z0-9_-][A-Za-z0-9._-]*" }),
      input("名称", "name", item.name, { required: true, maxLength: 256 }),
      input("Provider", "provider", item.provider, { kind: "select", choices:
        config.providers.map((provider) => [provider.id, provider.name]) }),
      input("API 模型名", "wire_model", item.wire_model, { required: true, maxLength: 256 }),
      input("默认协议", "default_protocol", item.default_protocol,
        { kind: "select", choices: protocols.map(([key, label]) => [key, label]) }),
      input("默认思考强度", "default_reasoning_effort", item.default_reasoning_effort,
        { kind: "select", choices: efforts.map((value) => [value, value]) }),
      input("计费模型", "billable", !item.free, { type: "checkbox" }),
    ]));
    form.append(checks("支持的协议", "protocol", item.protocols, protocols));
    const advanced = element("details", { className: "model-advanced" }, [
      copy("summary", "modelConfig.advanced", "能力与上下文参数"),
      checks("能力", "capability", item.capabilities, capabilities),
      checks("思考强度", "effort", item.reasoning_efforts,
        efforts.map((value) => [value, value])),
      checks("附件", "attachment", item.attachments, attachments),
      element("div", { className: "model-form-grid" }, [
        input("窗口模式", "window_mode", item.window.mode,
          { kind: "select", choices: [["shared-context", "共享上下文"],
            ["split-input-output", "输入/输出分离"]] }),
        ...[["context_tokens", "上下文 token"], ["max_input_tokens", "最大输入 token"],
          ["max_output_tokens", "最大输出 token"], ["output_reserve_tokens", "输出预留 token"],
          ["summary_tokens", "摘要 token"]].map(([key, label]) =>
          input(label, key, item.window[key], { type: "number", required: true,
            min: key === "output_reserve_tokens" || key === "summary_tokens" ? 0 : 1 })),
      ]),
    ]);
    form.append(advanced);
    const providerSelect = form.elements.provider;
    const defaultProtocol = form.elements.default_protocol;
    const syncProtocols = () => {
      const provider = config.providers.find((entry) => entry.id === providerSelect.value);
      for (const [key, , endpoint] of protocols) {
        const allowed = Boolean(provider?.endpoints?.[endpoint]);
        const checkbox = form.querySelector(`input[name="protocol"][value="${key}"]`);
        checkbox.disabled = !allowed;
        if (!allowed) checkbox.checked = false;
        defaultProtocol.querySelector(`option[value="${key}"]`).disabled = !allowed;
      }
      const enabled = [...form.querySelectorAll('input[name="protocol"]:not(:disabled)')];
      if (enabled.length && !enabled.some((checkbox) => checkbox.checked))
        enabled[0].checked = true;
      if (defaultProtocol.selectedOptions[0]?.disabled)
        defaultProtocol.value = enabled.find((checkbox) => checkbox.checked)?.value || "";
    };
    providerSelect.addEventListener("change", syncProtocols);
    syncProtocols();
  }

  function readProvider(form, old) {
    const data = new FormData(form);
    const endpoints = Object.fromEntries(["chat_completions", "responses", "anthropic_messages"]
      .map((name) => [name, String(data.get(name) || "").trim()])
      .filter(([, value]) => value));
    const secret = String(data.get("secret_ref") || "").trim();
    return { ...old, id: String(data.get("id") || "").trim(),
      name: String(data.get("name") || "").trim(), endpoints,
      credential: secret ? { secret_ref: secret } : undefined,
      timeout_ms: Number(data.get("timeout_ms")),
      verify_peer: data.has("verify_peer") };
  }

  function readModel(form, old) {
    const data = new FormData(form);
    const window = { mode: data.get("window_mode") };
    for (const key of ["context_tokens", "max_input_tokens", "max_output_tokens",
      "output_reserve_tokens", "summary_tokens"]) window[key] = Number(data.get(key));
    return { ...old, id: String(data.get("id") || "").trim(),
      name: String(data.get("name") || "").trim(),
      provider: data.get("provider"), wire_model: String(data.get("wire_model") || "").trim(),
      protocols: selected(form, "protocol"), default_protocol: data.get("default_protocol"),
      capabilities: selected(form, "capability"), window,
      reasoning_efforts: selected(form, "effort"),
      default_reasoning_effort: data.get("default_reasoning_effort"),
      attachments: selected(form, "attachment"), free: !data.has("billable") };
  }

  function render(preservedForm = null) {
    if (!config) return;
    clear(container);
    const controls = element("div", { className: "model-config-controls" });
    for (const [value, key, label] of [
      ["model", "modelConfig.models", "模型"],
      ["provider", "modelConfig.providers", "Provider"],
    ]) {
      const button = copy("button", key, label, {}, { className: "secondary-button",
        attrs: { type: "button", "aria-pressed": kind === value } });
      button.addEventListener("click", () => {
        if (!allowChange()) return;
        kind = value; selectedId = collection()[0]?.id ?? ""; render();
        container.querySelector('.model-config-controls [aria-pressed="true"]')?.focus();
      });
      controls.append(button);
    }
    const refresh = copy("button", "modelConfig.refresh", "刷新", {},
      { className: "secondary-button", attrs: { type: "button" } });
    refresh.addEventListener("click", async () => {
      if (!allowChange()) return;
      const applied = await load();
      // A successful reload replaces the clicked button. Leave later user
      // focus alone if they moved to another part of the app meanwhile.
      if (applied && container.isConnected && container.getClientRects().length &&
          globalThis.document.activeElement === globalThis.document.body)
        container.querySelector('[data-model-copy-key="modelConfig.refresh"]')?.focus();
    });
    const add = copy("button", kind === "model" ? "modelConfig.newModel" :
      "modelConfig.newProvider", kind === "model" ? "新增模型" : "新增 Provider",
    {}, { className: "primary-button", attrs: { type: "button" } });
    add.disabled = config.runtime_override;
    add.addEventListener("click", () => {
      if (!allowChange()) return;
      selectedId = ""; render();
      container.querySelector('input[name="id"]')?.focus();
    });
    controls.append(refresh, add);
    container.append(copy("p", "modelConfig.status",
      `当前默认：${config.default_model} · 配置 revision ${etag.replace(/\D/g, "")}`,
      { model: config.default_model, revision: etag.replace(/\D/g, "") },
      { className: "model-config-status" }), controls);
    if (config.runtime_override) container.append(copy("p", "modelConfig.runtimeOverride",
      "当前配置含运行时覆盖；请移除启动覆盖后再用页面编辑模型。",
      {}, { className: "resource-error" }));
    const layout = element("div", { className: "model-config-layout" });
    const list = translatedAttribute(element("div", { className: "model-config-list" }),
      "aria-label", kind === "model" ? "modelConfig.modelList" : "modelConfig.providerList",
      kind === "model" ? "模型列表" : "Provider 列表");
    for (const item of collection()) {
      const button = element("button", { className: "model-config-item",
        attrs: { type: "button", "aria-current": selectedId === item.id ? "true" : "false" } }, [
        element("strong", { text: item.name || item.id }),
        element("small", {}, [item.id,
          item.builtin ? copy("span", "modelConfig.builtinTag", " · 内置") : null,
          kind === "model" && item.id === config.default_model
            ? copy("span", "modelConfig.defaultTag", " · 默认") : null]),
      ]);
      button.addEventListener("click", () => {
        if (!allowChange()) return;
        selectedId = item.id; render();
        container.querySelector('.model-config-item[aria-current="true"]')?.focus();
      });
      list.append(button);
    }
    if (preservedForm) {
      layout.append(list, preservedForm);
      container.append(layout);
      return;
    }
    let source = current();
    let item = clone(source ?? (kind === "model" ?
      modelDefault(config.providers.find((provider) => !provider.builtin) ||
        config.providers[0]) : providerDefault()));
    const form = element("form", { className: "model-config-form" });
    if (kind === "model") renderModel(form, item);
    else renderProvider(form, item);
    rebaseForm.set(form, () => {
      source = current();
      item = clone(source ?? item);
      const id = form.querySelector('input[name="id"]');
      if (id) id.readOnly = Boolean(source);
      const title = form.querySelector("h3");
      if (source?.name && title) {
        title.textContent = source.name;
        for (const name of [...title.getAttributeNames()])
          if (name.startsWith("data-model-copy-")) title.removeAttribute(name);
      }
    });
    if (kind === "model" && source?.builtin) form.append(copy("p",
      "modelConfig.builtinModelDetail",
      `${source.id} · 上下文 ${source.window.context_tokens} tokens · ${source.default_protocol}`,
      { id: source.id, tokens: source.window.context_tokens,
        protocol: source.default_protocol }));
    if (source?.builtin && kind === "provider") form.append(copy("p",
      "modelConfig.builtinProviderDetail", `${source.id} · 内置接口和凭据引用由程序管理`,
      { id: source.id }));
    const actions = element("div", { className: "model-config-actions" });
    if (!source?.builtin && !config.runtime_override) {
      const save = copy("button", "modelConfig.save", "保存", {},
        { className: "primary-button", attrs: { type: "submit" } });
      save.disabled = Boolean(source);
      const discard = copy("button", "modelConfig.discard", "放弃修改", {},
        { className: "secondary-button", attrs: { type: "button" } });
      discard.hidden = true;
      discard.addEventListener("click", () => {
        dirty = false;
        render();
        // Replacing the form removes the activated button. Return keyboard
        // focus to the saved resource, or to Add for an unsaved new item.
        const target = source
          ? container.querySelector('.model-config-item[aria-current="true"]')
          : container.querySelector('.model-config-controls .primary-button');
        target?.focus();
      });
      form.addEventListener("input", () => { editVersion += 1; dirty = true; discard.hidden = false; save.disabled = writing; });
      form.addEventListener("change", () => { editVersion += 1; dirty = true; discard.hidden = false; save.disabled = writing; });
      form.addEventListener("submit", (event) => {
        event.preventDefault();
        if (busy) return;
        if (!form.reportValidity()) return;
        const next = clone(config);
        const value = kind === "model" ? readModel(form, item) : readProvider(form, item);
        if (kind === "provider" && !Object.keys(value.endpoints).length) {
          toast(t("modelConfig.endpointRequired", {},
            "至少填写一个协议接口 URL。"), "error");
          return;
        }
        if (kind === "model" && (!value.protocols.length ||
            !value.protocols.includes(value.default_protocol) ||
            !value.reasoning_efforts.includes(value.default_reasoning_effort))) {
          toast(t("modelConfig.selectionRequired", {},
            "选择至少一个协议和思考强度，并确认默认项属于所选范围。"), "error");
          return;
        }
        const key = kind === "model" ? "items" : "providers";
        if (source) next[key] = next[key].map((entry) => entry.id === source.id ? value : entry);
        else next[key].push(value);
        void transact(next, source ? "modelConfig.updated" : "modelConfig.added",
          source ? "配置已更新" : "配置已添加", kind, value.id, true);
      });
      actions.append(save, discard);
      if (source?.removable) {
        const used = kind === "provider" && config.items.some((model) => model.provider === source.id);
        const isDefault = kind === "model" && source.id === config.default_model;
        const remove = copy("button", "modelConfig.remove", "删除", {},
          { className: "danger-link", attrs: { type: "button" } });
        if (used || isDefault) translatedAttribute(remove, "title",
          used ? "modelConfig.providerInUse" : "modelConfig.defaultInUse",
          used ? "先将引用它的模型移到其他 Provider" : "先设置其他默认模型");
        remove.disabled = used || isDefault;
        const confirm = element("div", { className: "model-delete-confirm", attrs: { role: "group" } }, [
          copy("span", "modelConfig.removePrompt",
            `删除“${source.name || source.id}”？使用它的已有会话可能无法继续。`,
            { name: source.name || source.id }),
        ]);
        translatedAttribute(confirm, "aria-label", "modelConfig.removeGroup", "确认删除");
        const cancel = copy("button", "modelConfig.cancel", "取消", {},
          { className: "secondary-button", attrs: { type: "button" } });
        const apply = copy("button", "modelConfig.confirmRemove", "确认删除", {},
          { className: "danger-button", attrs: { type: "button" } });
        const dismiss = () => { confirm.hidden = true; remove.focus(); };
        cancel.addEventListener("click", dismiss);
        confirm.addEventListener("keydown", (event) => {
          if (event.key !== "Escape" || isImeKey(event)) return;
          event.preventDefault();
          event.stopPropagation();
          dismiss();
        });
        apply.addEventListener("click", () => {
          const next = clone(config);
          next[kind === "model" ? "items" : "providers"] = collection().filter((entry) => entry.id !== source.id);
          confirm.hidden = true;
          void transact(next, "modelConfig.removed", "配置已删除", kind, "");
        });
        confirm.append(cancel, apply);
        confirm.hidden = true;
        remove.addEventListener("click", () => { if (allowChange()) { confirm.hidden = false; cancel.focus(); } });
        actions.append(remove, confirm);
      }
    }
    if (kind === "model" && source && source.id !== config.default_model && !config.runtime_override) {
      const setDefault = copy("button", "modelConfig.setDefault", "设为默认模型", {},
        { className: "secondary-button", attrs: { type: "button" } });
      setDefault.addEventListener("click", () => {
        if (!allowChange()) return;
        const next = clone(config); next.default_model = source.id;
        void transact(next, "modelConfig.defaultUpdated", "默认模型已更新");
      });
      actions.append(setDefault);
    }
    form.append(actions);
    layout.append(list, form);
    container.append(layout);
  }

  const unsubscribe = subscribeLocale(() => translateCopy(container));
  return Object.freeze({
    ensureLoaded: () => config ? Promise.resolve() : load(),
    reload: () => load(),
    destroy: unsubscribe,
  });
}
