import { subscribeLocale, t as uiErrorText, setUiText as writeUiText, uiMessage, t as uiText } from "../../i18n.js";

import { createExtensionBrowser, detailTabs, detailFacts, browserText } from "../../components/extension-browser.js";
import { api } from "../../api/client.js";
import { stageExtensionFile } from "../../api/extension-archives.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { element, errorMessage, toast } from "../../utils/dom.js";
import { customizableResource, resourceCredits, storeReadOnlyMessage } from "./resource-credits.js";
import { openLayer } from "../../components/layers.js";
import { renderSourceMarkdown } from "../../utils/source-markdown.js";

const names = { agents:"Agent", subagents:"SubAgent", get tools() { return uiMessage("ecosystem.toolTitle", {}, "Agent 工具"); }, get mcp() { return uiMessage("ui.singleResourceStore.mcpService", {}, "MCP 服务"); }, skills:"Skill", get commands() { return uiMessage("ecosystem.commands", {}, "编辑器命令"); }, get "external-tools"() { return uiMessage("settings.toolpacks", {}, "外部工具"); }, get knowledge() { return uiMessage("settings.knowledge", {}, "知识库"); }, get plugins() { return uiMessage("settings.extensions", {}, "插件"); } };
const text = (tag, value, className = "") => element(tag, { text: value, className });
const device = () => targetState().selected?.id || "local";
const button = (label, action, primary = false) => { const b=element("button", {text:label,className:primary?"primary-button":"secondary-button",attrs:{type:"button"}});b.addEventListener("click",()=>{void Promise.resolve().then(action).catch(e=>toast(errorMessage(e),"error"));});return b; };
export const singleResource = (row, kind) => (row?.kind || row?.manifest?.kind) === kind;

