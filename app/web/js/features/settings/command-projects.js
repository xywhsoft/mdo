import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { openLayer } from "../../components/layers.js";
import { element, errorMessage } from "../../utils/dom.js";
import { renderProjectExtensions } from "./project-extension-selector.js";

export function createCommandWorkbench({owner,onChange=()=>{}}={}) {
  let active=null;const target=()=>targetState().selected?.id||"local",close=()=>{active?.dispose();active=null;},stop=subscribeTarget(close);
  async function open(){close();const device=target(),view=openLayer({title:"项目编辑器命令",icon:"terminal",width:"52rem",height:"80%",owner,onClosed(){if(active===view)active=null;}});active=view;
    const live=()=>active===view&&!view.signal.aborted&&device===target(),select=element("select",{attrs:{"aria-label":"项目"}}),list=element("div"),status=element("p",{attrs:{role:"status"}}),dismiss=element("button",{text:"关闭",className:"secondary-button",attrs:{type:"button"}});
    view.body.append(element("p",{text:"命令源码统一保存在 Home/commands 中。项目仅选择启停，更新目录后所有使用它的项目都读取同一份内容。"}),select,list,status);view.footer.append(dismiss);dismiss.addEventListener("click",()=>view.close());let serial=0;
    const load=async()=>{const epoch=++serial,project=select.value;try{await renderProjectExtensions(list,{kind:"commands",project,signal:view.signal,isCurrent:()=>live()&&epoch===serial&&project===select.value,onChange});}catch(e){if(live()&&e.name!=="AbortError")status.textContent=errorMessage(e);}};
    select.addEventListener("change",()=>void load());try{const projects=(await api.get("/projects",{signal:view.signal})).data.items||[];if(!live())return;if(!projects.some(p=>p.id==="default"))projects.unshift({id:"default",name:"默认项目"});for(const p of projects.filter(p=>p.status!=="trash"))select.append(element("option",{text:p.name||p.id,attrs:{value:p.id}}));select.value=projects[0].id;await load();}catch(e){if(live())status.textContent=errorMessage(e);}
  }
  return {open,close,hasPendingChanges:()=>false,destroy(){close();stop();}};
}
