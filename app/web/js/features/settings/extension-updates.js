import { t, subscribeLocale } from "../../i18n.js";
import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { extensionUpdates } from "../../state/extension-updates.js";
import { extensionInstaller, extensionInstallState } from "../../state/extension-installs.js";
import { downloadRegistry, downloadActive } from "../../state/downloads.js";
import { openLayer } from "../../components/layers.js";
import { element, errorMessage, toast } from "../../utils/dom.js";
import { reviewDependencyPlan } from "./dependency-plan.js";

const text=(key,args={})=>t("extensionUpdates."+key,args,key);
const device=()=>targetState().selected?.id||"local";
const writable=()=>{const s=targetState();return !s.selected||s.connected&&!s.runtimeChanged&&s.selected.mode!=="view";};
const preparing=new Set(),controls=new Map();
const jobFor=row=>downloadRegistry.list(device()).find(job=>job.source==="install"&&job.versionIds?.includes(row.version_id));
export function extensionUpdateState(row){
  const job=jobFor(row);
  return preparing.has(row.id)?"preparing":job&&downloadActive(job)?job.state:extensionInstallState(job,row.id)||"available";
}
export function extensionUpdateActionable(row){const state=extensionUpdateState(row);return !["unknown","complete","preparing"].includes(state)&&!downloadActive({state});}
export function extensionUpdateNeedsReview(plan,installed){
  if(!plan.ready)return true;
  return (plan.items||[]).some(item=>{
    if(item.operation!=="install")return false;
    if(item.id!==plan.root&&!item.old_exists)return true;
    const old=installed.find(row=>row.id===item.id),next=item.manifest||{};
    return ["permissions","effects","capabilities"].some(key=>JSON.stringify(old?.[key]??[])!==JSON.stringify(next[key]??[]));
  });
}
export function consistentUpdatePlans(plans){const seen=new Map();for(const item of plans.flatMap(plan=>plan.items||[])){if(!item.manifest)continue;const pin=item.version_id+":"+item.sha256;if(seen.has(item.id)&&seen.get(item.id)!==pin)return false;seen.set(item.id,pin);}return true;}
function updateControls(){for(const [button,item] of controls){
  if(!button.isConnected){controls.delete(button);continue;}
  const row=extensionUpdates.find(item);button.hidden=!row;if(!row)continue;
  const state=extensionUpdateState(row),busy=preparing.has(row.id)||downloadActive({state}),unknown=state==="unknown";
  button.disabled=!writable()||busy||state==="complete";button.setAttribute("aria-busy",String(busy));button.dataset.updateState=state;
  button.replaceChildren(...(busy?[element("span",{className:"extension-install-spinner",attrs:{"aria-hidden":"true"}})]:[]),document.createTextNode(text(busy?"updating":unknown?"viewTask":state==="complete"?"updated":state==="failed"?"retry":row.delivery==="apk"?"updateFull":"update")));
  button.title=jobFor(row)?.error||`${row.installed_version||"—"} → ${row.version||"—"}`;
}}
export function createExtensionUpdateButton(item){
  const button=element("button",{className:"secondary-button extension-update-button",text:text("update"),attrs:{type:"button"}});button.hidden=!extensionUpdates.find(item);controls.set(button,item);
  button.addEventListener("click",()=>{const row=extensionUpdates.find(item);if(!row)return;if(extensionUpdateState(row)==="unknown"){document.querySelector("#open-downloads")?.click();return;}void updateExtensions([row]).catch(e=>toast(errorMessage(e),"error"));});
  queueMicrotask(updateControls);return button;
}
export async function updateExtensions(candidates){
  if(candidates.some(row=>row.delivery==="apk")){if(!writable())return null;const owner=device();await api.post("/update",{edition:"full"});if(owner===device())window.dispatchEvent(new CustomEvent("mdo-open-full-update",{detail:{target:owner}}));return null;}
  const owner=device(),rows=candidates.filter(extensionUpdateActionable);if(!rows.length||!writable())return null;
  if(rows.length>128)throw Error(text("batchLimit"));rows.forEach(row=>preparing.add(row.id));updateControls();
  const plans=[];let queued=false;
  const current=()=>device()===owner&&writable();
  const discard=async()=>{if(current())await Promise.allSettled(plans.map(plan=>api.post("/ecosystem",{action:"discard_plan",plan_id:plan.id,revision:plan.revision})));};
  try{
    const installed=Object.values((await api.get("/ecosystem")).data);if(!current())return;
    for(const row of rows){const plan=(await api.post("/ecosystem",{action:"install_plan",version_id:row.version_id})).data;if(!current())return;plans.push(plan);}
    if(!consistentUpdatePlans(plans))throw Error(text("catalogChanged"));
    const enqueue=async()=>{if(!current())return null;const job=await extensionInstaller.enqueueBatch(plans,{name:rows.length===1?rows[0].name:text("batchName",{count:rows.length}),kind:rows.length===1?rows[0].kind:"",roots:rows.map(row=>row.id)});queued=true;return job;};
    if(plans.some(plan=>extensionUpdateNeedsReview(plan,installed))){
      const items=[...new Map(plans.flatMap(plan=>plan.items||[]).map(item=>[item.id,item])).values()];
      return await reviewDependencyPlan({ready:plans.every(plan=>plan.ready),items,issues:plans.flatMap(plan=>plan.issues||[])},{queue:enqueue,discard,update:true});
    }
    return await enqueue();
  }finally{rows.forEach(row=>preparing.delete(row.id));updateControls();if(!queued)await discard();}
}
extensionUpdates.subscribe(updateControls);
downloadRegistry.subscribe(updateControls);
subscribeLocale(updateControls);
let currentTarget=device();subscribeTarget(()=>{if(currentTarget!==device()||targetState().runtimeChanged){currentTarget=device();preparing.clear();}updateControls();});

