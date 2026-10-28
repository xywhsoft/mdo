import { api } from "../../api/client.js";
import { stageExtensionFile } from "../../api/extension-archives.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { openLayer } from "../../components/layers.js";
import { element, errorMessage, toast } from "../../utils/dom.js";
import { customizableResource } from "./resource-credits.js";
import { directoryPath, directoryId, directoryManifest, saveDirectory } from "./directory-documents.js";

const target=()=>targetState().selected?.id||"local";
// One bounded file editor for every extension. Files remain native directory
// entries; binary replacements use sealed raw-byte tasks, never JSON encoding.
export function createDirectoryEditor({owner,onChange=()=>{}}={}) {
  let active=null,disposed=false;
  const close=force=>{if(!active)return true;if(!force&&(active.busy||active.dirty&&!confirm("放弃未保存的目录修改？")))return false;active.view.dispose();return true;};
  const stop=subscribeTarget(()=>{if(active&&active.target!==target())close(true);});
  async function open({kind,item=null,manifest=null,text="",edits=[]}={}) {
    if(disposed||!close(false))return;
    const state={target:target(),busy:false,dirty:false,revision:"",item,edits:new Map(edits.map(e=>[e.path,e])),rows:[],offset:0,selected:null,textDirty:false};
    const releaseTask=id=>{if(id&&state.target===target())void Promise.resolve().then(()=>api.post(`/extension-tasks/${id}/cancel`,{})).catch(()=>{});};
    const view=openLayer({title:item?`扩展目录 · ${item.name||item.id}`:"新建扩展目录",icon:"folder",width:"60rem",height:"85%",owner,
      beforeClose:()=>!state.busy&&(!state.dirty||confirm("放弃未保存的目录修改？")),onClosed(){for(const edit of state.edits.values())releaseTask(edit.task);if(active===state)active=null;}});state.view=view;active=state;
    const live=()=>active===state&&!view.signal.aborted&&state.target===target(),options=()=>({signal:view.signal});
    let editable=!item||customizableResource(item);
    const message=element("p",{attrs:{role:"status"}}),form=element("form",{className:"extension-form"}),files=element("div",{className:"extension-directory-files"}),preview=element("textarea",{className:"extension-source",attrs:{"aria-label":"文件内容",spellcheck:"false",maxlength:"1048576"}});
    const controls=new Map();
    const field=(key,label,value,area=false)=>{const c=element(area?"textarea":"input",{attrs:{"aria-label":label,maxlength:area?"65536":"4096"}});c.value=value||"";form.append(element("label",{className:"extension-field"},[element("span",{text:label}),c]));controls.set(key,c);return c;};
    const id=field("id","本机目录名称",item?.id||manifest?.alias||""),metadata=field("manifest","extension.json（名称、别名、入口、平台、作者、版权与许可）","",true);
    const filename=element("input",{attrs:{"aria-label":"文件路径",maxlength:"4096",placeholder:"相对于扩展根的路径"}}),upload=element("input",{attrs:{type:"file",multiple:"","aria-label":"添加资源文件"}});upload.hidden=true;
    const button=(name,fn,primary=false)=>{const b=element("button",{text:name,className:primary?"primary-button":"secondary-button",attrs:{type:"button"}});b.addEventListener("click",()=>{void run(fn);});return b;};
    const lock=()=>{if(!live())return;view.element.querySelectorAll("input,textarea,select,button").forEach(c=>c.disabled=state.busy);id.disabled=state.busy||Boolean(item);metadata.readOnly=!editable;preview.readOnly=!editable||!state.selected||state.selected.binary||state.selected.path==="extension.json";filename.disabled=state.busy||!editable;for(const b of writeButtons)b.disabled=state.busy||!editable;};
    async function run(fn){if(state.busy||!live())return;state.busy=true;lock();try{await fn();}catch(e){if(live()&&e.name!=="AbortError")message.textContent=errorMessage(e);}finally{state.busy=false;lock();}}
    const flush=()=>{if(state.textDirty&&state.selected){const path=filename.value.trim();if(path!==state.selected.path)throw Error("重命名请先保存当前文本，再使用重命名操作");state.edits.set(path,{path,text:preview.value});state.textDirty=false;}};
    function show(file){state.selected=file;state.textDirty=false;filename.value=file.path;preview.value=file.text??"此文件为二进制或超过文本预览上限，原始字节会保留。";lock();}
    async function select(row){flush();const edit=state.edits.get(row.path);if(edit&&"text" in edit){show(edit);return;}if(!item){show({...row,binary:true});return;}const file=(await api.get(`${directoryPath(kind,item.id)}/file?path=${encodeURIComponent(row.path)}`,options())).data;if(live())show(file);}
    function renderFiles(){files.replaceChildren(...state.rows.map(row=>row.directory?element("p",{text:`${row.path}/`}):button(`${row.path} · ${row.bytes??"待保存"} B`,()=>select(row))));}
    async function list(reset=false){if(!item)return;if(reset){state.offset=0;state.rows=[];}const page=(await api.get(`${directoryPath(kind,item.id)}/files?offset=${state.offset}&limit=50`,options())).data;if(!live())return;state.rows.push(...page.items);state.offset+=page.items.length;renderFiles();more.hidden=state.offset>=page.total;}
    const save=button("保存目录",async()=>{flush();const identity=id.value.trim();if(!directoryId(identity))throw Error("请输入有效的本机目录名称（最多 96 字符）");const m=JSON.parse(metadata.value);if(m.kind!==kind)throw Error("不能改变扩展类型");if(state.edits.size>32)throw Error("每次最多保存 32 项修改，请分批保存或导入完整 ZIP");
      const result=await saveDirectory(kind,identity,m,[...state.edits.values()],state.revision,options());if(!live())return;state.edits.clear();state.dirty=false;state.busy=false;close(true);onChange(kind,result);toast(result.restart_required?"目录已保存，重启 mdo 后生效":result.pending?"目录已保存，等待旧插件完成清理":result.runtime_error?`文件已保存，运行时需要处理：${result.runtime_error}`:"目录已保存","success");},true);
    const newText=button("新建文本",()=>{flush();const path=filename.value.trim();if(!path||path==="extension.json"||state.rows.some(f=>f.path===path))throw Error("输入新的文件路径；根清单在上方编辑");const row={path,text:""};state.edits.set(path,row);state.rows.push(row);state.dirty=true;renderFiles();show(row);});
    const uploadButton=button("添加资源",()=>{upload.disabled=false;upload.click();});
    upload.addEventListener("change",()=>{void run(async()=>{flush();for(const file of upload.files){const chosen=filename.value.trim(),sameName=state.selected?.path?.split("/").at(-1)===file.name;const path=upload.files.length===1&&chosen&&(chosen!==state.selected?.path||sameName)?chosen:file.name;if(path==="extension.json")throw Error("请在上方编辑根清单");const task=file.size?await stageExtensionFile(file,{kind,purpose:"file",signal:view.signal}):null;if(!live()){releaseTask(task?.id);return;}releaseTask(state.edits.get(path)?.task);state.edits.set(path,task?{path,task:task.id}:{path,text:""});if(!state.rows.some(r=>r.path===path))state.rows.push({path,bytes:file.size});state.dirty=true;}upload.value="";renderFiles();message.textContent="原始资源已暂存，点击保存目录后生效。";});});
    const mutate=action=>async()=>{flush();if(state.dirty)throw Error("请先保存或放弃当前修改，再管理目录结构");if(!item||!state.selected)throw Error("先选择已保存文件");const path=state.selected.path,extra={action,expected_revision:state.revision,path};if(action==="file.move"){const destination=prompt("新的相对路径",path);if(destination===null)return;extra.destination=destination;}else if(!confirm(`删除 ${path}？`))return;
      await api.post(`${directoryPath(kind,item.id)}/actions`,extra,options());const fresh=(await api.get(directoryPath(kind,item.id),options())).data;if(!live())return;state.revision=fresh.revision;state.selected=null;preview.value="";await list(true);onChange(kind,fresh);};
    const mkdir=button("新建目录",async()=>{flush();if(!item||state.dirty)throw Error("请先保存当前扩展和修改，再创建空目录");const path=prompt("新目录的相对路径");if(path===null)return;await api.post(`${directoryPath(kind,item.id)}/actions`,{action:"file.mkdir",path,expected_revision:state.revision},options());const fresh=(await api.get(directoryPath(kind,item.id),options())).data;if(!live())return;state.revision=fresh.revision;await list(true);onChange(kind,fresh);});
    const rename=button("重命名",mutate("file.move")),remove=button("删除文件",mutate("file.remove")),more=button("更多文件",()=>list()),cancel=button("关闭",()=>{state.busy=false;return view.close();});
    const writeButtons=[save,newText,uploadButton,mkdir,rename,remove];
    form.addEventListener("input",()=>{state.dirty=true;});preview.addEventListener("input",()=>{state.textDirty=true;state.dirty=true;});
    view.body.append(form,element("p",{className:"extension-help",text:"此目录是唯一安装位置；未编辑文件和权限会保留。新增资源传输原始字节，发布时打包整个目录。"}),files,more,filename,element("div",{className:"eb-toolbar"},[newText,uploadButton,mkdir,rename,remove,upload]),preview,message);view.footer.append(save,cancel);
    metadata.value=JSON.stringify(manifest||directoryManifest(kind,item?.id||"my-extension"),null,2);more.hidden=true;
    await run(async()=>{if(item){const fresh=(await api.get(directoryPath(kind,item.id),options())).data;if(!live())return;editable=customizableResource(fresh);state.revision=fresh.revision;metadata.value=JSON.stringify(fresh.manifest||directoryManifest(kind,directoryId(item.id,64)?item.id:"my-extension",{name:item.name||item.id}),null,2);await list(true);if(live()&&fresh.manifest?.entry)await select({path:fresh.manifest.entry});else if(live())message.textContent="根清单缺失或损坏。可以修复上方 extension.json，并在文件列表中检查实际入口；未编辑文件会保留。";}else{const m=JSON.parse(metadata.value);state.edits.set(m.entry,{path:m.entry,text});for(const e of state.edits.values())state.rows.push(e);renderFiles();show({path:m.entry,text});}});
  }
  return {open,close,hasPendingChanges:()=>Boolean(active),destroy(){disposed=true;close(true);stop();}};
}
