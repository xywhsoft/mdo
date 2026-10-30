import assert from "node:assert/strict";
import test from "node:test";
import { readFileSync } from "node:fs";
import { testDocument,descendants } from "./helpers/extension-dom.mjs";
const flush=async()=>{for(let i=0;i<8;i++)await new Promise(r=>setImmediate(r));};
const button=(root,name)=>descendants(root).find(n=>n.tagName==="BUTTON"&&n.textContent===name);
function acceptPath(value){const view=document.body.querySelector(".extension-path-layer");assert(view,"Missing path dialog");if(value!==undefined){const input=view.querySelector("input");input.value=value;input.dispatchEvent(new Event("input"));}button(view,"确定").click();}
function environment(){const old={document:globalThis.document,window:globalThis.window,confirm:globalThis.confirm};globalThis.document=testDocument();globalThis.window=new EventTarget();globalThis.confirm=()=>true;return()=>Object.assign(globalThis,old);}
async function load(name,mock){if(mock.api&&!mock.api.get)mock.api.get=async()=>({data:{executable_supported:false}});globalThis.__directoryUI=mock;const url=new URL(`../app/web/js/features/settings/${name}.js`,import.meta.url);const source=readFileSync(url,"utf8")
 .replace('import { api } from "../../api/client.js";',"const {api}=globalThis.__directoryUI;")
 .replace('import { stageExtensionFile } from "../../api/extension-archives.js";',"const {stageExtensionFile}=globalThis.__directoryUI;")
 .replace('import { targetState, subscribeTarget } from "../../api/target.js";',"const {targetState,subscribeTarget}=globalThis.__directoryUI;")
 .replace('import { createMcpContext } from "./mcp-context.js";',"const createMcpContext=()=>({destroy(){},open(){}});")
 .replace('import { createMcpAuthorization } from "./mcp-authorization.js";',"const createMcpAuthorization=()=>({destroy(){},open(){}});")
 .replace('import { refreshMcp, disconnectMcp } from "../../state/resources.js";',"const refreshMcp=()=>{},disconnectMcp=()=>{};")
 .replace('import { directoryPath, directoryId, directoryManifest, saveDirectory } from "./directory-documents.js";',"const {directoryPath,directoryId,directoryManifest,saveDirectory}=globalThis.__directoryUI;")
 .replace(/from "(\.[^"]+)"/g,(_,p)=>`from ${JSON.stringify(new URL(p,url).href)}`);
 return import("data:text/javascript;base64,"+Buffer.from(source+`\n// ${Math.random()}`).toString("base64"));
}

