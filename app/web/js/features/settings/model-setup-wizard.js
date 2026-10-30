import { element, clear, errorMessage } from "../../utils/dom.js";
import { t, subscribeLocale } from "../../i18n.js";
import { api } from "../../api/client.js";
import { createModelProxyEditor } from "./model-proxy-editor.js";
import { presetProvider, presetModel, suggestedModels } from "./provider-presets.js";
import { parameterDraft, parameterIssues, applyParameters, createParameterEditor, parameterValue } from "./model-parameters.js";

const text=(key,fallback,params={})=>t(`modelWizard.${key}`,params,fallback);
const clone=value=>structuredClone(value);
let serial=0;
export function createModelSetupWizard({preset,choice,provider=null,config,etag,onCommit=()=>{},onDone=()=>{},onCancel=()=>{}}) {
  const root=element("section",{className:"model-setup-wizard"}),actions=element("div",{className:"supplier-actions model-wizard-actions"}),uid=`model-wizard-${++serial}`;
  let footerHost=null,renderedStage=0;
  let current=clone(config),revision=etag,connection=clone(provider||presetProvider(preset,choice,config.providers));
  let existing=Boolean(provider),key="",ref="",modelProxy={kind:"provider"},advancedOpen=false;
  let stage=1,mode="auto",manual="",ids=[],records=new Map(),selected=new Set(),errors=new Map();
  let discovered=null,loading=false,running=false,disposed=false,epoch=0,requestController=null,runController=null;
  let notice="",added=0,progress="",query="",defaultRequested=false,defaultApplied=false,parameterLoading=new Set();
  const stepNames=()=>[text("connection","连接供应商"),text("source","选择模型来源"),text("parameters","确认模型与参数"),text("verify","验证并添加")];
  const button=(label,execute,primary=false)=>{
    const node=element("button",{text:label,className:primary?"primary-button":"secondary-button",attrs:{type:"button"}});
    node.onclick=()=>void execute();return node;
  };
  function input(label,name,value,changed,{type="text",required=false,area=false}={}) {
    const control=element(area?"textarea":"input",{attrs:{name,type:area?null:type,required:required?"":null,autocomplete:type==="password"?"new-password":"off",maxLength:area?65536:type==="password"?4096:2048}});
    control.value=value;control.addEventListener("input",()=>changed(control.value));
    return element("label",{className:"model-field"},[element("span",{text:label}),control]);
  }
  function cancelRequest() { ++epoch;requestController?.abort();requestController=null;loading=false;parameterLoading.clear(); }
  function invalidate() {
    cancelRequest();discovered=null;
    for(const [id,value]of records){
      value.record.fields=Object.fromEntries(Object.entries(value.record.fields).filter(([,field])=>field.source==="manual"));
      if(!Object.keys(value.record.fields).length)records.delete(id);
    }
  }
  function endpoint() {
    const value=clone(connection);
    if(!existing&&ref.trim())value.credential={secret_ref:ref.trim()};
    return value;
  }
  const protocol=()=>existing?(connection.endpoints.responses?"openai-responses":connection.endpoints.chat_completions?"openai-chat-completions":"anthropic-messages"):choice.protocol;
  const chosen=()=>ids.filter(id=>selected.has(id));
  function normalized(record) {
    const value=parameterDraft(record);const context=parameterValue(value.record,"context_tokens"),limit=parameterValue(value.record,"max_output_tokens");
    if(context&&value.output>=context)value.output=Math.max(1,Math.min(limit||8192,8192,Math.floor(context/4)));
    return value;
  }
  function sourceIds() {
    return [...new Set((mode==="manual"?manual.split(/[\n,，;；]+/):discovered?.items||[]).map(id=>typeof id==="string"?id.trim():id.id).filter(Boolean))];
  }
  function createRows() {
    ids=sourceIds().filter(id=>!current.items.some(m=>m.provider===connection.id&&m.wire_model===id));
    const recommendations=mode==="manual"?ids:suggestedModels(ids,choice.models);
    selected=new Set(recommendations);errors.clear();
    for(const id of ids)if(!records.has(id))records.set(id,normalized({id,protocol:protocol(),fields:{}}));
  }
  async function discover() {
    if(disposed||running)return;
    cancelRequest();const token=epoch;requestController=new AbortController();loading=true;notice="";render();
    try {
      const body={provider:endpoint(),protocol:protocol()};if(key.trim())body.key=key.trim();
      const result=await api.post("/models/discover",body,{signal:requestController.signal});
      if(disposed||token!==epoch||mode!=="auto")return;
      discovered=result.data;
      for(const record of result.data.parameters||[]){
        const value=normalized(record),old=records.get(record.id);
        for(const [name,field]of Object.entries(old?.record.fields||{}))if(field.source==="manual")value.record.fields[name]=clone(field);
        if(old?.budgetEdited){value.output=old.output;value.budgetEdited=true;}
        if(old?.effortEdited){value.effort=old.effort;value.effortEdited=true;}
        records.set(record.id,value);
      }
    }catch(error){if(!disposed&&token===epoch&&error.name!=="AbortError")notice=errorMessage(error);}
    finally{if(!disposed&&token===epoch){loading=false;render();}}
  }
  function switchSource(next) {
    if(running)return;
    cancelRequest();mode=next;notice="";render();
    if(mode==="auto"&&!discovered)void discover();
  }
  async function resolve() {
    requestController?.abort();const control=new AbortController();requestController=control;const token=++epoch;
    const missing=ids.filter(id=>["context_tokens","max_output_tokens","reasoning"].some(field=>!records.get(id)?.record.fields[field]));
    parameterLoading=new Set(missing);render();
    try {
      for(let offset=0;offset<missing.length;offset+=32){
        const batch=missing.slice(offset,offset+32);
        const result=await api.post("/models/parameters",{provider:endpoint(),protocol:protocol(),models:batch,catalog_only:true,refresh_catalog:offset===0},{signal:control.signal});
        if(disposed||token!==epoch)return;
        for(const row of result.data.items){
          const old=records.get(row.id),record=clone(row);
          for(const [name,field]of Object.entries(old?.record.fields||{}))if(["manual","api"].includes(field.source))record.fields[name]=clone(field);
          // Compute an automatic budget from the effective merged limits, not
          // the catalog's larger limits before the API fields take precedence.
          const merged=normalized(record);
          if(old?.budgetEdited) {merged.output=old.output;merged.budgetEdited=true;}
          if(old?.effortEdited){merged.effort=old.effort;merged.effortEdited=true;}
          records.set(row.id,merged);parameterLoading.delete(row.id);
        }
        for(const id of batch)parameterLoading.delete(id);render();
      }
    }catch(error){if(!disposed&&token===epoch&&error.name!=="AbortError")notice=text("lookupFailed","部分参数未能自动识别，请补全标记的字段。")+" "+errorMessage(error);}
    finally{if(!disposed&&token===epoch){parameterLoading.clear();render();}}
  }
  function canVerify(){return chosen().length>0&&chosen().every(id=>!parameterLoading.has(id)&&!parameterIssues(records.get(id)).length);}
  function updatePrimary(){const node=actions.querySelector('[data-wizard-submit]');if(node){node.disabled=!canVerify();node.textContent=text("addVerify","添加并验证（{count}）",{count:chosen().length});}}
  async function readCurrent(signal,adopt=false) {
    const response=await api.get("/models/config",{signal});current=response.data;revision=response.etag;
    const saved=current.providers.find(p=>p.id===connection.id);
    if(saved&&adopt){connection=clone(saved);existing=true;key="";ref="";}
    onCommit(response);return response;
  }
  function installed(model) {
    const found=current.items.find(item=>item.id===model.id);
    return found&&["provider","wire_model","default_protocol","default_reasoning_effort","window","proxy","capabilities","reasoning_efforts","attachments"]
      .every(key=>JSON.stringify(found[key])===JSON.stringify(model[key]))&&current.verification?.[model.id]?.verified===true;
  }
  async function verify() {
    if(running||!canVerify())return;
    requestController?.abort();++epoch;parameterLoading.clear();running=true;stage=4;notice="";
    const control=new AbortController();runController=control;
    const work=chosen();const completed=new Set();let refreshed=false;
    try {
      await readCurrent(control.signal);refreshed=true;
      const already=current.providers.find(p=>p.id===connection.id);
      if((!existing&&already)||(existing&&JSON.stringify(already)!==JSON.stringify(connection)))
        throw new Error(text("connectionChanged","供应商连接已变化，请返回列表重新选择。"));
      for(const [index,id]of work.entries()){
        control.signal.throwIfAborted();progress=text("progress","正在验证并添加 {index}/{total}：{model}",{index:index+1,total:work.length,model:id});render();
        const model=applyParameters(presetModel(connection,id,current.items),records.get(id));model.proxy=clone(modelProxy);
        const body={provider:endpoint(),model,make_default:defaultRequested&&!defaultApplied};if(key.trim())body.key=key.trim();
        let acknowledged=false;
        try {
          const result=await api.post("/models/add",body,{ifMatch:revision,signal:control.signal});
          acknowledged=result.data.added===true;
          if(!acknowledged)throw new Error(text("notAdded","服务未确认模型已添加，请刷新列表。"));
          completed.add(id);++added;key="";
          if(body.make_default&&result.data.agent_ready)defaultApplied=true;
          if(!result.data.verification_saved)notice=text("evidenceWarning","模型已添加，但验证记录未保存；可稍后在列表重新验证。");
          await readCurrent(control.signal,true);
        }catch(error){
          // A lost reply can follow a committed write. Read first; do not
          // automatically repeat a billed probe or add the same model again.
          try {await readCurrent();refreshed=true;}catch {refreshed=false;}
          if(!acknowledged&&installed(model)){completed.add(id);++added;connection=clone(current.providers.find(p=>p.id===connection.id));existing=true;key="";if(current.default_model===model.id)defaultApplied=true;}
          else if(!acknowledged&&!control.signal.aborted)errors.set(id,errorMessage(error));
          if(control.signal.aborted||!refreshed||[409,412].includes(error.status))break;
        }
      }
    }catch(error){if(error.name!=="AbortError")notice=errorMessage(error);}
    finally {
      runController=null;running=false;
      if(disposed)return;
      ids=work.filter(id=>!completed.has(id));selected=new Set(ids);
      if(!ids.length){onDone({providerId:connection.id,added,notice});return;}
      // Only failed or unattempted models remain in the retry page.
      stage=3;progress="";
      notice=(control.signal.aborted?text("stopped","已停止，未完成的模型可稍后继续。"):
        added?text("partial","已添加 {count} 个模型；请处理剩余模型。",{count:added}):text("retry","请修改未通过的模型后重试。"))+(notice?" "+notice:"");
      render();
    }
  }
  function connectionPage(body) {
    if(!existing&&preset.variants.length>1){
      const select=element("select",{attrs:{name:"service-variant"}});
      for(const variant of preset.variants)select.append(element("option",{text:t(`modelSetup.${variant.label}`,{},variant.label),attrs:{value:variant.id}}));
      select.value=choice.id;select.addEventListener("change",()=>{
        const previous=connection;choice=preset.variants.find(v=>v.id===select.value);connection=presetProvider(preset,choice,current.providers);
        connection.proxy=previous.proxy;invalidate();records.clear();render();
      });
      body.append(element("label",{className:"model-field"},[element("span",{text:text("apiProduct","API 产品")}),select]));
    }
    if(choice.key_note)body.append(element("p",{className:"model-parameter-warning",text:choice.key_note}));
    if(choice.source_url&&/^https:\/\//.test(choice.source_url))body.append(element("a",{text:text("productDocs","接口与套餐说明"),attrs:{href:choice.source_url,target:"_blank",rel:"noopener noreferrer"}}));
    if(existing)body.append(element("p",{text:text("savedConnection","使用已保存的供应商连接与凭据。") }));
    else body.append(input("API Key","api-key",key,value=>{key=value;invalidate();},{type:"password",required:!preset.local&&!ref.trim()}));
    if(!existing&&preset.keyUrl)body.append(element("a",{text:t("modelSetup.getKey",{},"获取 API Key"),attrs:{href:preset.keyUrl,target:"_blank",rel:"noopener noreferrer"}}));
    const advanced=element("details",{className:"supplier-connection-advanced"},[element("summary",{text:t("modelSetup.advancedConnection",{},"高级设置")})]);advanced.open=advancedOpen;
    advanced.addEventListener("toggle",()=>{if(root.contains(advanced))advancedOpen=advanced.open;});
    if(!existing){
      advanced.append(input(text("supplierName","供应商名称"),"connection-name",connection.name,value=>{connection.name=value;}));
      advanced.append(input(text("baseUrl","服务地址（含 /v1）"),"base-url",choice.base,value=>{
        choice={...choice,base:value};const previous=connection;connection={...presetProvider(preset,choice,[],value),id:previous.id,name:previous.name,proxy:previous.proxy};invalidate();records.clear();
      },{type:"url",required:true}));
      advanced.append(input(t("modelSetup.credentialReference",{},"凭据引用（可代替 Key）"),"credential-ref",ref,value=>{ref=value;invalidate();const field=root.querySelector('input[name="api-key"]');if(field)field.required=!preset.local&&!ref.trim();}));
    }
    const proxy=createModelProxyEditor(connection.proxy,{collapsible:false,titleKey:"modelProxy.providerTitle",namePrefix:"provider_proxy_",onChange:value=>{connection.proxy=value;invalidate();}});
    if(existing)for(const node of proxy.element.querySelectorAll("input, select"))node.disabled=true;
    advanced.append(proxy.element,createModelProxyEditor(modelProxy,{allowProvider:true,collapsible:false,titleKey:"modelProxy.newModelsTitle",namePrefix:"model_proxy_",onChange:value=>{modelProxy=value;}}).element);body.append(advanced);
  }
  function sourcePage(body) {
    const tabs=element("div",{className:"model-source-tabs",attrs:{role:"tablist","aria-label":text("source","选择模型来源")}});
    for(const [value,label]of [["auto",text("automatic","自动获取")],["manual",text("manual","手动填写")]]){
      const tab=button(label,()=>switchSource(value));tab.setAttribute("role","tab");tab.id=uid+'-'+value;
      tab.setAttribute("aria-selected",String(mode===value));tab.setAttribute("aria-controls",uid+'-source');tab.tabIndex=mode===value?0:-1;
      tab.addEventListener("keydown",event=>{
        if(!["ArrowLeft","ArrowRight","Home","End"].includes(event.key))return;
        event.preventDefault();const next=event.key==="Home"?"auto":event.key==="End"?"manual":mode==="auto"?"manual":"auto";
        switchSource(next);root.querySelector('#'+uid+'-'+next)?.focus();
      });tabs.append(tab);
    }
    const panel=element("div",{className:"model-source-panel",attrs:{id:uid+'-source',role:"tabpanel","aria-labelledby":uid+'-'+mode}});body.append(tabs,panel);
    if(mode==="auto"){
      panel.append(element("p",{text:loading?text("discovering","正在自动获取模型列表…"):discovered?text("discovered","已获取 {count} 个模型，下一步选择要添加的模型。",{count:discovered.items.length}):text("discoveryFailed","未能获取模型列表，可重试或切换手动填写。"),attrs:{role:"status"}}));
      if(!loading&&!discovered)panel.append(button(text("retryFetch","重试"),discover));
    }else panel.append(input(text("modelIds","模型 ID（每行一个，也可用逗号分隔）"),"manual-models",manual,value=>{manual=value;const next=actions.querySelector('[data-wizard-next]');if(next)next.disabled=!sourceIds().length;}, {area:true,required:true}));
  }
  function parametersPage(body) {
    const filter=input(text("search","搜索模型"),"model-query",query,value=>{query=value;
      for(const row of body.querySelectorAll(".model-wizard-row"))row.hidden=!row.dataset.model.toLowerCase().includes(value.toLowerCase());
    });body.append(filter);
    if(!ids.length)body.append(element("p",{text:text("none","没有可添加的新模型，请返回修改来源。") }));
    for(const [index,id]of ids.entries()){
      const row=element("article",{className:"model-wizard-row"});row.dataset.model=id;
      const check=element("input",{attrs:{type:"checkbox",name:"selected-model",value:id}});check.checked=selected.has(id);
      const toggleEditor=()=>{editor.hidden=!check.checked;for(const field of editor.querySelectorAll("input, select"))field.disabled=!check.checked;};
      check.addEventListener("change",()=>{if(check.checked)selected.add(id);else selected.delete(id);toggleEditor();updatePrimary();});
      row.append(element("label",{className:"model-wizard-model"},[check,element("strong",{text:id})]));
      if(parameterLoading.has(id))row.append(element("p",{className:"model-config-status",text:text("identifying","正在自动识别参数…") }));
      const value=records.get(id),editor=createParameterEditor(value,{index,onChange:()=>{
        value.budgetEdited=true;value.effortEdited=true;errors.delete(id);updatePrimary();
      }});toggleEditor();row.append(editor);
      if(errors.has(id))row.append(element("p",{className:"resource-error",text:errors.get(id),attrs:{role:"status"}}));
      body.append(row);
      row.hidden=!id.toLowerCase().includes(query.toLowerCase());
    }
    const makeDefault=element("input",{attrs:{type:"checkbox",name:"make-default"}});makeDefault.checked=defaultRequested;
    makeDefault.addEventListener("change",()=>{defaultRequested=makeDefault.checked;});
    body.append(element("label",{className:"supplier-inline-check"},[makeDefault,element("span",{text:text("default","首个通过工具验证的模型设为默认")})]));
  }
  async function next() {
    if(stage===1){
      const form=root.querySelector("form");if(!form.reportValidity())return;
      if(!existing&&!preset.local&&!key.trim()&&!ref.trim()){notice=text("keyRequired","请填写 API Key 或凭据引用。");render();return;}
      stage=2;notice="";render();if(mode==="auto"&&!discovered)await discover();
    }else if(stage===2){
      const list=sourceIds();if(!list.length||list.some(id=>id.length>256)){notice=text("invalidIds","请提供有效的模型 ID，每个最多 256 个字符。");render();return;}
      createRows();stage=3;notice="";render();void resolve();
    }
  }
  function render() {
    if(disposed)return;
    const active=document.activeElement,focus=root.contains(active)?active.name:null,focusId=root.contains(active)?active.id:null;
    const selection=focus&&typeof active.selectionStart==="number"?[active.selectionStart,active.selectionEnd]:null;
    clear(root);
    const steps=element("ol",{className:"model-wizard-steps",attrs:{"aria-label":text("steps","添加模型步骤")}});
    for(const [index,name]of stepNames().entries())steps.append(element("li",{text:`${index+1}. ${name}`,attrs:{"aria-current":stage===index+1?"step":null}}));root.append(steps);
    root.append(element("h3",{text:connection.name}));
    if(notice)root.append(element("p",{className:"model-wizard-notice",text:notice,attrs:{role:"status"}}));
    const form=element("form",{className:"supplier-setup-form"});form.onsubmit=event=>{event.preventDefault();if(stage<3)void next();else if(stage===3&&canVerify())void verify();};
    form.addEventListener("invalid",event=>{const group=event.target.closest("details");if(group)group.open=true;},true);
    if(stage===1)connectionPage(form);else if(stage===2)sourcePage(form);else if(stage===3)parametersPage(form);
    else form.append(element("p",{className:"model-wizard-progress",text:progress||text("preparing","正在准备验证…"),attrs:{role:"status"}}));
    clear(actions);
    if(running)actions.append(button(text("stop","停止"),()=>runController?.abort()));
    else {
      actions.append(button(text("cancel","取消"),onCancel));
      if(stage>1)actions.append(button(text("previous","上一步"),()=>{cancelRequest();stage--;notice="";render();}));
      const primary=button(stage===3?text("addVerify","添加并验证（{count}）",{count:chosen().length}):text("next","下一步"),stage===3?verify:next,true);
      if(stage===3){primary.setAttribute("data-wizard-submit","");primary.disabled=!canVerify();}
      else if(stage===2){primary.setAttribute("data-wizard-next","");primary.disabled=mode==="auto"?(loading||!discovered||!discovered.items.length):!sourceIds().length;}
      actions.append(primary);
    }
    root.append(form);(footerHost||root).append(actions);
    if(renderedStage!==stage){const body=root.closest(".mdo-layer-body");if(body)body.scrollTop=0;renderedStage=stage;}
    if(focus){const target=[...root.querySelectorAll("input, select, textarea")].find(node=>node.name===focus);if(target){target.focus();if(selection)target.setSelectionRange?.(...selection);}}
    else if(focusId)root.querySelector('#'+focusId)?.focus();
  }
  const unsubscribe=subscribeLocale(()=>render());render();
  return {mount(host,footer=null){footerHost=footer;host.append(root);(footer||root).append(actions);},destroy(){disposed=true;++epoch;requestController?.abort();runController?.abort();key="";ref="";for(const field of root.querySelectorAll('input[type="password"]'))field.value="";unsubscribe();root.remove();actions.remove();}};
}
