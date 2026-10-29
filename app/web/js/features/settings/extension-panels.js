import { createExtensionBrowser, actionMenu, extensionToggle, detailTabs, browserText } from "../../components/extension-browser.js";
import { createSingleResourceStore } from "./single-resource-store.js";
import { customizableResource, resourceCredits, storeReadOnlyMessage } from "./resource-credits.js";
import { createSkillWorkbench } from "./skill-workbench.js";
import { createDirectoryEditor } from "./directory-editor.js";
import { directoryId, directoryManifest, readDirectoryEntry, saveDirectory, changeDirectory } from "./directory-documents.js";
import { createMcpContext } from "./mcp-context.js";
import { createMcpAuthorization } from "./mcp-authorization.js";
import { createMcpConnections } from "./mcp-directory-connections.js";

import { newTool } from "./tool-template.js";
import { newCommand } from "./command-template.js";
import { createCommandWorkbench } from "./command-projects.js";
import { newRole, roleAdvanced, parseRoleAdvanced, ROLE_ADVANCED_KEYS, openRolePlan, roleProgramTemplate } from "./agent-authoring.js";
import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { subscribeLocale, t } from "../../i18n.js";
import { clear, element, errorMessage, toast } from "../../utils/dom.js";
import { loadAgents } from "../../state/catalogs.js";
import { refreshMcp, disconnectMcp, reloadCatalog } from "../../state/resources.js";
import { EXTENSION_KINDS, portableId, promptFields, editPrompt } from "./extension-formats.js";
import { newMcpTemplate } from "./mcp-template.js";
import { exportDirectoryZip } from "../../api/extension-export.js";

import { saveBlobFile } from "../../utils/file-download.js";
import { createExtensionReads } from "./extension-reads.js";
import { openLayer } from "../../components/layers.js";
import { awaitResourceOperation } from "../../state/resource-operation.js";
import { extensionFieldError, validateExtensionId, requiredExtensionFields, validateExtensionForm, parseExtensionJson } from "./extension-validation.js";

const label = (key, fallback, args = {}) => t(`ecosystem.${key}`, args, fallback);
const title = kind => ({ agents: "Agent", tools: label("toolTitle", "工具"), subagents: "SubAgent", skills: "Skill", mcp: "MCP", knowledge: "知识库", commands: label("commands", "命令") })[kind];
const path = (kind, id = "") => `/extensions/${kind}${id ? `/${id}` : ""}`;
const revision = item => ({ ifMatch: `"${item?.revision || "new"}"` });
const targetKey = () => targetState().selected?.id || "local";
const availability = item => label(`availability.${item.availability}`, ({ available: "可用（仍受任务权限限制）",
  sign_in_required: "登录账号后可用", web_disabled: "请开启允许联机搜索", no_subagents: "尚未启用 SubAgent",
  no_mcp_servers: "尚未启用 MCP 服务器", unavailable: "暂不可用", missing: "工具未注册" })[item.availability] || item.availability);
function button(text, handler, primary = false) {
  const node = element("button", { className: primary ? "primary-button" : "secondary-button", text, attrs: { type: "button" } });
  node.addEventListener("click", () => { void handler(); }); return node;
}
function download(name, value) {
  saveBlobFile({ blob: new Blob([value], { type: "application/octet-stream" }), filename: name });
}
function inputField(form, name, text, value = "", { area = false, type = "text", hint = "", required = false } = {}) {
  const control = element(area ? "textarea" : "input", { attrs: { name, type: area ? null : type,
    autocomplete: "off", spellcheck: "false", maxlength: area ? 131072 : 4096,
    required: required ? "" : null, "data-field-label": text } });
  if (type === "checkbox") control.checked = Boolean(value); else control.value = value;
  const row = element("label", { className: type === "checkbox" ? "extension-check" : "extension-field" }, [
    element("span", { text: required ? `${text} · ${label("required", "必填")}` : text }), control, hint ? element("small", { text: hint }) : null]);
  form.append(row); return control;
}

