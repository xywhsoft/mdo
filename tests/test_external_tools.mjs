import test from "node:test";
import assert from "node:assert/strict";
import { createExternalToolsPanel, externalToolAction } from "../app/web/js/features/notifications/external-tools.js";
import { buildNotices } from "../app/web/js/features/notifications/model.js";

test("store identities keep same-name manual tools independent and update disabled tools",()=>{
  const row={tool_id:"community.abc",version:"2.0.0"};
  assert.equal(externalToolAction(row,[{id:"custom.abc",name:"same",version:"2.0.0",present:true}]),"install");
  assert.equal(externalToolAction(row,[{id:row.tool_id,version:"1.0.0",present:true,enabled:false}]),"update");
  assert.equal(externalToolAction(row,[{id:row.tool_id,version:row.version,present:true,enabled:false}]),null);
  assert.equal(externalToolAction(row,[{id:row.tool_id,version:row.version,present:false}]),"install");
});

test("new registry does not recreate old missing-tool recommendation notifications",()=>{
  for(const guide of ["new","accepted","dismissed","deployed"]){
    const rows=buildNotices({distribution:{cli:{guide,items:[]},tools:[],toolpacks:[{id:"core",revision:10}],installed:{core:"old",core_revision:1}},locale:"en-US",copy:{}});
    assert(!rows.some(n=>n.id==="tools-guide"||n.id==="tools-update"));
  }
});

class Element{
 constructor(tag){this.tag=tag;this.children=[];this.dataset={};this.attributes={};}
 append(...v){this.children.push(...v);}
 replaceChildren(...v){this.children=[...v];}
 setAttribute(k,v){this.attributes[k]=v;}
 remove(){this.removed=true;}
 addEventListener(){}
}
const all=e=>[e,...e.children.flatMap(all)];
test("external manager has exactly two tabs, ordinary rows and a durable first-use prompt",()=>{
 const saved=globalThis.document;globalThis.document={body:new Element("body"),createElement:tag=>new Element(tag)};
 try{
  let main=true;const panel=createExternalToolsPanel({refresh:async()=>{},openFull:async()=>{},openTools:()=>{},signedIn:()=>false,isMain:()=>main}),body=new Element("div");
  const state={writable:true,guide:"new",platform:"windows-x86_64",edition:"desktop",items:[{id:"custom.one",name:"<script>literal</script>",path:"C:/cli/tool.exe",prompt:"use --example",version:"1.0.0",license:"MIT",enabled:true,present:true}],tasks:[],store:{items:[]}};
  panel.render(body,state);
  assert.equal(all(body).filter(n=>n.attributes.role==="tab").length,2);
  assert(all(body).some(n=>n.textContent==="<script>literal</script>"));
  const guide=document.body.children[0];assert.equal(guide.hidden,false);main=false;panel.render(body,state);assert.equal(guide.hidden,true);main=true;panel.render(body,state);assert.equal(guide.hidden,false);
  for(const decision of ["accepted","dismissed","deployed"]){panel.render(body,{...state,guide:decision,items:[]});assert.equal(guide.hidden,true);}
  panel.render(body,{...state,guide:"accepted",platform:"android-arm64-v8a",edition:"full"});
  assert(all(body).some(n=>n.textContent==="Install full edition"));
  assert(!all(body).some(n=>n.textContent==="Share to store"));
  panel.destroy();assert(guide.removed);
 }finally{globalThis.document=saved;}
});

test("Android APK failures remain visible and do not escape the tool manager",async()=>{
 const saved=globalThis.document;globalThis.document={body:new Element("body"),createElement:tag=>new Element(tag)};
 try{
  const panel=createExternalToolsPanel({refresh:async()=>{},openTools:()=>{},signedIn:()=>false,openFull:async()=>{throw Error("APK service unavailable");}}),body=new Element("div");
  panel.render(body,{writable:true,guide:"accepted",platform:"android-arm64-v8a",edition:"lite",items:[],tasks:[],store:{items:[]}});
  await all(body).find(n=>n.textContent==="Install full edition").onclick();
  assert(all(body).some(n=>n.textContent==="APK service unavailable"));panel.destroy();
 }finally{globalThis.document=saved;}
});
