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
const statusText = { enabled: "已启用", disabled: "已停用", failed: "加载失败", recovery: "恢复模式", pending: "等待生效" };
const slotName = key => ({ "mdo.agent.default-orchestrator": "默认 Agent 编排", "mdo.ui.chat-layout": "对话布局接管" })[key] || key;
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
  const receipt = item => receipts[`plugins:${item.id}`]||null;
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
    const d=layer({title:file.path,width:"64rem"});d.footer.append(button("关闭",()=>d.close()));
    try{const data=(await api.get(`/extensions/plugins/${encodeURIComponent(item.id)}/file?path=${encodeURIComponent(file.path)}`,{signal:d.signal})).data;if(d.isCurrent())d.body.append(node("pre",data.binary?"二进制或超过预览上限，请导出完整 ZIP 查看。":data.text,"store-source"));}catch(e){if(d.isCurrent())d.body.append(node("p",errorMessage(e),"extension-error"));}
  }
  async function details(item) {
    const own = epoch, id = item.id;
    try {
      const full = await call("plugin.get", { id }, own); if (!browser.isSelected(id) || !current(own)) return;
      const spec = full.manifest, credits = resourceCredits({ ...item, ...spec, external: true, store_managed: !editable(item) }, receipt(item));
      const description = element("div", {}, [node("p", spec?.description || "旧格式插件"), credits.facts(), detailFacts([["状态", statusText[item.state] || item.state], ["当前修订", item.revision], ["实际生效修订", item.effective_revision || "未启用"], ["前端入口", spec?.entry?.frontend || full.frontend || "无"], ["原生入口", spec?.entry?.native || "无"]])]);
      const files = node("div");
      for (const f of full.files || []) { const targets = spec?.overrides?.filter(r => r.source === f.path).map(r => r.target) || []; files.append(element("div", { className: "resource-actions" }, [button(f.path, () => inspectFile(full, f)), node("small", `${f.bytes.toLocaleString()} 字节${targets.length ? ` → ${targets.join(" / ")}` : " · 插件私有文件"}`)])); }
      const relations = node("div"); relations.append(node("h4", "独占功能"), node("p", (spec?.exclusive || []).map(slotName).join("、") || "无"));
      for (const [key, title] of [["requires", "必需插件"], ["conflicts", "明确冲突"]]) {
        relations.append(node("h4", title)); for (const r of spec?.[key] || []) relations.append(node("p", `${r.id} · ${r.reason || ""}`));
      }
      const users = data.items.filter(r => r.manifest?.requires?.some(dep => dep.id === id)); relations.append(node("h4", "被以下插件依赖"), node("p", users.map(r => r.manifest.name).join("、") || "无"));
      browser.showDetail(id,detailTabs([{id:"description",label:"说明",content:description},{id:"files",label:"文件与入口",content:files},{id:"relations",label:"依赖与冲突",content:relations}]));
    } catch (e) { if (current(own) && browser.isSelected(id)) browser.showDetail(id, node("p", errorMessage(e), "extension-error")); }
  }
  async function toggle(item){
    if(busy)return;busy=true;
    const changes=[{id:item.id,enabled:!item.enabled}];
    try{
      const plan=await call("plugin.plan",{changes});
      if(!plan.allowed){await review(changes,item.enabled?"停用插件":"启用插件",plan);return;}
      const result=await call("plugin.commit",{changes,expected_revision:plan.base_revision,expected_plan:plan.plan_revision});changed();
      toast(result.restart_required?"插件选择已保存，重启 mdo 后生效":result.pending?"等待旧插件完成清理":"插件选择已生效");
    }catch(e){showError(e);}finally{busy=false;}
  }
  async function review(initial, title = "更改插件组合", prepared = null) {
    let changes = structuredClone(initial), saving = false;
    const d = layer({ title, beforeClose: () => !saving });
    const close = button("取消", () => d.close("cancel")), confirm = button("应用更改", submit, true); d.footer.append(confirm, close);
    let plan;
    async function refresh(value = null) {
      confirm.disabled = true; d.body.replaceChildren(node("p", "正在检查组合…"));
      try {
        plan = value || await call("plugin.plan", { changes }); if (!d.isCurrent()) return;
        d.body.replaceChildren();
        for (const change of changes) { const item = data.items.find(r => r.id === change.id); d.body.append(node("p", `${item?.manifest?.name || change.id}：${change.remove ? "卸载" : change.revision ? `切换修订 ${change.revision.slice(0, 12)}` : change.enabled ? "启用" : "停用"}`)); }
        for (const issue of plan.issues || []) {
          const row = element("section", { className: "warning-entry" }, [node("p", `${issue.id} ${issue.other}：${issue.code === "exclusive" ? `不能同时接管 ${slotName(issue.detail)}` : issue.detail}`, "extension-error")]);
          const set = patch => { const old = changes.find(r => r.id === patch.id); if (old) Object.assign(old, patch); else changes.push(patch); void refresh(); };
          if (["exclusive", "conflicts"].includes(issue.code)) for (const id of [issue.id, issue.other]) row.append(button(`停用 ${id}`, () => set({ id, enabled: false })));
          if (issue.code === "requires") { row.append(button(`同时停用 ${issue.id}`,()=>set({id:issue.id,enabled:false})));const dependency = data.items.find(r => r.id === issue.other); if (dependency && !dependency.enabled) row.append(button(`同时启用 ${issue.other}`, () => set({ id: issue.other, enabled: true }))); else row.append(node("p", "请先在插件商店安装符合要求的版本。")); }
          d.body.append(row);
        }
        d.body.append(node("p", "代码或资源目录变更需要重启。仍在运行的窗口或代码退出前，替代插件不会启动。", "extension-help")); confirm.disabled = !plan.allowed;
      } catch (e) { if (d.isCurrent()) d.body.replaceChildren(node("p", errorMessage(e), "extension-error")); }
    }
    async function submit() {
      if (!plan?.allowed || saving || !d.isCurrent()) return; saving = true; confirm.disabled = close.disabled = d.closeButton.disabled = true;
      try { const result = await call("plugin.commit", { changes, expected_revision: plan.base_revision, expected_plan: plan.plan_revision }); if (!d.isCurrent()) return; saving = false; d.dispose(); changed(); toast(result.restart_required ? "组合已保存，重启 mdo 后生效" : result.pending ? "等待旧插件完成清理" : "插件组合已生效"); }
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
    if(item){if(!editable(item))throw Error("商店插件不支持编辑或再次分享");await directoryEditor.open({kind:"plugins",item});return;}
    const entry=native?"runtime/plugin.c":"runtime/main.js",manifest=directoryManifest("plugins","my-plugin",{name:"我的插件",version:"1.0.0",license:"MIT",[native?"native":"frontend"]:entry});
    await directoryEditor.open({kind:"plugins",manifest,text:JSON.stringify({entry:{[native?"native":"frontend"]:entry},overrides:[],exclusive:[],requires:[],conflicts:[]},null,2),edits:[{path:entry,text:native?nativeScript("my-plugin"):defaultScript}]});
  }
  async function combinations() {
    const d = layer({ title: "插件组合方案", width: "48rem" });
    async function refresh() {
      try {
        const presets = await call("plugin.preset.list"); if (!d.isCurrent()) return; d.body.replaceChildren();
        const id = field(d.body, "组合 ID", "my-combination"), name = field(d.body, "组合名称", "我的组合");
        d.body.append(button("将当前插件选择保存为组合", async () => { try { await call("plugin.preset.save", { id: id.value.trim(), name: name.value.trim(), expected_revision: presets.revision }); await refresh(); } catch (e) { showError(e); } }));
        for (const row of presets.items) d.body.append(element("section", { className: "warning-entry" }, [node("h4", `${row.name}${row.matches ? " · 与当前一致" : ""}`), node("p", `${row.plugins.length} 个插件选择；不包含账号、凭据和运行数据。`), element("div", { className: "resource-actions" }, [
          button("应用", async () => { try { const p = await call("plugin.preset.plan", { id: row.id }); if (d.isCurrent()) await review(p.changes || [], `应用组合：${row.name}`, p); } catch (e) { showError(e); } }),
          button("更新为当前选择", async () => { try { await call("plugin.preset.save", { id: row.id, name: row.name, expected_revision: presets.revision }); await refresh(); } catch (e) { showError(e); } }),
          button("删除", async () => { if (!confirm(`删除组合“${row.name}”？不会卸载插件。`)) return; try { await call("plugin.preset.remove", { id: row.id, expected_revision: presets.revision }); await refresh(); } catch (e) { showError(e); } })]) ]));
      } catch (e) { if (d.isCurrent()) d.body.replaceChildren(node("p", errorMessage(e), "extension-error")); }
    }
    d.footer.append(button("刷新", refresh), button("关闭", () => d.close("close"))); await refresh();
  }
  async function importFile(file){try{await store.importPackage(file);}catch(e){showError(e);}}
  function render() {
    if (!root || disposed) return;
    browser.rememberFocus(); const tabs = element("div", { className: "store-tabs", attrs: { role: "tablist" } });
    for (const [value, text] of [["local", "本机插件"], ["store", "插件商店"]]) { const b = button(text, () => { if (editor || store.hasPendingChanges()) return; area = value; store.activate(value === "store"); render(); if (value === "local") void load(); }); b.setAttribute("role", "tab"); b.setAttribute("aria-selected", String(area === value)); tabs.append(b); }
    root.replaceChildren(tabs, store.statusRoot); if (area === "store") { root.append(store.root); return; }
    const search = element("form", { className: "store-search eb-toolbar" }), input = element("input", { attrs: { type: "search", placeholder: "搜索插件、作者或用途", "aria-label": "搜索插件" } }); input.value = query;
    const select = element("select", { attrs: { "aria-label": "插件状态" } }); for (const [value, text] of [["all", "全部状态"], ["enabled", "已启用"], ["disabled", "已停用"], ["pending", "等待生效"], ["failed", "加载失败"],["recovery","恢复模式"]]) select.append(element("option", { text, attrs: { value } })); select.value = filter; select.addEventListener("change", () => { filter = select.value; render(); });
    const picker = element("input", { attrs: { type: "file", accept: ".zip" } }); picker.hidden = true;
    picker.addEventListener("change", () => { if (picker.files[0]) void importFile(picker.files[0]); });
    search.append(input, select, button("搜索", () => { query = input.value.trim(); render(); }), button("刷新", load), button("组合方案", combinations), actionMenu([button("新建插件", () => edit()), button("新建 C 插件",()=>edit(null,true)), button("导入安装文件", () => picker.click())], "创建 / 导入"));
    search.addEventListener("submit", e => { e.preventDefault(); query = input.value.trim(); render(); });
    root.append(search, picker);
    if (error) root.append(node("p", error, "extension-error"));
    if(data.config_broken)root.append(node("p","插件登记文件损坏。请修复或恢复 config/plugins.json 后重启；原文件与插件目录均保留，当前不会覆盖它。","extension-error"));
    if (data.recovery_mode) root.append(node("p", "插件恢复模式已开启，覆盖和代码入口均暂停。请修复配置后重新启动 mdo。", "extension-error"));
    if(data.retiring?.length)root.append(node("p",`仍在等待退出的修订：${data.retiring.map(r=>(r.manifest?.name||r.id)+" "+r.revision.slice(0,12)).join("、")}`,"extension-help"));
    if (data.pending) root.append(node("p", data.restart_required ? "插件选择已保存，包含代码或资源目录变更，请重启 mdo 生效。" : "插件选择已保存，正在等待其他窗口清理旧插件；必要时刷新相关窗口。", "extension-help"));
    const rows = data.items.filter(p => (filter === "all" || p.state === filter) && `${p.id} ${p.manifest?.name || ""} ${p.manifest?.description || ""} ${p.manifest?.author || ""}`.toLocaleLowerCase().includes(query.toLocaleLowerCase())).map(item => {
      const spec = item.manifest || {}, record = receipt(item), credits = resourceCredits({ ...item, ...spec, external: true, store_managed: item.source === "store" || record && record.source !== "local" }, record), actions = [];
      if (editable(item)) actions.push(button("编辑", () => edit(item)), button("分享到创作中心", () => store.publish({...item,name:item.name||spec.name||item.id},record)));
      actions.push(...store.localActions(item));
      actions.push(button("卸载",async()=>{if(!confirm(`卸载 ${item.name||spec.name||item.id} 的整个目录？`))return;const result=await changeDirectory("plugins",item,"remove");changed();toast(result.pending_remove?"已停用，重启 mdo 后删除整个目录":"插件目录已卸载");}));
      return { key: item.id, title: spec.name || item.id, byline: credits.submitter, description: spec.description || "旧格式插件", meta: [...new Set([spec.version, ...credits.meta].filter(Boolean))], badge: { text: statusText[item.state] || item.state, tone: item.state === "failed" ? "error" : "" }, primary: extensionToggle(button(item.enabled ? "停用" : "启用", () => toggle(item)), item.enabled, spec.name || item.id), actions, onSelect: () => details(item) };
    });
    browser.setItems(rows, { pageKey: JSON.stringify([query, filter]), empty: "暂无本机插件，可以创建插件或前往插件商店。" }); root.append(browser.root);
  }
  const runtimeChanged=()=>{if(active&&owner==="local"&&!editor&&!busy)void load();};
  window.addEventListener("mdo-plugin-runtime-state",runtimeChanged);
  const unsubscribe = subscribeTarget(() => { if (owner === target()) return; owner = target(); epoch++; controller?.abort(); for (const d of layers) d.dispose(); directoryEditor.close(true);layers.clear(); editor = null; browser.reset(); data = { items: [] }; receipts = {}; ownershipKnown = false; error = ""; if (active) void load(); });
  return Object.freeze({ selectSection(section) { active = section === "extensions"; if (!active) { controller?.abort(); browser.closeDetail(false); for (const d of layers) d.dispose(); } store.activate(active && area === "store"); if (active && area === "local") void load(); }, hasPendingChanges: () => directoryEditor.hasPendingChanges() || busy || store.hasPendingChanges(), destroy() { disposed = true; epoch++; controller?.abort(); unsubscribe(); window.removeEventListener("mdo-plugin-runtime-state",runtimeChanged); for (const d of layers) d.dispose(); directoryEditor.destroy();store.destroy(); browser.destroy(); } });
}
