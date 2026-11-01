import assert from "node:assert/strict";
import test from "node:test";
import { readFileSync } from "node:fs";
import { testDocument, descendants, TestElement } from "./helpers/extension-dom.mjs";
import { mcpActivationAction, mcpActivationLabel } from "../app/web/js/features/settings/mcp-activation.js";

test("activation states offer only the necessary user action",()=>{
  for(const state of ["ready","starting","disabled","unsupported"])assert.equal(mcpActivationAction({enabled:true,state}),null);
  assert.equal(mcpActivationAction({enabled:true,state:"needs_authorization"}),"authorize");
  assert.equal(mcpActivationAction({enabled:true,state:"needs_configuration"}),"configure");
  assert.equal(mcpActivationAction({enabled:false,state:"failed"}),null);
  assert.notEqual(mcpActivationLabel({enabled:true,state:"starting"}),mcpActivationLabel({enabled:true,state:"needs_configuration"}));
});

test("a default service asks only for business inputs and saves with one action",async t=>{
  const doc=testDocument(),previous={document:globalThis.document,window:globalThis.window};globalThis.document=doc;globalThis.window=new EventTarget();t.after(()=>{globalThis.document=previous.document;globalThis.window=previous.window;});
  let view;globalThis.__mcpAutoLayer=()=>view={body:new TestElement("div"),footer:new TestElement("div"),element:new TestElement("div"),signal:new AbortController().signal,dispose(){},close(){}};
  const url=new URL("../app/web/js/features/settings/mcp-directory-connections.js",import.meta.url);
  let source=readFileSync(url,"utf8").replace('import { openLayer } from "../../components/layers.js";','const openLayer=globalThis.__mcpAutoLayer;');
  source=source.replace(/from "(\.[^"]+)"/g,(_,path)=>`from ${JSON.stringify(new URL(path,url).href)}`);
  const {createMcpConnections}=await import("data:text/javascript;base64,"+Buffer.from(source).toString("base64"));
  const old={id:"auto-fixture",extension_id:"mcp:qa.simple",variant:"default",managed_default:true,approval_digest:"a".repeat(64),revision:"b".repeat(64),name:"Simple service",enabled:true,values:{},secrets_set:{}};
  const template={inputs:[{id:"key",type:"text",secret:true,required:true,label:"API key"},{id:"unused",type:"text",required:true,label:"Unused advanced input"}],variants:[{id:"default",config:{transport:{headers:[{secret_ref:"input:key"}]}}}]};
  const writes=[];let targetChanged=false;t.mock.method(globalThis,"fetch",async(path,options={})=>{
    const headers={"X-Mdo-Write-Token":"qa"};let data={};
    if(path.endsWith("/mcp/templates"))data={items:[{id:old.extension_id,name:old.name,template_valid:true,template}]};
    else if(path.endsWith("/projects"))data={items:[]};
    else if(path.endsWith("/mcp/connections/auto-fixture"))data=old;
    else if(path.endsWith("/mcp/connections")&&options.method==="POST"){const body=JSON.parse(options.body);writes.push(body);data=body.action==="preview"?{approval_digest:old.approval_digest,target_changed:targetChanged,config:{transport:{type:"stdio"}}}:old;}
    else if(path.endsWith("/mcp/connections"))data=[old];
    else if(path.endsWith("/mcp"))data={items:[]};
    return Response.json({ok:true,data},{headers});
  });
  const connections=createMcpConnections();
  try{
    await connections.open({templateId:old.extension_id});
    const controls=descendants(view.body);assert.equal(controls.filter(n=>n.tagName==="INPUT").length,1);
    const key=controls.find(n=>n.tagName==="INPUT");assert.equal(key.getAttribute("type"),"password");key.value="fixture-key";
    assert(!controls.some(n=>n.tagName==="PRE"));assert(!controls.some(n=>n.textContent==="Unused advanced input"));
    const save=descendants(view.footer).find(n=>n.tagName==="BUTTON"&&n.textContent.includes("保存并连接"));assert(save);save.click();
    for(let i=0;i<12;i++)await new Promise(resolve=>setImmediate(resolve));
    assert.deepEqual(writes.map(w=>w.action),["preview","save"]);
    assert.equal(writes[1].id,old.id);assert.equal(writes[1].expected_revision,old.revision);assert.equal(writes[1].secrets.key,"fixture-key");assert.equal(writes[1].enabled,true);
    writes.length=0;targetChanged=true;await connections.open({templateId:old.extension_id});descendants(view.body).find(n=>n.tagName==="INPUT").value="fixture-key";descendants(view.footer).find(n=>n.tagName==="BUTTON"&&n.textContent.includes("保存并连接")).click();
    for(let i=0;i<12;i++)await new Promise(resolve=>setImmediate(resolve));assert.deepEqual(writes.map(w=>w.action),["preview"]);
  }finally{connections.destroy();delete globalThis.__mcpAutoLayer;}
});