test("directory editor preserves unopened resources, uses raw file tasks and freezes the complete form until atomic save",async()=>{
 const restore=environment(),writes=[];let release,fail=true,gate;const item={id:"WORK-XID",source:"manual",editable:true,revision:"a".repeat(64),manifest:{kind:"tools",name:"Fixture",alias:"fixture",entry:"tool.c",platforms:["any"]}};
 const mock={targetState:()=>({selected:null}),subscribeTarget:()=>()=>{},directoryPath:(k,id)=>`/extensions/${k}/${id}`,directoryId:()=>true,directoryManifest:()=>item.manifest,
  api:{get:async path=>({data:path.includes("/files?")?{items:[{path:"tool.c",bytes:12},{path:".hidden/deep/data.bin",bytes:5}],total:2}:path.includes("/file?")?{path:"tool.c",text:"original C"}:item})},
  stageExtensionFile:async(file,{purpose})=>{assert(file instanceof Blob);assert.equal(purpose,"file");return {id:"binary-task"};},
  saveDirectory:async(...args)=>{writes.push(args);if(gate)await gate;if(fail)throw Error("Disk write failed");return {};}};
 let editor;try{const mod=await load("directory-editor",mock);editor=mod.createDirectoryEditor();await editor.open({kind:"tools",item});const view=document.body.querySelector("dialog"),footer=view.querySelector("footer");
  assert(footer);assert(!view.querySelector(".mdo-layer-body").contains(footer));const source=view.querySelector('[aria-label="文件内容"]');source.value="changed C";source.dispatchEvent(new Event("input"));button(view,"保存目录").click();await flush();assert(view.textContent.includes("Disk write failed"));assert.equal(source.value,"changed C");
  assert.equal(view.querySelector(".extension-current-file-path").textContent,"tool.c");assert(!view.querySelector('[aria-label="文件路径"]'));
  const upload=view.querySelector('[aria-label="添加资源文件"]');upload.files=[Object.assign(new Blob([new Uint8Array([0,255,7])]),{name:"data.bin"}),Object.assign(new Blob(["more"]),{name:"more.txt"})];upload.dispatchEvent(new Event("change"));await flush();acceptPath("");await flush();
  fail=false;gate=new Promise(r=>release=r);button(view,"保存目录").click();button(view,"保存目录").click();await flush();assert.equal(writes.length,2);assert(view.querySelectorAll("input,textarea,select,button").every(n=>n.disabled));
  assert.equal(writes[1][4],item.revision);const edits=writes[1][3];assert.deepEqual(edits.find(e=>e.path==="tool.c"),{path:"tool.c",text:"changed C"});assert.deepEqual(edits.find(e=>e.path==="data.bin"),{path:"data.bin",task:"binary-task"});assert(!JSON.stringify(edits).includes("base64"));assert(!edits.some(e=>e.path===".hidden/deep/data.bin"));release();await flush();assert(!document.body.querySelector("dialog"));
 }finally{editor?.destroy();delete globalThis.__directoryUI;restore();}
});

test("store directory browser exposes files but cannot save, rename or replace resources",async()=>{
 const restore=environment(),item={id:"STORE-XID",source:"store",editable:false,revision:"b".repeat(64),manifest:{kind:"skills",entry:"SKILL.md"}},mock={targetState:()=>({selected:null}),subscribeTarget:()=>()=>{},directoryPath:(k,id)=>`/extensions/${k}/${id}`,directoryId:()=>true,directoryManifest:()=>item.manifest,
 api:{get:async p=>({data:p.includes("/files?")?{items:[],total:0}:p.includes("/file?")?{path:"SKILL.md",text:"Store skill"}:item})},saveDirectory:async()=>assert.fail("read-only save"),stageExtensionFile:async()=>assert.fail("read-only upload")};
 let editor;try{editor=(await load("directory-editor",mock)).createDirectoryEditor();await editor.open({kind:"skills",item});const view=document.body.querySelector("dialog");for(const name of ["保存目录","重命名","删除文件","添加资源","新建目录"])assert(button(view,name).disabled);assert(view.querySelector('[aria-label="文件内容"]').readOnly);button(view,"关闭").click();await flush();assert(!document.body.querySelector("dialog"));}finally{editor?.destroy();delete globalThis.__directoryUI;restore();}
});

test("current file is a plain heading and new text uses its own path dialog",async()=>{
 const restore=environment(),writes=[],manifest={kind:"commands",name:"Test",alias:"test",entry:"command.c",platforms:["any"]};let editor;
 const mock={targetState:()=>({selected:null}),subscribeTarget:()=>()=>{},directoryPath:(k,id)=>`/extensions/${k}/${id}`,directoryId:()=>true,directoryManifest:()=>manifest,api:{},saveDirectory:async(...args)=>{writes.push(args);return{};}};
 try{editor=(await load("directory-editor",mock)).createDirectoryEditor();await editor.open({kind:"commands",manifest,text:"original command"});const view=document.body.querySelector("dialog"),heading=view.querySelector(".extension-current-file");
  assert.equal(heading.querySelector("code").textContent,"command.c");assert(!heading.querySelector("input"));assert(heading.contains(button(view,"重命名")));
  button(view,"新建文本").click();await flush();let dialog=document.body.querySelector(".extension-path-layer");assert(dialog);button(dialog,"取消").click();await flush();assert.equal(heading.querySelector("code").textContent,"command.c");
  button(view,"新建文本").click();await flush();acceptPath("notes/usage.md");await flush();assert.equal(heading.querySelector("code").textContent,"notes/usage.md");
  const text=view.querySelector('[aria-label="文件内容"]');text.value="New documentation";text.dispatchEvent(new Event("input"));button(view,"保存目录").click();await flush();
  assert.deepEqual(writes[0][3],[{path:"command.c",text:"original command"},{path:"notes/usage.md",text:"New documentation"}]);
 }finally{editor?.destroy();delete globalThis.__directoryUI;restore();}
});