// All types share real directory archives. No resource bodies or files are
// reconstructed from catalog JSON; text previews request one selected file.
export function createSingleResourceStore(kind, onChange, onBrowse=()=>{}, onManage=null, policy={}) {
  if (!names[kind]) throw Error(uiErrorText("error.detail.unsupportedExtensionType", {}, "Unsupported extension type"));
  const root=text("div","","single-resource-store"),statusRoot=element("p",{className:"extension-help",attrs:{role:"status"}});
  const browser=createExtensionBrowser({kind,onClose:()=>{++detailEpoch;detailAbort?.abort();}});
  let items=[],installed={},query="",applied="",state="all",total=0,epoch=0,detailEpoch=0,controller=null,detailAbort=null,dialog=null,busy=false,saving=false,dirty=false,visible=false,disposed=false,activeTarget=device();
  const live=(n,target)=>!disposed&&n===epoch&&target===device();
  const call=async(body,signal=controller?.signal)=>(await api.post("/ecosystem",{kind,...body},{signal})).data;
  const owned=row=>Object.values(installed).find(local=>local.kind===kind&&local.id===row.xid&&local.source==="store");
  function report(error){writeUiText(statusRoot, errorMessage(error));statusRoot.className="extension-error";}
  function close(force=false){if(!dialog)return true;if(!force&&(saving||dirty&&!confirm(uiMessage("ui.singleResourceStore.discardUnsubmittedInformation", {}, "放弃未提交的信息？"))))return false;dialog.dispose();dialog=null;dirty=saving=false;return true;}
  function modal(title){if(!close())return null;const view=openLayer({title,icon:kind==="skills"?"spark":"folder",width:"48rem",height:"80%",beforeClose:()=>!saving&&(!dirty||confirm(uiMessage("ui.singleResourceStore.discardUnsubmittedInformation", {}, "放弃未提交的信息？"))),onClosed(){if(dialog===view){dialog=null;dirty=saving=false;}}});dialog=view;return view;}
  async function load(append=false){
    if(busy||disposed)return;const n=++epoch,target=device();controller?.abort();controller=new AbortController();busy=true;render();
    try{const q=append?applied:query;const [data,local]=await Promise.all([call({action:"catalog",q,offset:append?items.length:0,limit:100}),api.get("/ecosystem",{signal:controller.signal})]);if(!live(n,target))return;
      items=append?[...new Map([...items,...data.items].map(row=>[row.version_id,row])).values()]:data.items;total=data.total;installed=local.data;applied=q;writeUiText(statusRoot, "");
    }catch(e){if(live(n,target)&&e.name!=="AbortError")report(e);}finally{if(live(n,target)){busy=false;render();}}
  }
  function render(){
    browser.rememberFocus();const form=element("form",{className:"store-search eb-toolbar"}),input=element("input",{attrs:{type:"search",maxlength:"240",placeholder:uiMessage("ui.singleResourceStore.searchValue1AuthorsOrUses", { value1: names[kind] }, "搜索{value1}、作者或用途"),"aria-label":uiMessage("ui.singleResourceStore.searchValue1", { value1: names[kind] }, "搜索{value1}")}});input.value=query;input.addEventListener("input",()=>{query=input.value;});
    const select=element("select",{attrs:{"aria-label":uiMessage("task.meta.state", {}, "状态")}});for(const [value,label]of [["all",uiText("extensionBrowser.all", {}, "全部状态")],["installed",uiText("store.installed", {}, "已安装")],["updates",uiText("extensionBrowser.updates", {}, "可更新")],["new",uiText("extensionBrowser.notInstalled", {}, "未安装")]])select.append(element("option",{text:label,attrs:{value}}));select.value=state;select.addEventListener("change",()=>{state=select.value;render();});
    form.append(input,select,element("button",{text:uiMessage("store.searchButton", {}, "搜索"),className:"secondary-button",attrs:{type:"submit"}}),button(uiMessage("resource.refresh", {}, "刷新"),()=>load()));form.addEventListener("submit",e=>{e.preventDefault();query=input.value.trim();void load();});for(const b of form.querySelectorAll("button"))b.disabled=busy;
    const rows=items.filter(row=>{const local=owned(row),same=local?.installation?.version_id===row.version_id;return state==="all"||state==="installed"&&local||state==="updates"&&local&&!same||state==="new"&&!local;}).map(row=>{
      const local=owned(row),same=local?.installation?.version_id===row.version_id,credits=resourceCredits({...row,source:"store"});const primary=button(same?uiMessage("extensionBrowser.manage", {}, "管理"):local?uiMessage("extensionBrowser.update", {}, "更新"):uiMessage("extensionBrowser.install", {}, "安装"),()=>same&&onManage?onManage(local):detail(row));primary.disabled=busy;
      return {key:row.version_id,kind,title:row.name,byline:credits.submitter,description:row.description,meta:credits.meta,badge:{text:same?uiText("store.installed", {}, "已安装"):local?uiText("extensionBrowser.updates", {}, "可更新"):""},primary,onSelect:()=>loadDetail(row)};
    });
    browser.setItems(rows,{busy,pageKey:JSON.stringify([applied,state]),onNext:items.length<total?()=>load(true):null,empty:uiText("ui.singleResourceStore.noMatchingValue1", { value1: names[kind] }, "暂无匹配的{value1}。")});root.replaceChildren(form,browser.root);
  }
  function detail(row){onBrowse();if(!items.some(r=>r.version_id===row.version_id))items.unshift(row);state="all";render();browser.open(row.version_id);}
  async function filesView(row,host,signal,valid){
    let offset=0;const more=button(uiMessage("ui.singleResourceStore.moreFiles", {}, "更多文件"),()=>next()),list=text("div","");host.replaceChildren(list,more);
    async function next(){more.disabled=true;try{const page=await call({action:"files",version_id:row.version_id,offset,limit:50},signal);if(!valid())return;
      for(const f of page.items){const line=text("div","","eb-toolbar"),label=`${f.path}${f.directory?"/":""} · ${f.bytes} B`;line.append(f.directory?text("span",label):button(label,async()=>{const file=await call({action:"preview",version_id:row.version_id,path:f.path},signal);if(!valid())return;const v=modal(f.path);if(!v)return;v.body.append(file.binary?text("p",uiMessage("ui.singleResourceStore.thisFileIsBinaryOrExceedsThePreviewLimit", {}, "此文件为二进制或超过文本预览上限，请通过 ZIP 下载检查。")):text("pre",file.text,"store-source"));v.footer.append(button(uiMessage("context.close", {}, "关闭"),()=>v.close()));}));list.append(line);}
      offset+=page.items.length;more.hidden=offset>=page.total;
    }catch(e){if(valid()&&e.name!=="AbortError")host.append(text("p",errorMessage(e),"extension-error"));}finally{more.disabled=false;}}
    await next();
  }
  async function loadDetail(row){
    const n=++detailEpoch,target=device(),key=row.version_id;detailAbort?.abort();detailAbort=new AbortController();const signal=detailAbort.signal,valid=()=>!disposed&&n===detailEpoch&&target===device()&&browser.isSelected(key);
    try{const data=await call({action:"detail",version_id:key},signal);if(!valid())return;const credits=resourceCredits({...data,source:"store"}),description=text("div","");description.append(text("p",data.description||""),credits.facts(),detailFacts([[uiMessage("ui.singleResourceStore.alias", {}, "别名"),data.alias],[uiMessage("extensionBrowser.platform", {}, "支持平台"),data.platforms?.join(" / ")],[uiMessage("ui.singleResourceStore.archiveSize", {}, "归档大小"),`${data.bytes} B`],["SHA-256",data.sha256]]));
      if(data.manifest?.readme){const readme=await call({action:"preview",version_id:key,path:data.manifest.readme},signal);if(!valid())return;if(!readme.binary)description.append(renderSourceMarkdown(readme.text));}
      const files=text("div",""),tabs=detailTabs([{id:"description",label:uiMessage("backupImport.notice", {}, "说明"),content:description},{id:"files",label:uiMessage("ui.singleResourceStore.sourceAndFiles", {}, "源码与文件"),content:files}]);browser.showDetail(key,tabs);void filesView(data,files,signal,valid);
      let trusted=!["agents","subagents","tools","commands","mcp","plugins","external-tools"].includes(kind);const same=owned(data)?.installation?.version_id===key;
      const install=button(same?uiMessage("store.installed", {}, "已安装"):policy.installLabel?.()||uiMessage("ui.singleResourceStore.downloadAndInstall", {}, "下载并安装"),async()=>{if(!valid()||!trusted||busy||same)return;if(policy.beforeInstall&&!await policy.beforeInstall())return;busy=true;install.disabled=true;try{
        let previous=null;try{previous=(await api.get(`/extensions/${kind}/${encodeURIComponent(data.xid)}`,{signal})).data;}catch(e){if(e.status!==404)throw e;}
        const task=await call({action:"download",version_id:key},signal);if(!valid())return;
        await api.post(`/extension-tasks/${task.id}/install`,{trust_code:trusted,expected_revision:previous?.revision||"",...(!previous?{enabled:kind!=="plugins"}:{})},{signal});if(!valid())return;browser.closeDetail();onChange();busy=false;await load();
      }catch(e){if(valid()&&e.name!=="AbortError")report(e);}finally{busy=false;install.disabled=same||!trusted;}},true);install.disabled=same||!trusted;
      if(!trusted){const check=element("input",{attrs:{type:"checkbox"}});check.addEventListener("change",()=>{trusted=check.checked;install.disabled=!trusted||same;});description.append(element("label",{className:"store-code-consent"},[check,text("span",uiMessage("ui.singleResourceStore.iReviewedTheFilesAndTrustTheAuthorAnd", {}, "我已检查文件并信任作者，允许此扩展在设备上运行。")) ]));}
      browser.setActions(key,install,button(uiMessage("context.close", {}, "关闭"),()=>browser.closeDetail()));
    }catch(e){if(valid()&&e.name!=="AbortError")browser.showDetail(key,text("p",errorMessage(e),"extension-error"));}
  }
  async function publish(item){
    if(busy||disposed)return;if(!customizableResource(item)){toast(storeReadOnlyMessage(),"error");return;}
    const view=modal(uiText("ui.singleResourceStore.shareValue1Value2", { value1: names[kind], value2: item.name||item.id }, "分享{value1} · {value2}"));if(!view)return;const target=device(),form=element("form",{className:"store-publish-form"}),message=text("p",uiMessage("ui.singleResourceStore.readingAccountQuota", {}, "正在读取账号配额…"));form.id=`${view.element.id}-publish`;view.body.append(form,message);let task=null,proofTask=null;
    const add=(label,control)=>{form.append(element("label",{className:"extension-field"},[text("span",label),control]));return control;};
    const notes=add(uiMessage("ui.singleResourceStore.reviewNotesPurposeBuildAndRequiredPermissions", {}, "审核说明：用途、构建与所需权限"),element("textarea",{attrs:{maxlength:"8000"}})),source=add(uiMessage("ui.singleResourceStore.sourceUrlHttpsOptional", {}, "源码链接（HTTPS，可选）"),element("input",{attrs:{maxlength:"2000",type:"url"}})),included=add(uiMessage("ui.singleResourceStore.zipIncludesSourceOrReviewMaterials", {}, "ZIP 中包含源码或审核资料"),element("input",{attrs:{type:"checkbox"}})),proof=add(uiMessage("ui.singleResourceStore.reviewAttachmentOptional", {}, "审核附件（可选）"),element("input",{attrs:{type:"file"}})),publicly=add(uiMessage("ui.singleResourceStore.requestPublicationPublicAfterApproval", {}, "申请公布（审核通过后公开）"),element("input",{attrs:{type:"checkbox"}})),work=add(uiMessage("ui.singleResourceStore.work", {}, "作品"),element("select"));publicly.checked=true;work.append(element("option",{text:uiMessage("ui.singleResourceStore.createNewWork", {}, "创建新作品"),attrs:{value:""}}));
    const submit=element("button",{text:uiMessage("ui.singleResourceStore.submitWork", {}, "提交作品"),className:"primary-button",attrs:{type:"submit",form:form.id}});submit.disabled=true;view.footer.append(submit,button(uiMessage("sidebar.cancel", {}, "取消"),()=>view.close()));form.addEventListener("input",()=>{dirty=true;});
    try{const [profile,mine]=await Promise.all([call({action:"profile"},view.signal),call({action:"mine",kind,limit:100},view.signal)]);if(dialog!==view||device()!==target)return;
      writeUiText(message, uiMessage("ui.singleResourceStore.value1Value2WorksZipAndReviewAttachmentsAreLimited", { value1: profile.works, value2: profile.work_limit, value3: Math.floor(profile.version_bytes/1048576) }, "{value1}/{value2} 个作品；每版本 ZIP 与审核附件合计最多 {value3} MiB。发布整个扩展目录，请先在本机编辑其名称、描述、作者、许可和版本。"));
      const unique=new Map(mine.items.map(row=>[row.xid,row]));for(const row of unique.values())work.append(element("option",{text:uiMessage("ui.singleResourceStore.updateValue1Value2", { value1: row.name, value2: row.xid }, "更新 {value1} · {value2}"),attrs:{value:row.xid,"data-revision":row.work_revision}}));submit.disabled=false;
    }catch(e){if(dialog===view)writeUiText(message, errorMessage(e));}
    form.addEventListener("submit",async e=>{e.preventDefault();if(saving||dialog!==view||device()!==target)return;
      if(publicly.checked&&(!notes.value.trim()||!included.checked&&!source.value.trim()&&!proof.files[0])){writeUiText(message, uiMessage("ui.singleResourceStore.publicationRequiresReviewNotesAndSourceFilesASource", {}, "申请公布需要审核说明，以及源码、源码链接或审核附件。"));return;}
      saving=true;view.closeButton.disabled=true;for(const c of form.elements)c.disabled=true;submit.disabled=true;const current=()=>dialog===view&&!view.signal.aborted&&device()===target;
      try{writeUiText(message, uiMessage("ui.singleResourceStore.archivingAndUploadingTheEntireDirectory", {}, "正在打包完整目录并上传…"));if(!task)task=(await api.post(`/extensions/${kind}/${encodeURIComponent(item.id)}/pack`,{}, {signal:view.signal})).data;
        if(proof.files[0]&&!proofTask)proofTask=await stageExtensionFile(proof.files[0],{purpose:"review",signal:view.signal});
        const selected=work.selectedOptions[0],result=await call({action:"submit",task:task.id,...(proofTask?{proof_task:proofTask.id}:{}),publish:publicly.checked,review_notes:notes.value.trim(),source_url:source.value.trim(),source_included:included.checked,...(work.value?{work:work.value,expected_revision:Number(selected.dataset.revision)}:{})},view.signal);
        if(!current())return;dirty=false;saving=false;close(true);toast(result.state==="pending"?uiMessage("ui.singleResourceStore.workSubmittedForReview", {}, "作品已提交审核"):uiMessage("ui.singleResourceStore.privateWorkSaved", {}, "私有作品已保存"),"success");
      }catch(e){if(current())writeUiText(message, uiMessage("ui.singleResourceStore.value1TheArchiveTaskIsRetainedForRetryOr", { value1: errorMessage(e) }, "{value1}。归档任务已保留，可重试；也可在下载任务中取消。"));}finally{if(current()){saving=false;view.closeButton.disabled=false;submit.disabled=false;for(const c of form.elements)c.disabled=false;}}
    });
  }
  function localActions(receipt){if(receipt?.source!=="store"||receipt.kind!==kind)return[];return[button(uiMessage("ui.singleResourceStore.checkStoreUpdates", {}, "检查商店更新"),()=>{query=receipt.alias||receipt.name||"";void load();onBrowse();})];}
  async function importPackage(file){
    if(policy.beforeInstall&&!await policy.beforeInstall())return;
    if(!(file instanceof Blob))throw Error(uiText("ui.singleResourceStore.selectAZipOfTheEntireExtensionDirectoryJson", {}, "请选择整个扩展目录的 ZIP 文件；JSON 包已停止支持"));const view=modal(uiText("ui.singleResourceStore.importValue1Zip", { value1: names[kind] }, "导入{value1} ZIP"));if(!view)return;const message=text("p",`${file.name||"extension.zip"} · ${file.size} B`),identity=element("input",{attrs:{maxlength:"96",placeholder:uiMessage("ui.singleResourceStore.localDirectoryName", {}, "本机目录名称")}}),trust=element("input",{attrs:{type:"checkbox"}});view.body.append(message,identity,element("label",{},[trust,text("span",uiMessage("ui.singleResourceStore.iReviewedThisZipAndTrustItsContents", {}, "我已检查此 ZIP 并信任其内容")) ]));
    const install=button(uiMessage("ecosystem.import", {}, "导入"),async()=>{if(saving||!trust.checked)return;if(!/^[A-Za-z0-9_-][A-Za-z0-9._-]{0,95}$/.test(identity.value.trim()))throw Error(uiText("ui.singleResourceStore.enterAValidLocalDirectoryName", {}, "请填写有效的本机目录名称"));saving=true;install.disabled=true;try{const task=await stageExtensionFile(file,{kind,signal:view.signal});await api.post(`/extension-tasks/${task.id}/install`,{id:identity.value.trim(),trust_code:true,enabled:false},{signal:view.signal});saving=false;close(true);onChange();await load();}catch(e){writeUiText(message, errorMessage(e));}finally{saving=false;install.disabled=false;}},true);view.footer.append(install,button(uiMessage("sidebar.cancel", {}, "取消"),()=>view.close()));
  }
  const unsubscribe=subscribeTarget(()=>{if(device()===activeTarget)return;activeTarget=device();++epoch;++detailEpoch;controller?.abort();detailAbort?.abort();browser.reset();close(true);items=[];installed={};total=0;busy=false;if(visible)void load();});render();
  const stopLocale = subscribeLocale(render);
  return {root,statusRoot,publish,localActions,importPackage,activate(value){visible=value;if(!value)browser.closeDetail(false);else if(!busy)void load();},hasPendingChanges:()=>Boolean(dialog)||saving,destroy(){disposed=true;++epoch;controller?.abort();detailAbort?.abort();browser.destroy();unsubscribe();close(true);}};
}
