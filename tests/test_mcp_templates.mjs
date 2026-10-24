import assert from "node:assert/strict";
import {readFileSync} from "node:fs";
import test from "node:test";
import {testDocument,descendants} from "./helpers/extension-dom.mjs";
import {mcpVariantInputs,mcpTemplateDiff,parseMcpTemplate} from "../app/web/js/features/settings/mcp-template.js";
const flush=()=>new Promise(resolve=>setImmediate(resolve));
const recipe={format:"mdo.mcp-template.v1",id:"service",inputs:[{id:"key",label:"Private key",type:"text",secret:true,required:true},{id:"other",label:"Other variant",type:"text",secret:false,required:true}],variants:[{id:"remote",label:"Remote",config:{transport:{type:"streamable-http",endpoint:"https://example.invalid/mcp",headers:[{name:"Authorization",secret_ref:"input:key"}]}}}]};
test("MCP schemas isolate variant parameters, disallow private defaults and describe target changes",()=>{
 assert.deepEqual(mcpVariantInputs(recipe,recipe.variants[0]).map(i=>i.id),["key"]);
 const bad=structuredClone(recipe);bad.inputs[0].default="private";assert.throws(()=>parseMcpTemplate(JSON.stringify(bad)),/默认值/);
 const changed=structuredClone(recipe);changed.variants[0].config.transport.endpoint="https://other.invalid/mcp";
 assert.deepEqual(mcpTemplateDiff(recipe,changed),[{path:"variants.0.config.transport.endpoint",before:"https://example.invalid/mcp",after:"https://other.invalid/mcp"}]);
});
test("MCP connection form requires a fresh preview and closes on device switches without leaking saved credentials",async()=>{
 const previous=globalThis.document;globalThis.document=testDocument();const calls=[];let target="local",notify;
 const receipt={id:7,key:"7",name:"Service",version:"1.0.0",submitter:"author",source:"store"};
 globalThis.__mcpForm={api:{get:async path=>({data:path==="/projects"?{items:[]}:{items:[{valid:true,receipt,template:recipe}]}}),post:async(path,body)=>{calls.push({path,body});return{data:body.action==="preview"?{config:{transport:{endpoint:"https://example.invalid/mcp",headers:[{name:"Authorization",secret_ref:"[private credential]"}]}}}:{id:body.id}};}},targetState:()=>({selected:{id:target}}),subscribeTarget:fn=>{notify=fn;return()=>{};}};
 const url=new URL("../app/web/js/features/settings/mcp-connections.js",import.meta.url);
 const source=readFileSync(url,"utf8").replace('import { api } from "../../api/client.js";',"const {api}=globalThis.__mcpForm;").replace('import { targetState, subscribeTarget } from "../../api/target.js";',"const {targetState,subscribeTarget}=globalThis.__mcpForm;").replace(/from "(\.[^"]+)"/g,(_,path)=>`from ${JSON.stringify(new URL(path,url).href)}`);
 let manager;try{
  const {createMcpConnections}=await import("data:text/javascript;base64,"+Buffer.from(source).toString("base64"));manager=createMcpConnections(()=>{});await manager.open({receipt});
  const nodes=()=>descendants(document.body),click=text=>nodes().find(n=>n.tagName==="BUTTON"&&n.textContent===text).click();
  assert(!nodes().some(n=>n.getAttribute?.("aria-label")==="Other variant"));
  click("保存连接");await flush();assert.equal(calls.length,0);
  const form=nodes().find(n=>n.tagName==="FORM"),identity=nodes().find(n=>n.placeholder==="personal-service"||n.getAttribute?.("placeholder")==="personal-service");identity.value="personal";
  const key=nodes().find(n=>n.getAttribute?.("aria-label")==="Private key");assert.equal(key.type,"password");key.value="private-ui-sentinel";
  click("预览配置");await flush();assert.equal(calls.at(-1).body.action,"preview");assert(!nodes().find(n=>n.tagName==="PRE").textContent.includes(key.value));
  form.dispatchEvent(new Event("input"));click("保存连接");await flush();assert.equal(calls.length,1,"edits invalidate prior preview");
  notify();assert.equal(manager.hasPendingChanges(),true,"same-device notifications keep the draft open");
  click("预览配置");await flush();target="another-device";notify();assert.equal(manager.hasPendingChanges(),false);assert(!document.body.querySelector("dialog")?.open);
 }finally{manager?.destroy();delete globalThis.__mcpForm;globalThis.document=previous;}
});