test("resource paths remain explicit and cancelling staging or closing the owner keeps no orphan dialog",async()=>{
 const restore=environment(),writes=[],manifest={kind:"mcp",name:"Test",alias:"test",entry:"mcp.json",platforms:["any"]};let editor,staged=0;
 const mock={targetState:()=>({selected:null}),subscribeTarget:()=>()=>{},directoryPath:(k,id)=>`/extensions/${k}/${id}`,directoryId:()=>true,directoryManifest:()=>manifest,api:{delete:async()=>({})},stageExtensionFile:async()=>{staged++;return{id:"raw-resource"};},saveDirectory:async(...args)=>{writes.push(args);return{};}};
 try{editor=(await load("directory-editor",mock)).createDirectoryEditor();await editor.open({kind:"mcp",manifest,text:"{}"});const view=document.body.querySelector("dialog"),upload=view.querySelector('[aria-label="添加资源文件"]');
  upload.files=[new File(["raw"],"server")];upload.dispatchEvent(new Event("change"));await flush();button(document.body.querySelector(".extension-path-layer"),"取消").click();await flush();assert.equal(staged,0);
  upload.dispatchEvent(new Event("change"));await flush();acceptPath("bin/server");await flush();assert.equal(staged,1);button(view,"保存目录").click();await flush();assert.deepEqual(writes[0][3].find(e=>e.path==="bin/server"),{path:"bin/server",task:"raw-resource"});
  await editor.open({kind:"mcp",manifest,text:"{}"});button(document.body.querySelector("dialog"),"新建文本").click();await flush();assert(document.body.querySelector(".extension-path-layer"));editor.destroy();await flush();assert(!document.body.querySelector("dialog"));
 }finally{editor?.destroy();delete globalThis.__directoryUI;restore();}
});

test("missing manual manifest stays repairable, and discarded binary staging is cancelled",async()=>{
 const restore=environment(),calls=[],item={id:"broken",name:"Broken",source:"manual",editable:true,revision:"d".repeat(64)};
 const mock={targetState:()=>({selected:null}),subscribeTarget:()=>()=>{},directoryPath:(k,id)=>`/extensions/${k}/${id}`,directoryId:()=>true,directoryManifest:(kind,id)=>({kind,alias:id,entry:"mcp.json",platforms:["any"]}),api:{get:async p=>({data:p.includes("/files?")?{items:[{path:"bin/server",bytes:3}],total:1}:item}),post:async()=>assert.fail("Cancellation has no POST route"),delete:async p=>{calls.push({p});return{data:{state:"cancelled"}};}},stageExtensionFile:async()=>({id:"discard-owned-file"})};
 let editor;try{editor=(await load("directory-editor",mock)).createDirectoryEditor();await editor.open({kind:"mcp",item});const view=document.body.querySelector("dialog");assert(view.textContent.includes("根清单缺失或损坏"));assert.equal(JSON.parse(view.querySelector('[aria-label="extension.json（稳定 ID、依赖、冲突、名称、入口与许可）"]').value).kind,"mcp");
  const upload=view.querySelector('[aria-label="添加资源文件"]');upload.files=[new File([new Uint8Array([0,255])],"server")];upload.dispatchEvent(new Event("change"));await flush();acceptPath();await flush();button(view,"关闭").click();await flush();assert(calls.some(c=>c.p==="/extension-tasks/discard-owned-file"));
 }finally{editor?.destroy();delete globalThis.__directoryUI;restore();}
});

