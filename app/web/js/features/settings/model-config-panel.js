import { api } from "../../api/client.js";
import { loadModels } from "../../state/catalogs.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";

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

function input(label, name, value, options = {}) {
  const field = element("label", { className: "model-field", text: label });
  const control = element(options.kind === "select" ? "select" : "input", {
    attrs: { name, type: options.type || "text", required: options.required ? "" : null,
      min: options.min, max: options.max, maxLength: options.maxLength,
      pattern: options.pattern,
      readOnly: options.readOnly ? "" : null, placeholder: options.placeholder },
  });
  if (options.kind === "select") {
    for (const [key, text] of options.choices ?? [])
      control.append(element("option", { text, attrs: { value: key } }));
  }
  if (options.type === "checkbox") control.checked = Boolean(value);
  else control.value = value ?? "";
  field.append(control);
  return field;
}

function checks(title, name, values, choices) {
  const group = element("fieldset", { className: "model-checks" });
  group.append(element("legend", { text: title }));
  for (const [key, label] of choices) {
    const line = element("label", { text: label });
    const box = element("input", { attrs: { type: "checkbox", name, value: key } });
    box.checked = values?.includes(key) ?? false;
    line.prepend(box);
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
  let document = null;
  let etag = "";
  let kind = "model";
  let selectedId = "";
  let busy = false;
  let dirty = false;

  async function load(preferredKind = kind, preferredId = selectedId) {
    if (busy) return;
    busy = true;
    try {
      const response = await api.get("/models/config");
      document = response.data;
      etag = response.etag;
      kind = preferredKind;
      selectedId = preferredId;
      if (selectedId && !collection().some((item) => item.id === selectedId))
        selectedId = "";
      if (!selectedId) selectedId = kind === "model" ? document.default_model :
        document.providers[0]?.id ?? "";
      dirty = false;
      render();
    } catch (cause) {
      clear(container);
      container.append(element("p", { className: "resource-error",
        text: errorMessage(cause) }));
    } finally { busy = false; }
  }

  function collection() { return kind === "model" ? document.items : document.providers; }
  function current() { return collection().find((item) => item.id === selectedId) ?? null; }
  function allowChange() {
    if (!dirty) return true;
    toast("当前表单有未保存的修改，请先保存或放弃。", "error");
    return false;
  }

  async function transact(next, message, focusKind = kind, focusId = selectedId) {
    if (busy || !document || document.runtime_override) return;
    busy = true;
    try {
      const patch = { default_model: next.default_model,
        providers: next.providers, items: next.items };
      const body = { schema_version: 1, patch };
      await api.post("/settings/models/preview", body);
      await api.put("/settings/models", body, { ifMatch: etag });
      await loadModels();
      busy = false;
      await load(focusKind, focusId);
      container.querySelector('.model-config-item[aria-current="true"]')?.focus();
      toast(message);
    } catch (cause) {
      busy = false;
      toast(errorMessage(cause), "error");
      if (cause?.status === 412) {
        dirty = false;
        await load();
      }
    }
  }

  function renderProvider(form, item) {
    const builtin = item?.builtin;
    form.append(element("h3", { text: item?.name || "新增 Provider" }),
      element("p", { text: builtin ? "内置 Provider 由 mdo 提供，配置不可更改。" :
        "使用凭据引用，不在页面或配置中保存 API Key 明文。" }));
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
    form.append(element("h3", { text: item?.name || "新增模型" }),
      element("p", { text: builtin ? "Ling 3.0 Tiny 是内置免费模型，参数不可编辑。" :
        "模型引用 Provider；协议必须有对应的 Provider URL。" }));
    if (builtin) return;
    form.append(element("div", { className: "model-form-grid" }, [
      input("标识", "id", item.id, { required: true, readOnly: Boolean(selectedId), maxLength: 128,
        pattern: "[A-Za-z0-9_-][A-Za-z0-9._-]*" }),
      input("名称", "name", item.name, { required: true, maxLength: 256 }),
      input("Provider", "provider", item.provider, { kind: "select", choices:
        document.providers.map((provider) => [provider.id, provider.name]) }),
      input("API 模型名", "wire_model", item.wire_model, { required: true, maxLength: 256 }),
      input("默认协议", "default_protocol", item.default_protocol,
        { kind: "select", choices: protocols.map(([key, label]) => [key, label]) }),
      input("默认思考强度", "default_reasoning_effort", item.default_reasoning_effort,
        { kind: "select", choices: efforts.map((value) => [value, value]) }),
      input("计费模型", "billable", !item.free, { type: "checkbox" }),
    ]));
    form.append(checks("支持的协议", "protocol", item.protocols, protocols));
    const advanced = element("details", { className: "model-advanced" }, [
      element("summary", { text: "能力与上下文参数" }),
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
      const provider = document.providers.find((entry) => entry.id === providerSelect.value);
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

  function render() {
    if (!document) return;
    clear(container);
    const controls = element("div", { className: "model-config-controls" });
    for (const [value, label] of [["model", "模型"], ["provider", "Provider"]]) {
      const button = element("button", { className: "secondary-button", text: label,
        attrs: { type: "button", "aria-pressed": kind === value } });
      button.addEventListener("click", () => {
        if (!allowChange()) return;
        kind = value; selectedId = collection()[0]?.id ?? ""; render();
      });
      controls.append(button);
    }
    const refresh = element("button", { className: "secondary-button", text: "刷新",
      attrs: { type: "button" } });
    refresh.addEventListener("click", () => {
      if (allowChange()) void load();
    });
    const add = element("button", { className: "primary-button",
      text: kind === "model" ? "新增模型" : "新增 Provider",
      attrs: { type: "button" } });
    add.disabled = document.runtime_override;
    add.addEventListener("click", () => {
      if (!allowChange()) return;
      selectedId = ""; render();
      container.querySelector('input[name="id"]')?.focus();
    });
    controls.append(refresh, add);
    container.append(element("p", { className: "model-config-status", text:
      `当前默认：${document.default_model} · 配置 revision ${etag.replace(/\D/g, "")}` }), controls);
    if (document.runtime_override) container.append(element("p", { className: "resource-error",
      text: "当前配置含运行时覆盖；请移除启动覆盖后再用页面编辑模型。" }));
    const layout = element("div", { className: "model-config-layout" });
    const list = element("div", { className: "model-config-list", attrs: { "aria-label": kind === "model" ? "模型列表" : "Provider 列表" } });
    for (const item of collection()) {
      const button = element("button", { className: "model-config-item",
        attrs: { type: "button", "aria-current": selectedId === item.id ? "true" : "false" } }, [
        element("strong", { text: item.name || item.id }),
        element("small", { text: `${item.id}${item.builtin ? " · 内置" : ""}${kind === "model" && item.id === document.default_model ? " · 默认" : ""}` }),
      ]);
      button.addEventListener("click", () => {
        if (!allowChange()) return;
        selectedId = item.id; render();
      });
      list.append(button);
    }
    const source = current();
    const item = clone(source ?? (kind === "model" ?
      modelDefault(document.providers.find((provider) => !provider.builtin) ||
        document.providers[0]) : providerDefault()));
    const form = element("form", { className: "model-config-form" });
    if (kind === "model") renderModel(form, item);
    else renderProvider(form, item);
    if (kind === "model" && source?.builtin) form.append(element("p", {
      text: `${source.id} · ${source.window.context_tokens} context · ${source.default_protocol}` }));
    if (source?.builtin && kind === "provider") form.append(element("p", {
      text: `${source.id} · 内置接口和凭据引用由程序管理` }));
    const actions = element("div", { className: "model-config-actions" });
    if (!source?.builtin && !document.runtime_override) {
      const save = element("button", { className: "primary-button", text: "保存",
        attrs: { type: "submit" } });
      save.disabled = Boolean(source);
      const discard = element("button", { className: "secondary-button", text: "放弃修改",
        attrs: { type: "button" } });
      discard.hidden = true;
      discard.addEventListener("click", () => { dirty = false; render(); });
      form.addEventListener("input", () => { dirty = true; discard.hidden = false; save.disabled = false; });
      form.addEventListener("change", () => { dirty = true; discard.hidden = false; save.disabled = false; });
      form.addEventListener("submit", (event) => {
        event.preventDefault();
        if (!form.reportValidity()) return;
        const next = clone(document);
        const value = kind === "model" ? readModel(form, item) : readProvider(form, item);
        if (kind === "provider" && !Object.keys(value.endpoints).length) {
          toast("至少填写一个协议接口 URL。", "error"); return;
        }
        if (kind === "model" && (!value.protocols.length ||
            !value.protocols.includes(value.default_protocol) ||
            !value.reasoning_efforts.includes(value.default_reasoning_effort))) {
          toast("选择至少一个协议和思考强度，并确认默认项属于所选范围。", "error");
          return;
        }
        const key = kind === "model" ? "items" : "providers";
        if (source) next[key] = next[key].map((entry) => entry.id === source.id ? value : entry);
        else next[key].push(value);
        void transact(next, source ? "配置已更新" : "配置已添加", kind, value.id);
      });
      actions.append(save, discard);
      if (source?.removable) {
        const used = kind === "provider" && document.items.some((model) => model.provider === source.id);
        const isDefault = kind === "model" && source.id === document.default_model;
        const remove = element("button", { className: "danger-link", text: "删除",
          attrs: { type: "button", title: used ? "先将引用它的模型移到其他 Provider" :
            isDefault ? "先设置其他默认模型" : "" } });
        remove.disabled = used || isDefault;
        const confirm = element("div", { className: "model-delete-confirm", attrs: { role: "group", "aria-label": "确认删除" } }, [
          element("span", { text: `删除“${source.name}”？使用它的已有会话可能无法继续。` }),
        ]);
        const cancel = element("button", { className: "secondary-button", text: "取消", attrs: { type: "button" } });
        const apply = element("button", { className: "danger-button", text: "确认删除", attrs: { type: "button" } });
        cancel.addEventListener("click", () => { confirm.hidden = true; remove.focus(); });
        apply.addEventListener("click", () => {
          const next = clone(document);
          next[kind === "model" ? "items" : "providers"] = collection().filter((entry) => entry.id !== source.id);
          confirm.hidden = true;
          void transact(next, "配置已删除", kind, "");
        });
        confirm.append(cancel, apply);
        confirm.hidden = true;
        remove.addEventListener("click", () => { if (allowChange()) { confirm.hidden = false; cancel.focus(); } });
        actions.append(remove, confirm);
      }
    }
    if (kind === "model" && source && source.id !== document.default_model && !document.runtime_override) {
      const setDefault = element("button", { className: "secondary-button", text: "设为默认模型",
        attrs: { type: "button" } });
      setDefault.addEventListener("click", () => {
        if (!allowChange()) return;
        const next = clone(document); next.default_model = source.id;
        void transact(next, "默认模型已更新");
      });
      actions.append(setDefault);
    }
    form.append(actions);
    layout.append(list, form);
    container.append(layout);
  }

  void load();
  return Object.freeze({ reload: () => load() });
}
