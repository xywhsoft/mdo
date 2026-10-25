import { api } from "../../api/client.js";
import { targetState, subscribeTarget } from "../../api/target.js";
import { element, errorMessage, toast } from "../../utils/dom.js";
import { openLayer } from "../../components/layers.js";
import { portableId } from "./extension-formats.js";
import { knowledgeMetadata, parseKnowledge, validateKnowledgeFiles, knowledgeFolder, knowledgeBytes, knowledgeBase64, knowledgeText, KNOWLEDGE_LIMIT } from "./knowledge-format.js";

const node=(tag,text,attrs={})=>element(tag,{text,attrs});
const button=(text,action)=>{const b=element("button",{text,className:"secondary-button",attrs:{type:"button"}});b.addEventListener("click",()=>{void action();});return b;};
const target=()=>targetState().selected?.id||"local";
export function createKnowledgeEditor({owner,onChange}) {
  let active=null,serial=0,disposed=false;
  const close=(force=false)=>{if(!active)return true;if(!force&&(active.saving||active.importing||active.dirty&&!confirm("放弃未保存的知识库？")))return false;active.layer.dispose();active=null;return true;};
  async function open({item=null,imported=null}={}) {
    if(!close())return;const epoch=++serial,device=target();
    try {
      const bundle=item?(await api.get(`/extensions/knowledge/${encodeURIComponent(item.id)}/bundle`)).data:imported;
      if(disposed||epoch!==serial||device!==target())return;
      const state={device,dirty:false,saving:false,importing:false,files:structuredClone(bundle?.files||[{path:"index.md",base64:knowledgeBase64(new TextEncoder().encode("# 知识库索引\n\n在这里整理文档链接和内容简介。\n"))}]),selected:0,bundleRevision:bundle?.bundle_revision};
      const doc=knowledgeMetadata(bundle?.content?JSON.parse(bundle.content):{});
      const layer=openLayer({title:item?"编辑知识库":"新建知识库",icon:"folder",width:"64rem",height:"86%",owner,beforeClose:()=>!state.saving&&!state.importing&&(!state.dirty||confirm("放弃未保存的知识库？")),onClosed(){if(active===state)active=null;}});state.layer=layer;active=state;layer.element.classList.add("extension-authoring");
      const form=element("form",{className:"extension-form"}),fields={},formId=layer.element.id+"-form";form.setAttribute("id",formId);
      const field=(key,title,value,area=false)=>{const c=element(area?"textarea":"input",{attrs:{name:key,required:["id","name","description","index"].includes(key)?"":null,spellcheck:"false",maxlength:key==="prompt"?8192:key==="description"?600:key==="copyright"?4096:key==="id"?64:220}});c.value=value;fields[key]=c;form.append(element("label",{className:"extension-field"},[node("span",title),c]));return c;};
      field("id","知识库 ID（小写英文）",item?.id||imported?.id||"").disabled=!!item;
      for(const [key,title,area]of[["name","名称"],["description","描述：面向用户说明资料内容与用途",true],["index","索引文件（相对路径）"],["prompt","注入提示词：{$path} 将替换为本机知识库目录",true],["author","作者（可选）"],["copyright","版权声明（可选）",true],["license","许可证（可选）"]])field(key,title,doc[key]||"",area);
      const inject=element("input",{attrs:{type:"checkbox"}});inject.checked=doc.inject_prompt;form.append(element("label",{className:"extension-check"},[inject,node("span","向 Agent 注入提示词（文档内容按需读取，不会整体注入）")]));
      const selector=element("select",{attrs:{"aria-label":"文档"}}),filename=element("input",{attrs:{"aria-label":"文档相对路径"}}),source=element("textarea",{className:"extension-source",attrs:{"aria-label":"文档内容",spellcheck:"false"}}),note=node("p",""),status=node("p","",{role:"status"});
      let displayed=-1,textMode=false;
      const busy=()=>state.saving||state.importing;
      const current=()=>!disposed&&active===state&&device===target();
      const flush=()=>{if(displayed<0||!state.files[displayed])return;state.files[displayed].path=filename.value;if(textMode)state.files[displayed].base64=knowledgeBase64(new TextEncoder().encode(source.value));};
      const show=()=>{const f=state.files[state.selected];selector.replaceChildren(...state.files.map((f,i)=>node("option",f.path,{value:String(i)})));selector.value=String(state.selected);displayed=state.selected;filename.value=f?.path||"";const text=f?knowledgeText(f):"";textMode=text!==null&&text.length<=2*1024*1024;source.disabled=!textMode;source.value=textMode?text:"此文件为二进制或较大文档，保存时保留原始字节。";filename.disabled=!f;note.textContent=`${state.files.length} / 128 个文档 · ${(state.files.reduce((n,f)=>n+knowledgeBytes(f.base64).length,0)/1048576).toFixed(2)} MiB / 64 MiB`;};
      selector.addEventListener("change",()=>{if(busy()||!current())return;flush();state.selected=Number(selector.value);show();});
      const addFiles=element("input",{attrs:{type:"file",multiple:""}});addFiles.hidden=true;
      addFiles.addEventListener("change",async()=>{
        if(busy()||!current())return;
        const input=Array.from(addFiles.files||[]);if(!input.length)return;
        state.importing=true;lock(true);status.textContent="正在读取文档…";
        try{
          flush();const staged=[];let total=state.files.reduce((n,v)=>n+knowledgeBytes(v.base64).length,0);
          for(const f of input){
            if(state.files.length+staged.length>=128)throw Error("最多 128 个文档");
            if(total+f.size>KNOWLEDGE_LIMIT)throw Error("文档超过 64 MiB");
            const bytes=new Uint8Array(await f.arrayBuffer());if(!current())return;
            total+=bytes.length;if(total>KNOWLEDGE_LIMIT)throw Error("文档超过 64 MiB");
            staged.push({path:f.name,base64:knowledgeBase64(bytes)});
          }
          state.files.push(...staged);state.dirty=true;state.selected=state.files.length-1;status.textContent="";
        }catch(e){if(current())status.textContent=errorMessage(e);}
        finally{state.importing=false;lock(false);addFiles.value="";if(current())show();}
      });
      const tools=element("div",{className:"eb-toolbar"},[selector,button("新建文档",()=>{if(busy()||!current())return;flush();if(state.files.length>=128){status.textContent="最多 128 个文档";return;}state.files.push({path:`document-${state.files.length+1}.md`,base64:""});state.selected=state.files.length-1;state.dirty=true;show();}),button("添加文件",()=>{if(!busy()&&current())addFiles.click();}),button("移除文档",()=>{if(busy()||!current())return;flush();if(!state.files.length)return;state.files.splice(state.selected,1);state.selected=Math.max(0,state.selected-1);state.dirty=true;show();}),addFiles]);
      form.append(node("h3","文档目录"),tools,filename,source,note,status);show();
      form.addEventListener("input",()=>{if(!busy())state.dirty=true;});form.addEventListener("change",()=>{if(!busy())state.dirty=true;});
      const save=element("button",{text:item?"保存":"添加",className:"primary-button",attrs:{type:"submit",form:formId}}),cancel=button("取消",()=>layer.close("cancel"));layer.footer.append(save,cancel);layer.body.append(form);
      const disabled=new Map();
      function lock(value){
        if(value){for(const c of [...form.querySelectorAll("input,textarea,select,button"),save,cancel]){disabled.set(c,!!c.disabled);c.disabled=true;}}
        else {for(const [c,wasDisabled]of disabled)c.disabled=wasDisabled;disabled.clear();}
        form.setAttribute("aria-busy",String(value));
      }
      form.addEventListener("submit",async event=>{event.preventDefault();if(busy()||!current())return;try{flush();const id=fields.id.value.trim();if(!portableId(id))throw Error("请使用可移植的小写英文 ID。");const metadata=parseKnowledge(JSON.stringify({...doc,...Object.fromEntries(Object.entries(fields).filter(([k])=>k!=="id").map(([k,c])=>[k,c.value])),inject_prompt:inject.checked}));validateKnowledgeFiles(metadata,state.files);
        state.saving=true;lock(true);status.textContent="正在保存文档目录…";
        await api.put(`/extensions/knowledge/${id}`,{content:JSON.stringify(metadata),files:structuredClone(state.files),...(state.bundleRevision?{bundle_revision:state.bundleRevision}:{})},{ifMatch:`"${item?.revision||"new"}"`});
        if(active!==state||device!==target())return;state.dirty=false;state.saving=false;close(true);onChange();toast("知识库已保存，下次任务运行时使用最新提示词。","success");
      }catch(e){if(current())status.textContent=errorMessage(e);}finally{state.saving=false;lock(false);}});
    }catch(e){if(epoch===serial&&device===target())toast(errorMessage(e),"error");}
  }
  const unsubscribe=subscribeTarget(()=>{if(active&&active.device!==target()){++serial;close(true);}});
  return {open,async importFolder(files){try{const device=target(),epoch=++serial,data=await knowledgeFolder(files);if(device===target()&&epoch===serial)await open({imported:data});}catch(e){toast(errorMessage(e),"error");}},async importFile(file){try{const device=target(),epoch=++serial;if(file.size>100*1024*1024)throw Error("知识库包超过 100 MiB");const p=JSON.parse(await file.text());const r=p.resources?.length===1&&p.resources[0].kind==="knowledge"?p.resources[0]:p.kind==="knowledge"?p:null;if(!r)throw Error("请选择单知识库 JSON 文件，普通文档目录请使用“导入目录”。");parseKnowledge(r.content);validateKnowledgeFiles(JSON.parse(r.content),r.files);if(device===target()&&epoch===serial)await open({imported:r});}catch(e){toast(errorMessage(e),"error");}},hasPendingChanges:()=>!!active,destroy(){disposed=true;++serial;unsubscribe();close(true);}};
}
