import { setUiText as writeUiText, uiMessage, t as uiText } from "../../i18n.js";

import { api } from "../../api/client.js";
import { targetState } from "../../api/target.js";
import { element, errorMessage } from "../../utils/dom.js";

const states={get ready() { return uiText("ui.extensionDependencies.installedDependenciesSatisfied", {}, "已安装且依赖满足"); },get disabled() { return uiText("ecosystem.disabled", {}, "已停用"); },get dependency_missing() { return uiText("ui.extensionDependencies.notInstalled", {}, "尚未安装"); },get dependency_disabled() { return uiText("ui.extensionDependencies.dependencyDisabled", {}, "依赖已停用"); },get dependency_cycle() { return uiText("ui.extensionDependencies.dependencyCycle", {}, "存在循环依赖"); },get dependency_conflict() { return uiText("ui.extensionDependencies.conflict", {}, "存在冲突"); },get dependency_blocked() { return uiText("ui.extensionDependencies.dependenciesNotReady", {}, "依赖尚未就绪"); },get dependency_source_mismatch() { return uiText("ui.extensionDependencies.sourceIdentityMismatch", {}, "来源身份不匹配"); },get dependency_depth() { return uiText("ui.extensionDependencies.dependencyChainExceedsTheDepthLimit", {}, "依赖链超过深度限制"); },get duplicate_identity() { return uiText("ui.extensionDependencies.identityConflict", {}, "身份冲突"); },get needs_configuration() { return uiText("ui.extensionDependencies.configurationRequired", {}, "需要配置"); },get needs_authorization() { return uiText("ui.extensionDependencies.signInRequired", {}, "需要登录"); }};
export function dependencyList(item){
  const root=element("section",{className:"extension-dependencies",attrs:{"aria-label":uiMessage("ui.extensionDependencies.extensionDependencies", {}, "扩展依赖")}});
  root.append(element("h4",{text:uiMessage("ui.extensionDependencies.requiredExtensions", {}, "依赖扩展")}));
  const rows=item.dependency_details||item.dependencies||item.manifest?.dependencies||[];
  if(!rows.length)root.append(element("p",{className:"extension-help",text:uiMessage("ui.extensionDependencies.noExtensionDependencies", {}, "无扩展依赖")}));
  for(const row of rows){
    const line=element("div",{className:"extension-dependency"});
    line.append(element("strong",{text:row.name||row.id}),element("code",{text:row.id}),element("span",{text:row.optional?uiMessage("ui.extensionDependencies.optional", {}, "可选"):uiMessage("ui.extensionDependencies.required", {}, "必需")}));
    if(row.available===false||row.dependency_state)line.append(element("span",{text:row.available===false?uiMessage("ui.extensionDependencies.notInstalledOrUnavailable", {}, "尚未安装或不可用"):states[row.dependency_state]||row.dependency_state}));
    if(row.reason)line.append(element("p",{text:row.reason}));root.append(line);
  }
  for(const issue of item.dependency_issues||[]){if(issue.optional)continue;root.append(element("p",{className:"extension-error",text:`${issue.chain?.join(" → ")||issue.dependency_id||""}：${states[issue.code]||issue.message||issue.code}`}));}
  const conflicts=item.conflicts||item.manifest?.conflicts||[];
  if(conflicts.length){root.append(element("h4",{text:uiMessage("ui.extensionDependencies.cannotBeEnabledTogether", {}, "不能同时启用")}));for(const conflict of conflicts)root.append(element("p",{text:`${conflict.id}${conflict.reason?` · ${conflict.reason}`:""}`}));}
  if(item.dependents?.length){root.append(element("h4",{text:uiMessage("ui.extensionDependencies.requiredByTheseExtensions", {}, "被以下扩展依赖")}));for(const user of item.dependents)root.append(element("p",{text:`${user.name||user.id} · ${user.id}`}));}
  return root;
}
export function localDependencyPanel(kind,item,options={}){
  const panel=element("div",{},[dependencyList(item)]),id=item.directory_id||item.id,scope=item.project||options.project||"global",path=`/extensions/${kind}/${encodeURIComponent(id)}`;
  const device=()=>targetState().selected?.id||"local",target=device(),live=()=>panel.isConnected&&device()===target&&!options.signal?.aborted;
  if(id)void api.get(`${path}${scope!=="global"?`?project=${encodeURIComponent(scope)}`:""}`,options).then(async result=>{
    if(!live())return;
    const detail=result.data;panel.replaceChildren(dependencyList(detail));
    const required=(detail.dependency_details||detail.dependencies||[]).filter(row=>row.id?.startsWith("mcp:")&&!row.optional);if(!required.length)return;
    panel.append(element("p",{className:"extension-help",text:uiMessage("ui.extensionDependencies.installedMcpTemplatesStillRequireConfiguredAndAuthorizedPrivate", {}, "MCP 模板安装就绪后，仍需配置并授权私有连接。连接绑定只保存在本机，不进入发布归档。")}));
    const connections=(await api.get("/mcp/connections",options)).data;if(!live())return;let revision=detail.bindings_revision;
    for(const dependency of required){
      const line=element("label",{className:"extension-field"}),select=element("select",{attrs:{"aria-label":uiMessage("ui.extensionDependencies.value1PrivateConnection", { value1: dependency.id }, "{value1} 私有连接")}}),save=element("button",{className:"secondary-button",text:uiMessage("ui.extensionDependencies.saveConnectionBinding", {}, "保存连接绑定"),attrs:{type:"button"}}),status=element("small",{attrs:{role:"status"}});
      select.append(element("option",{text:uiMessage("ui.extensionDependencies.noExplicitConnectionResolveInTheCurrentScopeWhen", {}, "未指定连接（仅一个可用连接时按当前作用域解析）"),attrs:{value:""}}));
      const choices=connections.filter(connection=>connection.extension_id===dependency.id&&connection.enabled&&(!connection.project_id||scope!=="global"&&connection.project_id===scope));
      for(const connection of choices)select.append(element("option",{text:connection.name||connection.id,attrs:{value:connection.id}}));
      const existing=detail.dependency_bindings?.[dependency.id]||"";if(existing&&!choices.some(connection=>connection.id===existing))select.append(element("option",{text:uiMessage("ui.extensionDependencies.value1CurrentlyUnavailable", { value1: existing }, "{value1}（当前不可用）"),attrs:{value:existing}}));select.value=existing;
      save.addEventListener("click",async()=>{if(!live())return;save.disabled=select.disabled=true;try{
        const response=(await api.post(`${path}/actions`,{action:"bind",dependency_id:dependency.id,connection_id:select.value,scope,expected_binding_revision:revision},options)).data;if(!live())return;revision=response.revision;writeUiText(status, uiMessage("ui.extensionDependencies.bindingSavedSignInConnectionAndCapabilityDiscoveryAre", {}, "绑定已保存；登录、连接和能力发现会在运行前检查。"));
      }catch(error){if(live()&&error.name!=="AbortError")writeUiText(status, errorMessage(error));}finally{if(live())save.disabled=select.disabled=false;}});
      line.append(element("span",{text:dependency.name||dependency.id}),select,save,status);panel.append(line);
    }
  }).catch(error=>{if(live()&&error.name!=="AbortError")panel.append(element("p",{className:"extension-help",text:uiMessage("ui.extensionDependencies.dependencyStatusCouldNotBeRefreshedValue1", { value1: errorMessage(error) }, "依赖状态暂时无法更新：{value1}")}));});
  return panel;
}