test("a new directory atomically saves its first binary and empty resources without replacing the selected config",async()=>{
 const restore=environment(),writes=[],cancelled=[],manifest={kind:"mcp",name:"New MCP",alias:"new-mcp",entry:"mcp.json",platforms:["any"]};let staged=0;
 const mock={targetState:()=>({selected:null}),subscribeTarget:()=>()=>{},directoryPath:(k,id)=>`/extensions/${k}/${id}`,directoryId:()=>true,directoryManifest:()=>manifest,api:{post:async p=>{cancelled.push(p);}},stageExtensionFile:async()=>{staged++;return{id:"new-binary-task"};},saveDirectory:async(...args)=>{writes.push(args);return{};}};
 let editor;try{editor=(await load("directory-editor",mock)).createDirectoryEditor();await editor.open({kind:"mcp",manifest,text:"{}"});const view=document.body.querySelector("dialog"),upload=view.querySelector('[aria-label="添加资源文件"]');upload.files=[new File([new Uint8Array([0,255])],"server.exe"),new File([],"empty")];upload.dispatchEvent(new Event("change"));await flush();acceptPath("");await flush();button(view,"保存目录").click();await flush();
  assert.equal(staged,1);assert.equal(writes[0][4],"");assert.deepEqual(writes[0][3],[{path:"mcp.json",text:"{}"},{path:"server.exe",task:"new-binary-task"},{path:"empty",text:""}]);assert.equal(cancelled.length,0);
 }finally{editor?.destroy();delete globalThis.__directoryUI;restore();}
});

test("multiple resource staging uses all editor slots and rejects oversized selections before uploading",async()=>{
 const restore=environment(),writes=[];let staged=0,editor;const manifest={kind:"mcp",name:"Batch",alias:"batch",entry:"mcp.json",platforms:["any"]};
 const mock={targetState:()=>({selected:null}),subscribeTarget:()=>()=>{},directoryPath:(k,id)=>`/extensions/${k}/${id}`,directoryId:()=>true,directoryManifest:()=>manifest,api:{delete:async()=>({data:{state:"cancelled"}})},stageExtensionFile:async()=>({id:`owned-${++staged}`}),saveDirectory:async(...args)=>{writes.push(args);return{};}};
 try{editor=(await load("directory-editor",mock)).createDirectoryEditor();await editor.open({kind:"mcp",manifest,text:"{}"});const view=document.body.querySelector("dialog"),upload=view.querySelector('[aria-label="添加资源文件"]');
  upload.files=Array.from({length:33},(_,i)=>new File(["qa"],`file-${i}`));upload.dispatchEvent(new Event("change"));await flush();assert.equal(staged,0);assert(view.textContent.includes("每次最多保存 32 项修改"));
  upload.files=Array.from({length:4},(_,i)=>new File(["qa"],`file-${i}`));upload.dispatchEvent(new Event("change"));await flush();acceptPath("");await flush();assert.equal(staged,4);button(view,"保存目录").click();await flush();assert.equal(writes[0][3].length,5);
 }finally{editor?.destroy();delete globalThis.__directoryUI;restore();}
});

test("Unix directory editors can mark a newly uploaded native program executable",async()=>{
 const restore=environment(),writes=[];let editor;const manifest={kind:"mcp",name:"Executable",alias:"executable",entry:"mcp.json",platforms:["linux-x86_64"]};
 const mock={targetState:()=>({selected:null}),subscribeTarget:()=>()=>{},directoryPath:(k,id)=>`/extensions/${k}/${id}`,directoryId:()=>true,directoryManifest:()=>manifest,api:{get:async path=>{assert.equal(path,"/extensions/mcp?limit=1");return{data:{executable_supported:true}};},delete:async()=>({data:{state:"cancelled"}})},stageExtensionFile:async()=>({id:"native-file"}),saveDirectory:async(...args)=>{writes.push(args);return{};}};
 try{editor=(await load("directory-editor",mock)).createDirectoryEditor();await editor.open({kind:"mcp",manifest,text:"{}"});const view=document.body.querySelector("dialog"),upload=view.querySelector('[aria-label="添加资源文件"]');upload.files=[new File([new Uint8Array([127,69])],"server")];upload.dispatchEvent(new Event("change"));await flush();acceptPath();await flush();
  button(view,"server · 2 B").click();await flush();const mode=view.querySelector('[aria-label="可执行权限（Linux / Unix）"]');assert(!mode.disabled);mode.checked=true;mode.dispatchEvent(new Event("change"));await flush();button(view,"保存目录").click();await flush();
  assert.deepEqual(writes[0][3].find(e=>e.path==="server"),{path:"server",task:"native-file",executable:true});assert(!writes[0][3].find(e=>e.path==="mcp.json").executable);
 }finally{editor?.destroy();delete globalThis.__directoryUI;restore();}
});

