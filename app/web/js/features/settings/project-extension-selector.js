import { api } from "../../api/client.js";
import { element, errorMessage } from "../../utils/dom.js";

// Project state selects Home installations. It never owns another copy of the
// source, program or resources, including when the installed item is read-only.
export async function renderProjectExtensions(host,{kind,project,signal,isCurrent=()=>true,onChange=()=>{}}) {
  const base=`/${kind}/project`,query=`?project=${encodeURIComponent(project)}`,options=()=>({signal});
  const result=(await api.get(base+query,options())).data;if(!isCurrent())return;
  const status=element("p",{attrs:{role:"status"}});host.replaceChildren(status);
  for(const item of result.items){const row=element("article",{className:"resource-card"},[element("h3",{text:item.name||item.alias}),element("p",{text:`${item.alias} · ${item.id} · ${item.enabled?"已启用":"已停用"}`})]);
    function button(name,action){const b=element("button",{text:name,className:"secondary-button",attrs:{type:"button"}});b.addEventListener("click",async()=>{if(!isCurrent())return;const all=host.querySelectorAll("button");all.forEach(c=>c.disabled=true);try{await action();if(isCurrent()){onChange();await renderProjectExtensions(host,{kind,project,signal,isCurrent,onChange});}}catch(e){if(isCurrent())status.textContent=errorMessage(e);}finally{if(isCurrent())all.forEach(c=>c.disabled=false);}});return b;}
    const path=`${base}/${encodeURIComponent(item.id)}`;row.append(button(item.project_enabled?"在此项目停用":"在此项目启用",()=>api.post(`${path}/enabled${query}`,{enabled:!item.project_enabled},{...options(),ifMatch:`"${result.revision}"`})),button("恢复全局设置",()=>api.delete(path+query,{...options(),ifMatch:`"${result.revision}"`})));
    host.append(row);
  }
  if(!result.items.length)host.append(element("p",{text:"尚无 Home 扩展，请先在本机列表创建或安装。"}));
}
