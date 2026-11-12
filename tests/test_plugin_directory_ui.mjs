import assert from "node:assert/strict";
import test from "node:test";
import { testDocument,descendants } from "./helpers/extension-dom.mjs";
const flush=async()=>{for(let i=0;i<12;i++)await new Promise(r=>setImmediate(r));},button=(root,name)=>descendants(root).find(n=>n.tagName==="BUTTON"&&n.textContent===name);
function environment(){const saved={document:globalThis.document,window:globalThis.window,confirm:globalThis.confirm};globalThis.document=testDocument();globalThis.window=new EventTarget();globalThis.confirm=()=>true;return()=>Object.assign(globalThis,saved);}
test("plugin creation writes root metadata, recipe and real source as directory text edits",async t=>{
 const restore=environment();const calls=[];t.mock.method(globalThis,"fetch",async(url,o)=>{if(o.method==="PUT"){calls.push(JSON.parse(o.body));return Response.json({ok:true,data:{}});}if(url==="/api/v1/application/call")return Response.json({ok:true,data:{items:[]}});return Response.json({ok:true,data:{}});});
 const {createPluginWorkbench}=await import("../app/web/js/features/settings/plugin-workbench.js");const root=document.createElement("div");document.body.append(root);const panel=createPluginWorkbench(root);t.after(()=>{panel.destroy();restore();});panel.selectSection("extensions");await flush();button(root,"新建 C 插件").click();await flush();const view=document.body.querySelector("dialog");assert(view);button(view,"保存目录").click();await flush();assert.equal(calls.length,1);const body=calls[0];assert.equal(body.manifest.kind,"plugins");assert.equal(body.manifest.native,"runtime/plugin.c");assert.equal(body.expected_revision,"");const entry=JSON.parse(body.edits.find(e=>e.path==="plugin.json").text);assert.deepEqual(Object.keys(entry).sort(),["entry","exclusive","overrides"]);assert.equal(entry.entry.native,"runtime/plugin.c");assert(body.edits.find(e=>e.path==="runtime/plugin.c").text.includes("MDO_PLUGIN_ID"));assert(!Object.hasOwn(body,"files"));assert(!JSON.stringify(body).includes("base64"));assert(!document.body.querySelector("dialog"));
});
test("plugin uninstall reviews the logical dependency plan before applying",async t=>{
 const restore=environment();const row={kind:"plugins",id:"plugin:qa.published",directory_id:"PHYSICAL-XID",source:"store",name:"Published",revision:"a".repeat(64),enabled:false,state:"disabled",manifest:{id:"plugin:qa.published",name:"Published",entry:{frontend:"main.js"}}},calls=[];
 const plan={id:"b".repeat(32),revision:"c".repeat(64),ready:true,items:[{...row,operation:"remove"}]};
 t.mock.method(globalThis,"fetch",async(url,o)=>{if(url==="/api/v1/application/call")return Response.json({ok:true,data:{items:[row]}});if(o.method==="POST"){const body=JSON.parse(o.body);calls.push({url,body});return Response.json({ok:true,data:body.action==="dependency_plan"?plan:{removed:true,runtime_ready:true}});}return Response.json({ok:true,data:url.includes("/extensions/")?row:{"plugins:PHYSICAL-XID":row}});});
 const {createPluginWorkbench}=await import("../app/web/js/features/settings/plugin-workbench.js");const root=document.createElement("div"),panel=createPluginWorkbench(root);t.after(()=>{panel.destroy();restore();});panel.selectSection("extensions");await flush();assert(!button(root,"编辑"));assert(!button(root,"分享到创作中心"));button(root,"卸载").click();await flush();assert.equal(calls[0].url,"/api/v1/ecosystem");assert.deepEqual(calls[0].body,{action:"dependency_plan",operation:"remove",ref:row.id,cascade:true});
 const view=document.body.querySelector("dialog"),apply=button(view,"应用此计划");assert(view.textContent.includes(row.id));assert(!view.querySelector("input"));assert(!apply.disabled);apply.click();await flush();assert.deepEqual(calls[1].body,{action:"apply_plan",plan_id:plan.id,revision:plan.revision,trust_code:true});assert(!document.body.querySelector("dialog"));
});

test("plugin list, search and details use directory metadata, independently of the recipe",async t=>{
 const restore=environment();
 const row={kind:"plugins",id:"plugin:mdo.browser-use",directory_id:"WORK-XID",source:"store",name:"Browser Use",description:"Browser automation and shared services",author:"Browser Author",license:"MIT",version:"0.1.2",revision:"a".repeat(64),enabled:true,state:"enabled",manifest:{entry:{native:"runtime/plugin.c"},prompts:[]}};
 t.mock.method(globalThis,"fetch",async(url,o)=>{
  if(url==="/api/v1/application/call")return Response.json({ok:true,data:JSON.parse(o.body).method==="plugin.get"?{...row,description:"Full directory description",manifest:{...row.manifest,name:"Ignored recipe name",description:"Ignored recipe description",version:"0.0.0",author:"Ignored recipe author"}}:{items:[row]}});
  return Response.json({ok:true,data:url.includes("/extensions/")?row:{}});
 });
 const {createPluginWorkbench}=await import("../app/web/js/features/settings/plugin-workbench.js");
 const root=document.createElement("div");document.body.append(root);const panel=createPluginWorkbench(root);
 t.after(()=>{panel.destroy();restore();});panel.selectSection("extensions");await flush();
 assert.equal(root.querySelector(".eb-name").textContent,"Browser Use");
 assert.equal(root.querySelector(".eb-description").textContent,row.description);
 assert(root.querySelector(".eb-meta").textContent.includes("0.1.2"));
 assert(root.querySelector(".eb-meta").textContent.includes("Browser Author"));
 const search=root.querySelector("form"),input=search.querySelector("input");
 for(const query of ["Browser Use","shared services","Browser Author"]){input.value=query;search.dispatchEvent(new Event("submit",{cancelable:true}));await flush();assert(root.querySelector(".eb-name"),query);}
 root.querySelector(".eb-open").click();await flush();
 const detail=document.body.querySelector("dialog");assert(detail);
 assert(detail.textContent.includes("Full directory description"));
 assert(detail.textContent.includes("Browser Author"));assert(detail.textContent.includes("runtime/plugin.c"));
 assert(!detail.textContent.includes("Ignored recipe"));assert(!detail.textContent.includes("0.0.0"));
});

test("an optional empty plugin description does not imply an obsolete format",async t=>{
 const restore=environment(),row={kind:"plugins",id:"plugin:qa.no-description",source:"manual",name:"No description",revision:"b".repeat(64),enabled:false,state:"disabled",manifest:{entry:{frontend:"runtime/main.js"}}};
 t.mock.method(globalThis,"fetch",async(url,o)=>Response.json({ok:true,data:url==="/api/v1/application/call"?(JSON.parse(o.body).method==="plugin.get"?row:{items:[row]}):url.includes("/extensions/")?row:{}}));
 const {createPluginWorkbench}=await import("../app/web/js/features/settings/plugin-workbench.js");
 const root=document.createElement("div");document.body.append(root);const panel=createPluginWorkbench(root);
 t.after(()=>{panel.destroy();restore();});panel.selectSection("extensions");await flush();
 assert.equal(root.querySelector(".eb-description").textContent,"暂无说明");
 root.querySelector(".eb-open").click();await flush();
 const detail=document.body.querySelector("dialog");assert(detail.textContent.includes("暂无说明"));assert(detail.textContent.includes("runtime/main.js"));
 assert(!root.textContent.includes("旧格式插件"));assert(!detail.textContent.includes("旧格式插件"));
});
