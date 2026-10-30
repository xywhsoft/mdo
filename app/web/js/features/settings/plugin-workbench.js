import { currentLocale, t as uiText, uiMessage, subscribeLocale } from "../../i18n.js";

import { localDependencyPanel } from "./extension-dependencies.js";
import { api } from "../../api/client.js";
import { createApplicationClient } from "../../api/application.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { element, errorMessage, toast } from "../../utils/dom.js";
import { saveBlobFile } from "../../utils/file-download.js";
import { openLayer } from "../../components/layers.js";
import { createExtensionBrowser, extensionToggle, actionMenu, detailFacts, detailTabs } from "../../components/extension-browser.js";
import { createSingleResourceStore } from "./single-resource-store.js";
import { createDirectoryEditor } from "./directory-editor.js";
import { directoryManifest, changeDirectory } from "./directory-documents.js";
import { resourceCredits } from "./resource-credits.js";

const node = (tag, text = "", className = "") => element(tag, { text, className });
const button = (text, run, primary = false) => { const b = element("button", { text, className: primary ? "primary-button" : "secondary-button", attrs: { type: "button" } }); b.addEventListener("click", () => { Promise.resolve().then(run).catch(e => { if(e?.name!=="AbortError") toast(errorMessage(e),"error"); }); }); return b; };
const target = () => targetState().selected?.id || "local";
const statusText = { get enabled() { return uiText("schedule.enabled", {}, "已启用"); }, get disabled() { return uiText("ecosystem.disabled", {}, "已停用"); }, get failed() { return uiText("ui.pluginWorkbench.loadFailed", {}, "加载失败"); }, get recovery() { return uiText("ui.pluginWorkbench.recoveryMode", {}, "恢复模式"); }, get pending() { return uiText("ui.pluginWorkbench.pendingActivation", {}, "等待生效"); } };
const slotName = key => ({ "mdo.agent.default-orchestrator": uiText("ui.pluginWorkbench.defaultAgentOrchestration", {}, "默认 Agent 编排"), "mdo.ui.chat-layout": uiText("ui.pluginWorkbench.conversationLayoutOverride", {}, "对话布局接管") })[key] || key;
const defaultScript = "export function activate(sdk) {\n  // 使用 sdk.ui 注册面板、菜单和快捷键；可以返回清理函数。\n  return () => {};\n}\n";
const nativeScript = id => `#include "mdo/sdk.h"\n#ifndef MDO_PLUGIN_ID\n#define MDO_PLUGIN_ID "plugin.${id}"\n#define MDO_PLUGIN_VERSION "1.0.0"\n#endif\nstatic mdo_result Register(const mdo_host_services_v1* H,const mdo_registrar_v1* R,void** D,char* E,size_t N) {\n (void)H;(void)R;(void)D;(void)E;(void)N;\n return MDO_RESULT_OK;\n}\nstatic const mdo_module_v1 M={MDO_V1_HEADER(mdo_module_v1),MDO_PLUGIN_ID,"My plugin","Plugin entry",MDO_PLUGIN_VERSION,MDO_CAPABILITY_APPLICATION,NULL,0,Register,NULL};\nconst mdo_module_v1* mdoModuleEntry(void){return &M;}\n`;

