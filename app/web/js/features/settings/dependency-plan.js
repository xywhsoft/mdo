import { setUiText as writeUiText, uiMessage, t as uiText } from "../../i18n.js";

import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { element, errorMessage } from "../../utils/dom.js";
import { openLayer } from "../../components/layers.js";

export const dependencyActionLabel = item => item.operation === "remove" ? uiText("store.uninstall", {}, "卸载") : item.operation === "install" ? item.old_exists ? uiText("extensionBrowser.update", {}, "更新") : uiText("extensionBrowser.install", {}, "安装") : item.desired_enabled ? uiText("ui.dependencyPlan.enableKeepEnabled", {}, "启用 / 保持启用") : uiText("resource.disable", {}, "停用");
export function dependencyPlanSummary(plan,{compact=false}={}) {
  const root=element("div",{className:"dependency-plan"});
  if(!compact)root.append(element("p",{text:uiMessage("ui.dependencyPlan.theseExtensionsShareOneOperationPlanSharedDependenciesAre", {}, "以下扩展属于同一操作计划。共享依赖只处理一次；更新失败会恢复整组目录和启用状态。" )}));
  for(const item of plan.items||[])root.append(element("article",{className:"extension-dependency"},[
    element("strong",{text:item.name||item.manifest?.name||item.id}),...(!compact?[element("code",{text:item.id})]:[]),
    ...(compact&&(item.submitter||item.manifest?.author)?[element("small",{text:item.submitter||item.manifest.author})]:[]),
    element("span",{text:dependencyActionLabel(item)}),...(item.version?[element("small",{text:uiMessage("ui.dependencyPlan.versionValue1", { value1: item.version }, "版本 {value1}")})]:[]),
  ]));
  for(const issue of plan.issues||[])root.append(element("p",{className:"extension-error",text:`${issue.chain?.join(" → ")||issue.id||""}：${issue.message||issue.code}`}));
  if(plan.items?.some(item=>item.kind==="mcp"))root.append(element("p",{className:"extension-help",text:uiMessage("mcpActivation.installConsent", {}, "安装并启用即允许运行默认 MCP 程序或连接默认服务，并自动发现工具。仅缺少参数或需要账号授权时才需继续配置；高级权限方案不会自动启用。")}));
  return root;
}
export function reviewDependencyPlan(plan,options={}) {
  return new Promise(resolve=>{
    const device=()=>targetState().selected?.id||"local",target=device();let busy=false,finished=false,attempted=false,committedReceipt=null;
    const queued=typeof options.queue==="function",controller=new AbortController();let stop=()=>{};
    const view=openLayer({title:queued?uiMessage("extensionInstall.trustTitle", {}, "信任并安装扩展？"):uiMessage("ui.dependencyPlan.confirmExtensionDependencyPlan", {}, "确认扩展依赖计划"),icon:"folder",width:"48rem",height:"auto",beforeClose:()=>!busy,onClosed(){controller.abort();stop();options.signal?.removeEventListener("abort",abort);if(!attempted&&device()===target)void (options.discard?options.discard():api.post("/ecosystem",{action:"discard_plan",plan_id:plan.id,revision:plan.revision})).catch(()=>{});if(!finished){finished=true;resolve(committedReceipt);}}});
    const abort=()=>{if(!busy)view.close();};options.signal?.addEventListener("abort",abort,{once:true});
    stop=subscribeTarget(()=>{if(device()!==target){controller.abort();busy=false;view.close();}});
    const code=(plan.items||[]).some(item=>{const meta=item.manifest||item;return item.operation!=="remove"&&(item.operation==="install"||item.desired_enabled)&&(["tools","commands","plugins","mcp","external-tools"].includes(item.kind)||meta.native||typeof meta.entry==="string"&&meta.entry.endsWith(".c"));});
    const summary=dependencyPlanSummary(plan,{compact:queued}),status=element("p",{attrs:{role:"status"}}),trust=element("input",{attrs:{type:"checkbox","aria-label":uiMessage("ui.dependencyPlan.confirmTheEntireExtensionGroup", {}, "确认整组扩展内容")}});trust.checked=queued||!code;
    if(queued)view.body.append(element("p",{text:uiMessage("extensionInstall.trustPrompt", {}, "请确认你信任以下扩展及其来源。点击“安装”后将加入下载任务，自动下载并安装；可在下载任务中查看结果或取消。")}));
    view.body.append(summary,...(!queued&&code?[element("label",{className:"store-code-consent"},[trust,element("span",{text:uiMessage("ui.dependencyPlan.iReviewedTheseOperationsAndAuthorsAndTrustThe", {}, "我已核对以上操作及作者，信任整组扩展的内容和程序。")})])]:[]),status);
    const apply=element("button",{className:"primary-button",text:queued?uiMessage("extensionBrowser.install", {}, "安装"):uiMessage("ui.dependencyPlan.applyThisPlan", {}, "应用此计划"),attrs:{type:"button"}}),close=element("button",{className:"secondary-button",text:uiMessage("sidebar.cancel", {}, "取消"),attrs:{type:"button"}});apply.disabled=!plan.ready;view.footer.append(apply,close);
    close.addEventListener("click",()=>view.close());trust.addEventListener("change",()=>{apply.disabled=!plan.ready||!trust.checked;});apply.disabled=!plan.ready||!trust.checked;
    apply.addEventListener("click",async()=>{
      if(busy||!trust.checked||!plan.ready||device()!==target)return;busy=true;attempted=true;apply.disabled=close.disabled=trust.disabled=view.closeButton.disabled=true;writeUiText(status, queued?uiMessage("extensionInstall.queueing", {}, "正在加入下载任务…"):uiMessage("ui.dependencyPlan.downloadingVerifyingAndApplyingTheExtensionGroupPleaseWait", {}, "正在下载、验证并应用整组扩展，请等待结果…"));
      try{
        if(queued){const job=await options.queue(plan);if(device()!==target)return;busy=false;finished=true;resolve(job);view.close();return;}
        const receipt=(await api.post("/ecosystem",{action:"apply_plan",plan_id:plan.id,revision:plan.revision,trust_code:true},{signal:controller.signal})).data;
        if(device()!==target)return;
        if(receipt.runtime_ready===false){committedReceipt=receipt;writeUiText(status, uiMessage("ui.dependencyPlan.filesAndEnabledPreferencesAreSavedRuntimeNeedsAttention", { value1: receipt.runtime_error||uiMessage("ui.dependencyPlan.checkDependencyConfigurationOrRestart", {}, "请检查依赖配置或重新启动") }, "文件与启用意图已保存，运行状态需要处理：{value1}"));busy=false;writeUiText(close, uiMessage("context.close", {}, "关闭"));close.disabled=view.closeButton.disabled=false;return;}
        busy=false;finished=true;resolve(receipt);view.close();
      }catch(error){if(!controller.signal.aborted){writeUiText(status, uiMessage("ui.dependencyPlan.value1IfDisconnectedRetryThisPlanToReadIts", { value1: errorMessage(error) }, "{value1}。如果连接中断，可重试此计划以读取操作回执。"));busy=false;apply.disabled=!trust.checked;close.disabled=trust.disabled=view.closeButton.disabled=false;}}
    });
    if(options.signal?.aborted)abort();
  });
}
export async function changeWithDependencyPlan(item,operation,options={}) {
  const plan=(await api.post("/ecosystem",{action:"dependency_plan",operation,ref:item.id,cascade:true},options)).data;
  return reviewDependencyPlan(plan,options);
}
