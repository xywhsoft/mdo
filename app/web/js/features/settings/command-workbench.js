import { api } from "../../api/client.js";
import { subscribeTarget, targetState } from "../../api/target.js";
import { clear, element, errorMessage } from "../../utils/dom.js";
import { portableId } from "./extension-formats.js";
import { newCommand } from "./command-template.js";

export function createCommandWorkbench({ onChange = () => {} } = {}) {
  let dialog, controller, serial = 0,guard=()=>false;
  const owner = () => targetState().selected?.id || "local";
  function close(force=false) {
    if(!force&&guard()&&!window.confirm("放弃尚未保存的项目命令修改？"))return false;
    ++serial;controller?.abort();controller=null;dialog?.remove();dialog=null;guard=()=>false;return true;
  }
  const unsubscribe = subscribeTarget(()=>close(true));
  async function open() {
    if(!close())return;const target = owner();dialog=element("dialog",{className:"extension-editor"});
    const project=element("select",{attrs:{"aria-label":"项目"}}),list=element("div",{className:"extension-list"});
    project.append(element("option",{text:"默认项目",attrs:{value:"default"}}));
    const id=element("input",{attrs:{placeholder:"my-command","aria-label":"命令 ID",maxlength:64}});
    const source=element("textarea",{className:"extension-source",attrs:{"aria-label":"C 命令源代码",spellcheck:"false"}});
    const status=element("p",{attrs:{role:"status"}});let revision="new",selected=null,busy=false,baseline="";
    guard=()=>Boolean(dialog&&(busy||source.value!==baseline||(!selected&&id.value)));
    const action=(text,handler)=>{const b=element("button",{className:"secondary-button",text,attrs:{type:"button"}});b.onclick=()=>void handler();return b;};
    function reset(){selected=null;revision="new";id.disabled=false;id.value="";source.value=newCommand();baseline=source.value;status.textContent="新建 .mdo/commands/<名称>.c";}
    async function load() {
      controller?.abort();controller=new AbortController();const generation=++serial,scope=project.value;reset();clear(list);
      try {
        const response=await api.get(`/commands?project=${encodeURIComponent(scope)}`,{signal:controller.signal});
        if(!dialog||generation!==serial||target!==owner())return;
        for(const item of Object.values(response.data||{})){
          const row=element("div",{className:"extension-row"},[
            element("span",{text:`/${item.id} · ${item.scope} · ${item.enabled?"启用":"停用"}${item.valid?"":" · 配置无效"}`}),
            action(item.scope==="project"?"编辑":"复制到项目",async()=>{
              const token=++serial;controller?.abort();controller=new AbortController();
              try {
                const path=item.scope==="project"?`/commands/project/${item.id}?project=${encodeURIComponent(scope)}`:`/extensions/commands/${item.id}`;
                const data=(await api.get(path,{signal:controller.signal})).data;
                if(!dialog||token!==serial||scope!==project.value||target!==owner())return;
                selected=item;id.value=item.id;id.disabled=true;source.value=data.content;baseline=source.value;
                revision=item.scope==="project"?data.revision:"new";
                status.textContent=item.scope==="project"?"修改当前项目命令":"保存后创建当前项目的同名覆盖文件";
              }catch(error){if(dialog&&token===serial&&error.name!=="AbortError")status.textContent=errorMessage(error);}
            }),
          ]);list.append(row);
        }
      }catch(error){if(dialog&&generation===serial&&error.name!=="AbortError")status.textContent=errorMessage(error);}
    }
    async function mutate(kind) {
      if(busy)return;
      try {
        if(!portableId(id.value))throw Error("请输入合法命令 ID");
        if(kind!=="save"&&selected?.scope!=="project")throw Error("请先选择项目命令");
        const path=`/commands/project/${id.value}${kind==="toggle"?"/enabled":""}?project=${encodeURIComponent(project.value)}`;
        busy=true;dialog.querySelectorAll("button,input,textarea,select").forEach(n=>n.disabled=true);
        if(kind==="delete")await api.delete(path,{ifMatch:`"${revision}"`});
        else if(kind==="toggle")await api.post(path,{enabled:!selected.enabled},{ifMatch:`"${revision}"`});
        else await api.put(path,{content:source.value},{ifMatch:`"${revision}"`});
        if(!dialog||target!==owner())return;onChange();await load();
      }catch(error){if(dialog&&target===owner())status.textContent=errorMessage(error);}
      finally{busy=false;if(dialog){dialog.querySelectorAll("button,input,textarea,select").forEach(n=>n.disabled=false);id.disabled=Boolean(selected);}}
    }
    id.addEventListener("input",()=>{if(source.value===baseline&&portableId(id.value)){source.value=newCommand(id.value);baseline=source.value;}});
    let previousProject=project.value;
    project.onchange=()=>{
      if(guard()&&!window.confirm("放弃尚未保存的项目命令修改？")){project.value=previousProject;return;}
      previousProject=project.value;void load();
    };
    dialog.addEventListener("cancel",e=>{e.preventDefault();if(!busy)close();});
    dialog.append(element("h2",{text:"项目命令"}),element("p",{text:"项目命令优先于个人和内置命令。保存仅校验元数据；C 代码在调用时编译运行。"}),project,list,id,source,status,
      element("div",{className:"extension-toolbar"},[action("新建",reset),action("保存",()=>mutate("save")),action("启用 / 停用",()=>mutate("toggle")),action("删除覆盖文件",()=>mutate("delete")),action("关闭",()=>{if(!busy)close();})]));
    document.body.append(dialog);dialog.showModal();reset();
    try {
      controller=new AbortController();const items=(await api.get("/projects",{signal:controller.signal})).data.items||[];
      if(!dialog||target!==owner())return;
      for(const item of items)if(item.id!=="default")project.append(element("option",{text:item.name||item.id,attrs:{value:item.id}}));
      await load();
    }catch(error){if(dialog&&error.name!=="AbortError")status.textContent=errorMessage(error);}
  }
  return {open,close,hasPendingChanges:()=>guard(),destroy(){close(true);unsubscribe();}};
}