export function createExtensionUpdateCenter(){
  let layer=null,timer=0,destroyed=false,owner="",signature="";
  const button=(label,action)=>{const b=element("button",{className:"secondary-button",text:label,attrs:{type:"button"}});b.addEventListener("click",()=>void Promise.resolve().then(action).catch(e=>toast(errorMessage(e),"error")));return b;};
  function schedule(){clearTimeout(timer);if(layer&&!destroyed)timer=setTimeout(async()=>{await extensionUpdates.refresh();schedule();},extensionUpdates.get().busy?1000:5000);}
  function close(){clearTimeout(timer);layer?.dispose();layer=null;signature="";}
  function render(){if(!layer)return;if(owner!==device())return close();const data=extensionUpdates.get(),next=JSON.stringify([data,data.items.map(row=>[row.id,extensionUpdateState(row),jobFor(row)?.error]),writable(),text("title")]);if(signature===next)return;signature=next;const scroll=layer.body.scrollTop,active=document.activeElement?.dataset.updateId,expanded=new Set([...layer.body.querySelectorAll("details[open]")].map(node=>node.dataset.updateNotesId));
    layer.update({title:text("title")});const list=element("div",{className:"extension-updates-list"});
    const status=element("p",{className:data.message?"extension-error":"extension-help",text:data.busy?text("checking"):data.message?text("stale"):data.checked_at?text("checked",{time:new Date(data.checked_at*1000).toLocaleString()}):text("checking")});
    if(!data.items.length)list.append(element("p",{className:"download-empty",text:data.busy?text("checking"):data.stale?text("unavailable"):text("empty")}));
    for(const row of data.items){const entry=element("div",{className:"extension-update-row"}),info=element("div",{className:"extension-update-info"});
      const kinds={agents:"Agent",subagents:"SubAgent",tools:t("ecosystem.toolTitle"),mcp:"MCP",skills:"Skill",commands:t("ecosystem.commands"),"external-tools":t("settings.toolpacks"),knowledge:t("settings.knowledge"),plugins:t("settings.extensions")};
      info.append(element("strong",{text:row.name}),element("small",{text:[kinds[row.kind]||row.kind,row.submitter||row.author].filter(Boolean).join(" · ")}),element("span",{text:`${row.installed_version||"—"} → ${row.version||"—"} · ${(row.bytes/1048576).toFixed(1)} MiB`}));
      const notes=row.release_notes||row.description;if(notes){const summary=element("summary",{text:text("details")});summary.dataset.updateId=row.id+"-notes";const details=element("details",{},[summary,element("p",{text:notes})]);details.dataset.updateNotesId=row.id;details.open=expanded.has(row.id);info.append(details);}
      const control=createExtensionUpdateButton(row);control.dataset.updateId=row.id;entry.append(info,control);list.append(entry);
    }
    layer.body.replaceChildren(status,list);layer.footer.replaceChildren();
    const check=button(text("check"),()=>extensionUpdates.refresh({check:true}));check.disabled=data.busy||!writable();const available=data.items.filter(row=>row.delivery!=="apk"&&extensionUpdateActionable(row));
    const all=button(text("all",{count:available.length}),()=>updateExtensions(available));all.className="primary-button";all.disabled=!available.length||!writable();layer.footer.append(check,all,button(text("close"),close));layer.body.scrollTop=scroll;
    updateControls();if(active)for(const b of layer.body.querySelectorAll("[data-update-id]"))if(b.dataset.updateId===active)b.focus({preventScroll:true});
  }
  const stop=extensionUpdates.subscribe(render),downloads=downloadRegistry.subscribe(render),locale=subscribeLocale(render),target=subscribeTarget(()=>{if(layer&&owner!==device())close();});
  return {open(){if(destroyed)return;if(layer)return layer.element.focus();owner=device();signature="";layer=openLayer({title:text("title"),icon:"download",width:"56rem",height:"80%",onClosed(){clearTimeout(timer);layer=null;signature="";}});layer.element.id="extension-updates-dialog";render();void extensionUpdates.refresh().then(()=>{render();schedule();});},close,destroy(){destroyed=true;close();stop();downloads();locale();target();}};
}
