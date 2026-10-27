import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { element, errorMessage, toast } from "../../utils/dom.js";
import { openLayer } from "../../components/layers.js";
import { mcpVariantInputs } from "./mcp-template.js";
import { createMcpContext } from "./mcp-context.js";
import { createMcpAuthorization } from "./mcp-authorization.js";
import { refreshMcp, disconnectMcp } from "../../state/resources.js";

const target=()=>targetState().selected?.id||"local";
export function createMcpConnections(onChange=()=>{}) {
  let active=null;const context=createMcpContext(),authorization=createMcpAuthorization();
  const close=(force=false)=>{if(!active)return true;if(!force&&(active.busy||active.dirty&&!confirm("放弃未保存的连接配置？")))return false;active.view.dispose();return true;};
  const stop=subscribeTarget(()=>{if(active?.target!==target())close(true);});
  async function open({item=null,templateId=""}={}) {
    if(!close())return;const state={target:target(),busy:false,dirty:false,approval:null};
    const view=openLayer({title:"MCP 连接",icon:"globe",width:"56rem",height:"85%",beforeClose:()=>!state.busy&&(!state.dirty||confirm("放弃未保存的连接配置？")),onClosed(){if(active===state)active=null;}});state.view=view;active=state;
    const live=()=>active===state&&!view.signal.aborted&&state.target===target(),options=()=>({signal:view.signal});
    const content=element("div"),message=element("p",{attrs:{role:"status"}});view.body.append(content,message);
    const lock=()=>{if(live())view.element.querySelectorAll("input,textarea,select,button").forEach(n=>n.disabled=state.busy||n.dataset.fixed==="true");};
    async function run(fn){if(state.busy||!live())return;state.busy=true;lock();try{await fn();}catch(e){if(live()&&e.name!=="AbortError")message.textContent=errorMessage(e);}finally{state.busy=false;lock();}}
    const button=(name,fn,primary=false)=>{const b=element("button",{text:name,className:primary?"primary-button":"secondary-button",attrs:{type:"button"}});b.addEventListener("click",()=>void run(fn));return b;};
    const dismiss=button("关闭",()=>{state.busy=false;return view.close();});let templates=[],projects=[];
    async function list(){
      const [connections,runtime]=await Promise.all([api.get("/mcp/connections",options()),api.get("/mcp",options())]);if(!live())return;
      content.replaceChildren(element("p",{className:"extension-help",text:"一个服务目录可以创建多个私有连接。参数、项目和凭据只保存在本机，不进入发布 ZIP。"}));view.footer.replaceChildren(button("新建连接",()=>edit(null)),button("刷新",list),dismiss);
      for(const connection of connections.data){const status=runtime.data.items?.find(r=>r.id===connection.id),row=element("article",{className:"resource-card"},[element("h3",{text:connection.name||connection.id}),element("p",{text:`${connection.extension_id||"服务不可用"} · ${connection.project_id||"所有项目"} · ${status?.state||"未连接"}`})]);
        row.append(button("配置",()=>edit(connection)),button("测试连接",()=>refreshMcp(connection.id,{signal:view.signal,isCurrent:live})),button("资源与提示词",()=>context.open(connection.id)),button("授权",()=>authorization.open(connection.id)),button("断开",()=>disconnectMcp(connection.id)),button("删除连接",async()=>{if(!confirm(`删除私有连接 ${connection.name||connection.id}？`))return;const current=(await api.get(`/mcp/connections/${encodeURIComponent(connection.id)}`,options())).data;await api.post("/mcp/connections",{action:"remove",id:current.id,expected_revision:current.revision},options());if(live()){onChange();await list();}}));content.append(row);
      }
      if(!connections.data.length)content.append(element("p",{text:"尚未配置连接。请先安装或创建服务目录，然后新建连接。"}));
    }
    async function edit(old){
      if(old)old=(await api.get(`/mcp/connections/${encodeURIComponent(old.id)}`,options())).data;if(!live())return;
      state.dirty=false;state.approval=null;const form=element("form",{className:"extension-form"}),fields=element("div"),preview=element("pre",{className:"store-source"});
      const row=(label,c)=>{form.append(element("label",{className:"extension-field"},[element("span",{text:label}),c]));return c;};
      const input=(label,value="",type="text")=>{const c=element("input",{attrs:{type,maxlength:"4096",autocomplete:"off","aria-label":label}});c.value=value;return row(label,c);};
      const id=input("连接名称标识",old?.id||""),name=input("显示名称",old?.name||"MCP 连接");id.dataset.fixed=String(Boolean(old));
      const select=row("服务目录",element("select",{attrs:{"aria-label":"服务目录"}}));for(const t of templates)select.append(element("option",{text:`${t.name} · ${t.id}`,attrs:{value:t.id}}));select.value=old?.extension_id||templateId||templates[0]?.id||"";
      const variant=row("连接方案",element("select",{attrs:{"aria-label":"连接方案"}})),project=row("所属项目",element("select"));project.append(element("option",{text:"所有项目",attrs:{value:""}}));for(const p of projects)project.append(element("option",{text:p.name||p.id,attrs:{value:p.id}}));project.value=old?.project_id||"";
      const enabled=input("保存后启用","","checkbox");enabled.checked=old?.enabled||false;const trust=input("已审阅并允许运行此程序或连接目标","","checkbox");
      let values=new Map(),clears=new Set(),currentTemplate=null;
      function invalidate(){state.approval=null;trust.checked=false;preview.textContent="修改后请重新预览；凭据不会显示在预览中。";}
      function renderInputs(){currentTemplate=templates.find(t=>t.id===select.value);const recipe=currentTemplate?.template,choice=recipe?.variants.find(v=>v.id===variant.value);fields.replaceChildren();values=new Map();clears=new Set();invalidate();
        const same=old?.extension_id===select.value&&old?.variant===variant.value;
        for(const d of mcpVariantInputs(recipe||{},choice)){const c=element(d.type==="boolean"||d.type==="select"?"select":"input",{attrs:{type:d.secret?"password":d.type==="number"?"number":"text",autocomplete:"off",maxlength:"4096","aria-label":d.label}});if(c.tagName==="SELECT")for(const option of d.type==="boolean"?["false","true"]:d.options||[])c.append(element("option",{text:option,attrs:{value:option}}));c.value=d.secret?"":same?old.values?.[d.id]??d.default??"":d.default??"";
          const saved=d.secret&&same&&old.secrets_set?.[d.id];if(saved)c.placeholder="已保存，留空保留；服务目录变化后需重填";
          fields.append(element("label",{className:"extension-field"},[element("span",{text:`${d.label}${d.secret?" · 私密":""}${d.required?" · 必填":""}`}),c,element("small",{text:d.description||""})]));values.set(d.id,{control:c,secret:d.secret});
          if(saved&&!d.required){const clear=element("input",{attrs:{type:"checkbox"}});clear.addEventListener("change",()=>clear.checked?clears.add(d.id):clears.delete(d.id));fields.append(element("label",{},[clear,element("span",{text:`清除 ${d.label}`})]));}
        }
      }
      function variants(){const t=templates.find(t=>t.id===select.value);variant.replaceChildren(...(t?.template.variants||[]).map(v=>element("option",{text:v.label,attrs:{value:v.id}})));variant.value=old?.extension_id===select.value?old.variant:t?.template.variants[0]?.id||"";renderInputs();}
      select.addEventListener("change",variants);variant.addEventListener("change",renderInputs);variants();
      form.append(fields,preview);for(const event of ["input","change"])form.addEventListener(event,e=>{state.dirty=true;if(e.target!==trust)invalidate();});
      function body(){const ordinary={},secrets={};for(const [key,v]of values)if(v.secret){if(v.control.value)secrets[key]=v.control.value;}else ordinary[key]=String(v.control.value);return {id:id.value.trim(),name:name.value.trim(),extension_id:select.value,variant:variant.value,project_id:project.value,enabled:enabled.checked,values:ordinary,secrets,clear_secrets:[...clears]};}
      async function perform(action){const payload=body();if(!currentTemplate)throw Error("请先安装可用的 MCP 服务目录");const fingerprint=JSON.stringify(payload);if(action==="save"&&(!trust.checked||state.approval?.fingerprint!==fingerprint))throw Error("请预览当前配置并勾选运行确认，再保存");
        const result=(await api.post("/mcp/connections",{...payload,action,expected_revision:old?.revision||"",...(action==="save"?{approval_digest:state.approval.digest}:{})},options())).data;if(!live())return;
        if(action==="preview"){state.approval={fingerprint,digest:result.approval_digest};preview.textContent=JSON.stringify(result.config,null,2);message.textContent=result.target_changed?"服务目录、程序或连接目标已改变，旧凭据不会沿用，请重新提供。":"预览未保存、未启动服务；请核对后确认。";}
        else{for(const v of values.values())if(v.secret)v.control.value="";state.dirty=false;onChange();await list();toast("私有连接已保存","success");}
      }
      content.replaceChildren(form);view.footer.replaceChildren(button("预览配置",()=>perform("preview")),button("保存连接",()=>perform("save"),true),button("返回连接列表",()=>{if(!state.dirty||confirm("放弃未保存的连接配置？")){state.dirty=false;return list();}}),dismiss);lock();
    }
    await run(async()=>{const [catalog,p]=await Promise.all([api.get("/mcp/templates",options()),api.get("/projects",options())]);if(!live())return;templates=catalog.data.items.filter(t=>t.template_valid);projects=(p.data.items||[]).filter(p=>p.status!=="trash");if(item||templateId)await edit(item);else await list();});
  }
  return {open,hasPendingChanges:()=>Boolean(active),destroy(){close(true);stop();context.destroy();authorization.destroy();}};
}
