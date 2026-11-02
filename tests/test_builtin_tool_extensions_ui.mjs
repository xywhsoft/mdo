import test from "node:test";
import assert from "node:assert/strict";
import { testDocument, TestElement, descendants } from "./helpers/extension-dom.mjs";
import { createExtensionPanels } from "../app/web/js/features/settings/extension-panels.js";

const flush = async () => { for (let i=0;i<15;i++) await new Promise(r=>setImmediate(r)); };
test("bundled read and todo use the same list, trust window and activation endpoint",async t=>{
 const old={document:globalThis.document,window:globalThis.window,fetch:globalThis.fetch};
 const roots=new Map(["agents","subagents","skills","tools","commands","knowledge","mcp"].map(k=>[k,new TestElement("div")]));
 const doc=testDocument();doc.querySelector=s=>roots.get(s.match(/^\[data-extension-manager="([^"]+)"\]$/)?.[1])||null;
 globalThis.document=doc;globalThis.window=new EventTarget();
 const item=name=>({kind:"tools",id:"tool:"+name,directory_id:name,alias:name,name,description:"Fixture description",path:"tools/"+name,source:"builtin",author:"墨斗",enabled:false,valid:true,external:true,editable:false,shareable:false});
 const items=[item("read"),item("todo")],calls=[];
 globalThis.fetch=async(url,o={})=>{
  const body=o.body?JSON.parse(o.body):null;calls.push({url,body});let data;
  if(body?.action==="dependency_plan")data={id:"a".repeat(32),revision:"b".repeat(64),ready:true,items:[{id:body.ref,kind:"tools",name:body.ref.slice(5),operation:"flags",desired_enabled:true}]};
  else if(body?.action==="apply_plan")data={runtime_ready:true};
  else data={items,total:items.length};
  return Response.json({ok:true,data});
 };
 const panels=createExtensionPanels();t.after(()=>{panels.destroy();Object.assign(globalThis,old);});
 panels.selectSection("tools");await flush();const root=roots.get("tools");
 assert.equal(root.querySelectorAll(".eb-row").length,2);
 assert(!calls.some(c=>c.url==="/api/v1/tools"),"the list must not add a second native-tool catalog");
 for(const name of ["read","todo"]){
  const row=root.querySelectorAll(".eb-row").find(r=>r.dataset.key==="tool:"+name);
  assert(row.textContent.includes("内置")&&!row.textContent.includes("本机添加"));
  const toggle=row.querySelector('.eb-switch');assert(toggle&&!toggle.disabled);toggle.click();await flush();
  const dialog=document.body.querySelector("dialog");assert(dialog&&dialog.textContent.includes(name));
  const apply=descendants(dialog).find(n=>n.tagName==="BUTTON"&&n.textContent==="应用此计划"),trust=dialog.querySelector("input");
  apply.click();await flush();assert(dialog.textContent.includes("请先勾选信任确认"));
  const count=calls.filter(c=>c.body?.action==="apply_plan").length;assert.equal(count,items.indexOf(items.find(i=>i.name===name)));
  trust.checked=true;trust.dispatchEvent(new Event("change"));apply.click();await flush();
 }
 assert.equal(calls.filter(c=>c.body?.action==="apply_plan").length,2);
});
