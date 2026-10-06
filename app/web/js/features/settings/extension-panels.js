import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { subscribeLocale, t } from "../../i18n.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";
import { loadAgents } from "../../state/catalogs.js";
import { refreshMcp, disconnectMcp, reloadCatalog } from "../../state/resources.js";
import { EXTENSION_KINDS, portableId, promptFields, editPrompt, newMcp, prepareMcpCredentials, mcpImportDocuments, bytesToBase64 } from "./extension-formats.js";

const label = (key, fallback, args = {}) => t(`ecosystem.${key}`, args, fallback);
const title = kind => ({ subagents: "SubAgent", skills: "Skill", mcp: "MCP", commands: label("commands", "命令") })[kind];
const path = (kind, id = "") => `/extensions/${kind}${id ? `/${id}` : ""}`;
const revision = item => ({ ifMatch: `"${item?.revision || "new"}"` });
const targetKey = () => targetState().selected?.id || "local";
function button(text, handler, primary = false) {
  const node = element("button", { className: primary ? "primary-button" : "secondary-button", text, attrs: { type: "button" } });
  node.addEventListener("click", () => { void handler(); }); return node;
}
function download(name, value) {
  const url = URL.createObjectURL(new Blob([value], { type: "application/octet-stream" }));
  const link = element("a", { attrs: { href: url, download: name } });
  document.body.append(link); link.click(); link.remove(); window.setTimeout(() => URL.revokeObjectURL(url), 30000);
}
function inputField(form, name, text, value = "", { area = false, type = "text", hint = "" } = {}) {
  const control = element(area ? "textarea" : "input", { attrs: { name, type: area ? null : type,
    autocomplete: "off", spellcheck: "false", maxlength: area ? 131072 : 4096 } });
  if (type === "checkbox") control.checked = Boolean(value); else control.value = value;
  const row = element("label", { className: type === "checkbox" ? "extension-check" : "extension-field" }, [
    element("span", { text }), control, hint ? element("small", { text: hint }) : null]);
  form.append(row); return control;
}