export function createExtensionPanels() {
  const states = new Map(EXTENSION_KINDS.map(kind => [kind, { loaded: false, serial: 0, items: [], catalog: [], view: "all", stateFilter: "all", error: "", busy: false, reading: false, filter: "", area: "local" }]));
  const roots = new Map(EXTENSION_KINDS.map(kind => [kind, document.querySelector(`[data-extension-manager="${kind}"]`)]));
  let selected = "", editing = null, activeTarget = targetKey(), pendingResource = null;
  let disposed = false, paused = false;
  const reads = createExtensionReads();
  const directoryEditor = createDirectoryEditor({onChange:kind=>{announceChange(kind);if(selected===kind)void load(kind);}});
  const skillWorkbench = createSkillWorkbench({ owner: roots.get("skills"), onChange: () => announceChange("skills"), onShare: item => stores.get("skills").publish(item),onEdit:()=>openEditor("skills") });
  const mcpContext = createMcpContext();
  const mcpAuthorization = createMcpAuthorization();
  const mcpConnections = createMcpConnections(() => { announceChange("mcp"); if(selected==="mcp")void load("mcp"); });

  const commandWorkbench = createCommandWorkbench({ owner: roots.get("commands"), onChange: () => announceChange("commands"), onShare: item => stores.get("commands").publish(item) });
  const stores = new Map(["agents", "subagents", "skills", "tools", "commands", "mcp", "knowledge"].map(kind => [kind, createSingleResourceStore(kind, () => { announceChange(kind); if (selected === kind) void load(kind); }, () => { states.get(kind).area = "store"; render(kind); }, async receipt => {
    if(receipt.kind!==kind||selected!==kind)return;
    const state=states.get(kind);state.area="local";state.view="custom";state.filter="";
    stores.get(kind).activate(false);render(kind);await load(kind);
    browsers.get(kind).open(receipt.id);
  })]));
  const actions = new Map();
  const browsers = new Map(EXTENSION_KINDS.map(kind => [kind, createExtensionBrowser({ kind, onClose: () => reads.cancel(`detail:${kind}`) })]));
  const current = (kind, target) => !disposed && !paused && selected === kind && target === targetKey();
  function cancelReads() {
    reads.cancel();
    for (const controller of actions.values()) controller.abort();
    for (const state of states.values()) {
      ++state.serial;
      if (state.reading) { state.reading = false; state.busy = false; }
    }
  }

  function announceChange(kind) {
    window.dispatchEvent(new CustomEvent("mdo-extensions-changed", { detail: { kind } }));
    if (["agents", "subagents", "tools"].includes(kind)) void loadAgents();
  }
  async function load(kind,append=false) {
    if (!current(kind, targetKey())) return;
    const state = states.get(kind), serial = ++state.serial, target = targetKey();
    const isCurrent = () => current(kind, target) && serial === state.serial;
    state.busy = state.reading = true; state.error = ""; render(kind);
    try {
      const [response, catalog] = await Promise.all([
        reads.read("catalog", `${path(kind)}?offset=${append?state.items.length:0}&limit=100`, { isCurrent }),
        kind === "tools" ? reads.read("catalog-tools", "/tools", { isCurrent }) : null,
      ]);
      if (!isCurrent()) return;
      state.catalog = catalog?.data.items ?? [];
      state.items=append?[...new Map([...state.items,...response.data.items].map(row=>[row.id,row])).values()]:response.data.items||[];state.total=response.data.total??state.items.length;state.loaded=true;
    } catch (error) { if (isCurrent() && error?.name !== "AbortError") { state.loaded = false; state.error = errorMessage(error); } }
    finally { if (isCurrent()) {
      for (const key of ["catalog", "catalog-tools", "packages", "mcp-templates"]) reads.cancel(key);
      state.busy = state.reading = false; render(kind);if(pendingResource?.kind===kind)void openLinkedResource();
    } }
  }
  async function openLinkedResource() {
    const ref=pendingResource;if(!ref||states.get(ref.kind)?.busy)return;pendingResource=null;
    const target=targetKey(), isCurrent=()=>current(ref.kind,target);
    try{const data=await readDirectoryEntry(ref.kind,ref.id);if(isCurrent())openEditor(ref.kind,data);}
    catch(e){if(isCurrent()&&e?.name!=="AbortError")toast(errorMessage(e),"error");}
  }
  async function act(kind, action) {
    const state = states.get(kind);
    if (state.busy || editing?.saving) return;
    const target = targetKey();
    const controller = new AbortController(); actions.set(kind, controller);
    const scope = { signal: controller.signal, isCurrent: () => current(kind, target) && !controller.signal.aborted };
    state.loaded = false; state.operation = null; state.error = "";
    state.busy = true; render(kind);
    try { await action(scope); if (!scope.isCurrent()) return; announceChange(kind); await load(kind); }
    catch (error) {
      // A multi-server import can have completed earlier saves before a failure.
      if (target !== targetKey()) return;
      if (error?.operation) state.operation = error.operation;
      if (!scope.isCurrent() || error?.name === "AbortError") return;
      announceChange(kind); await load(kind); state.error = errorMessage(error);
    }
    finally { if (actions.get(kind) === controller) {
      actions.delete(kind);
      if (target === targetKey()) { state.busy = false; render(kind); if (current(kind, target) && !state.loaded) void load(kind); }
    } }
  }
  function render(kind) {
    const root = roots.get(kind), state = states.get(kind);
    if (!root) return;
    browsers.get(kind)?.rememberFocus(); clear(root);
    const store = stores.get(kind);
    if (store) {
      const tabs = element("div", { className: "store-tabs", attrs: { role: "tablist", "aria-label": title(kind) } });
      for (const [area, text] of [["local", `本机 ${title(kind)}`], ["store", `${title(kind)}商店`]]) {
        const tab = button(text, () => { if (state.busy || store.hasPendingChanges()) return; state.area = area; store.activate(area === "store"); render(kind); if (area === "local") void load(kind); });
        tab.setAttribute("role", "tab"); tab.setAttribute("aria-selected", String(state.area === area)); tabs.append(tab);
      }
      root.append(tabs, store.statusRoot);
      if (state.area === "store") { root.append(store.root); return; }
    }
    const add = button(label("create", "新建"), () => openEditor(kind));
    const file = element("input", { attrs: { type: "file", accept: ".zip", "aria-label": label("import", "导入") } });
    file.hidden = true; file.addEventListener("change", () => { void importFile(kind, file.files?.[0]); });
    const pick = button(label("import", "导入"), () => file.click());
    const actions = [add, pick, button(label("refresh", "刷新"), () => act(kind, async scope => {
      const catalog = ({ agents: "modules", tools: "modules", subagents: "modules", skills: "skills", mcp: "mcp" })[kind];
      if (catalog && !(kind === "tools" && state.view === "builtin")) await reloadCatalog(catalog, scope);
    })), file];
    if (state.operation) {
      const operation = state.operation;
      actions.splice(3, 0, button(t("resource.recheckOperation", {}, "重新读取状态"), () => act(kind, scope => awaitResourceOperation(operation, scope))));
    }
    if (kind === "skills") {
      actions.push(button(label("skillBuilder", "Skill 构建器"), () => skillWorkbench.open({ builder: true })));
      actions.push(button(label("skillSources", "来源、项目与 AI 创建"), () => skillWorkbench.open()));
    }
    const builtinView = kind === "tools" && state.view === "builtin";
    if (kind === "mcp") actions.push(button("管理连接", () => mcpConnections.open()));
    if (kind === "commands") actions.push(button("项目命令", () => commandWorkbench.open()));
    const toolbar = element("div", { className: "extension-toolbar eb-toolbar" });
    const refresh = actions.find(control => control.tagName === "BUTTON" && control.textContent === label("refresh", "刷新"));
    const creation = actions.filter(control => control.tagName === "BUTTON" && control !== refresh);
    toolbar.append(refresh, actionMenu(creation, browserText("createImport", "创建 / 导入")), ...actions.filter(control => control.tagName === "INPUT"));
    toolbar.querySelectorAll("button").forEach(node => { node.disabled = state.busy; });
    const search = element("input", { className: "extension-search", attrs: { type: "search", placeholder: label("filter", "按名称或说明筛选"), "aria-label": label("filter", "按名称或说明筛选") } });
    search.value = state.filter;
    const browser = browsers.get(kind), list = browser.root;
    const statusFilter = element("select", { attrs: { "aria-label": browserText("status", "状态") } });
    for (const [value, text] of [["all", browserText("all", "全部状态")], ["enabled", label("enabled", "已启用")], ["disabled", label("disabled", "已停用")], ["error", browserText("needsAttention", "需要处理")]]) statusFilter.append(element("option", { text, attrs: { value } }));
    statusFilter.value = state.stateFilter;
    const sourceFilter = kind === "tools" ? element("select", { attrs: { "aria-label": browserText("source", "来源") } }) : null;
    if (sourceFilter) {
      for (const [value, text] of [["all", browserText("allSources", "全部来源")], ["builtin", label("builtinTools", "mdo 内建工具")], ["custom", label("customTools", "自定义工具")]]) sourceFilter.append(element("option", { text, attrs: { value } }));
      sourceFilter.value = state.view;
      sourceFilter.addEventListener("change", () => { state.view = sourceFilter.value; renderItems(); });
    }
    statusFilter.addEventListener("change", () => { state.stateFilter = statusFilter.value; renderItems(); });
    const renderItems = () => {
      const needle = state.filter.toLocaleLowerCase(), rows = [];
      const builtin = kind === "tools" ? state.catalog.filter(item => item.source === "builtin").map(item => ({ ...item, builtinTool: true })) : [];
      const sourceItems = kind === "tools" ? state.view === "builtin" ? builtin : state.view === "custom" ? state.items : [...builtin, ...state.items] : state.items;
      const items = sourceItems.filter(item => `${item.id} ${item.name} ${item.description}`.toLocaleLowerCase().includes(needle)).filter(item => {
        if (state.stateFilter === "all") return true;
        if (state.stateFilter === "error") return item.builtinTool ? !item.available : !item.valid || item.state === "failed";
        const enabled = item.builtinTool ? item.available : item.enabled;
        return state.stateFilter === "enabled" ? enabled : !enabled;
      });
      for (const item of items) {
        if (item.builtinTool) {
          rows.push({ key: `builtin:${item.id}`, title: item.id, kind, description: label(`toolDescription.${item.id}`, item.description),
            meta: [label("builtin", "内置"), item.member_only ? label("member", "会员") : ""], badge: { text: availability(item) },
            detail: () => detailTabs([{ id: "description", label: browserText("description", "说明"), content: element("div", {}, [element("p", { text: label(`toolDescription.${item.id}`, item.description) }), element("p", { text: availability(item) }), element("p", { className: "extension-help", text: label("builtinToolsHelp", "内建工具由 mdo 维护。能否调用取决于登录、功能设置和当前任务权限。") })]) }]) });
          continue;
        }
        const meta = element("div", { className: "resource-meta" }, [
          element("code", { text: item.path }),
          element("span", { text: item.enabled ? label("enabled", "已启用") : label("disabled", "已停用") }),
          item.external ? null : element("span", { text: label("builtin", "内置") }),
          kind === "mcp" ? element("span", { text: t(`resource.mcp${({ ready: "Ready", failed: "Failed", disabled: "Disabled", disconnected: "Disconnected" })[item.state] || "Disconnected"}`) }) : null,
          kind === "mcp" ? element("span", { text: t("resource.toolCount", { count: item.tool_count || 0 }) }) : null,
        ]);
        const owner=null, protectedKind=["agents","subagents","tools","skills","commands","mcp","knowledge"].includes(kind);
        const editable=!protectedKind||customizableResource(item,owner), credits=protectedKind?resourceCredits(item,owner):null;
        const edit = button(label("edit", "编辑"), async () => {
          const target = targetKey(), isCurrent = () => current(kind, target);
          try { const data = await readDirectoryEntry(kind,item.id); if (isCurrent()) openEditor(kind,data); }
          catch (error) { if (isCurrent() && error?.name !== "AbortError") toast(errorMessage(error), "error"); }
        });
        const toggle = button(item.enabled ? label("disable", "停用") : label("enable", "启用"), () => act(kind, async () => {
          await api.post(`${path(kind, item.id)}/enabled`, { enabled: !item.enabled });
        }));
        const controls = editable ? [edit, button(label("export", "导出"), () => exportItem(kind, item))] : [];
        if (["agents","subagents"].includes(kind)) controls.push(button("上下文预览",()=>openRolePlan({kind,id:item.id,owner:roots.get(kind)})));
        if (store && item.valid && item.shareable!==false && editable && !(kind==="agents"&&item.id==="default")) controls.push(button("分享到创作中心", () => store.publish(item, owner)));
        if (store) controls.push(...store.localActions(item));
        controls.push(button("目录与文件",()=>directoryEditor.open({kind,item})));
        if (kind === "skills" && editable) controls.push(button(label("improveSkill", "用 AI 改进"), () =>
          skillWorkbench.open({ builder: true, mode: "improve", id: item.id, scope: "user" })));
        if(kind==="mcp")controls.push(button("创建连接",()=>mcpConnections.open({templateId:item.id})));
        controls.push(button("卸载",()=>act(kind,async()=>{
          if(!confirm(`卸载 ${item.name||item.id} 的整个目录？`))return;
          await changeDirectory(kind,item,"remove");
        })));
        const canToggle = !(kind === "agents" && item.id === "default");
        toggle.disabled = state.busy;
        controls.forEach(control => { control.disabled = state.busy; });
        const status = kind === "mcp" ? ({ready:"已连接",failed:"连接失败",disabled:"已停用",disconnected:"未连接"})[item.state]||"未连接" : !item.valid ? browserText("needsAttention", "需要处理") : "";
        rows.push({ key: item.id, kind, title: item.name || item.id, description: item.description, byline: credits?.submitter,
          meta: credits ? [...credits.meta, owner?.source === "store" || item.store_managed ? browserText("storeInstalled", "商店安装") : item.external ? browserText("custom", "自定义") : label("builtin", "内置")] : [owner?.author, owner?.version, item.external ? browserText("custom", "自定义") : label("builtin", "内置")],
          badge: { text: status, tone: !item.valid || item.state === "failed" ? "error" : "" },
          toggle: canToggle ? extensionToggle(toggle, item.enabled, item.name || item.id) : null, actions: controls,
          detail: () => {
            const description = element("div", {}, [element("p", { text: item.description }), !item.valid ? element("p", { className: "extension-error", text: item.error }) : null]);
            if(credits)description.append(credits.facts());if(!editable)description.append(element("p",{className:"extension-help",text:storeReadOnlyMessage()}));
            const usage = element("div", {}, [element("p", { text: kind === "skills" ? `$${item.alias} · /skill:${item.alias}` : kind === "commands" ? `/${item.alias}` : kind === "mcp" ? status : kind === "knowledge" ? `${item.directory}/${item.index}\n${item.inject_prompt ? item.prompt : "已关闭提示词注入"}` : item.name || item.id }),
              kind === "tools" ? element("div", { className: "tool-exports" }, (item.tools || []).map(tool => element("code", { text: tool.id }))) : null]);
            const source = element("div", {}, [meta]), preview = element("div"); source.append(preview); let sourceLoaded = false;
            const readSource = async () => {
              if (sourceLoaded) return;
              const target = targetKey(), isCurrent = () => current(kind, target) && browser.isSelected(item.id) && source.isConnected;
              preview.replaceChildren(element("p", { text: browserText("loading", "正在载入…") }));
              try {
                const data=await readDirectoryEntry(kind,item.id);
                if(!isCurrent())return;
                preview.replaceChildren(element("pre",{className:"store-source",text:data.content}),button("浏览完整目录",()=>directoryEditor.open({kind,item})));
                sourceLoaded = true;
              } catch (error) { if (isCurrent() && error?.name !== "AbortError") preview.replaceChildren(element("p", { className: "extension-error", text: errorMessage(error) }), button(browserText("retry", "重试"), readSource)); }
            };
            return detailTabs([{ id: "description", label: browserText("description", "说明"), content: description }, { id: "usage", label: browserText("usage", "使用方式"), content: usage }, { id: "source", label: browserText("sourceFiles", "来源与文件"), content: source, onSelect: () => queueMicrotask(readSource) }]);
          } });
      }
      browser.setItems(rows, { busy: state.busy, pageKey:JSON.stringify([state.filter,state.stateFilter,state.view]),onNext:state.items.length<state.total?()=>load(kind,true):null, empty: state.filter || state.stateFilter !== "all" ? browserText("empty", "没有匹配的扩展") : label("empty", "还没有资源。新建或导入一个即可开始。") });
    };
    search.addEventListener("input", () => { state.filter = search.value; renderItems(); });
    toolbar.prepend(search, statusFilter); if (sourceFilter) toolbar.insertBefore(sourceFilter, statusFilter);
    renderItems(); root.append(toolbar,
      element("p", { className: "extension-help", text: label(kind === "tools" && state.view === "all" ? "allAgentToolsHelp" : builtinView ? "builtinToolsHelp" : `${kind}Help`, ({
        agents: "默认 Agent 可直接编辑。提示词留空使用系统默认规则；C 代码仅用于高级扩展。",
        tools: builtinView ? "内建工具由 mdo 维护。能否调用取决于登录、功能设置和当前任务权限。" : "C 文件保存在 mdo-home/tools。保存后自动编译注册；C 代码在本机进程执行，请审阅来源。",
        subagents: "独立委派任务；模型默认跟随主 Agent。子任务仍受父任务权限限制。",
        skills: "按需读取 SKILL.md 和参考文件；安装 Skill 不会自动执行脚本。",
        mcp: "保存配置不会启动程序。测试连接或模型调用时才建立连接；远程服务使用 HTTPS，本机服务也可使用 HTTP。",
        knowledge: "整理好的文档目录。启用后可将检索提示注入 Agent，{$path} 自动指向本机目录；文档按需读取，不会整体加入上下文。",
        commands: "在输入框键入 / 执行独立 C 命令。参数通过 xvalue 数组传入；命令在隔离进程中运行。",
      })[kind]) }),
      state.error ? element("p", { className: "extension-error", attrs: { role: "alert" }, text: state.error }) : "", list);
  }
  function dirty() {
    if (!editing) return false;
    try { return editing.saving || editing.manifest.value!==editing.manifestBaseline ||
      editing.id.value !== editing.originalId || currentSource() !== editing.baseline; }
    catch { return true; }
  }
  function closeEditor(force = false) {
    if (!editing || (!force && editing.saving)) return false;
    if (!force && dirty() && !window.confirm(label("discardConfirm", "放弃未保存的编辑？"))) return false;
    reads.cancel("tool-picker");
    const state = editing; editing = null;
    state.layer.dispose({ restoreFocus: true }); return true;
  }
  function fieldValues() {
    return Object.fromEntries([...editing.form.elements].filter(node => node.name).map(node => [node.name,
      node.type === "checkbox" ? node.checked : node.value]));
  }
  function toolPicker(value, schema) {
    const editor = editing, target = targetKey();
    const raw = Array.isArray(value) ? value : String(value || "").replace(/^\[|\]$/g, "").replace(/["']/g, "").split(/[,\s]+/).filter(Boolean);
    const aliases = { Read: "read", Grep: "grep", Glob: "glob", Bash: "exec", Edit: "edit", Write: "write", WebSearch: "search", WebFetch: "open", Skill: "skill", poll: "status", task_status: "status", ask_user: "ask", web_search: "search", web_open: "open", web_find: "find", tool_search: "tools", tool_load: "load", mcp_read: "fetch" };
    const special = ["inherit","all","none"].includes(value);
    const chosen = new Set((special?[]:raw).map(id => Object.hasOwn(aliases,id) ? aliases[id] : id));
    const group = element("fieldset", { className: "agent-tool-picker" }, [element("legend", { text: label("tools", "工具") })]);
    const inherit = element("input", { attrs: { type: "checkbox" } }); inherit.checked = value === "inherit" || value === "all" || value !== "none" && !schema && chosen.size === 0 || value == null;
    const filter = element("input", { attrs: { type: "search", placeholder: label("filterTools", "筛选工具"), "aria-label": label("filterTools", "筛选工具") } });
    const list = element("div", { className: "agent-tool-list" });
    const status = element("small", { attrs: { role: "status" } });
    group.append(element("label", { className: "extension-check" }, [inherit, element("span", { text: label("inheritTools", "继承全部可用工具") })]),
      element("small", { text: label("toolPolicyHint", "勾选范围会与登录状态、功能设置、任务权限取交集；子 Agent 也受父任务限制。") }), filter, list, status);
    editor.inheritTools = inherit; editor.toolList = list; editor.getSelectedTools = () => [...chosen]; editor.form.append(group);
    const render = () => {
      const needle = filter.value.toLocaleLowerCase(); clear(list);
      // MCP callable names are discovered lazily and can disappear on restart.
      // Select tools/load here; each server owns its tool allowlist.
      const catalog = (editor.toolCatalog || []).filter(tool => tool.source === "builtin" || tool.source === "c");
      for (const id of chosen) if (!catalog.some(item => item.id === id)) catalog.push({ id, description: "", availability: "missing" });
      for (const tool of catalog) {
        if (!`${tool.id} ${tool.description}`.toLocaleLowerCase().includes(needle)) continue;
        const check = element("input", { attrs: { type: "checkbox", value: tool.id } });
        check.checked = chosen.has(tool.id); check.disabled = editor.saving || inherit.checked;
        check.addEventListener("change", () => { if (check.checked) chosen.add(tool.id); else chosen.delete(tool.id); });
        list.append(element("label", { className: "agent-tool-option", attrs: { title: tool.description } }, [check,
          element("code", { text: tool.id }), tool.member_only ? element("span", { className: "tool-member-badge", text: label("member", "会员") }) : null,
          element("small", { text: availability(tool) })]));
      }
    };
    inherit.addEventListener("change", render); filter.addEventListener("input", render); render();
    if (editor.toolCatalog) return;
    editor.loadTools = () => {
      status.textContent = label("loading", "正在载入…");
      void reads.read("tool-picker", "/tools", { isCurrent: () =>
        current(editor.kind, target) && editing === editor && editor.toolList === list }).then(response => {
        if (editing !== editor || target !== targetKey() || editor.toolList !== list) return;
        editor.toolCatalog = response.data.items || []; status.textContent = ""; render();
      }).catch(error => { if (current(editor.kind, target) && editing === editor && editor.toolList === list && error?.name !== "AbortError") status.textContent = errorMessage(error); });
    };
    editor.loadTools();
  }
  function currentSource() {
    if (editing.sourceMode) return editing.source.value;
    const values = fieldValues();
    const fields = { name: values.name, description: values.description, prompt: values.prompt };
    if (editing.kind === "commands") { delete fields.name; fields["argument-hint"] = values.argument_hint; }
    if (["agents", "subagents"].includes(editing.kind)) {
      fields.model = values.model || "inherit"; fields.reasoning_effort = values.reasoning_effort; fields.thoughtLevel = "";
      fields.read_only = values.read_only; fields.allow_delegation = values.allow_delegation;
      fields.schema = "mdo.agent.v1"; fields.extends = values.extends; fields.mode = values.mode;
      fields.permission_profile = values.permission_profile; fields.prompt_mode = values.prompt_mode || "append";
      fields.allow_background = values.allow_background; fields.require_verification_after_write = values.require_verification_after_write;
      // Hidden search results remain selected. Filtering changes visibility,
      // never the persisted allowlist.
      fields.tools = editing.inheritTools.checked ? "inherit" : editing.getSelectedTools();
      fields.code = values.code;
      const advanced = parseRoleAdvanced(values.role_options);
      for (const key of ROLE_ADVANCED_KEYS) fields[key] = advanced[key] ?? undefined;
    }
    return editPrompt(editing.source.value, fields);
  }
  function fillForm(source) {
    clear(editing.form);
    const kind = editing.kind;
    editing.syncTransport = null;
    const required = name => requiredExtensionFields(kind).includes(name);
    const values = promptFields(source);
    if (kind !== "commands") inputField(editing.form, "name", label("name", "名称"), values.name || "", { required: required("name") });
    inputField(editing.form, "description", label("description", "说明"), values.description || "", { required: required("description") });
    if (["agents", "subagents"].includes(kind)) {
      inputField(editing.form,"extends","继承角色（可选）",values.extends || "",{hint:"mdo.general、agent.<id> 或 subagent.<id>；对象逐项合并，数组整体替换。"});
      inputField(editing.form,"mode","运行模式",values.mode || (kind === "agents"?"primary":"subagent"),{hint:"primary、subagent 或 both；主角色与子角色使用同一套开发接口。"});
      inputField(editing.form, "model", label("model", "模型"), values.model || "inherit", { hint: label("modelHint", "inherit 跟随主 Agent；或填写模型 ID。") });
      inputField(editing.form, "reasoning_effort", label("effort", "思考强度（可选）"), values.reasoning_effort || values.thoughtLevel || "");
      toolPicker(values.tools, values.schema);
      inputField(editing.form, "code", "结合 C 角色程序", values.code === true || values.code === "true", { type: "checkbox", hint: `在本角色目录添加 C 程序并通过根清单 native 字段指定，使用 mdo/agent.h。支持上下文、模型请求、响应、工具、委派回调和完整驱动；下面的配置继续生效。` });
      inputField(editing.form,"permission_profile","权限配置（可选）",values.permission_profile || "",{hint:"inherit、read-only、balanced 或 full-access；始终受用户当前授权限制。"});
      inputField(editing.form,"prompt_mode","与继承/C 基础提示词组合",values.prompt_mode || "append",{hint:"append 追加，replace 替换。"});
      inputField(editing.form, "read_only", label("readOnly", "只读任务"), values.read_only === true || values.read_only === "true", { type: "checkbox" });
      inputField(editing.form, "allow_delegation", label("delegation", "允许再次委派"), values.allow_delegation === true || values.allow_delegation === "true", { type: "checkbox" });
      inputField(editing.form,"allow_background","允许后台委派",values.allow_background !== false && values.allow_background !== "false",{type:"checkbox"});
      inputField(editing.form,"require_verification_after_write","修改后要求验证",values.require_verification_after_write === true || values.require_verification_after_write === "true",{type:"checkbox"});
      inputField(editing.form,"role_options","高级配置（JSON）",JSON.stringify(roleAdvanced(source),null,2),{area:true,hint:"context：各来源的启停、筛选、顺序、角色、模板、字节上限及刷新；runtime：执行与模型额度；delegation：可委派角色及上下文；history：历史与图片策略；model_options：采样；mcp_servers、skills：能力选择。取消继承全部工具后不选择任何工具，表示禁用所有工具。"});
    }
    if (kind === "commands") inputField(editing.form, "argument_hint", label("argumentHint", "参数提示（可选）"), values["argument-hint"] || "");
    inputField(editing.form, "prompt", label(kind === "agents" ? "agentInstructions" : "instructions", kind === "agents" ? "系统提示词（留空使用默认规则）" : "指令"), values.prompt || "", { area: true, required: required("prompt") });
  }
  function openEditor(kind, item = null, imported = null) {
    if(item&&!customizableResource(item)){toast(storeReadOnlyMessage(),"error");return;}
    if(item&&["agents","subagents"].includes(kind)&&/\.c$/i.test(item.manifest?.entry||"")){void directoryEditor.open({kind,item});return;}
    if(kind==="mcp"||kind==="knowledge"){
      const text=kind==="mcp"?JSON.stringify(newMcpTemplate(),null,2):JSON.stringify({format:"mdo.knowledge.v1",name:"my-knowledge",description:"知识库",index:"index.md",prompt:"涉及相关内容时参考 {$path}，index.md 是索引。",inject_prompt:true},null,2);
      void directoryEditor.open({kind,item,manifest:item?.manifest||directoryManifest(kind,kind==="mcp"?"my-server":"my-knowledge"),text,edits:kind==="knowledge"?[{path:"index.md",text:"# 知识库索引\n"}]:[]});return;
    }
    if(item&&["tools","skills","commands","knowledge"].includes(kind)&&!customizableResource(item)){toast(storeReadOnlyMessage(),"error");return;}
    reads.cancel("editor");
    if (editing && !closeEditor()) return;
    const initial = imported?.content ?? item?.content ?? (kind === "commands" ? newCommand() : kind === "tools" ? newTool() : ["agents","subagents"].includes(kind) ? newRole(kind) :
      editPrompt("", { name: kind === "commands" ? undefined : "", description: "", prompt: "" }));
    let editor;
    const layer = openLayer({ title: `${item ? label("edit", "编辑") : label("create", "新建")} ${title(kind)}`,
      icon: kind === "skills" ? "spark" : "terminal", width: "48rem", height: "80%", owner: roots.get(kind),
      beforeClose: () => editing !== editor || !editor.saving && (!dirty() || window.confirm(label("discardConfirm", "放弃未保存的编辑？"))),
      onClosed() { if (editing === editor) { reads.cancel("tool-picker"); editing = null; } } });
    const dialog = layer.element; dialog.classList.add("extension-authoring");
    const id = element("input", { attrs: { name: "extension_id", maxlength: 96, autocomplete: "off", spellcheck: "false", required: "", placeholder: "my-resource", "data-field-label": label("id", "文件 ID") } });
    id.value = imported?.id ?? item?.id ?? ""; id.disabled = Boolean(item);
    const manifest=element("textarea",{attrs:{"aria-label":"extension.json",maxlength:"65536",rows:"8"}});
    manifest.value=JSON.stringify(item?.manifest||directoryManifest(kind,id.value||"my-extension"),null,2);
    const manifestBaseline=manifest.value;
    const form = element("form", { className: "extension-form", attrs: { novalidate: "" } });
    const source = element("textarea", { className: "extension-source", attrs: { spellcheck: "false", "aria-label": label("source", "源文件"), "data-field-label": label("source", "源文件"), maxlength: 131072 } });
    source.value = initial; source.hidden = true;
    const status = element("p", { className: "extension-editor-status", attrs: { role: "status" } });
    const formMode = button(label("form", "表单"), () => changeMode(false));
    const sourceMode = button(label("source", "源文件"), () => changeMode(true));
    const save = button(label("save", "保存"), saveEditor, true);
    const cancel = button(label("cancel", "取消"), () => closeEditor());
    editing = editor = { kind, item, dialog, layer, target: targetKey(), id, source, form, status, save, cancel, formMode,
      baseline: initial, originalId: id.value, sourceMode: ["tools", "commands"].includes(kind), toolCatalog: null, saving: false, manifest,manifestBaseline };
    try { if (kind !== "tools") fillForm(initial); }
    catch { editing.sourceMode = true; source.hidden = false; form.hidden = true; }
    if (["tools", "commands"].includes(kind)) {
      source.hidden = false; form.hidden = true; formMode.hidden = true; sourceMode.hidden = true;
      if (!item && !imported) id.addEventListener("input", () => {
        if (source.value === editing.baseline && portableId(id.value)) { source.value = kind === "commands" ? newCommand(id.value) : newTool(id.value); editing.baseline = source.value; }
      });
    }
    // Imported files should keep their literal source until the user chooses a form.
    if (imported) { editing.sourceMode = true; source.hidden = false; form.hidden = true; }
    if (!imported && !editing.sourceMode) editing.baseline = currentSource();
    layer.body.append(element("label", { className: "extension-field" }, [element("span", { text: `${label("id", "文件 ID")} · ${label("required", "必填")}` }), id,
      element("small", { text: label("idHint", "小写字母、数字、点、短横线或下划线；创建后不改名。") })]),
      element("div", { className: "extension-toolbar" }, [formMode, sourceMode]), form, source,
      imported?.files?.length ? element("p", { text: label("fileCount", "包含 {count} 个附属文件", { count: imported.files.length }) }) : "",
      element("label",{className:"extension-field"},[element("span",{text:"根清单：名称、别名、入口、平台、作者、版权和许可"}),manifest]),status);
    layer.footer.append(save, cancel);
    if(["agents","subagents"].includes(kind))layer.footer.prepend(button("上下文预览",()=>{
      try{if(!directoryId(id.value.trim()))throw Error("目录名称无效");void openRolePlan({kind,id:id.value.trim(),content:currentSource(),owner:dialog});}
      catch(error){showEditorError(editor,error);}
    }));
    if(["agents","subagents"].includes(kind))layer.body.append(button("添加 C 角色程序",()=>{
      try{
        const meta=JSON.parse(manifest.value);if(!portableId(meta.alias))throw Error("请先填写有效调用别名");
        editor.nativeText=roleProgramTemplate(kind,meta.alias);meta.native=meta.native||"agent.c";manifest.value=JSON.stringify(meta,null,2);
        editor.status.textContent="保存时将 C 程序与配置一起写入本角色目录；之后可通过目录与文件窗口继续编辑。";
      }catch(error){showEditorError(editor,error);}
    }));
    form.addEventListener("submit", event => { event.preventDefault(); void saveEditor(); });
    const clearValidation = () => {
      if (editing?.dialog === dialog) clearEditorValidation(editing);
    };
    dialog.addEventListener("input", clearValidation); dialog.addEventListener("change", clearValidation);
    (id.disabled ? form.querySelector("input") || source : id).focus();
  }
  function changeMode(sourceMode) {
    if (!editing || editing.saving || editing.sourceMode === sourceMode) return;
    try {
      const current = currentSource();
      if (!sourceMode) fillForm(current);
      editing.source.value = current; editing.sourceMode = sourceMode;
      editing.form.hidden = sourceMode; editing.source.hidden = !sourceMode; editing.status.textContent = "";
      clearEditorValidation(editing);
      (sourceMode ? editing.source : editing.form.querySelector("input,textarea"))?.focus();
    } catch (error) { showEditorError(editing, error); }
  }
  function clearEditorValidation(editor) {
    editor.dialog.querySelectorAll('[aria-invalid="true"]').forEach(node => node.removeAttribute("aria-invalid"));
    editor.dialog.querySelectorAll('[aria-describedby="extension-field-error"]').forEach(node => node.removeAttribute("aria-describedby"));
    editor.dialog.querySelector("#extension-field-error")?.remove();
    if (editor.validationError) { editor.validationError = false; editor.status.textContent = ""; }
  }
  function showEditorError(editor, error) {
    clearEditorValidation(editor);
    if (!error?.extensionValidation) { editor.status.textContent = errorMessage(error); return; }
    const control = error.field === "source" || editor.sourceMode && error.field !== "extension_id" ? editor.source :
      error.field === "extension_id" ? editor.id : editor.form.elements.namedItem(error.field);
    editor.validationError = true;
    const message = label(error.extensionValidation, undefined, { ...error.args, field: control?.dataset.fieldLabel || error.field });
    editor.status.textContent = control ? "" : message;
    if (control) {
      const hint = element("small", { className: "extension-field-error", text: message, attrs: { id: "extension-field-error", role: "alert" } });
      control.insertAdjacentElement("afterend", hint); control.setAttribute("aria-describedby", "extension-field-error");
    }
    control?.setAttribute("aria-invalid", "true"); control?.focus();
  }
  async function saveEditor() {
    const editor = editing;
    if (!editor || editor.saving || editor.layer.signal.aborted || editor.target !== targetKey()) return;
    try {
      const id = editor.id.value.trim();
      if(!directoryId(id))throw extensionFieldError("idInvalid","extension_id");
      if (!editor.sourceMode) validateExtensionForm(editor.kind, fieldValues());
      if (["tools", "commands"].includes(editor.kind) && !editor.source.value.trim()) throw extensionFieldError("fieldRequired", "source");
      let content=currentSource();const manifest=JSON.parse(editor.manifest.value);
      if(!editor.item&&editor.manifest.value===editor.manifestBaseline){manifest.name=promptFields(content).name||id;manifest.alias=id;manifest.description=promptFields(content).description||"";}
      if(editor.kind==="skills"&&!editor.sourceMode)content=editPrompt(content,{name:manifest.alias});
      if(["agents","subagents"].includes(editor.kind)&&!editor.sourceMode){
        if(fieldValues().code&&!manifest.native){manifest.native="agent.c";editor.nativeText=roleProgramTemplate(editor.kind,manifest.alias);}
        if(!fieldValues().code)delete manifest.native;
      }
      const edits=[{path:manifest.entry,text:content}];
      if(editor.nativeText&&manifest.native)edits.push({path:manifest.native,text:editor.nativeText});
      editor.saving = true; editor.dialog.querySelectorAll("input,textarea,select,button").forEach(node => { node.disabled = true; });
      editor.status.textContent = label("saving", "正在保存…");
      const result=await saveDirectory(editor.kind,id,manifest,edits,editor.item?.revision||"",{signal:editor.layer.signal});
      if (editing !== editor || editor.layer.signal.aborted || editor.target !== targetKey()) return;
      editor.saving = false; closeEditor(true); announceChange(editor.kind); await load(editor.kind);
      toast(result.runtime_error?`文件已保存，运行时需要处理：${result.runtime_error}`:editor.item?"目录已保存":"目录已保存，请启用后使用", "success");
    } catch (error) {
      if (editing !== editor || editor.layer.signal.aborted || editor.target !== targetKey()) return;
      editor.saving = false; editor.dialog.querySelectorAll("input,textarea,select,button").forEach(node => { node.disabled = false; }); editor.id.disabled = Boolean(editor.item);
      if (editor.inheritTools?.checked) editor.toolList.querySelectorAll("input").forEach(node => { node.disabled = true; });
      editor.syncTransport?.();
      if (error?.status === 412) editor.status.textContent = label("conflict", "文件已被其他操作修改。请复制当前内容，重新载入后再保存。");
      else showEditorError(editor, error);
    }
  }
  async function importFile(kind,file){if(!file)return;try{await stores.get(kind).importPackage(file);}catch(e){toast(errorMessage(e),"error");}}
  async function exportItem(kind,item){try{await exportDirectoryZip(kind,item.id);}catch(e){toast(errorMessage(e),"error");}}
  const unsubscribeLocale = subscribeLocale(() => { for (const kind of EXTENSION_KINDS) render(kind); });
  const invalidate = event => { const state = states.get(event.detail?.kind);if (state) state.loaded = false; };
  window.addEventListener("mdo-extensions-changed", invalidate);
  const openResource=event=>{const ref=event.detail;if(!EXTENSION_KINDS.includes(ref?.kind)||!directoryId(ref?.id))return;pendingResource=ref;if(stores.has(ref.kind))states.get(ref.kind).area="local";if(ref.kind==="tools")states.get("tools").view="custom";if(selected===ref.kind)void openLinkedResource();};
  window.addEventListener("mdo-open-resource",openResource);
  const unsubscribeTarget = subscribeTarget(() => {
    if (activeTarget === targetKey()) return;
    cancelReads();
    activeTarget = targetKey(); closeEditor(true); for (const browser of browsers.values()) browser.reset();
    pendingResource=null;
    for (const state of states.values()) { ++state.serial; state.loaded = false; state.busy = false; state.items = []; state.catalog = []; state.error = ""; state.operation = null; }
    if (selected) void load(selected);
  });
  const onHide = () => { paused = true; cancelReads(); };
  const onShow = () => {
    if (!paused || disposed) return;
    paused = false; if (selected) void load(selected);
    if (editing && !editing.toolCatalog) editing.loadTools?.();
  };
  window.addEventListener("pagehide", onHide);
  window.addEventListener("pageshow", onShow);
  return Object.freeze({
    selectSection(kind) {
      const next = EXTENSION_KINDS.includes(kind) ? kind : "";
      if (selected !== next) { cancelReads(); for (const [id, browser] of browsers) if (id !== next) browser.closeDetail(false); if(pendingResource?.kind!==next)pendingResource=null; }
      for (const [kind, store] of stores) store.activate(kind === next && states.get(kind).area === "store");
      selected = next;
      if (selected && (!states.get(kind).loaded || kind === "tools") && !states.get(kind).busy) void load(kind);
      else if(pendingResource?.kind===selected)void openLinkedResource();
    },
    hasPendingChanges: () => [...stores.values()].some(s => s.hasPendingChanges()) || dirty() || directoryEditor.hasPendingChanges() || skillWorkbench.hasPendingChanges() || mcpConnections.hasPendingChanges() || commandWorkbench.hasPendingChanges(),
    destroy() { disposed = true; for (const browser of browsers.values()) browser.destroy(); for (const store of stores.values()) store.destroy(); directoryEditor.destroy(); skillWorkbench.destroy(); mcpConnections.destroy(); commandWorkbench.destroy(); mcpAuthorization.destroy(); mcpContext.destroy(); cancelReads(); closeEditor(true); unsubscribeLocale(); unsubscribeTarget();window.removeEventListener("mdo-extensions-changed", invalidate);window.removeEventListener("mdo-open-resource",openResource);window.removeEventListener("pagehide",onHide);window.removeEventListener("pageshow",onShow); },
  });
}
