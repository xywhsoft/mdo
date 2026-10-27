import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { descendants, testDocument } from "./helpers/extension-dom.mjs";
const flush=async()=>{for(let i=0;i<8;i++)await new Promise(resolve=>setImmediate(resolve));};
async function loadStore(api, device=()=>({selected:null}), subscribe=()=>()=>{}) {
 const url=new URL("../app/web/js/features/settings/single-resource-store.js",import.meta.url);
 globalThis.__zipStore={api,targetState:device,subscribeTarget:subscribe};
 const source=readFileSync(url,"utf8").replace('import { api } from "../../api/client.js";',"const { api }=globalThis.__zipStore;").replace('import { targetState, subscribeTarget } from "../../api/target.js";',"const { targetState, subscribeTarget }=globalThis.__zipStore;").replace(/from "(\.[^"]+)"/g,(_,path)=>`from ${JSON.stringify(new URL(path,url).href)}`);
 return import("data:text/javascript;base64,"+Buffer.from(source+`\n// ${Math.random()}`).toString("base64"));
}
function environment(){const previous={document:globalThis.document,window:globalThis.window,confirm:globalThis.confirm};globalThis.document=testDocument();globalThis.window=new EventTarget();globalThis.confirm=()=>true;return()=>{Object.assign(globalThis,previous);delete globalThis.__zipStore;};}

test("all nine stores use directory metadata and the shared numbered pager",async()=>{
 const restore=environment(),calls=[];const rows=Array.from({length:35},(_,i)=>({xid:`work-${i}`,version_id:`version-${i}`,name:`Resource ${i}`,owner:1,alias:`resource-${i}`}));
 const api={get:async()=>({data:{}}),post:async(path,body)=>{calls.push(body);assert.equal(body.action,"catalog");return{data:{items:rows.slice(body.offset,body.offset+20),total:35}};}};
 try{const {createSingleResourceStore}=await loadStore(api);for(const kind of ["agents","subagents","tools","mcp","skills","commands","external-tools","knowledge","plugins"]){const store=createSingleResourceStore(kind,()=>{});try{
   store.activate(true);await flush();const pager=()=>store.root.querySelector(".eb-pagination");const page=n=>pager().querySelectorAll("button").find(b=>b.textContent===String(n));page(2).click();
   const search=store.root.querySelector("input");search.value="draft query";search.dispatchEvent(new Event("input"));pager().querySelectorAll("button").find(b=>b.textContent==="下一页").click();await flush();
   assert.equal(calls.at(-1).kind,kind);assert.equal(calls.at(-1).offset,20);assert.equal(calls.at(-1).q,"");assert(!Object.hasOwn(calls.at(-1),"single_kind"));
   assert.equal(store.root.querySelector(".eb-name").textContent,"Resource 20");assert.equal(pager().querySelector(".eb-page-status").textContent,"共 35 条");
   const toolbar=store.root.querySelector("form");assert.deepEqual(toolbar.children.map(n=>n.tagName),["INPUT","SELECT","BUTTON","BUTTON"]);
   assert(!descendants(store.root).some(n=>n.textContent==="创作中心"));
 }finally{store.destroy();}}}finally{restore();}
});

test("publication packs the real directory and sends only task and review metadata",async()=>{
 const restore=environment(),calls=[];
 const api={post:async(path,body)=>{calls.push({path,body});if(path==="/extensions/skills/authored/pack")return{data:{id:"V--fixture-publication-task-00000"}};if(body.action==="profile")return{data:{works:0,work_limit:3,version_bytes:10485760}};if(body.action==="mine")return{data:{items:[]}};if(body.action==="submit")return{data:{state:"pending"}};throw Error(`Unexpected ${path}`);}};
 let store;try{const {createSingleResourceStore}=await loadStore(api);store=createSingleResourceStore("skills",()=>{});await store.publish({id:"authored",name:"Fixture",source:"manual"});const layer=document.body.children[0],form=layer.querySelector("form");
  assert(layer.classList.contains("mdo-layer"));assert(layer.querySelector(".mdo-layer-footer").querySelectorAll("button").some(b=>b.textContent==="提交作品"));
  form.querySelector("textarea").value="Source and usage reviewed";const checks=form.querySelectorAll("input").filter(n=>n.type==="checkbox");checks[0].checked=true;for(const file of form.querySelectorAll("input").filter(n=>n.type==="file"))file.files=[];form.querySelector("select").selectedOptions=[{dataset:{}}];
  form.dispatchEvent(new Event("submit",{cancelable:true}));await flush();assert(calls.some(c=>c.path==="/extensions/skills/authored/pack"));const submit=calls.find(c=>c.body.action==="submit");assert(submit);assert.equal(submit.body.task,"V--fixture-publication-task-00000");assert.equal(submit.body.publish,true);assert.equal(submit.body.source_included,true);for(const field of ["package","resources","files","base64"])assert(!Object.hasOwn(submit.body,field));
 }finally{store?.destroy();restore();}
});

test("store managed items cannot be published or accept old JSON import packages",async()=>{
 const restore=environment();let requests=0;const api={post:async()=>{requests++;throw Error("No request expected");}};
 let store;try{const {createSingleResourceStore}=await loadStore(api);store=createSingleResourceStore("mcp",()=>{});await store.publish({id:"store-id",source:"store",editable:false});assert.equal(requests,0);await assert.rejects(store.importPackage({manifest:{kind:"mcp"},resources:[]}),/ZIP/);}finally{store?.destroy();restore();}
});
