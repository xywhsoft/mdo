import { api } from "../../api/client.js";
import { createApplicationClient } from "../../api/application.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { element, errorMessage, toast } from "../../utils/dom.js";
import { saveBlobFile } from "../../utils/file-download.js";
import { openLayer } from "../../components/layers.js";
import { createExtensionBrowser, extensionToggle, actionMenu, detailFacts, detailTabs } from "../../components/extension-browser.js";
import { createSingleResourceStore } from "./single-resource-store.js";
import { resourceReferences } from "./store-formats.js";
import { resourceCredits } from "./resource-credits.js";

const node = (tag, text = "", className = "") => element(tag, { text, className });
const button = (text, run, primary = false) => { const b = element("button", { text, className: primary ? "primary-button" : "secondary-button", attrs: { type: "button" } }); b.addEventListener("click", () => { Promise.resolve().then(run).catch(e => { if(e?.name!=="AbortError") toast(errorMessage(e),"error"); }); }); return b; };
const target = () => targetState().selected?.id || "local";
const encode = bytes => { let result = ""; for (let i = 0; i < bytes.length; i += 32768) result += String.fromCharCode(...bytes.subarray(i, i + 32768)); return btoa(result); };
const decode = text => Uint8Array.from(atob(text), c => c.charCodeAt(0));
const textOf = bytes => { try { if (bytes.includes(0)) return null; return new TextDecoder("utf-8", { fatal: true }).decode(bytes); } catch { return null; } };
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
  const receipt = item => Object.values(receipts).find(row => resourceReferences(row).some(r => r.kind === "plugins" && r.id === item.id));
  const editable = item => ownershipKnown && item.source !== "store" && (!receipt(item) || receipt(item).source === "local");
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
  async function fileBytes(item, path, maximum = 64 * 1024 * 1024) {
    const own = epoch, file = item.files.find(f => f.path === path);
    if (!file || file.bytes > maximum) throw Error("文件超过本次查看或编辑的大小限制");
    const bytes = new Uint8Array(file.bytes); let offset = 0;
    while (offset < bytes.length) {
      const chunk = await call("plugin.file.read", { id: item.id, revision: item.revision, path, offset, limit: 65536 }, own), part = decode(chunk.base64);
      if (!part.length || chunk.next !== offset + part.length || chunk.total !== bytes.length || chunk.next > bytes.length) throw Error("插件文件返回了无效的范围");
      bytes.set(part, offset); offset = chunk.next;
    }
    const digest=[...new Uint8Array(await crypto.subtle.digest("SHA-256",bytes))].map(b=>b.toString(16).padStart(2,"0")).join("");
    if(!current(own))throw new DOMException("Device changed","AbortError");
    if(digest!==file.sha256)throw Error("插件文件摘要已改变，请重新检查该修订");return bytes;
  }
  async function inspectFile(item, file) {
    const d = layer({ title: file.path, width: "64rem" }); d.body.append(node("p", "正在读取…"));
    d.footer.append(button("下载文件",async()=>{const bytes=await fileBytes(item,file.path);await saveBlobFile({blob:new Blob([bytes]),filename:file.path.split("/").at(-1)});}),button("关闭",()=>d.close("close")));
    if(file.bytes>1024*1024){d.body.replaceChildren(node("p","此文件超过 1 MiB，请下载后查看。"));return;}
    try {
      const bytes = await fileBytes(item, file.path, 1024 * 1024); if (!d.isCurrent()) return;
      const text = textOf(bytes); d.body.replaceChildren(text === null ? node("p", "二进制文件，请下载后查看。") : node("pre", text, "store-source"));

    } catch (e) { if (d.isCurrent()) d.body.replaceChildren(node("p", errorMessage(e), "extension-error")); }

  }
  async function compare(item){
    const d=layer({title:"对比上一修订",width:"64rem"});d.body.append(node("p","正在读取修订…"));d.footer.append(button("关闭",()=>d.close("close")));
    try{
      const previous=await call("plugin.get",{id:item.id,revision:item.previous});if(!d.isCurrent())return;
      d.body.replaceChildren(node("pre",JSON.stringify({previous:previous.manifest,current:item.manifest},null,2),"store-source"));
      const paths=[...new Set([...(previous.files||[]),...(item.files||[])].map(f=>f.path))];let count=0;
      for(const path of paths){const before=previous.files.find(f=>f.path===path),after=item.files.find(f=>f.path===path);if(before?.sha256===after?.sha256)continue;count++;
        const row=element("section",{className:"warning-entry"},[node("h4",path),node("p",!before?"新增":!after?"删除":"内容改变")]);
        if(before)row.append(button("查看旧文件",()=>inspectFile(previous,before)));if(after)row.append(button("查看当前文件",()=>inspectFile(item,after)));d.body.append(row);
      }if(!count)d.body.append(node("p","文件内容相同；清单或兼容声明发生变化。"));
    }catch(e){if(d.isCurrent())d.body.replaceChildren(node("p",errorMessage(e),"extension-error"));}
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
        relations.append(node("h4", title)); for (const r of spec?.[key] || []) relations.append(node("p", `${r.id} · ${r.min_version ? `≥ ${r.min_version}` : ""} ${r.max_version_exclusive ? `< ${r.max_version_exclusive}` : ""} ${r.reason || ""}`));
      }
      const users = data.items.filter(r => r.manifest?.requires?.some(dep => dep.id === id)); relations.append(node("h4", "被以下插件依赖"), node("p", users.map(r => r.manifest.name).join("、") || "无"));
      const versions = element("div", {}, [node("p", `当前版本：${spec?.version || "旧格式"}`), node("p", full.previous ? `保留的上一修订：${full.previous}` : "没有上一修订")]);
      if(full.previous)versions.append(button("对比上一修订",()=>compare(full)));
      if (editable(item) && full.previous) versions.append(button("回滚到上一修订", () => review([{ id, revision: full.previous }], "回滚插件")));
      browser.showDetail(id, detailTabs([{ id: "description", label: "说明", content: description }, { id: "files", label: "文件与入口", content: files }, { id: "relations", label: "兼容关系", content: relations }, { id: "versions", label: "版本", content: versions }]));
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
  async function edit(item = null, imported = null) {
    if (editor || busy) return; busy = true;
    try {
      const full = item ? await call("plugin.get", { id: item.id }) : null;
      if (item && !editable(item)) throw Error("商店插件不支持编辑或再次分享");
      if (full && !full.manifest) throw Error("旧格式插件请先从原项目整理为 plugin.json 后导入；原文件和历史记录会保留。");
      const spec = structuredClone(imported?.manifest || full?.manifest || { format: "mdo.plugin.v1", id: "my-plugin", name: "我的插件", version: "1.0.0", description: "", license: "MIT" });
      let files = imported?.files?.map(f => ({ ...f })) || full?.files.map(f => ({ path: f.path, bytes: f.bytes, sha256: f.sha256, original: true })) || [], saving = false, dirty = false;
      const d = layer({ title: item ? "编辑插件" : "新建插件", width: "64rem", beforeClose: () => !saving && (!dirty || confirm("放弃未保存的插件修改？")), onClosed() { if (editor === d) editor = null; } }); editor = d;
      const form = element("form", { className: "extension-form plugin-editor extension-authoring" }), fields = {};
      for (const [key, name] of [["id", "插件 ID"], ["name", "名称"], ["version", "版本"], ["description", "简介"], ["author", "作者"], ["copyright", "版权声明"], ["license", "许可"]]) fields[key] = field(form, name, spec[key] || "", ["description", "copyright"].includes(key));
      fields.id.disabled = Boolean(item);
      const fileList = node("div"), frontend = field(form, "前端入口（可选，插件内 .js 路径）", spec.entry?.frontend || ""), native = field(form, "原生入口（可选，插件内 .c 路径）", spec.entry?.native || "");
      const exclusive = field(form, "独占功能标记（每行一个，只有接管默认行为才需要）", (spec.exclusive || []).join("\n"), true);
      const relations = field(form, "依赖与冲突（JSON，包含 requires 和 conflicts 数组）", JSON.stringify({ requires: spec.requires || [], conflicts: spec.conflicts || [] }, null, 2), true);
      const mappings = new Map((spec.overrides || []).map(r => [r.source, r.target]));
      const message = node("p", "", "extension-help"), picker = element("input", { attrs: { type: "file", multiple: "" } }); picker.hidden = true;
      const add = file => { if (files.some(f => f.path === file.path)) throw Error(`文件已存在：${file.path}`); files.push(file); dirty = true; drawFiles(); };
      function drawFiles() {
        fileList.replaceChildren(node("h3", "插件文件"));
        for (const file of files) {
          const row = node("div", "", "extension-field"); row.append(node("strong", file.path));
          const destination = element("input", { attrs: { type: "text", placeholder: "应用覆盖目标；留空则为插件私有文件", "aria-label": `${file.path} 的覆盖目标` } }); destination.value = mappings.get(file.path) || "";
          destination.addEventListener("input", () => { dirty = true; const value = destination.value.trim(); if (value) mappings.set(file.path, value); else mappings.delete(file.path); });
          row.append(destination, element("div", { className: "resource-actions" }, [button("查看 / 编辑", () => editFile(file)), button("移除", () => { files = files.filter(f => f !== file); mappings.delete(file.path); dirty = true; drawFiles(); })])); fileList.append(row);
        }
      }
      async function editFile(file) {
        const f = layer({ title: file.path, width: "64rem" }); f.body.append(node("p", "正在读取…"));
        try {
          if(file.bytes>1024*1024){f.body.replaceChildren(node("p","此文件超过编辑预览大小，可移除后重新添加替换。"));f.footer.append(button("关闭",()=>f.close("close")));return;}
          const bytes = file.data || (file.original ? await fileBytes(full, file.path) : new Uint8Array()); if (!f.isCurrent()) return;
          const text = textOf(bytes);
          if (text === null || bytes.length > 1024 * 1024) { f.body.replaceChildren(node("p", "此文件以二进制或大文件管理，可移除后重新添加替换。")); f.footer.append(button("关闭", () => f.close("close"))); return; }
          const input = element("textarea", { className: "extension-source", attrs: { spellcheck: "false", "aria-label": file.path } }); input.value = text; f.body.replaceChildren(input);
          f.footer.append(button("保存", () => { file.data = new TextEncoder().encode(input.value); file.bytes = file.data.length; file.original = false; dirty = true; f.dispose(); }, true), button("取消", () => f.close("cancel")));
        } catch (e) { if (f.isCurrent()) { f.body.replaceChildren(node("p", errorMessage(e), "extension-error")); f.footer.append(button("关闭", () => f.close("close"))); } }
      }
      picker.addEventListener("change", async () => { try { let total=files.reduce((n,f)=>n+f.bytes,0);for (const f of picker.files) {total+=f.size;if(total>64*1024*1024)throw Error("插件文件总量不能超过 64 MiB");add({ path: f.name, bytes: f.size, data: new Uint8Array(await f.arrayBuffer()) });} } catch (e) { message.textContent = errorMessage(e); } finally { picker.value = ""; } });
      const newText = button("新建文本文件", () => {
        const f = layer({ title: "新建插件文件", width: "40rem", height: "auto" }), path = field(f.body, "插件内相对路径", "files/example.txt");
        f.footer.append(button("添加", () => { try { add({ path: path.value.trim(), bytes: 0, data: new Uint8Array() }); f.dispose(); } catch (e) { toast(errorMessage(e), "error"); } }, true), button("取消", () => f.close("cancel")));
      });
      form.append(node("p", "所有目标相对于 mdo 应用文件空间。执行入口保持私有，无须再添加覆盖目标。", "extension-help"), element("div", { className: "resource-actions" }, [button("添加文件", () => picker.click()), newText,
        button("添加 JS 入口范例", () => { try { add({ path: "runtime/main.js", data: new TextEncoder().encode(defaultScript), bytes: new TextEncoder().encode(defaultScript).length }); frontend.value = "runtime/main.js"; } catch (e) { message.textContent = errorMessage(e); } }),
        button("添加 C 入口范例", () => { try { const source = nativeScript(fields.id.value.trim()); add({ path: "runtime/plugin.c", data: new TextEncoder().encode(source), bytes: source.length }); native.value = "runtime/plugin.c"; } catch (e) { message.textContent = errorMessage(e); } })]), picker, fileList, message);
      form.addEventListener("input", () => { dirty = true; }); drawFiles();
      const save = button("保存插件", async () => {
        if (saving || !d.isCurrent()) return; const own = epoch, uploads = []; saving = true;
        for (const control of form.querySelectorAll("input,textarea,button,select")) control.disabled = true; save.disabled = cancel.disabled = d.closeButton.disabled = true;
        try {
          const relationship = JSON.parse(relations.value);
          if (!relationship || Object.keys(relationship).some(k => !["requires", "conflicts"].includes(k))) throw Error("兼容关系仅支持 requires 和 conflicts");
          const doc = { format: "mdo.plugin.v1", ...Object.fromEntries(Object.entries(fields).map(([k, v]) => [k, v.value.trim()])), overrides: [...mappings].map(([source, destination]) => ({ source, target: destination })), entry: {}, exclusive: exclusive.value.split(/[\s,，]+/).filter(Boolean), ...relationship };
          if (frontend.value.trim()) doc.entry.frontend = frontend.value.trim(); if (native.value.trim()) doc.entry.native = native.value.trim();
          const payload = []; let total = 0, inline = 0;
          for (const file of files) {
            if (!d.isCurrent()) throw new DOMException("Device changed", "AbortError");
            if(file.original){total+=file.bytes;if(total>64*1024*1024)throw Error("插件文件总量不能超过 64 MiB");payload.push({path:file.path,plugin_revision:full.revision,bytes:file.bytes,sha256:file.sha256});continue;}
            const bytes = file.data || new Uint8Array(); total += bytes.length;
            if (total > 64 * 1024 * 1024) throw Error("插件文件总量不能超过 64 MiB");
            if (inline+bytes.length <= 256 * 1024) { inline+=bytes.length;payload.push({ path: file.path, base64: encode(bytes) }); continue; }
            const storage_path = `plugin-uploads/${crypto.randomUUID()}`, hash = [...new Uint8Array(await crypto.subtle.digest("SHA-256", bytes))].map(b => b.toString(16).padStart(2, "0")).join(""); uploads.push(storage_path);
            for (let offset = 0; offset < bytes.length; offset += 256 * 1024) {
              message.textContent = `正在准备 ${file.path}：${Math.min(bytes.length, offset + 256 * 1024)} / ${bytes.length}`;
              if (!offset) await call("storage.write", { path: storage_path, base64: encode(bytes.subarray(offset, offset + 256 * 1024)) }, own);
              else await call("fs.append", { space: "home", path: `data/extensions/mdo.plugin-manager/${storage_path}`, base64: encode(bytes.subarray(offset, offset + 256 * 1024)) }, own);
            }
            payload.push({ path: file.path, storage_path, bytes: bytes.length, sha256: hash });
          }
          message.textContent = "正在保存…"; await call("plugin.apply", { id: doc.id, manifest: doc, files: payload }, own);
          dirty = false; saving = false; d.dispose(); changed(); toast("插件已保存；启用时会检查完整组合");
        } catch (e) { if (d.isCurrent()) message.textContent = errorMessage(e); }
        finally {
          saving = false; for (const path of uploads) if (current(own)) { try { await call("storage.remove", { path }, own); } catch { /* Incomplete private uploads may be removed later. */ } }
          if (d.isCurrent()) { for (const control of form.querySelectorAll("input,textarea,button,select")) control.disabled = false; fields.id.disabled = Boolean(item); save.disabled = cancel.disabled = d.closeButton.disabled = false; }
        }
      }, true), cancel = button("取消", () => d.close("cancel")); d.footer.append(save, cancel); d.body.append(form);
    } catch (e) { showError(e); } finally { busy = false; }
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
  async function importFile(file) {
    try { const value = JSON.parse(await file.text()); await store.importPackage(value); } catch (e) { showError(e); }
  }
  async function importDirectory(files) {
    try {
      const manifestFile = [...files].find(f => f.webkitRelativePath?.split("/").length === 2 && f.name === "plugin.json"); if (!manifestFile) throw Error("选择的目录根部需要有 plugin.json");
      const manifest = JSON.parse(await manifestFile.text()), prefix = manifestFile.webkitRelativePath.slice(0, -"plugin.json".length), payload = [];
      if (files.length > 129) throw Error("每个插件最多支持 128 个文件");
      if ([...files].reduce((n,f)=>n+f.size,0)>64*1024*1024) throw Error("插件文件总量不能超过 64 MiB");
      for (const file of files) if (file !== manifestFile) payload.push({ path: file.webkitRelativePath.slice(prefix.length), bytes: file.size, data: new Uint8Array(await file.arrayBuffer()) });
      await edit(null, { manifest, files: payload });
    } catch (e) { showError(e); }
  }
  function render() {
    if (!root || disposed) return;
    browser.rememberFocus(); const tabs = element("div", { className: "store-tabs", attrs: { role: "tablist" } });
    for (const [value, text] of [["local", "本机插件"], ["store", "插件商店"]]) { const b = button(text, () => { if (editor || store.hasPendingChanges()) return; area = value; store.activate(value === "store"); render(); if (value === "local") void load(); }); b.setAttribute("role", "tab"); b.setAttribute("aria-selected", String(area === value)); tabs.append(b); }
    root.replaceChildren(tabs, store.statusRoot); if (area === "store") { root.append(store.root); return; }
    const search = element("form", { className: "store-search eb-toolbar" }), input = element("input", { attrs: { type: "search", placeholder: "搜索插件、作者或用途", "aria-label": "搜索插件" } }); input.value = query;
    const select = element("select", { attrs: { "aria-label": "插件状态" } }); for (const [value, text] of [["all", "全部状态"], ["enabled", "已启用"], ["disabled", "已停用"], ["pending", "等待生效"], ["failed", "加载失败"],["recovery","恢复模式"]]) select.append(element("option", { text, attrs: { value } })); select.value = filter; select.addEventListener("change", () => { filter = select.value; render(); });
    const picker = element("input", { attrs: { type: "file", accept: ".json" } }), directory = element("input", { attrs: { type: "file", webkitdirectory: "", multiple: "" } }); picker.hidden = directory.hidden = true;
    picker.addEventListener("change", () => { if (picker.files[0]) void importFile(picker.files[0]); }); directory.addEventListener("change", () => { if (directory.files.length) void importDirectory(directory.files); });
    search.append(input, select, button("搜索", () => { query = input.value.trim(); render(); }), button("刷新", load), button("组合方案", combinations), actionMenu([button("新建插件", () => edit()), button("导入插件项目目录", () => directory.click()), button("导入安装文件", () => picker.click())], "创建 / 导入"));
    search.addEventListener("submit", e => { e.preventDefault(); query = input.value.trim(); render(); });
    root.append(search, picker, directory);
    if (error) root.append(node("p", error, "extension-error"));
    if(data.config_broken)root.append(node("p","插件登记文件损坏。请修复或恢复 config/plugins.json 后重启；原文件与插件目录均保留，当前不会覆盖它。","extension-error"));
    if (data.recovery_mode) root.append(node("p", "插件恢复模式已开启，覆盖和代码入口均暂停。请修复配置后重新启动 mdo。", "extension-error"));
    if(data.retiring?.length)root.append(node("p",`仍在等待退出的修订：${data.retiring.map(r=>(r.manifest?.name||r.id)+" "+r.revision.slice(0,12)).join("、")}`,"extension-help"));
    if (data.pending) root.append(node("p", data.restart_required ? "插件选择已保存，包含代码或资源目录变更，请重启 mdo 生效。" : "插件选择已保存，正在等待其他窗口清理旧插件；必要时刷新相关窗口。", "extension-help"));
    const rows = data.items.filter(p => (filter === "all" || p.state === filter) && `${p.id} ${p.manifest?.name || ""} ${p.manifest?.description || ""} ${p.manifest?.author || ""}`.toLocaleLowerCase().includes(query.toLocaleLowerCase())).map(item => {
      const spec = item.manifest || {}, record = receipt(item), credits = resourceCredits({ ...item, ...spec, external: true, store_managed: item.source === "store" || record && record.source !== "local" }, record), actions = [];
      if (editable(item)) actions.push(button("编辑", () => edit(item)), button("分享到创作中心", () => store.publish({ id: item.id, ...spec, name: spec.name || item.id }, record)));
      actions.push(...store.localActions(record));
      if (!record && editable(item)) actions.push(button("卸载", () => review([{ id: item.id, remove: true }], "卸载插件")));
      return { key: item.id, title: spec.name || item.id, byline: credits.submitter, description: spec.description || "旧格式插件", meta: [...new Set([spec.version, ...credits.meta].filter(Boolean))], badge: { text: statusText[item.state] || item.state, tone: item.state === "failed" ? "error" : "" }, primary: extensionToggle(button(item.enabled ? "停用" : "启用", () => toggle(item)), item.enabled, spec.name || item.id), actions, onSelect: () => details(item) };
    });
    browser.setItems(rows, { pageKey: JSON.stringify([query, filter]), empty: "暂无本机插件，可以创建插件或前往插件商店。" }); root.append(browser.root);
  }
  const unsubscribe = subscribeTarget(() => { if (owner === target()) return; owner = target(); epoch++; controller?.abort(); for (const d of layers) d.dispose(); layers.clear(); editor = null; browser.reset(); data = { items: [] }; receipts = {}; ownershipKnown = false; error = ""; if (active) void load(); });
  return Object.freeze({ selectSection(section) { active = section === "extensions"; if (!active) { controller?.abort(); browser.closeDetail(false); for (const d of layers) d.dispose(); } store.activate(active && area === "store"); if (active && area === "local") void load(); }, hasPendingChanges: () => Boolean(editor) || busy || store.hasPendingChanges(), destroy() { disposed = true; epoch++; controller?.abort(); unsubscribe(); for (const d of layers) d.dispose(); store.destroy(); browser.destroy(); } });
}
