import { setUiText as writeUiText, t as uiText, uiMessage, subscribeLocale } from "../../i18n.js";

import { localDependencyPanel } from "../settings/extension-dependencies.js";
import { createExtensionBrowser,actionMenu,extensionToggle,detailTabs } from "../../components/extension-browser.js";
import { openLayer } from "../../components/layers.js";
import { api } from "../../api/client.js";
import { createApplicationClient } from "../../api/application.js";
import { targetState } from "../../api/target.js";
import { element,errorMessage,toast } from "../../utils/dom.js";
import { createSingleResourceStore } from "../settings/single-resource-store.js";
import { createDirectoryEditor } from "../settings/directory-editor.js";
import { directoryId,directoryManifest,readDirectoryEntry,saveDirectory,changeDirectory } from "../settings/directory-documents.js";
import { resourceCredits,customizableResource } from "../settings/resource-credits.js";
import { exportDirectoryZip } from "../../api/extension-export.js";
import { reviewDependencyPlan } from "../settings/dependency-plan.js";
import { extensionInstaller } from "../../state/extension-installs.js";
import { extensionPresentation } from "../../components/extension-presentation.js";
import { downloadActive, downloadRegistry } from "../../state/downloads.js";

const node=(tag,text="",className="")=>element(tag,{text,className}),device=()=>targetState().selected?.id||"local";
const recommendedAliases=["busybox","curl","jq","openssh","aria2","ripgrep","7zip"];
const recommendedId=alias=>`external-tool:mdo.${alias}`;
const installedTool=(row,installed)=>installed.find(t=>t.directory_id===row.xid||t.id===(row.id||row.xid||row.tool_id));
export const externalToolAction=(row,installed)=>{const local=installedTool(row,installed);return !local?"install":local.installation?.version_id!==row.version_id?"update":null;};
export function createExternalToolsPanel({refresh=()=>{},openFull=()=>{},signedIn=()=>false,isMain=()=>true}={}) {
  let data={},tab="local",query="",filter="all",error="",context=device(),busy=false,editor=null,disposed=false,storeActive=false,guideController=null;
  const hosts=new Set(),recommendButtons=new Set(),browser=createExtensionBrowser({kind:"external-tools"}),sdk=createApplicationClient(()=>({extension:"mdo.external-tools"}));
  const android=()=>data.platform?.startsWith("android"),writable=()=>data.writable!==false&&(!targetState().selected||targetState().connected&&!targetState().runtimeChanged&&targetState().selected.mode!=="view");
  const report=e=>{if(e.name!=="AbortError"){error=errorMessage(e);renderAll();}};
  const button=(name,fn,disabled=false)=>{const b=element("button",{text:name,className:"secondary-button",attrs:{type:"button"}});b.disabled=disabled;b.addEventListener("click",()=>{const own=device(),failed=e=>{if(own===device()&&!disposed)report(e);};try{void Promise.resolve(fn()).catch(failed);}catch(e){failed(e);}});return b;};
  const changed=async()=>{const own=device(),snapshot=(await api.get("/cli-tools")).data;if(disposed||own!==device())return;data=snapshot;renderAll();void refresh();};
  const files=createDirectoryEditor({onChange:()=>void changed()}),store=createSingleResourceStore("external-tools",()=>void changed(),()=>{tab="store";renderAll();},()=>{tab="local";renderAll();},{installLabel:()=>android()?uiText("ui.externalDirectoryTools.installFullEdition", {}, "安装完整版"):uiText("ui.singleResourceStore.downloadAndInstall", {}, "下载并安装"),beforeInstall:async()=>{if(android()){await openFull();return false;}return true;}});
  const guide=element("aside",{className:"cli-first-use",attrs:{"aria-live":"polite"}});guide.hidden=true;document.body.append(guide);
  async function command(request){if(!writable())return false;const own=device();try{const snapshot=(await api.post("/cli-tools",request)).data;if(disposed||own!==device())return false;data=snapshot;renderAll();return true;}catch(e){if(own===device()&&!disposed){error=errorMessage(e);renderAll();}return false;}}
  function renderAll(){for(const host of hosts)render(host,data);}
  const hasRecommended=alias=>(data.items||[]).some(item=>item.id===recommendedId(alias)&&item.valid===true);
  const recommendationPending=()=>downloadRegistry.list(device()).some(job=>job.source==="install"&&job.kind==="external-tools"&&recommendedAliases.some(alias=>job.resourceId===recommendedId(alias))&&downloadActive(job));
  function updateRecommendButtons(){const pending=busy||recommendationPending();for(const control of recommendButtons){if(![...hosts].some(host=>host.contains(control))){recommendButtons.delete(control);continue;}writeUiText(control,pending?uiMessage("ui.externalDirectoryTools.installing", {}, "正在安装…"):uiMessage("ui.externalDirectoryTools.installRecommendedToolsOneClick", {}, "一键安装推荐工具"));control.disabled=pending||!writable()||context!==device();control.setAttribute("aria-busy",String(pending));}}
  async function recommend(missingOnly=false){
    if(disposed||busy||recommendationPending()||!writable()||context!==device())return;if(android()){if(await command({action:"accept"}))await openFull();return;}
    busy=true;error="";guideController=new AbortController();renderAll();const own=device(),plans=[],existing=[];
    const discard=()=>device()===own?Promise.allSettled(plans.map(({plan})=>api.post("/ecosystem",{action:"discard_plan",plan_id:plan.id,revision:plan.revision}))):Promise.resolve();
    let reviewed=false;try{
      const catalog=(await api.post("/ecosystem",{action:"catalog",kind:"external-tools",limit:100})).data;
      const aliases=recommendedAliases.filter(alias=>!missingOnly||!hasRecommended(alias)),rows=aliases.map(alias=>catalog.items.find(r=>r.alias===alias&&r.owner===0));
      if(!aliases.length)return;
      if(rows.some(r=>!r))throw Error(uiText("ui.externalDirectoryTools.theRecommendedCatalogIsNotFullyPublishedYetInstall", {}, "官方推荐目录尚未完整发布，请在工具商店安装可用工具。"));
      for(const row of rows){if(own!==device()||disposed)throw new DOMException("Device changed","AbortError");const previous=installedTool(row,data.items||[]);if(previous?.valid===true&&previous.installation?.version_id===row.version_id){existing.push({...row,directory_id:row.xid,operation:"enable",desired_enabled:true});continue;}
        plans.push({row,plan:(await api.post("/ecosystem",{action:"install_plan",kind:"external-tools",version_id:row.version_id})).data});
      }
      if(own!==device()||disposed){await discard();return;}
      const combined={ready:plans.every(({plan})=>plan.ready),items:[...plans.flatMap(({plan})=>plan.items),...existing],issues:plans.flatMap(({plan})=>plan.issues||[])};
      reviewed=true;const queued=await reviewDependencyPlan(combined,{discard,signal:guideController.signal,queue:async()=>{
        for(const row of existing){if(own!==device())break;await api.post(`/extensions/external-tools/${row.xid}/enabled`,{enabled:true});}
        const jobs=[];for(const {row,plan} of plans){if(own!==device())break;try{jobs.push(await extensionInstaller.enqueue(plan,{kind:"external-tools",name:row.name,resourceId:row.id,directoryId:row.xid,versionId:row.version_id}));}catch(e){report(e);}}
        return {queued:jobs.length};
      }});
      if(queued&&own===device()){await command({action:"accept"});await changed();}
    }finally{if(!reviewed)await discard();guideController=null;busy=false;if(own===device())renderAll();}
  }
  async function edit(item=null,owner){
    if(editor){editor.view.closeButton.focus();return;}if(android()||!writable()||item&&!customizableResource(item))return;
    const state={target:device(),busy:false,dirty:false,job:null};const view=openLayer({title:item?uiMessage("ui.externalDirectoryTools.editExternalTool", {}, "编辑外部工具"):uiMessage("ui.externalDirectoryTools.addCustomTool", {}, "添加自定义工具"),icon:"terminal",width:"50rem",height:"85%",owner,beforeClose:()=>!state.busy&&(!state.dirty||confirm(uiMessage("ui.externalDirectoryTools.discardUnsavedToolChanges", {}, "放弃未保存的工具修改？"))),onClosed(){if(editor===state)editor=null;}});state.view=view;editor=state;
    const live=()=>editor===state&&state.target===device()&&!view.signal.aborted,options=()=>({signal:view.signal}),form=element("form",{className:"extension-form"}),message=element("p",{attrs:{role:"status"}}),controls={};form.id=`${view.element.id}-tool`;
    const fields=[["id",uiText("ui.externalDirectoryTools.directoryName", {}, "目录名称")],["name",uiText("schedule.label", {}, "名称")],["description",uiText("ui.externalDirectoryTools.description", {}, "描述"),"area"],...(!item?[["directory",uiText("ui.externalDirectoryTools.sourceDirectoryCopyProgramAndDependenciesLeaveTheOriginal", {}, "来源目录（完整复制程序与依赖，不修改原目录）")]]:[]),["program",uiText("ui.externalDirectoryTools.programPathWithinTheDirectoryEGBinRg", {}, "目录内的程序路径，例如 bin/rg.exe")],["command",uiText("ui.externalDirectoryTools.commandName", {}, "命令名")],["aliases",uiText("ui.externalDirectoryTools.additionalCommandAliasesOnePerLine", {}, "其他命令别名（每行一个）"),"area"],["prompt",uiText("ui.externalDirectoryTools.injectedPromptWhenAndHowToUseTheTool", {}, "注入提示词（何时使用、如何调用）"),"area"],["version",uiText("store.version", {}, "版本")],["author",uiText("extensionBrowser.author", {}, "作者")],["copyright",uiText("extensionBrowser.copyright", {}, "版权声明"),"area"],["license",uiText("extensionBrowser.license", {}, "许可")],["links",uiText("ui.externalDirectoryTools.relatedLinksOnePerLine", {}, "相关链接（每行一个）"),"area"]];
    for(const [key,label,area]of fields){const c=element(area?"textarea":"input",{attrs:{name:key,autocomplete:"off",maxlength:key==="prompt"?"16384":"4096","aria-label":label}});form.append(element("label",{className:"extension-field"},[node("span",label),c]));controls[key]=c;}
    const save=element("button",{text:uiMessage("sidebar.save", {}, "保存"),className:"primary-button",attrs:{type:"submit",form:form.id}}),cancel=button(uiMessage("context.close", {}, "关闭"),()=>view.close());view.body.append(node("p",uiMessage("ui.externalDirectoryTools.eachToolLivesInHomeExternalToolsDirectoryProgram", {}, "每个工具保存在 Home/external-tools/<目录名>。程序路径相对于该目录；发布和导出打包整个目录，描述面向用户，注入提示词面向模型。"),"extension-help"),form,message);view.footer.append(save,cancel);
    let original=null;const lock=()=>{if(live())view.element.querySelectorAll("input,textarea,select,button").forEach(n=>n.disabled=state.busy||n===controls.id&&Boolean(item));};
    try{state.busy=true;lock();if(item)original=await readDirectoryEntry("external-tools",item.id,options());if(!live())return;const config=original?JSON.parse(original.content):{},meta=original?.manifest||{};for(const [key,c]of Object.entries(controls))c.value=key==="id"?item?.directory_id||"":key==="aliases"||key==="links"?(config[key]||[]).join("\n"):config[key]??meta[key]??(key==="version"?"1.0.0":"");}catch(e){if(live())writeUiText(message, errorMessage(e));}finally{state.busy=false;lock();}
    form.addEventListener("input",()=>{state.dirty=true;});form.addEventListener("submit",async e=>{e.preventDefault();if(!live()||state.busy)return;state.busy=true;lock();try{
      const value=Object.fromEntries(Object.entries(controls).map(([k,c])=>[k,c.value.trim()])),id=value.id;if(!directoryId(id))throw Error(uiText("ui.externalDirectoryTools.enterAValidDirectoryName", {}, "请输入有效目录名称"));if(!value.name||!value.description||!value.prompt||!value.program)throw Error(uiText("ui.externalDirectoryTools.enterANameDescriptionProgramPathAndInjectedPrompt", {}, "请填写名称、描述、程序路径和注入提示词"));
      const manifest={...(original?.manifest||directoryManifest("external-tools",id,{platforms:[data.platform||"any"]})),...Object.fromEntries(["name","description","version","author","copyright","license"].map(k=>[k,value[k]]))};
      const config={...(original?JSON.parse(original.content):{}),program:value.program,prompt:value.prompt,command:value.command||manifest.alias,aliases:value.aliases.split(/\r?\n/).filter(Boolean),links:value.links.split(/\r?\n/).filter(Boolean)},entryText=JSON.stringify(config,null,2);
      if(item)await saveDirectory("external-tools",id,manifest,[{path:manifest.entry,text:entryText}],original.revision,options());
      else{if(!value.directory)throw Error(uiText("ui.externalDirectoryTools.selectTheCompleteSourceDirectoryContainingTheProgramAnd", {}, "请选择程序及全部依赖所在的完整来源目录"));writeUiText(message, uiMessage("ui.externalDirectoryTools.copyingTheEntireSourceDirectory", {}, "正在复制整个来源目录…"));let job=await sdk.start("extension.import",{kind:"external-tools",id,directory:value.directory,manifest,entry_text:entryText,expected_revision:"",trust_code:true,enabled:false},undefined,options());state.job=job.id;
        while(!job.terminal){if(!live())return;await new Promise(r=>setTimeout(r,250));job=await sdk.call("operation.get",{id:job.id},options());}
        if(job.state!=="succeeded")throw Error(job.error?.message||uiText("ui.externalDirectoryTools.directoryImportFailed", {}, "目录导入失败"));if(!live())return;await sdk.result(job.id,options());await sdk.call("operation.release",{id:job.id},options());
      }
      if(!live())return;state.dirty=false;state.busy=false;view.dispose();await changed();toast(uiMessage("ui.externalDirectoryTools.toolDirectorySavedEnableItWhenNeeded", {}, "工具目录已保存，请按需启用"),"success");
    }catch(e){if(live())writeUiText(message, errorMessage(e));}finally{state.busy=false;lock();}});
  }
  function render(host,snapshot={}){
    if(!host||disposed)return;hosts.add(host);data=snapshot;if(context!==device()){reset();context=device();data=snapshot;}
    guide.hidden=data.guide!=="new"||!writable()||Boolean(targetState().selected)||!isMain();guide.replaceChildren(node("p",uiMessage("ui.externalDirectoryTools.commonToolsHelpTheAgentSearchDownloadConnectTo", {}, "安装常用工具可帮助 Agent 搜索、下载、连接远程设备和处理文件；之后可自由停用、替换或卸载。")),button(busy?uiMessage("ui.externalDirectoryTools.installing", {}, "正在安装…"):uiMessage("ui.externalDirectoryTools.installRecommendedTools", {}, "安装推荐工具"),recommend,busy),button(uiMessage("ui.externalDirectoryTools.donTShowAgain", {}, "不再提示"),()=>command({action:"dismiss"}),busy));
    browser.rememberFocus();const tabbar=element("div",{className:"cli-tabbar"}),tabs=element("div",{className:"store-tabs",attrs:{role:"tablist"}});for(const [id,name]of [["local",uiText("ui.externalDirectoryTools.localTools", {}, "本机工具")],["store",uiText("ui.externalDirectoryTools.toolStore", {}, "工具商店")]]){const b=button(name,()=>{if(editor||files.hasPendingChanges()||store.hasPendingChanges())return;tab=id;if(storeActive!==(tab==="store")){storeActive=tab==="store";store.activate(storeActive);}renderAll();});b.setAttribute("role","tab");b.setAttribute("aria-selected",String(tab===id));tabs.append(b);}tabbar.append(tabs);host.replaceChildren(tabbar,store.statusRoot);
    if(recommendedAliases.some(alias=>!hasRecommended(alias))){const install=button("",()=>recommend(true));install.classList.add("cli-recommend");tabbar.append(install);recommendButtons.add(install);}updateRecommendButtons();if(tab==="store"){host.append(store.root);return;}
    const toolbar=element("div",{className:"eb-toolbar"}),search=element("input",{attrs:{type:"search",placeholder:uiMessage("ui.externalDirectoryTools.searchToolsAuthorsOrUses", {}, "搜索工具、作者或用途"),"aria-label":uiMessage("ui.externalDirectoryTools.searchTools", {}, "搜索工具")}}),select=element("select",{attrs:{"aria-label":uiMessage("task.meta.state", {}, "状态")}});search.value=query;for(const [value,name]of [["all",uiText("extensionBrowser.all", {}, "全部状态")],["enabled",uiText("schedule.enabled", {}, "已启用")],["disabled",uiText("ecosystem.disabled", {}, "已停用")],["error",uiText("extensionBrowser.needsAttention", {}, "需要处理")]])select.append(element("option",{text:name,attrs:{value}}));select.value=filter;
    const zip=element("input",{attrs:{type:"file",accept:".zip"}});zip.hidden=true;zip.addEventListener("change",()=>{if(zip.files[0])void store.importPackage(zip.files[0]);});
    toolbar.append(search,select,button(uiMessage("resource.refresh", {}, "刷新"),changed),actionMenu([...(android()?[]:[button(uiMessage("ui.externalDirectoryTools.addCustomTool", {}, "添加自定义工具"),()=>edit(null,host))]),button(uiMessage("ui.externalDirectoryTools.importZip", {}, "导入 ZIP"),()=>store.importPackagePrompt?store.importPackagePrompt():zip.click())],uiText("extensionBrowser.createImport", {}, "创建 / 导入")));host.append(toolbar,zip);
    if(android())host.append(node("p",uiMessage("ui.externalDirectoryTools.androidProgramsAreInstalledAndUpdatedWithTheFull", {}, "Android 外部程序随完整版 APK 安装和更新，不能动态添加或更换执行文件。")),button(uiMessage("ui.externalDirectoryTools.installFullEdition", {}, "安装完整版"),openFull));if(error||data.binding_error)host.append(node("p",error||data.binding_error,"extension-error"));
    const draw=()=>{const rows=(data.items||[]).filter(i=>`${i.name} ${i.description} ${i.author}`.toLocaleLowerCase().includes(query.toLocaleLowerCase())).filter(i=>filter==="all"||filter==="enabled"&&i.enabled||filter==="disabled"&&!i.enabled||filter==="error"&&!i.valid).map(item=>{const credits=resourceCredits(item),custom=customizableResource(item)&&!android(),actions=[];
      if(custom)actions.push(button(uiMessage("timeline.edit", {}, "编辑"),()=>edit(item,host)),button(uiMessage("ui.externalDirectoryTools.shareToPlatform", {}, "分享到平台"),()=>store.publish(item),!signedIn()),button(uiMessage("ui.extensionPanels.directoryAndFiles", {}, "目录与文件"),()=>files.open({kind:"external-tools",item})),button(uiMessage("ui.externalDirectoryTools.exportZip", {}, "导出 ZIP"),()=>exportDirectoryZip("external-tools",item.id)));
      actions.push(...store.localActions(item));if(!android())actions.push(button(uiMessage("store.uninstall", {}, "卸载"),async()=>{if(confirm(uiMessage("ui.externalDirectoryTools.uninstallTheEntireToolDirectoryForValue1OriginalSource", { value1: item.name }, "卸载 {value1} 的整个工具目录？原始来源文件不受影响。"))){await changeDirectory("external-tools",item,"remove");await changed();}}));
      const toggle=extensionToggle(button(item.enabled?uiMessage("resource.disable", {}, "停用"):uiMessage("resource.enable", {}, "启用"),async()=>{const result=await changeDirectory("external-tools",item,"enable",{enabled:!item.enabled});if(result)await changed();},!writable()),item.enabled,item.name);
      return {...extensionPresentation(item),key:item.id,title:item.name,description:item.description,byline:credits.submitter,meta:credits.meta,toggle,actions,badge:{text:item.valid?"":item.error||uiText("extensionBrowser.needsAttention", {}, "需要处理"),tone:item.valid?"":"error"},detail:()=>detailTabs([{id:"description",label:uiMessage("backupImport.notice", {}, "说明"),content:element("div",{},[node("p",item.description),credits.facts(),localDependencyPanel("external-tools",item)])},{id:"usage",label:uiMessage("extensionBrowser.usage", {}, "使用方式"),content:element("div",{},[node("code",item.path),node("pre",item.prompt,"store-source"),node("p",uiMessage("ui.externalDirectoryTools.commandDirectoryValue1", { value1: data.bin_path||"" }, "命令目录：{value1}"))])}])};});browser.setItems(rows,{sort:true,pageKey:JSON.stringify([query,filter]),empty:uiText("ui.externalDirectoryTools.noLocalToolsAddYourOwnToolDirectoryOr", {}, "暂无本机工具，可以添加自己的工具目录或前往工具商店。")});};
    search.addEventListener("input",()=>{query=search.value;draw();});select.addEventListener("change",()=>{filter=select.value;draw();});draw();host.append(browser.root);
  }
  function reset(){guideController?.abort();editor?.view.dispose();editor=null;files.close(true);data={};error="";query="";filter="all";tab="local";storeActive=false;store.activate(false);browser.reset();guide.hidden=true;}
  const stopLocale = subscribeLocale(renderAll);
  const events=window,stopDownloads=downloadRegistry.subscribe(updateRecommendButtons),installedChanged=event=>{if(event.detail?.kind==="external-tools"&&!storeActive&&!disposed){const own=device();void changed().catch(e=>{if(!disposed&&own===device())report(e);});}};
  events.addEventListener("mdo-extensions-changed",installedChanged);
  return {render,command,reset,destroy(){stopLocale();stopDownloads();events.removeEventListener("mdo-extensions-changed",installedChanged);disposed=true;reset();files.destroy();store.destroy();browser.destroy();guide.remove();hosts.clear();recommendButtons.clear();}};
}
