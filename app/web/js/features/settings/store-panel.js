import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { subscribeLocale, t } from "../../i18n.js";
import { element, errorMessage, toast } from "../../utils/dom.js";
import { loadAgents } from "../../state/catalogs.js";
import { loadResource } from "../../state/resources.js";
import { makeStorePackage, STORE_KINDS, STORE_PLATFORMS, containsCode, resourceReferences, receiptKey, packageFields } from "./store-formats.js";
import { createStoreDraft } from "./store-draft.js";
import { renderSourceMarkdown } from "../../utils/source-markdown.js";
import { saveBlobFile } from "../../utils/file-download.js";

const text = (key, fallback, args = {}) => t(`store.${key}`, args, fallback);
const kindName = kind => ({ agents: "Agent", subagents: "SubAgent", tools: text("tools", "工具"),
  skills: "Skill", mcp: "MCP", commands: text("commands", "命令"), "c-agents": "C Agent", "c-subagents": "C SubAgent" })[kind] || kind;
const node = (tag, value, className) => element(tag, { text: value, className });
const publicationMessage = error => {
  const key = ({ "Invalid package ID": "id", "Invalid version": "version",
    "Name, description, documentation and license are required": "fields",
    "Select resources and supported platforms": "selection", "Package exceeds 1 MiB": "size" })[error.message];
  return key ? text(`validation.${key}`, error.message) : errorMessage(error);
};