export function createExtensionPanels() {
  const states = new Map(EXTENSION_KINDS.map(kind => [kind, { loaded: false, serial: 0, items: [], error: "", busy: false, filter: "" }]));
  const roots = new Map(EXTENSION_KINDS.map(kind => [kind, document.querySelector(`[data-extension-manager="${kind}"]`)]));
  let selected = "", editing = null, activeTarget = targetKey();

  function announceChange(kind) {
    window.dispatchEvent(new CustomEvent("mdo-extensions-changed", { detail: { kind } }));
    if (kind === "subagents") void loadAgents();
  }
  async function load(kind) {
    const state = states.get(kind), serial = ++state.serial, target = targetKey();
    state.busy = true; state.error = ""; render(kind);
    try {
      const response = await api.get(path(kind));
      if (serial !== state.serial || target !== targetKey()) return;
      state.items = (response.data.items ?? []).sort((a, b) => a.id.localeCompare(b.id)); state.loaded = true;
    } catch (error) { if (serial === state.serial && target === targetKey()) state.error = errorMessage(error); }
    finally { if (serial === state.serial) { state.busy = false; render(kind); } }
  }
  async function act(kind, action) {
    const state = states.get(kind);
    if (state.busy || editing?.saving) return;
    const target = targetKey();
    state.busy = true; render(kind);
    try { await action(); if (target !== targetKey()) return; announceChange(kind); await load(kind); }
    catch (error) {
      // A multi-server import can have completed earlier saves before a failure.
      if (target !== targetKey()) return;
      announceChange(kind); await load(kind); state.error = errorMessage(error);
    }
    finally { if (target === targetKey()) { state.busy = false; render(kind); } }
  }
  function render(kind) {
    const root = roots.get(kind), state = states.get(kind);
    if (!root) return;
    clear(root);
    const add = button(label("create", "新建"), () => openEditor(kind));
    const file = element("input", { attrs: { type: "file", accept: kind === "mcp" ? ".json" : ".md,.json", "aria-label": label("import", "导入") } });
    file.hidden = true; file.addEventListener("change", () => { void importFile(kind, file.files?.[0]); });
    const pick = button(label("import", "导入"), () => file.click());
    const actions = [add, pick, button(label("refresh", "刷新"), () => act(kind, async () => {
      const catalog = ({ subagents: "modules", skills: "skills", mcp: "mcp" })[kind];
      if (catalog) await reloadCatalog(catalog);
    })), file];
    if (kind === "skills") {
      const folder = element("input", { attrs: { type: "file", webkitdirectory: "", multiple: "", "aria-label": label("importFolder", "导入文件夹") } });
      folder.hidden = true; folder.addEventListener("change", () => { void importFolder([...folder.files]); });
      actions.splice(2, 0, button(label("importFolder", "导入文件夹"), () => folder.click()), folder);
    }
    const toolbar = element("div", { className: "extension-toolbar" }, actions);
    toolbar.querySelectorAll("button").forEach(node => { node.disabled = state.busy; });
    const search = element("input", { className: "extension-search", attrs: { type: "search", placeholder: label("filter", "按名称或说明筛选"), "aria-label": label("filter", "按名称或说明筛选") } });
    search.value = state.filter;
    const list = element("div", { className: "extension-list" });
    const renderItems = () => {
      clear(list);
      const needle = state.filter.toLocaleLowerCase();
      const items = state.items.filter(item => `${item.id} ${item.name} ${item.description}`.toLocaleLowerCase().includes(needle));
      if (!items.length) list.append(element("p", { className: "empty-state", text: state.busy ? label("loading", "正在载入…") : label("empty", "还没有资源。新建或导入一个即可开始。") }));
      for (const item of items) {
        const meta = element("div", { className: "resource-meta" }, [
          element("code", { text: item.path }),
          element("span", { text: item.enabled ? label("enabled", "已启用") : label("disabled", "已停用") }),
          item.external ? null : element("span", { text: label("builtin", "内置") }),
          kind === "mcp" ? element("span", { text: t(`resource.mcp${({ ready: "Ready", failed: "Failed", disabled: "Disabled", disconnected: "Disconnected" })[item.state] || "Disconnected"}`) }) : null,
          kind === "mcp" ? element("span", { text: t("resource.toolCount", { count: item.tool_count || 0 }) }) : null,
        ]);
        const edit = button(label("edit", "编辑"), async () => {
          const target = targetKey();
          try { const data = (await api.get(path(kind, item.id))).data; if (target === targetKey()) openEditor(kind, data); }
          catch (error) { toast(errorMessage(error), "error"); }
        });
        const toggle = button(item.enabled ? label("disable", "停用") : label("enable", "启用"), () => act(kind, async () => {
          await api.post(`${path(kind, item.id)}/enabled`, { enabled: !item.enabled }, revision(item));
        }));
        const controls = [edit, toggle, button(label("export", "导出"), () => exportItem(kind, item))];
        if (kind === "mcp") controls.push(button(label("test", "测试连接"), () => act(kind, async () => {
          const result = await refreshMcp(item.id);
          toast(result?.message || label("testOk", "连接测试完成"), "success");
        })));
        if (kind === "mcp" && item.connected) controls.push(button(t("resource.disconnect"), () => act(kind, () => disconnectMcp(item.id))));
        if (item.external) controls.push(button(label("delete", "删除"), () => act(kind, async () => {
          if (!window.confirm(label("deleteConfirm", "删除 {name}？", { name: item.name }))) return;
          await api.delete(path(kind, item.id), revision(item));
        })));
        const row = element("article", { className: "resource-card" }, [element("h3", { text: item.name || item.id }),
          element("p", { text: item.description }), meta,
          item.valid ? null : element("p", { className: "extension-error", text: item.error }),
          element("div", { className: "resource-actions" }, controls)]);
        row.querySelectorAll("button").forEach(node => { node.disabled = state.busy; }); list.append(row);
      }
    };
    search.addEventListener("input", () => { state.filter = search.value; renderItems(); });
    renderItems(); root.append(toolbar, search,
      element("p", { className: "extension-help", text: label(`${kind}Help`, ({
        subagents: "独立委派任务；模型默认跟随主 Agent。子任务仍受父任务权限限制。",
        skills: "按需读取 SKILL.md 和参考文件；安装 Skill 不会自动执行脚本。",
        mcp: "保存配置不会启动程序。测试连接或模型调用时才建立连接；HTTP 使用 HTTPS。",
        commands: "在输入框键入 / 选择命令；$ARGUMENTS 代表参数，展开后确认发送。",
      })[kind]) }),
      state.error ? element("p", { className: "extension-error", attrs: { role: "alert" }, text: state.error }) : "", list);
  }
  function dirty() {
    if (!editing) return false;
    try { return editing.saving || editing.files?.length > 0 || editing.secrets?.length > 0 ||
      editing.id.value !== editing.originalId || currentSource() !== editing.baseline; }
    catch { return true; }
  }
  function closeEditor(force = false) {
    if (!editing || editing.saving) return false;
    if (!force && dirty() && !window.confirm(label("discardConfirm", "放弃未保存的编辑？"))) return false;
    const state = editing; editing = null;
    state.dialog.close(); state.dialog.remove(); state.returnFocus?.focus(); return true;
  }
  function fieldValues() {
    return Object.fromEntries([...editing.form.elements].filter(node => node.name).map(node => [node.name,
      node.type === "checkbox" ? node.checked : node.value]));
  }
  function currentSource() {
    if (editing.sourceMode) return editing.source.value;
    const values = fieldValues();
    if (editing.kind === "mcp") {
      const doc = JSON.parse(editing.source.value);
      doc.id = editing.id.value; doc.name = values.name; doc.description = values.description;
      doc.enabled = values.enabled;
      const old = doc.transport;
      doc.transport = values.transport === "stdio" ? {
        type: "stdio", program: values.program, arguments: JSON.parse(values.arguments || "[]"),
        working_directory: values.working_directory || null, inherit_environment: true,
        environment: JSON.parse(values.environment || "[]"),
      } : { type: "streamable-http", endpoint: values.endpoint, headers: JSON.parse(values.headers || "[]") };
      if (old?.type === "stdio" && doc.transport.type === "stdio") doc.transport.inherit_environment = old.inherit_environment ?? true;
      return JSON.stringify(doc, null, 2);
    }
    const fields = { name: values.name, description: values.description, prompt: values.prompt };
    if (editing.kind === "commands") { delete fields.name; fields["argument-hint"] = values.argument_hint; }
    if (editing.kind === "subagents") {
      fields.model = values.model || "inherit"; fields.reasoning_effort = values.reasoning_effort; fields.thoughtLevel = "";
      fields.read_only = values.read_only; fields.allow_delegation = values.allow_delegation;
      fields.tools = values.tools.trim() ? values.tools.split(/[,\s]+/).filter(Boolean) : [];
    }
    return editPrompt(editing.source.value, fields);
  }
  function fillForm(source) {
    clear(editing.form);
    const kind = editing.kind;
    if (kind === "mcp") {
      const doc = JSON.parse(source), transport = doc.transport;
      inputField(editing.form, "name", label("name", "名称"), doc.name);
      inputField(editing.form, "description", label("description", "说明"), doc.description);
      inputField(editing.form, "enabled", label("enable", "启用"), doc.enabled, { type: "checkbox" });
      const select = element("select", { attrs: { name: "transport" } }, [
        element("option", { text: "stdio", attrs: { value: "stdio" } }),
        element("option", { text: "Streamable HTTP", attrs: { value: "streamable-http" } }),
      ]); select.value = transport.type;
      editing.form.append(element("label", { className: "extension-field" }, [element("span", { text: label("transport", "连接方式") }), select]));
      const stdio = element("div", { className: "extension-transport" });
      inputField(stdio, "program", label("program", "程序"), transport.program || "", { hint: label("programHint", "填写目标设备上已安装的程序路径或名称，例如 npx、uvx；Android 通常使用 HTTP MCP。") });
      inputField(stdio, "arguments", label("arguments", "参数（JSON 数组）"), JSON.stringify(transport.arguments || []));
      inputField(stdio, "working_directory", label("workingDirectory", "工作目录（可选）"), transport.working_directory || "");
      inputField(stdio, "environment", label("environment", "环境变量（JSON）"), JSON.stringify(transport.environment || [], null, 2), { area: true, hint: label("secretHint", "使用 secret_ref 引用 env:、file: 或已保存的 vault: 凭据。导入普通配置中的凭据会自动加密保存。") });
      const http = element("div", { className: "extension-transport" });
      inputField(http, "endpoint", label("endpoint", "服务器地址"), transport.endpoint || "", { type: "url" });
      inputField(http, "headers", label("headers", "请求头（JSON）"), JSON.stringify(transport.headers || [], null, 2), { area: true, hint: label("secretHint", "普通 JSON 对象的值会自动加密保存；原生数组使用 secret_ref 引用 env:、file: 或 vault: 凭据。") });
      editing.form.append(stdio, http);
      const sync = () => { stdio.hidden = select.value !== "stdio"; http.hidden = !stdio.hidden; };
      select.addEventListener("change", sync); sync(); return;
    }
    const values = promptFields(source);
    if (kind !== "commands") inputField(editing.form, "name", label("name", "名称"), values.name || "");
    inputField(editing.form, "description", label("description", "说明"), values.description || "");
    if (kind === "subagents") {
      inputField(editing.form, "model", label("model", "模型"), values.model || "inherit", { hint: label("modelHint", "inherit 跟随主 Agent；或填写模型 ID。") });
      inputField(editing.form, "reasoning_effort", label("effort", "思考强度（可选）"), values.reasoning_effort || values.thoughtLevel || "");
      inputField(editing.form, "tools", label("tools", "工具（可选）"), Array.isArray(values.tools) ? values.tools.join(", ") : String(values.tools || "").replace(/^\[|\]$/g, "").replace(/["']/g, ""), { hint: label("toolsHint", "逗号分隔；留空继承父任务可用工具。填写工具名可缩小范围，例如 read, grep, skill。") });
      inputField(editing.form, "read_only", label("readOnly", "只读任务"), values.read_only === true || values.read_only === "true", { type: "checkbox" });
      inputField(editing.form, "allow_delegation", label("delegation", "允许再次委派"), values.allow_delegation === true || values.allow_delegation === "true", { type: "checkbox" });
    }
    if (kind === "commands") inputField(editing.form, "argument_hint", label("argumentHint", "参数提示（可选）"), values["argument-hint"] || "");
    inputField(editing.form, "prompt", label("instructions", "指令"), values.prompt || "", { area: true });
  }
  function openEditor(kind, item = null, imported = null) {
    if (editing && !closeEditor()) return;
    const initial = imported?.content ?? item?.content ?? (kind === "mcp" ? JSON.stringify(newMcp(), null, 2) :
      editPrompt("", { name: kind === "commands" ? undefined : "", description: "", prompt: "" }));
    const dialog = element("dialog", { className: "extension-editor", attrs: { "aria-labelledby": "extension-editor-title" } });
    const id = element("input", { attrs: { name: "extension_id", maxlength: 64, autocomplete: "off", spellcheck: "false", required: "", placeholder: "my-resource" } });
    id.value = imported?.id ?? item?.id ?? ""; id.disabled = Boolean(item);
    const form = element("form", { className: "extension-form" });
    const source = element("textarea", { className: "extension-source", attrs: { spellcheck: "false", "aria-label": label("source", "源文件"), maxlength: 131072 } });
    source.value = initial; source.hidden = true;
    const status = element("p", { className: "extension-editor-status", attrs: { role: "status" } });
    const formMode = button(label("form", "表单"), () => changeMode(false));
    const sourceMode = button(label("source", "源文件"), () => changeMode(true));
    const save = button(label("save", "保存"), saveEditor, true);
    const cancel = button(label("cancel", "取消"), () => closeEditor());
    const heading = element("div", { className: "extension-editor-heading" }, [
      element("h2", { text: `${item ? label("edit", "编辑") : label("create", "新建")} ${title(kind)}`, attrs: { id: "extension-editor-title" } }), cancel]);
    editing = { kind, item, dialog, id, source, form, status, save, cancel, formMode,
      baseline: initial, originalId: id.value, sourceMode: false, saving: false, files: imported?.files ?? [],
      secrets: imported?.secrets ?? [], returnFocus: document.activeElement };
    try { fillForm(initial); }
    catch { editing.sourceMode = true; source.hidden = false; form.hidden = true; }
    // Imported files should keep their literal source until the user chooses a form.
    if (imported) { editing.sourceMode = true; source.hidden = false; form.hidden = true; }
    if (!imported && !editing.sourceMode) editing.baseline = currentSource();
    dialog.append(heading, element("label", { className: "extension-field" }, [element("span", { text: label("id", "文件 ID") }), id,
      element("small", { text: label("idHint", "小写字母、数字、点、短横线或下划线；创建后不改名。") })]),
      element("div", { className: "extension-toolbar" }, [formMode, sourceMode]), form, source,
      imported?.files?.length ? element("p", { text: label("fileCount", "包含 {count} 个附属文件", { count: imported.files.length }) }) : "",
      status, element("div", { className: "extension-editor-footer" }, [save]));
    form.addEventListener("submit", event => { event.preventDefault(); void saveEditor(); });
    dialog.addEventListener("cancel", event => { event.preventDefault(); closeEditor(); });
    document.body.append(dialog); dialog.showModal(); (id.disabled ? form.querySelector("input") || source : id).focus();
  }
  function changeMode(sourceMode) {
    if (!editing || editing.saving || editing.sourceMode === sourceMode) return;
    try {
      const current = currentSource();
      if (!sourceMode) fillForm(current);
      editing.source.value = current; editing.sourceMode = sourceMode;
      editing.form.hidden = sourceMode; editing.source.hidden = !sourceMode; editing.status.textContent = "";
      (sourceMode ? editing.source : editing.form.querySelector("input,textarea"))?.focus();
    } catch (error) { editing.status.textContent = errorMessage(error); }
  }
  async function saveEditor() {
    const editor = editing;
    if (!editor || editor.saving) return;
    try {
      const id = editor.id.value.trim();
      if (!portableId(id)) throw new Error(label("idInvalid", "请填写有效的文件 ID。"));
      let content = currentSource();
      let secrets = editor.secrets;
      if (editor.kind === "mcp") {
        const doc = JSON.parse(content); doc.id = id;
        ({ content, secrets } = prepareMcpCredentials(doc, secrets));
      }
      const body = { content };
      if (editor.files.length) body.files = editor.files;
      if (secrets.length) body.secrets = secrets;
      if (new TextEncoder().encode(JSON.stringify(body)).length > 250000) throw new Error(label("importLimit", "导入内容过大；请直接复制资源目录到 mdo-home 后刷新。"));
      editor.saving = true; editor.dialog.querySelectorAll("input,textarea,select,button").forEach(node => { node.disabled = true; });
      editor.status.textContent = label("saving", "正在保存…");
      await api.put(path(editor.kind, id), body, revision(editor.item));
      editor.saving = false; closeEditor(true); announceChange(editor.kind); await load(editor.kind);
      toast(label("saved", "已保存并生效"), "success");
    } catch (error) {
      editor.saving = false; editor.dialog.querySelectorAll("input,textarea,select,button").forEach(node => { node.disabled = false; }); editor.id.disabled = Boolean(editor.item);
      editor.status.textContent = error?.status === 412 ? label("conflict", "文件已被其他操作修改。请复制当前内容，重新载入后再保存。") : errorMessage(error);
    }
  }
  async function importFile(kind, file) {
    if (!file) return;
    const target = targetKey();
    try {
      if (file.size > 250000) throw new Error(label("importLimit", "导入内容过大；请直接复制资源目录到 mdo-home 后刷新。"));
      const content = await file.text();
      if (target !== targetKey()) return;
      if (kind === "mcp") {
        const documents = mcpImportDocuments(content);
        if (documents.length === 1) openEditor(kind, null, documents[0]);
        else {
          // Multi-server import is explicit, and each completed save is reported.
          if (!window.confirm(label("importServers", "导入 {count} 个 MCP 服务器？同名资源不会覆盖。", { count: documents.length }))) return;
          await act(kind, async () => {
            let saved = 0;
            for (const doc of documents) {
              try { await api.put(path(kind, doc.id), { content: doc.content, secrets: doc.secrets }, revision(null)); ++saved; }
              catch (error) { throw new Error(label("partialImport", "已导入 {count} 个；其余未导入。", { count: saved }) + " " + errorMessage(error)); }
            }
          });
        }
      } else if (/\.json$/i.test(file.name)) {
        const bundle = JSON.parse(content);
        if (bundle.kind !== kind || typeof bundle.content !== "string") throw new Error(label("wrongKind", "文件类型与当前分类不符。"));
        openEditor(kind, null, { ...bundle, id: bundle.id });
      } else {
        const fields = promptFields(content);
        const id = kind === "skills" && file.name.toLowerCase() === "skill.md" ? fields.name || "" : file.name.replace(/\.md$/i, "").toLowerCase();
        openEditor(kind, null, { id, content });
      }
    } catch (error) { toast(errorMessage(error), "error"); }
  }
  async function importFolder(files) {
    if (!files.length) return;
    const target = targetKey();
    try {
      const skill = files.find(file => /^[^/]+\/SKILL\.md$/.test(file.webkitRelativePath));
      if (!skill) throw new Error(label("skillMissing", "所选文件夹的根目录必须有 SKILL.md。"));
      const root = skill.webkitRelativePath.split("/")[0];
      const resources = files.filter(file => file !== skill && file.webkitRelativePath.startsWith(`${root}/`) &&
        !file.webkitRelativePath.slice(root.length + 1).split("/").some(part => part.startsWith(".")));
      if (resources.length > 128 || files.reduce((sum, file) => sum + file.size, 0) > 128 * 1024) throw new Error(label("importLimit", "导入内容过大；请直接复制资源目录到 mdo-home 后刷新。"));
      const content = await skill.text();
      const bundle = [];
      for (const file of resources) bundle.push({ path: file.webkitRelativePath.slice(root.length + 1), base64: bytesToBase64(new Uint8Array(await file.arrayBuffer())) });
      if (target !== targetKey()) return;
      openEditor("skills", null, { id: root.toLowerCase(), content, files: bundle });
    } catch (error) { toast(errorMessage(error), "error"); }
  }
  async function exportItem(kind, item) {
    const target = targetKey();
    try {
      const data = (await api.get(`${path(kind, item.id)}${kind === "skills" ? "/bundle" : ""}`)).data;
      if (target !== targetKey()) return;
      if (kind === "skills") download(`${item.id}.skill.json`, JSON.stringify({ kind, id: item.id, content: data.content, files: data.files }, null, 2));
      else download(`${item.id}${kind === "mcp" ? ".json" : ".md"}`, data.content);
      if (kind === "mcp") toast(label("exportSecrets", "凭据不会导出。换设备导入后，请重新配置凭据。"));
    } catch (error) { toast(errorMessage(error), "error"); }
  }
  const unsubscribeLocale = subscribeLocale(() => { for (const kind of EXTENSION_KINDS) render(kind); });
  const unsubscribeTarget = subscribeTarget(() => {
    if (activeTarget === targetKey()) return;
    activeTarget = targetKey(); closeEditor(true);
    for (const state of states.values()) { ++state.serial; state.loaded = false; state.busy = false; state.items = []; state.error = ""; }
    if (selected) void load(selected);
  });
  return Object.freeze({
    selectSection(kind) { selected = EXTENSION_KINDS.includes(kind) ? kind : ""; if (selected && !states.get(kind).loaded && !states.get(kind).busy) void load(kind); },
    hasPendingChanges: dirty,
    destroy() { closeEditor(true); unsubscribeLocale(); unsubscribeTarget(); },
  });
}
