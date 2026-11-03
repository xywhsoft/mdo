import { t } from "../i18n.js";
import { createApplicationClient } from "../api/application.js";
import { targetState } from "../api/target.js";
import { downloadActive, downloadRegistry } from "./downloads.js";

export function extensionInstallState(job,root){return job?.results?.find(row=>row.id===root)?.state||job?.state||"";}

// SDK operations own their cancellation and durable admission receipt. A page
// or details window can close without stopping an accepted installation.
export function createExtensionInstaller({ registry = downloadRegistry, sdk = createApplicationClient(() => ({ extension: "mdo.extension-store" })), target = () => targetState().selected?.id || "local", notify = () => {} } = {}) {
  let timer = null, polling = false, disposed = false;
  const pending = device => registry.list(device).filter(row => row.source === "install" && (downloadActive(row) || row.state === "unknown"));
  const update = (device,id,patch) => registry.install(device,id,patch);
  function schedule() {
    clearTimeout(timer); timer = null;
    if (!disposed && pending(target()).length) {
      timer = setTimeout(() => void refresh(), pending(target()).some(downloadActive)?1000:5000); timer.unref?.();
    }
  }
  async function finish(device,row,job) {
    if (!job.terminal) { update(device,row.operationId,{state:"installing",error:""}); return; }
    if (job.state !== "succeeded") {
      update(device,row.operationId,{state:job.state === "cancelled" ? "cancelled" : job.state === "interrupted" ? "unknown" : "failed",error:job.error?.message || t("extensionInstall.interrupted")});
      if(job.state!=="interrupted")try{await sdk.call("operation.release",{id:row.operationId});}catch{}
      return;
    }
    // Do not infer successful activation from a completed download or HTTP 200.
    const result = await sdk.result(row.operationId);
    if (target() !== device) return;
    const receipt = result?.data ?? result;
    if (receipt?.runtime_ready !== true && receipt?.runtime_ready !== false) throw Error(t("extensionInstall.interrupted"));
    const error = receipt.runtime_ready === false ? receipt.runtime_error || t("extensionInstall.runtimeAttention") : "";
    update(device,row.operationId,{state:error ? "failed" : "complete",error,results:receipt.results||[]});
    notify(row,receipt);
    try { await sdk.call("operation.release",{id:row.operationId}); } catch { /* The completed admission receipt remains queryable. */ }
  }
  async function refresh({ discover = false } = {}) {
    if (disposed || polling) return; polling = true; const device=target();
    try {
      if (discover) {
        const jobs=await sdk.call("operation.list");
        if(target()!==device)return;
        for(const job of jobs || []) if(job.extension==="mdo.extension-store" && job.method==="product.ecosystem.post" && !job.terminal && !registry.list(device).some(row=>row.operationId===job.id))
          update(device,job.id,{name:t("extensionInstall.title"),state:"installing",versionIds:[]});
      }
      for(const row of pending(device)) {
        if(target()!==device)break;
        try {
          const job=await sdk.call("operation.get",{id:row.operationId});
          if(target()===device)await finish(device,row,job);
        } catch(error) {
          if(target()===device)update(device,row.operationId,{state:"unknown",error:error.message});
        }
      }
    } catch { /* Transient reads never replay installation or discard its receipt. */ }
    finally { polling=false;schedule(); }
  }
  async function enqueue(plan,metadata={}) {
    if(disposed || !plan?.ready || !plan.id || !plan.revision)throw Error(t("extensionInstall.invalidPlan"));
    const device=target(),versionIds=(plan.items||[]).filter(item=>item.operation==="install").map(item=>item.version_id);
    const existing=registry.list(device).find(row=>row.source==="install" && ((metadata.versionId&&row.versionId===metadata.versionId)||row.planId===plan.id) && (downloadActive(row)||row.state==="unknown"));
    if(existing)return existing;
    const id="sdk-"+crypto.randomUUID().replaceAll("-","");
    const total=(plan.items||[]).filter(item=>item.operation==="install").reduce((sum,item)=>sum+(Number.isSafeInteger(item.bytes)?item.bytes:0),0);
    update(device,id,{...metadata,versionIds,planId:plan.id,revision:plan.revision,state:"queued",error:"",total,done:0});
    try {
      const job=await sdk.start("product.ecosystem.post",{body:{action:"apply_plan",plan_id:plan.id,revision:plan.revision,trust_code:true,queued:true}},id);
      if(target()===device)await finish(device,{...metadata,operationId:id},job);
      return registry.list(device).find(row=>row.operationId===id) || {operationId:id,state:"queued"};
    } catch(error) {
      update(device,id,{state:error.status ? "failed" : "unknown",error:error.message});
      throw error;
    } finally { schedule(); }
  }
  async function enqueueBatch(plans,metadata={}){
    if(disposed||!plans.length||plans.some(plan=>!plan.ready||!plan.id||!plan.revision))throw Error(t("extensionInstall.invalidPlan"));
    const device=target(),items=[...new Map(plans.flatMap(plan=>plan.items||[]).filter(item=>item.operation==="install").map(item=>[item.id,item])).values()];
    const versionIds=items.map(item=>item.version_id),roots=plans.map(plan=>plan.root);
    const existing=registry.list(device).find(row=>row.source==="install"&&row.roots?.some(root=>roots.includes(root))&&(downloadActive(row)||row.state==="unknown"));if(existing)return existing;
    const id="sdk-"+crypto.randomUUID().replaceAll("-","");
    update(device,id,{...metadata,roots,versionIds,state:"queued",error:"",total:items.reduce((sum,item)=>sum+(item.bytes||0),0),done:0});
    try{const job=await sdk.start("product.ecosystem.post",{body:{action:"update_batch",plans:plans.map(plan=>({id:plan.id,revision:plan.revision})),trust_code:true,queued:true}},id);if(target()===device)await finish(device,{...metadata,operationId:id},job);return registry.list(device).find(row=>row.operationId===id)||{operationId:id,state:"queued"};}
    catch(error){update(device,id,{state:error.status?"failed":"unknown",error:error.message});throw error;}
    finally{schedule();}
  }
  const stop=registry.subscribe(schedule);
  return {enqueue,enqueueBatch,refresh,async cancel(row) {const device=target();await sdk.cancel(row.operationId);if(device===target())update(device,row.operationId,{state:"cancelling"});void refresh();},destroy(){disposed=true;clearTimeout(timer);stop();}};
}

export const extensionInstaller=createExtensionInstaller({notify(row,receipt){
  for(const kind of new Set([row.kind,...(receipt?.items||[]).map(item=>item.kind)].filter(Boolean)))
    globalThis.window?.dispatchEvent(new CustomEvent("mdo-extensions-changed",{detail:{kind}}));
}});