export function createPluginWorkbench(root) {
  root.classList.add("plugin-workbench");
  const sdk = createApplicationClient(() => ({ extension: "mdo.plugin-manager" }));
  const browser = createExtensionBrowser({ kind: "plugins" }), layers = new Set();
  let active = false, disposed = false, owner = target(), epoch = 0, controller = null, area = "local", query = "", filter = "all", busy = false;
  let data = { items: [] }, receipts = {}, ownershipKnown = false, error = "", editor = null;
  const current = own => !disposed && own === epoch && owner === target();
  const call = async (method, params = {}, own = epoch) => { if (!current(own)) throw new DOMException("Device changed", "AbortError"); const result = await sdk.call(method, params); if (!current(own)) throw new DOMException("Device changed", "AbortError"); return result; };
  const changed = () => { window.dispatchEvent(new CustomEvent("mdo-plugins-changed")); if (active) void load(); };
  const store = createSingleResourceStore("plugins", changed, () => { area = "store"; render(); }, async () => { area = "local"; await load(); });
  const receipt = item => receipts[`plugins:${item.directory_id||item.id}`]||null;
  const editable = item => ownershipKnown && item.source === "manual";
  const directoryEditor=createDirectoryEditor({owner:root,onChange:changed});
  const showError = e => { if (e.name !== "AbortError") toast(errorMessage(e), "error"); };
  const layer = options => {
    const own = epoch; let view;
    view = openLayer({ icon: "settings", width: "56rem", height: "80%", owner: root, ...options,
      onClosed() { layers.delete(view); options.onClosed?.(); } });
    view.body.classList.add("extension-authoring");
    layers.add(view); view.isCurrent = () => current(own) && !view.signal.aborted; return view;
  };
  async function load() {
    if (!active || disposed) return;
    const own = epoch; controller?.abort(); controller = new AbortController(); const signal = controller.signal;
    try {
      const result = await call("plugin.list", {}, own);
      const local = await api.get("/ecosystem", { signal });
      if (!current(own) || signal.aborted) return;
      data = result; receipts = local.data; ownershipKnown = true; error = "";
    } catch (e) { if (current(own) && e.name !== "AbortError") { ownershipKnown = false; error = errorMessage(e); } }
    if (current(own)) render();
  }
  async function inspectFile(item,file){
    const d=layer({title:file.path,width:"64rem"});d.footer.append(button(uiMessage("context.close", {}, "关闭"),()=>d.close()));
    try{const data=(await api.get(`/extensions/plugins/${encodeURIComponent(item.id)}/file?path=${encodeURIComponent(file.path)}`,{signal:d.signal})).data;if(d.isCurrent())d.body.append(node("pre",data.binary?uiMessage("ui.pluginWorkbench.binaryFileOrPreviewLimitExceededExportTheFull", {}, "二进制或超过预览上限，请导出完整 ZIP 查看。"):data.text,"store-source"));}catch(e){if(d.isCurrent())d.body.append(node("p",errorMessage(e),"extension-error"));}
  }
  async function details(item) {
    const own = epoch, id = item.id;
    try {
      const full = await call("plugin.get", { id }, own); if (!browser.isSelected(id) || !current(own)) return;
      const spec = full.manifest, credits = resourceCredits({ ...item, ...spec, external: true, store_managed: !editable(item) }, receipt(item));
      const description = element("div", {}, [node("p", spec?.description || uiMessage("ui.pluginWorkbench.legacyPluginFormat", {}, "旧格式插件")), credits.facts(), detailFacts([[uiMessage("task.meta.state", {}, "状态"), statusText[item.state] || item.state], [uiMessage("ui.pluginWorkbench.currentRevision", {}, "当前修订"), item.revision], [uiMessage("ui.pluginWorkbench.activeRevision", {}, "实际生效修订"), item.effective_revision || uiMessage("ui.pluginWorkbench.notEnabled", {}, "未启用")], [uiMessage("ui.pluginWorkbench.frontendEntry", {}, "前端入口"), spec?.entry?.frontend || full.frontend || uiMessage("settings.reasoningNone", {}, "无")], [uiMessage("ui.pluginWorkbench.nativeEntry", {}, "原生入口"), spec?.entry?.native || uiMessage("settings.reasoningNone", {}, "无")]])]);
      const files = node("div");
      for (const f of full.files || []) { const targets = spec?.overrides?.filter(r => r.source === f.path).map(r => r.target) || []; files.append(element("div", { className: "resource-actions" }, [button(f.path, () => inspectFile(full, f)), node("small", uiMessage("ui.filesView.value1BytesValue2", { value1: f.bytes.toLocaleString(currentLocale()), value2: targets.length ? ` → ${targets.join(" / ")}` : uiMessage("ui.pluginWorkbench.pluginPrivateFile", {}, " · 插件私有文件") }, "{value1} 字节{value2}"))])); }
      const relations = localDependencyPanel("plugins",full);
      browser.showDetail(id,detailTabs([{id:"description",label:uiMessage("backupImport.notice", {}, "说明"),content:description},{id:"files",label:uiMessage("ui.pluginWorkbench.filesAndEntries", {}, "文件与入口"),content:files},{id:"relations",label:uiMessage("ui.pluginWorkbench.dependenciesAndConflicts", {}, "依赖与冲突"),content:relations}]));
    } catch (e) { if (current(own) && browser.isSelected(id)) browser.showDetail(id, node("p", errorMessage(e), "extension-error")); }
  }
  async function toggle(item){
    if(busy)return;busy=true;
    try{
      const result=await changeDirectory("plugins",item,"enable",{enabled:!item.enabled});if(!result)return;changed();
      toast(result.restart_required?uiMessage("ui.pluginWorkbench.pluginSelectionSavedRestartMdoToApplyIt", {}, "插件选择已保存，重启 mdo 后生效"):result.pending?uiMessage("ui.pluginWorkbench.waitingForTheOldPluginToFinishCleanup", {}, "等待旧插件完成清理"):uiMessage("ui.pluginWorkbench.pluginSelectionApplied", {}, "插件选择已生效"));
    }catch(e){showError(e);}finally{busy=false;}
  }
  async function review(initial, title = uiText("ui.pluginWorkbench.changePluginCombination", {}, "更改插件组合"), prepared = null) {
    let changes = structuredClone(initial), saving = false;
    const d = layer({ title, beforeClose: () => !saving });
    const close = button(uiMessage("sidebar.cancel", {}, "取消"), () => d.close("cancel")), confirm = button(uiMessage("ui.pluginWorkbench.applyChanges", {}, "应用更改"), submit, true); d.footer.append(confirm, close);
    let plan;
    async function refresh(value = null) {
      confirm.disabled = true; d.body.replaceChildren(node("p", uiMessage("ui.pluginWorkbench.checkingCombination", {}, "正在检查组合…")));
      try {
        plan = value || await call("plugin.plan", { changes }); if (!d.isCurrent()) return;
        d.body.replaceChildren();
        for (const change of changes) { const item = data.items.find(r => r.id === change.id); d.body.append(node("p", `${item?.manifest?.name || change.id}：${change.remove ? uiMessage("store.uninstall", {}, "卸载") : change.revision ? uiMessage("ui.pluginWorkbench.switchToRevisionValue1", { value1: change.revision.slice(0, 12) }, "切换修订 {value1}") : change.enabled ? uiMessage("resource.enable", {}, "启用") : uiMessage("resource.disable", {}, "停用")}`)); }
        for (const issue of plan.issues || []) {
          const row = element("section", { className: "warning-entry" }, [node("p", `${issue.id} ${issue.other}：${issue.code === "exclusive" ? uiMessage("ui.pluginWorkbench.cannotOverrideValue1AtTheSameTime", { value1: slotName(issue.detail) }, "不能同时接管 {value1}") : issue.detail}`, "extension-error")]);
          const set = patch => { const old = changes.find(r => r.id === patch.id); if (old) Object.assign(old, patch); else changes.push(patch); void refresh(); };
          if (["exclusive", "dependency_conflict"].includes(issue.code)) for (const id of [issue.id, issue.other]) row.append(button(uiMessage("ui.pluginWorkbench.disableValue1", { value1: id }, "停用 {value1}"), () => set({ id, enabled: false })));
          if (["dependency_disabled","dependency_missing","dependency_blocked"].includes(issue.code)) { row.append(button(uiMessage("ui.pluginWorkbench.alsoDisableValue1", { value1: issue.id }, "同时停用 {value1}"),()=>set({id:issue.id,enabled:false})));const dependency = data.items.find(r => r.id === issue.other); if (dependency && !dependency.enabled) row.append(button(uiMessage("ui.pluginWorkbench.alsoEnableValue1", { value1: issue.other }, "同时启用 {value1}"), () => set({ id: issue.other, enabled: true }))); else row.append(node("p", uiMessage("ui.pluginWorkbench.installACompatibleVersionFromThePluginStoreFirst", {}, "请先在插件商店安装符合要求的版本。"))); }
          d.body.append(row);
        }
        d.body.append(node("p", uiMessage("ui.pluginWorkbench.codeOrResourceDirectoryChangesRequireARestartReplacement", {}, "代码或资源目录变更需要重启。仍在运行的窗口或代码退出前，替代插件不会启动。"), "extension-help")); confirm.disabled = !plan.allowed;
      } catch (e) { if (d.isCurrent()) d.body.replaceChildren(node("p", errorMessage(e), "extension-error")); }
    }
    async function submit() {
      if (!plan?.allowed || saving || !d.isCurrent()) return; saving = true; confirm.disabled = close.disabled = d.closeButton.disabled = true;
      try { const result = await call("plugin.commit", { changes, expected_revision: plan.base_revision, expected_plan: plan.plan_revision }); if (!d.isCurrent()) return; saving = false; d.dispose(); changed(); toast(result.restart_required ? uiMessage("ui.pluginWorkbench.combinationSavedRestartMdoToApplyIt", {}, "组合已保存，重启 mdo 后生效") : result.pending ? uiMessage("ui.pluginWorkbench.waitingForTheOldPluginToFinishCleanup", {}, "等待旧插件完成清理") : uiMessage("ui.pluginWorkbench.pluginCombinationApplied", {}, "插件组合已生效")); }
      catch (e) { if (d.isCurrent()) { d.body.append(node("p", errorMessage(e), "extension-error")); plan = null; } }
      finally { saving = false; if (d.isCurrent()) { close.disabled = d.closeButton.disabled = false; confirm.disabled = !plan?.allowed; } }
    }
    await refresh(prepared);
  }
  function field(form, text, value = "", area = false) {
    const input = element(area ? "textarea" : "input", { attrs: { type: area ? null : "text", spellcheck: "false", autocomplete: "off" } }); input.value = value;
    form.append(element("label", { className: "extension-field" }, [node("span", text), input])); return input;
  }
  async function edit(item=null,native=false){
    if(item){if(!editable(item))throw Error(uiText("ui.pluginWorkbench.storePluginsCannotBeEditedOrSharedAgain", {}, "商店插件不支持编辑或再次分享"));await directoryEditor.open({kind:"plugins",item});return;}
    const entry=native?"runtime/plugin.c":"runtime/main.js",manifest=directoryManifest("plugins","my-plugin",{name:uiText("ui.pluginWorkbench.myPlugin", {}, "我的插件"),version:"1.0.0",license:"MIT",[native?"native":"frontend"]:entry});
    await directoryEditor.open({kind:"plugins",manifest,text:JSON.stringify({entry:{[native?"native":"frontend"]:entry},overrides:[],exclusive:[]},null,2),edits:[{path:entry,text:native?nativeScript("my-plugin"):defaultScript}]});
  }
  async function combinations() {
    const d = layer({ title: uiText("ui.pluginWorkbench.pluginCombinations", {}, "插件组合方案"), width: "48rem" });
    async function refresh() {
      try {
        const presets = await call("plugin.preset.list"); if (!d.isCurrent()) return; d.body.replaceChildren();
        const id = field(d.body, uiMessage("ui.pluginWorkbench.combinationId", {}, "组合 ID"), "my-combination"), name = field(d.body, uiMessage("ui.pluginWorkbench.combinationName", {}, "组合名称"), uiMessage("ui.pluginWorkbench.myCombination", {}, "我的组合"));
        d.body.append(button(uiMessage("ui.pluginWorkbench.saveCurrentPluginSelectionAsACombination", {}, "将当前插件选择保存为组合"), async () => { try { await call("plugin.preset.save", { id: id.value.trim(), name: name.value.trim(), expected_revision: presets.revision }); await refresh(); } catch (e) { showError(e); } }));
        for (const row of presets.items) d.body.append(element("section", { className: "warning-entry" }, [node("h4", `${row.name}${row.matches ? uiMessage("ui.pluginWorkbench.sameAsCurrent", {}, " · 与当前一致") : ""}`), node("p", uiMessage("ui.pluginWorkbench.value1PluginSelectionsAccountsCredentialsAndRuntimeDataAre", { value1: row.plugins.length }, "{value1} 个插件选择；不包含账号、凭据和运行数据。")), element("div", { className: "resource-actions" }, [
          button(uiMessage("ui.pluginWorkbench.apply", {}, "应用"), async () => { try { const p = await call("plugin.preset.plan", { id: row.id }); if (d.isCurrent()) await review(p.changes || [], uiText("ui.pluginWorkbench.applyCombinationValue1", { value1: row.name }, "应用组合：{value1}"), p); } catch (e) { showError(e); } }),
          button(uiMessage("ui.pluginWorkbench.updateToCurrentSelection", {}, "更新为当前选择"), async () => { try { await call("plugin.preset.save", { id: row.id, name: row.name, expected_revision: presets.revision }); await refresh(); } catch (e) { showError(e); } }),
          button(uiMessage("schedule.delete", {}, "删除"), async () => { if (!confirm(uiMessage("ui.pluginWorkbench.deleteCombinationValue1PluginsWillNotBeUninstalled", { value1: row.name }, "删除组合“{value1}”？不会卸载插件。"))) return; try { await call("plugin.preset.remove", { id: row.id, expected_revision: presets.revision }); await refresh(); } catch (e) { showError(e); } })]) ]));
      } catch (e) { if (d.isCurrent()) d.body.replaceChildren(node("p", errorMessage(e), "extension-error")); }
    }
    d.footer.append(button(uiMessage("resource.refresh", {}, "刷新"), refresh), button(uiMessage("context.close", {}, "关闭"), () => d.close("close"))); await refresh();
  }
  async function importFile(file){try{await store.importPackage(file);}catch(e){showError(e);}}
  function render() {
    if (!root || disposed) return;
    browser.rememberFocus(); const tabs = element("div", { className: "store-tabs", attrs: { role: "tablist" } });
    for (const [value, text] of [["local", uiText("ui.pluginWorkbench.localPlugins", {}, "本机插件")], ["store", uiText("ui.pluginWorkbench.pluginStore", {}, "插件商店")]]) { const b = button(text, () => { if (editor || store.hasPendingChanges()) return; area = value; store.activate(value === "store"); render(); if (value === "local") void load(); }); b.setAttribute("role", "tab"); b.setAttribute("aria-selected", String(area === value)); tabs.append(b); }
    root.replaceChildren(tabs, store.statusRoot); if (area === "store") { root.append(store.root); return; }
    const search = element("form", { className: "store-search eb-toolbar" }), input = element("input", { attrs: { type: "search", placeholder: uiMessage("ui.pluginWorkbench.searchPluginsAuthorsOrUses", {}, "搜索插件、作者或用途"), "aria-label": uiMessage("ui.pluginWorkbench.searchPlugins", {}, "搜索插件") } }); input.value = query;
    const select = element("select", { attrs: { "aria-label": uiMessage("ui.pluginWorkbench.pluginState", {}, "插件状态") } }); for (const [value, text] of [["all", uiText("extensionBrowser.all", {}, "全部状态")], ["enabled", uiText("schedule.enabled", {}, "已启用")], ["disabled", uiText("ecosystem.disabled", {}, "已停用")], ["pending", uiText("ui.pluginWorkbench.pendingActivation", {}, "等待生效")], ["failed", uiText("ui.pluginWorkbench.loadFailed", {}, "加载失败")],["recovery",uiText("ui.pluginWorkbench.recoveryMode", {}, "恢复模式")]]) select.append(element("option", { text, attrs: { value } })); select.value = filter; select.addEventListener("change", () => { filter = select.value; render(); });
    const picker = element("input", { attrs: { type: "file", accept: ".zip" } }); picker.hidden = true;
    picker.addEventListener("change", () => { if (picker.files[0]) void importFile(picker.files[0]); });
    search.append(input, select, button(uiMessage("store.searchButton", {}, "搜索"), () => { query = input.value.trim(); render(); }), button(uiMessage("resource.refresh", {}, "刷新"), load), button(uiMessage("ui.pluginWorkbench.combinations", {}, "组合方案"), combinations), actionMenu([button(uiMessage("ui.pluginWorkbench.newPlugin", {}, "新建插件"), () => edit()), button(uiMessage("ui.pluginWorkbench.newCPlugin", {}, "新建 C 插件"),()=>edit(null,true)), button(uiMessage("ui.pluginWorkbench.importInstallationFile", {}, "导入安装文件"), () => picker.click())], uiText("extensionBrowser.createImport", {}, "创建 / 导入")));
    search.addEventListener("submit", e => { e.preventDefault(); query = input.value.trim(); render(); });
    root.append(search, picker);
    if (error) root.append(node("p", error, "extension-error"));
    if(data.config_broken)root.append(node("p",uiMessage("ui.pluginWorkbench.pluginRegistryDamagedRepairOrRestoreConfigPluginsJson", {}, "插件登记文件损坏。请修复或恢复 config/plugins.json 后重启；原文件与插件目录均保留，当前不会覆盖它。"),"extension-error"));
    if (data.recovery_mode) root.append(node("p", uiMessage("ui.pluginWorkbench.pluginRecoveryModeIsActiveOverridesAndCodeEntries", {}, "插件恢复模式已开启，覆盖和代码入口均暂停。请修复配置后重新启动 mdo。"), "extension-error"));
    if(data.retiring?.length)root.append(node("p",uiMessage("ui.pluginWorkbench.revisionsStillWaitingToExitValue1", { value1: data.retiring.map(r=>(r.manifest?.name||r.id)+" "+r.revision.slice(0,12)).join("、") }, "仍在等待退出的修订：{value1}"),"extension-help"));
    if (data.pending) root.append(node("p", data.restart_required ? uiMessage("ui.pluginWorkbench.pluginSelectionSavedWithCodeOrResourceChangesRestart", {}, "插件选择已保存，包含代码或资源目录变更，请重启 mdo 生效。") : uiMessage("ui.pluginWorkbench.pluginSelectionSavedWaitingForOtherWindowsToClean", {}, "插件选择已保存，正在等待其他窗口清理旧插件；必要时刷新相关窗口。"), "extension-help"));
    const rows = data.items.filter(p => (filter === "all" || p.state === filter) && `${p.id} ${p.manifest?.name || ""} ${p.manifest?.description || ""} ${p.manifest?.author || ""}`.toLocaleLowerCase().includes(query.toLocaleLowerCase())).map(item => {
      const spec = item.manifest || {}, record = receipt(item), credits = resourceCredits({ ...item, ...spec, external: true, store_managed: item.source === "store" || record && record.source !== "local" }, record), actions = [];
      if (editable(item)) actions.push(button(uiMessage("timeline.edit", {}, "编辑"), () => edit(item)), button(uiMessage("ui.pluginWorkbench.shareToCreatorCenter", {}, "分享到创作中心"), () => store.publish({...item,name:item.name||spec.name||item.id},record)));
      actions.push(...store.localActions(item));
      actions.push(button(uiMessage("store.uninstall", {}, "卸载"),async()=>{if(!confirm(uiMessage("ui.pluginWorkbench.uninstallTheEntireDirectoryForValue1", { value1: item.name||spec.name||item.id }, "卸载 {value1} 的整个目录？")))return;const result=await changeDirectory("plugins",item,"remove");if(!result)return;changed();toast(result.pending_remove?uiMessage("ui.pluginWorkbench.disabledTheEntireDirectoryWillBeDeletedAfterRestarting", {}, "已停用，重启 mdo 后删除整个目录"):uiMessage("ui.pluginWorkbench.pluginDirectoryUninstalled", {}, "插件目录已卸载"));}));
      const stopLocale = subscribeLocale(render);
  return { key: item.id, title: spec.name || item.id, byline: credits.submitter, description: spec.description || uiText("ui.pluginWorkbench.legacyPluginFormat", {}, "旧格式插件"), meta: [...new Set([spec.version, ...credits.meta].filter(Boolean))], badge: { text: statusText[item.state] || item.state, tone: item.state === "failed" ? "error" : "" }, primary: extensionToggle(button(item.enabled ? uiMessage("resource.disable", {}, "停用") : uiMessage("resource.enable", {}, "启用"), () => toggle(item)), item.enabled, spec.name || item.id), actions, onSelect: () => details(item) };
    });
    browser.setItems(rows, { pageKey: JSON.stringify([query, filter]), empty: uiText("ui.pluginWorkbench.noLocalPluginsCreateOneOrVisitThePlugin", {}, "暂无本机插件，可以创建插件或前往插件商店。") }); root.append(browser.root);
  }
  const runtimeChanged=()=>{if(active&&owner==="local"&&!editor&&!busy)void load();};
  window.addEventListener("mdo-plugin-runtime-state",runtimeChanged);
  const unsubscribe = subscribeTarget(() => { if (owner === target()) return; owner = target(); epoch++; controller?.abort(); for (const d of layers) d.dispose(); directoryEditor.close(true);layers.clear(); editor = null; browser.reset(); data = { items: [] }; receipts = {}; ownershipKnown = false; error = ""; if (active) void load(); });
  return Object.freeze({ selectSection(section) { active = section === "extensions"; if (!active) { controller?.abort(); browser.closeDetail(false); for (const d of layers) d.dispose(); } store.activate(active && area === "store"); if (active && area === "local") void load(); }, hasPendingChanges: () => directoryEditor.hasPendingChanges() || busy || store.hasPendingChanges(), destroy() { disposed = true; epoch++; controller?.abort(); unsubscribe(); window.removeEventListener("mdo-plugin-runtime-state",runtimeChanged); for (const d of layers) d.dispose(); directoryEditor.destroy();store.destroy(); browser.destroy(); } });
}
