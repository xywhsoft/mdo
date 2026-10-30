import { setUiText as writeUiText, t as uiText } from "../../i18n.js";

import { api } from "../../api/client.js";
import { openLayer } from "../../components/layers.js";
import { element, errorMessage } from "../../utils/dom.js";
import { promptFields, editPrompt } from "./extension-formats.js";

export const ROLE_ADVANCED_KEYS = Object.freeze(["context", "runtime", "delegation", "history", "model_options", "mcp_servers", "skills"]);
export function roleAdvanced(source) {
  const fields = promptFields(source);
  return Object.fromEntries(ROLE_ADVANCED_KEYS.filter(key => fields[key] !== undefined).map(key => [key, fields[key]]));
}
export function parseRoleAdvanced(text) {
  const value = JSON.parse(text || "{}");
  if (!value || typeof value !== "object" || Array.isArray(value) || Object.keys(value).some(key => !ROLE_ADVANCED_KEYS.includes(key)))
    throw Error(uiText("ui.agentAuthoring.advancedConfigurationAcceptsOnlyContextRuntimeDelegationHistoryModel", {}, "高级配置只接受 context、runtime、delegation、history、model_options、mcp_servers 和 skills。其他字段请在源文件中编辑。"));
  return value;
}
export function newRole(kind) {
  return editPrompt("", { schema: "mdo.agent.v1", name: "", description: "", mode: kind === "agents" ? "primary" : "subagent",
    extends: "mdo.general", tools: "inherit", read_only: false, allow_delegation: false, prompt: "" });
}
export function roleProgramTemplate(kind,id) {
  const roleId=kind === "agents" ? id === "default" ? "mdo.default" : `agent.${id}` : `subagent.${id}`;
  return `#include "mdo/agent.h"

/* Optional callbacks. Every pointer is borrowed for this invocation only.
 * Context.Application exposes the same Application SDK as other extensions. */
static mdo_result OnEvent(void* Data,mdo_agent_phase Phase,
    const mdo_agent_context_v1* Context,char* Error,size_t Capacity)
{
    (void)Data;(void)Error;(void)Capacity;
    if(Phase==MDO_AGENT_CONTEXT){
        /* Edit individual sources through mdoAgentContextSource/SetContextText.
         * The configuration has already selected and rendered their inputs. */
        if(!mdoAgentContextSource(Context,"system"))return MDO_RESULT_ERROR;
    }
    if(Phase==MDO_AGENT_BEFORE_REQUEST){
        xwork_agent_extension_context* Native=mdoAgentNative(Context);
        /* Native->pRequest contains the owned, mutable final request. */
        (void)Native;
    }
    return MDO_RESULT_OK;
}
static const mdo_agent_program_v1 Program={MDO_V1_HEADER(mdo_agent_program_v1),OnEvent,NULL};
static const char* Tools[]={"read","ls","glob","grep","skill"};
static const mdo_agent_v1 Role={
    .Size=sizeof(mdo_agent_v1),.AbiVersion=MDO_MODULE_ABI_VERSION,
    .Id=${JSON.stringify(roleId)},.Name=${JSON.stringify(id)},
    .Description="Local C role",.SystemPrompt="Inspect relevant evidence and report actionable findings.",
    .Tools=Tools,.ToolCount=sizeof(Tools)/sizeof(*Tools),
    .AllowedEffects=MDO_TOOL_EFFECT_READ,
    .Flags=MDO_AGENT_MAIN|MDO_AGENT_SUBAGENT|MDO_AGENT_READ_ONLY|MDO_AGENT_EXPLICIT_TOOLS,
    .MaxTurns=64,.MaxDepth=4,.MaxFinalBytes=65536,.Program=&Program
};
MDO_AGENT_MODULE(Role)
`;
}
export async function openRolePlan({kind,id,content,owner}) {
  const layer = openLayer({title:uiText("ui.agentAuthoring.roleContextPreview", {}, "角色上下文预览"),icon:"terminal",width:"64rem",height:"85%",owner});
  const status = element("p",{text:uiText("ui.agentAuthoring.buildingPreview", {}, "正在构建预览…"),attrs:{role:"status"}});layer.body.append(status);
  const close=element("button",{className:"secondary-button",text:uiText("context.close", {}, "关闭"),attrs:{type:"button"}});
  close.addEventListener("click",()=>layer.close());layer.footer.append(close);
  try {
    const url=`/extensions/${kind}/${encodeURIComponent(id)}/plan?project=default`;
    const {data}=content === undefined ? await api.get(url,{signal:layer.signal}) : await api.post(url,{content},{signal:layer.signal});
    if(layer.signal.aborted)return;
    writeUiText(status, uiText("ui.agentAuthoring.configurationPreviewDoesNotRunCCallbacksOrCall", {}, "配置预览不运行 C 回调、不调用模型。C 回调后的实际请求可通过运行时检查接口查看。预览包含你的提示词与记忆，仅在当前设备显示。"));
    const sources=element("div",{className:"agent-plan-sources"});
    for(const source of [...data.sources].sort((a,b)=>a.order-b.order)){
      const details=element("details",{},[element("summary",{text:`${source.enabled?uiText("resource.enable", {}, "启用"):uiText("resource.disable", {}, "停用")} · ${source.id} · ${source.role} · ${new TextEncoder().encode(source.text).length} B${source.truncated?uiText("ui.agentAuthoring.truncated", {}, " · 已裁剪"):""}`}),element("pre",{className:"store-source",text:source.text})]);sources.append(details);
    }
    layer.body.append(sources,element("h3",{text:uiText("ui.agentAuthoring.systemPrompt", {}, "系统提示词")}),element("pre",{className:"store-source",text:data.system_prompt}),element("h3",{text:uiText("ui.agentAuthoring.userContext", {}, "用户上下文")}),element("pre",{className:"store-source",text:data.user_context}),element("details",{},[element("summary",{text:uiText("ui.agentAuthoring.fullConfigurationAndSources", {}, "完整配置与来源数据")}),element("pre",{className:"store-source",text:JSON.stringify(data,null,2)})]));
  } catch(error) { if(!layer.signal.aborted)writeUiText(status, errorMessage(error)); }
}