test("MCP connections use installed directory IDs, private revisions and preview-bound approvals, without publishing credentials",async()=>{
 const restore=environment(),calls=[];const template={id:"SERVICE-XID",name:"Service",template_valid:true,template:{inputs:[{id:"endpoint",label:"Endpoint",type:"text"},{id:"token",label:"Token",type:"text",secret:true}],variants:[{id:"default",label:"HTTP",config:{transport:{endpoint:{input:"endpoint"},headers:[{secret_ref:"input:token"}]}}}]}};
 const mock={targetState:()=>({selected:null}),subscribeTarget:()=>()=>{},api:{get:async p=>({data:p==="/mcp/templates"?{items:[template]}:p==="/projects"?{items:[]}:p==="/mcp/connections"?[]:{items:[]}}),post:async(p,b)=>{calls.push({p,b});return{data:b.action==="preview"?{config:{name:"safe"},approval_digest:"c".repeat(64)}:{id:b.id}};}}};
 let manager;try{manager=(await load("mcp-directory-connections",mock)).createMcpConnections();await manager.open({templateId:"SERVICE-XID"});let view=document.body.querySelector("dialog");view.querySelector('[aria-label="连接名称标识"]').value="private-connection";view.querySelector('[aria-label="Endpoint"]').value="https://example.invalid/mcp";view.querySelector('[aria-label="Token"]').value="local-only-test-secret";
  button(view,"保存连接").click();await flush();assert.equal(calls.length,0);button(view,"预览配置").click();await flush();assert.equal(calls[0].b.extension_id,"SERVICE-XID");assert.deepEqual(calls[0].b.secrets,{token:"local-only-test-secret"});assert(!Object.hasOwn(calls[0].b.values,"token"));assert(!view.querySelector("pre").textContent.includes("local-only-test-secret"));
  view.querySelector('[aria-label="已审阅并允许运行此程序或连接目标"]').checked=true;button(view,"保存连接").click();await flush();assert.equal(calls[1].b.approval_digest,"c".repeat(64));assert.equal(calls[1].b.expected_revision,"");assert(!Object.hasOwn(calls[1].b,"receipt_key"));assert(!Object.hasOwn(calls[1].b,"config"));assert(!view.querySelector('[aria-label="Token"]'));assert(view.textContent.includes("尚未配置连接"));
  button(view,"关闭").click();await flush();assert(!document.body.querySelector("dialog"));
 }finally{manager?.destroy();delete globalThis.__directoryUI;restore();}
});

test("project selectors only toggle Home XIDs with a revision precondition",async()=>{
 const restore=environment(),calls=[],mock={api:{get:async()=>({data:{revision:"pref-hash",items:[{id:"PHYSICAL-XID",alias:"SkillAlias",name:"Test",project_enabled:true,enabled:true}]}}),post:async(p,b,o)=>{calls.push({p,b,o});return{};}}};
 try{const {renderProjectExtensions}=await load("project-extension-selector",mock);const host=document.createElement("div");await renderProjectExtensions(host,{kind:"skills",project:"project-id"});button(host,"在此项目停用").click();await flush();assert.equal(calls[0].p,"/skills/project/PHYSICAL-XID/enabled?project=project-id");assert.deepEqual(calls[0].b,{enabled:false});assert.equal(calls[0].o.ifMatch,'"pref-hash"');}finally{delete globalThis.__directoryUI;restore();}
});