export function createStorePanel() {
  const root = document.querySelector("[data-extension-store]");
  let visible = false, view = "discover", items = [], installed = {}, query = "", kind = "", cursor = 0;
  let busy = false, error = "", loginRequired = false, serial = 0, dialog = null, editor = null, pendingPackageKey = null;
  const targetKey = () => targetState().selected?.id || "local";
  const call = async body => (await api.post("/ecosystem", body)).data;
  const button = (label, action, primary = false) => {
    const b = element("button", { text: label, className: primary ? "primary-button" : "secondary-button", attrs: { type: "button" } });
    b.addEventListener("click", () => { void action(); }); return b;
  };
  const feedback = e => {
    error = ["ecosystem_login_required", "ecosystem_login_refresh"].includes(e.code)
      ? text("loginRequired", "登录 ai.xywhsoft.com 账号后，即可浏览、安装和发布插件。") : errorMessage(e);
    loginRequired = e.status === 401;
  };
  const changed = () => {
    for (const kind of STORE_KINDS) window.dispatchEvent(new CustomEvent("mdo-extensions-changed", { detail: { kind } }));
    void loadAgents();
    for (const name of ["modules", "skills", "mcp"]) void loadResource(name);
  };
  function closeDialog() { editor?.draft?.dispose();dialog?.close();dialog?.remove();dialog=null;editor=null; }
  async function closePublication() {
    if (busy) return;
    if (editor?.draft?.pending) {
      try { await editor.draft.flush(); }
      catch { toast(text("draftSaveFailed", "草稿保存失败，请重试或导出插件包。"), "error");return; }
    }
    closeDialog();if(view === "drafts")void load();
  }
  function createDialog(title) {
    closeDialog(); dialog = element("dialog", { className: "store-dialog", attrs: { "aria-label": title } });
    const heading = node("div", "", "store-dialog-heading");heading.append(node("h2", title), button(text("close", "关闭"), closePublication));
    dialog.append(heading);document.body.append(dialog);
    dialog.addEventListener("cancel", e => { e.preventDefault();void closePublication(); });
    return dialog;
  }
  async function load(append = false) {
    if (busy) return;
    const epoch = ++serial, target = targetKey();busy = true;error = "";render();
    try {
      const records = (await api.get("/ecosystem")).data;
      const data = view === "installed" ? null : await call({ action: view === "drafts" ? "drafts" : view === "mine" ? "mine" : "catalog", query, kind, before: append ? cursor : 0 });
      if (epoch !== serial || target !== targetKey()) return;
      installed = records;items = view === "installed" ? Object.values(records) : append ? items.concat(data.items) : data.items;
      if(view === "drafts")items.sort((a,b) => b.updated_at-a.updated_at);
      cursor = data?.next_before || 0;loginRequired = false;
    } catch (e) { if (epoch === serial) { items = []; feedback(e); } }
    finally { if (epoch === serial) { busy = false;render();if(pendingPackageKey&&visible){const row=Object.values(installed).find(r=>receiptKey(r)===pendingPackageKey);pendingPackageKey=null;if(row)installedDetails(row);} } }
  }
  async function act(work) {
    if (busy) return;const target = targetKey();busy = true;error = "";
    try { await work();if (target !== targetKey()) return;changed();busy = false;await load(); }
    catch (e) { if (target === targetKey()) { feedback(e);toast(error, "error"); } }
    finally { if (target === targetKey()) { busy = false;render(); } }
  }
  function render() {
    if (!root) return;
    const heading = node("div", "", "store-heading");
    const topActions=node("div","","store-card-actions");topActions.append(button(text("importPackage","导入插件包"), importPackage),button(text("createPackage","新建插件"), publish, true));
    heading.append(node("div", "", "store-intro"),topActions);
    heading.firstChild.append(node("p", text("subtitle", "分享你的工作方式，让墨斗更顺手。")));
    const tabs = node("div", "", "store-tabs");tabs.setAttribute("role", "tablist");
    for (const [id, label] of [["discover", text("discover", "发现")], ["installed", text("installed", "已安装")], ["drafts",text("drafts","本地草稿")], ["mine", text("mine", "我的发布")]]) {
      const b = button(label, () => { if(busy)return;view = id;items=[];cursor=0;query = "";kind = "";void load(); });b.setAttribute("role", "tab");b.setAttribute("aria-selected", String(view === id));tabs.append(b);
    }
    const nodes = [heading, tabs];
    if (view === "discover") {
      const form = element("form", { className: "store-search" });
      const input = element("input", { attrs: { type: "search", placeholder: text("search", "搜索插件、作者或用途"), "aria-label": text("search", "搜索插件、作者或用途"), maxlength: "100" } });input.value = query;
      const select = element("select", { attrs: { "aria-label": text("type", "资源类型") } });
      select.append(element("option", { text: text("all", "全部类型"), attrs: { value: "" } }));
      for (const k of STORE_KINDS) select.append(element("option", { text: kindName(k), attrs: { value: k } }));select.value = kind;
      const submit = element("button", { text: text("searchButton", "搜索"), className: "secondary-button", attrs: { type: "submit" } });submit.disabled = busy;
      form.append(input, select, submit);form.onsubmit = e => { e.preventDefault();query = input.value.trim();kind = select.value;void load(); };nodes.push(form);
    }
    if (error || busy) { const status = node("p", busy ? text("loading", "正在读取…") : error, "store-status");status.setAttribute("role", "status");nodes.push(status); }
    if (loginRequired) nodes.push(button(text("signIn", "登录账号"), () => { location.hash = "#/settings/account"; }));
    else if (!items.length && !busy && !error) nodes.push(node("div", text(view === "drafts" ? "noDrafts" : view === "mine" ? "noPublished" : view === "installed" ? "noInstalled" : "noResults", view === "drafts" ? "草稿保存在当前设备的 mdo-home，可随时继续编辑和发布。" : view === "mine" ? "还没有投稿。选择本地资源，发布你的第一个插件。" : view === "installed" ? "安装的插件会显示在这里。" : "暂无公开插件，欢迎发布你的作品。"), "store-empty"));
    const cards = node("div", "", "store-grid");
    for (const row of items) {
      if(view === "drafts") {
        const card=node("article","","store-card"),actions=node("div","","store-card-actions");
        card.append(node("h3",row.fields.name || text("untitled","未命名插件")),node("p",`${row.fields.slug || "—"} · ${row.fields.version}`,"store-card-description"),node("small",new Date(row.updated_at*1000).toLocaleString()));
        actions.append(button(text("continueEditing","继续编辑"),()=>publish({draftId:row.id})),button(text("deleteDraft","删除草稿"),()=>act(async()=>{if(confirm(text("deleteDraftConfirm","删除此本地草稿？已安装资源和线上投稿会保留。")))await call({action:"draft_delete",id:row.id,revision:row.revision});})));card.append(actions);cards.append(card);continue;
      }
      const card = node("article", "", "store-card"), icon = node("div", [...(row.name || "M")][0], "store-card-icon");
      card.append(icon, node("h3", row.name), node("p", row.description || row.manifest?.description || text("installedHint", "资源保存在本机，可在对应管理页编辑。"), "store-card-description"), node("div", `${row.source === "local" ? text("localImport","本地导入") : row.author} · ${row.version}`, "store-card-author"));
      const types=[...new Set(resourceReferences(row).map(r=>r.kind).concat((row.kinds || "").split(",").filter(Boolean)))];
      const badges=node("div","","store-badges");for(const type of types)badges.append(node("span",kindName(type),"store-badge"));card.append(badges);
      const actions = node("div", "", "store-card-actions");
      if (view === "installed") {
        if(row.source !== "local")actions.append(button(text("checkUpdate", "检查更新"), async () => {
          try { const latest = await call({ action: "latest", id: row.id });
            if (!latest || latest.id === row.id) toast(text("upToDate", "已是最新公开版本。"));else await detail(latest); }
          catch (e) { toast(errorMessage(e), "error"); }
        }));
        actions.append(button(text("manageResources","管理资源"),()=>installedDetails(row)),button(text("editPackage","编辑插件包"),()=>publish({installedEntry:row})),button(text("uninstall", "卸载"), () => {
          if (confirm(text("uninstallConfirm", "卸载此插件？安装后修改过的文件将保留为本地自定义资源。"))) void act(() => call({ action: "uninstall", entry: { id: row.id, source:row.source || "store",slug:row.slug } }));
        }));
      } else {
        actions.append(button(text("details", "查看详情"), () => detail(row)));
        if (view === "mine") {
          const label = text(`state.${row.state}`, { pending: "待审核", published: "已公开", rejected: "已退回", hidden: "已下架", withdrawn:"已撤回" }[row.state]);
          card.append(node("span", label, `store-badge store-${row.state}`));if (row.reason) card.append(node("p", row.reason, "store-status"));
          actions.append(button(text("newVersion", "发布新版本"), () => publish(row)));
          if(row.state !== "withdrawn")actions.append(button(text("withdraw","撤回此版本"),()=>act(async()=>{if(confirm(text("withdrawConfirm","撤回此版本？它将停止公开或审核；已安装副本会保留。重新发布需使用新版本号。")))await call({action:"withdraw",id:row.id,revision:row.revision});})));
        } else if (installed[row.id]) card.append(node("span", text("installed", "已安装"), "store-badge"));
      }
      card.append(actions);cards.append(card);
    }
    nodes.push(cards);if (cursor) nodes.push(button(text("more", "加载更多"), () => load(true)));
    nodes.push(button(text("refresh", "刷新"), () => load()));root.replaceChildren(...nodes);
    for (const b of root.querySelectorAll("button")) b.disabled = busy;
  }
  async function detail(row) {
    if (busy) return;busy = true;
    try {
      const data = await call({ action: "detail", id: row.id }), p = data.package;
      const d = createDialog(data.name);d.append(node("p", `${data.author} · ${data.version} · ${p.manifest.license}`, "store-detail-meta"), node("p", data.description));
      const readme=node("div","","store-readme markdown-body");readme.append(renderSourceMarkdown(p.manifest.readme));d.append(readme);
      if(p.manifest.changelog){const notes=node("div","","store-readme markdown-body");notes.append(node("h3",text("changelog","更新说明")),renderSourceMarkdown(p.manifest.changelog));d.append(notes);}
      const files = node("div", "", "store-resource-list");files.append(node("h3", text("included", "包含的资源")));
      for (const r of p.resources) { const source = node("details");source.append(node("summary", `${kindName(r.kind)} / ${r.id}`), node("pre", r.content, "store-source"));for (const f of r.files || []) source.append(node("p", f.path));files.append(source); }d.append(files);
      d.append(node("p", p.manifest.platforms.join(" · "), "store-detail-meta"));
      const history=node("details","","store-history"),versions=node("div");history.append(node("summary",text("versionHistory","版本历史")),versions);d.append(history);
      let historyCursor=0,historyLoading=false,historyLoaded=false;
      const loadHistory=async append=>{if(historyLoading)return;historyLoading=true;const target=targetKey();
        try{const data=await call({action:"history",id:row.id,before:append?historyCursor:0});if(target!==targetKey()||!d.isConnected)return;
          if(!append)versions.replaceChildren();for(const version of data.items){const item=node("div","","store-version"),header=node("div","","store-card-actions");header.append(node("strong",version.version),node("span",text(`state.${version.state}`,version.state),"store-badge"),button(text("details","查看详情"),()=>detail(version)));item.append(header,node("small",new Date(version.created*1000).toLocaleString()));if(version.changelog){const notes=node("div","","markdown-body");notes.append(renderSourceMarkdown(version.changelog));item.append(notes);}versions.append(item);}
          historyCursor=data.next_before || 0;historyLoaded=true;if(historyCursor){const more=button(text("more","加载更多"),async()=>{more.remove();await loadHistory(true);});versions.append(more);}
        }catch(e){versions.append(node("p",errorMessage(e),"store-status"));}finally{historyLoading=false;}
      };
      history.addEventListener("toggle",()=>{if(history.open&&!historyLoaded)void loadHistory(false);});
      if (p.resources.some(r => r.kind === "mcp")) d.append(node("p", text("mcpSetup", "MCP 安装后保持停用。请在 MCP 管理页填写本机凭据、检查程序或地址，再启用并测试连接。"), "store-note"));
      const hasCode = containsCode(p.resources), already = Boolean(installed[row.id]);let approved = !hasCode;
      if (data.state === "published") {
        const install = button(already ? text("installed", "已安装") : text("install", "安装到当前设备"), () => {
          if (!approved || already) return;closeDialog();void act(() => call({ action: "install", id: row.id, trust_code: approved }));
        }, true);install.disabled = already || !approved;
        if (hasCode) { const label = node("label", "", "store-code-consent"), checkbox = element("input", { attrs: { type: "checkbox" } });
          label.append(checkbox, node("span", text("codeConsent", "我已检查源码并信任作者。C 扩展在墨斗进程内运行，编译注册时也能执行代码。")));checkbox.onchange = () => { approved = checkbox.checked;install.disabled = !approved || already; };d.append(label); }
        d.append(install);
      }
      busy = false;d.showModal();
    } catch (e) { feedback(e);toast(error, "error"); }
    finally { busy = false; }
  }
  function openResource(ref,row) {
    closeDialog();
    if(ref.kind.startsWith("c-")){location.hash="#/settings/extensions";return;}
    window.dispatchEvent(new CustomEvent("mdo-open-resource",{detail:{...ref,packageKey:receiptKey(row)}}));
    location.hash=`#/settings/${ref.kind}`;
  }
  function installedDetails(row) {
    const d=createDialog(row.name);d.append(node("p",`${row.source === "local" ? text("localImport","本地导入") : row.author} · ${row.version}`,"store-detail-meta"));
    if(row.manifest?.readme){const readme=node("div","","store-readme markdown-body");readme.append(renderSourceMarkdown(row.manifest.readme));d.append(readme);}
    for(const ref of resourceReferences(row)){const item=node("div","","store-resource-row");item.append(node("span",`${kindName(ref.kind)} · ${ref.id}`),button(text(ref.kind.startsWith("c-")?"viewModule":"editResource",ref.kind.startsWith("c-")?"查看 C 模块":"编辑资源"),()=>openResource(ref,row)));d.append(item);}
    d.append(node("p",text("localEdits","编辑会改变本地资源。更新前会检查修改冲突，卸载会保留修改过的文件。"),"store-note"));d.showModal();
  }
  function importPackage() {
    if(busy)return;
    const input=element("input",{attrs:{type:"file",accept:".json,application/json"}});
    input.onchange=async()=>{
      const file=input.files?.[0];if(!file)return;busy=true;
      try{
        if(file.size>1024*1024)throw Error(text("packageLimit","插件包不能超过 1 MiB。"));
        const p=JSON.parse(await file.text());if(p.format!=="mdo.extension.v1"||!p.manifest||!Array.isArray(p.resources))throw Error(text("invalidPackage","不是有效的墨斗插件源码包。"));
        makeStorePackage(packageFields(p.manifest),p.resources);await api.get("/ecosystem");
        const d=createDialog(p.manifest.name),readme=node("div","","store-readme markdown-body");readme.append(renderSourceMarkdown(p.manifest.readme));d.append(node("p",text("offlineImportHint","从本地文件导入，不代表通过线上审核。请检查全部资源；此操作无需连接商店。"),"store-note"),readme);
        for(const r of p.resources){const source=node("details");source.append(node("summary",`${kindName(r.kind)} / ${r.id}`),node("pre",r.content,"store-source"));for(const f of r.files || [])source.append(node("p",f.path));d.append(source);}
        let trust=!containsCode(p.resources);
        const install=button(text("confirmImport","确认导入"),()=>{if(!trust)return;closeDialog();view="installed";void act(()=>call({action:"import",package:p,trust_code:trust}));},true);install.disabled=!trust;
        if(!trust){const label=node("label","","store-code-consent"),check=element("input",{attrs:{type:"checkbox"}});label.append(check,node("span",text("codeConsent","我已检查源码并信任作者。C 扩展在墨斗进程内运行，编译注册时也能执行代码。")));check.onchange=()=>{trust=check.checked;install.disabled=!trust;};d.append(label);}
        d.append(install);d.showModal();
      }catch(e){toast(publicationMessage(e),"error");}finally{busy=false;}
    };input.click();
  }
  async function publish(previous = null) {
    if (busy) return;busy = true;
    try {
      // Native store access also checks login, before exposing publish tools.
      await api.get("/ecosystem");
      let selectedResources = [],savedDraft=null;
      if(previous?.draftId){savedDraft=await call({action:"draft_read",id:previous.draftId});selectedResources=savedDraft.references;previous=savedDraft.fields;}
      else if(previous?.installedEntry){const row=previous.installedEntry;selectedResources=resourceReferences(row);previous=packageFields(row.manifest || row);}
      else if (previous?.id) {
        const old = await call({ action: "detail", id: previous.id });
        selectedResources = old.package.resources;previous = { ...previous, ...old.package.manifest };
        const parts = /^(\d+)\.(\d+)\.(\d+)$/.exec(previous.version);
        if (parts) previous.version = `${parts[1]}.${parts[2]}.${Number(parts[3]) + 1}`;
      }
      const lists = await Promise.all(STORE_KINDS.map(async k => [k, (await api.get(`/extensions/${k}`)).data.items]));
      const cSources = await call({ action: "c_sources" });
      for (const k of ["c-agents", "c-subagents"]) lists.push([k, cSources.items.filter(r => r.kind === k)]);
      const d = createDialog(text("editPackage", "编辑插件包")), form = element("form", { className: "store-publish-form" });
      editor = { dirty: false, saved: false, draft:null };
      const fields = {};
      for (const [key, label, limit, multiline] of [["name", text("name", "插件名称"), 100], ["slug", text("slug", "插件 ID（小写英文）"), 64], ["version", text("version", "版本"), 32], ["description", text("description", "简短描述"), 600, true], ["readme", text("readme", "使用说明（Markdown）"), 32768, true], ["license", text("license", "许可证"), 80], ["changelog",text("changelog","更新说明"),8192,true]]) {
        const l = node("label", label), input = element(multiline ? "textarea" : "input", { attrs: { name: key, maxlength: String(limit), ...(key !== "changelog" ? {required:""} : {}), autocomplete: "off" } });
        input.value = previous?.[key] || (key === "version" ? "1.0.0" : key === "license" ? "MIT" : "");fields[key] = input;l.append(input);form.append(l);
      }
      const platformBox = element("fieldset");platformBox.append(node("legend", text("platforms", "支持的平台（按实际验证选择）")));
      const platforms = [];
      for (const p of STORE_PLATFORMS) { const label = node("label", "", "store-choice"), b = element("input", { attrs: { type: "checkbox", value: p } });b.checked = previous?.platforms?.includes(p) || false;platforms.push(b);label.append(b, node("span", p));platformBox.append(label); }form.append(platformBox);
      const choices = element("fieldset");choices.append(node("legend", text("selectResources", "选择要分享的资源（最多 16 项）")));const refs = [];
      for (const [k, list] of lists) for (const r of list) {
        if (k === "agents" && r.id === "default") continue;
        const label = node("label", "", "store-choice"), b = element("input", { attrs: { type: "checkbox" } });
        b.checked = selectedResources.some(old => old.kind === k && old.id === r.id);
        refs.push({ b, kind: k, id: r.id });label.append(b, node("span", `${kindName(k)} · ${r.name || r.id}`));choices.append(label);
      }
      for(const ref of selectedResources)if(!refs.some(r=>r.kind===ref.kind&&r.id===ref.id)){
        const label=node("label","","store-choice"),b=element("input",{attrs:{type:"checkbox"}});b.checked=true;refs.push({b,...ref});label.append(b,node("span",`${kindName(ref.kind)} · ${ref.id} (${text("resourceMissing","本地资源缺失，取消选择或先恢复资源")})`));choices.append(label);
      }
      form.append(choices, node("p", text("publishHint", "只上传选中的资源。MCP 凭据和本机工作目录会自动移除；请检查源码、说明和其他资源中是否包含私密信息。投稿经管理员审核后公开。"), "store-note"));
      const preview = node("pre", "", "store-source");preview.hidden = true;
      const message = node("p", "", "store-status");message.setAttribute("role", "status");
      const draftStatus=node("p","","store-draft-status");draftStatus.setAttribute("role","status");
      const snapshot=()=>({fields:{...Object.fromEntries(Object.entries(fields).map(([key,input])=>[key,input.value])),platforms:platforms.filter(b=>b.checked).map(b=>b.value)},references:refs.filter(r=>r.b.checked).map(({kind,id})=>({kind,id}))});
      const draftId=savedDraft?.id || `draft-${crypto.randomUUID().replaceAll("-","")}`;
      const currentEditor=editor;
      const makeDraft=(id,revision="",existing=false)=>createStoreDraft({id,revision,snapshot,save:call,existing,onStatus:(status,e)=>{if(editor!==currentEditor)return;draftStatus.textContent=status==="saving"?text("draftSaving","正在保存本地草稿…"):status==="saved"?text("draftSaved","草稿已保存到当前设备"):errorMessage(e);currentEditor.dirty=status!=="saved";}});
      editor.draft=makeDraft(draftId,savedDraft?.revision || "",Boolean(savedDraft));
      form.oninput=()=>{currentEditor.dirty=true;currentEditor.draft.schedule();};
      const actions = node("div", "", "store-card-actions");let prepared = null;
      const saveDraft=button(text("saveDraft","保存草稿"),async()=>{try{await currentEditor.draft.flush();}catch(e){if(e.status===412&&confirm(text("draftForkConfirm","草稿已被其他操作修改。将当前内容另存为新草稿？"))){currentEditor.draft.dispose();currentEditor.draft=makeDraft(`draft-${crypto.randomUUID().replaceAll("-","")}`);try{await currentEditor.draft.flush();}catch(error){message.textContent=errorMessage(error);}}else message.textContent=errorMessage(e);}});
      const build = async () => {
        const resources = (await call({ action: "export", references: refs.filter(r => r.b.checked).map(({ kind, id }) => ({ kind, id })) })).resources;
        return makeStorePackage(Object.fromEntries([...Object.entries(fields).map(([k, v]) => [k, v.value]), ["platforms", platforms.filter(b => b.checked).map(b => b.value)]]), resources);
      };
      const previewButton = button(text("preview", "预览发布内容"), async () => {
        previewButton.disabled = true;message.textContent = "";
        try { if (!form.reportValidity()) return;prepared = await build();preview.textContent = JSON.stringify(prepared, null, 2);preview.hidden = false; }
        catch (e) { message.textContent = publicationMessage(e); }finally { previewButton.disabled = false; }
      });
      const exportButton = button(text("export", "导出插件包"), async () => {
        exportButton.disabled = true;
        try { if (!form.reportValidity()) return;const p = await build();
          saveBlobFile({ blob: new Blob([JSON.stringify(p, null, 2)], { type: "application/json" }),
            filename: `${p.manifest.slug}-${p.manifest.version}.mdo-extension.json` });
        } catch (e) { message.textContent = publicationMessage(e); }finally { exportButton.disabled = false; }
      });
      const submit = element("button", { text: text("submit", "提交审核"), className: "primary-button", attrs: { type: "submit" } });
      form.onsubmit = async e => {
        e.preventDefault();if (busy) return;busy = true;submit.disabled = true;previewButton.disabled = true;message.textContent = text("submitting", "正在提交…");
        const controls = [...form.querySelectorAll("input,textarea,button")];controls.forEach(control => { control.disabled = true; });
        try { await currentEditor.draft.flush();prepared = await build();await call({ action: "submit", package: prepared });editor.saved = true;editor.dirty = false;closeDialog();view = "mine";busy = false;await load();toast(text("submitted", "投稿已提交，审核通过后会公开显示。")); }
        catch (e) { message.textContent = publicationMessage(e); }
        finally { busy = false;controls.forEach(control => { control.disabled = false; }); }
      };
      actions.append(saveDraft,previewButton, exportButton, submit);form.append(draftStatus,actions, message, preview);d.append(form);busy = false;d.showModal();
    } catch (e) { feedback(e);render(); }
    finally { busy = false; }
  }
  const unsubLocale = subscribeLocale(() => { if (!dialog?.open) render(); });
  const openPackage=event=>{view="installed";items=[];cursor=0;pendingPackageKey=event.detail?.key || null;if(visible)void load();};
  window.addEventListener("mdo-open-package",openPackage);
  const unsubTarget = subscribeTarget(() => { ++serial;items = [];installed = {};closeDialog();if (visible) void load(); });
  return {
    selectSection(section) { visible = section === "store";if (visible) void load(); },
    hasPendingChanges: () => busy || Boolean(editor?.draft?.pending && dialog?.open),
    destroy() { ++serial;closeDialog();unsubLocale();unsubTarget();window.removeEventListener("mdo-open-package",openPackage); },
  };
}
