import assert from "node:assert/strict";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { TestElement as Node } from "./helpers/extension-dom.mjs";
globalThis.window=new EventTarget();globalThis.document=new Node("document");document.body=new Node("body");const sidebar=new Node("aside");
document.hidden=false;document.createElement=tag=>new Node(tag);document.querySelector=selector=>selector==='.sidebar'?sidebar:null;
const stored=new Map();globalThis.sessionStorage={getItem:key=>stored.get(key)||null,setItem:(key,value)=>stored.set(key,value)};
let plugins=[];const writes=[],calls=[];
globalThis.fetch=async(url,options={})=>{
  const input=options.body?JSON.parse(options.body):null;
  if(input?.method)calls.push(input.method);
  let data={};if(input?.method==="plugin.list")data={items:plugins,recovery_mode:false};if(input?.method==="client.poll")data=[];
  if(input?.method==="storage.write")writes.push({text:input.params.text,extension:input.context.extension});
  return Response.json({ok:true,data},{headers:{"X-Mdo-Write-Token":"a".repeat(32)+"-0"}});
};
const { createApplicationUi }=await import("../app/web/js/features/shell/application-sdk.js");
test("conversation refresh uses the bound displaying host without issuing a target UI RPC",async()=>{
 const original=document.querySelector,host=new Node("section");let route={projectId:"p",sessionId:"first",view:"workspace"},bound;const refreshed=[];
 document.querySelector=selector=>selector==="#sdk-conversation-views"?host:original(selector);
 const navigation={preferredProject:()=>route.projectId,workspace:()=>route,get:()=>route};
 const app=createApplicationUi({navigation,executeActions:async()=>{},refreshSession:async context=>refreshed.push(context)});
 try{
  app.sdk.ui.registerConversationView("qa.refresh",{mount(root,{sdk}){bound=sdk;}});
  for(let i=0;i<10&&!bound;i++)await new Promise(setImmediate);
  assert.ok(bound);route={...route,sessionId:"second"};const before=calls.length;
  await bound.ui.refreshSession();
  assert.equal(refreshed[0].session,"first");assert.equal(refreshed[0].project,"p");
  assert.equal(calls.slice(before).includes("ui.invoke"),false);
 }finally{app.destroy();document.querySelector=original;}
 assert.throws(()=>bound.ui.refreshSession(),/disposed/);
});
test("UI contribution lifecycle closes owned panels and releases menus/keybindings",async()=>{
  const navigation={preferredProject:()=>"default",workspace:()=>({projectId:"default",sessionId:"s"}),get:()=>({projectId:"default",sessionId:"s"})};
  const app=createApplicationUi({navigation,executeActions:async()=>{}});let cleaned=0,menuCalls=0;
  try {
    const removePanel=app.sdk.ui.registerPanel("qa.panel",{render(root,{signal}){assert.equal(signal.aborted,false);return ()=>cleaned++;}});
    await app.sdk.ui.openPanel("qa.panel");assert.equal(document.body.children.length,1);
    removePanel();assert.equal(cleaned,1);assert.equal(document.body.children.length,0);
    const layer=app.sdk.ui.layers.open({title:"Owned window",width:"75%",icon:"code"});await layer.ready;
    assert.equal(document.body.children.length,1);assert.equal(layer.element.querySelector("h2").textContent,"Owned window");
    await assert.rejects(()=>app.sdk.ui.openPanel("qa.panel"),/not registered/);
    app.sdk.ui.registerMenu("qa.menu",{label:"Test",execute:()=>menuCalls++});await sidebar.children[0].onclick();assert.equal(menuCalls,1);
    assert.throws(()=>app.sdk.ui.registerMenu("qa.menu",{execute(){}}),/Duplicate/);
    assert.equal(sidebar.children.length,1,'Failed registration must not attach another button');
    app.sdk.ui.registerKeybinding("qa.key",{code:"KeyJ",mod:true,execute:()=>menuCalls++});
    const event=new Event("keydown");Object.assign(event,{code:"KeyJ",ctrlKey:true});document.dispatchEvent(event);await Promise.resolve();assert.equal(menuCalls,2);
  } finally {app.destroy();}
  assert.equal(sidebar.children.length,0);
  assert.equal(document.body.children.length,0,"Application disposal closes public SDK windows");
  assert.throws(()=>app.sdk.ui.registerPanel("qa.late",{}),/disposed/);
  assert.throws(()=>app.sdk.ui.layers.open({title:"late"}),/disposed/);
});
test("startup recovery gate delays registration and plugin activation until writable",async()=>{
 const frontend='web/'+fileURLToPath(new URL('./fixtures/application-sdk-plugin.mjs',import.meta.url)).replaceAll('\\','/').replace(/^\/+/, '');
 const navigation={preferredProject:()=>"default",workspace:()=>({projectId:"default",sessionId:"s"}),get:()=>({projectId:"default",sessionId:"s"})};
 let ready=false;plugins=[{id:"qa-scope",enabled:true,frontend,revision:"gate"}];calls.length=0;
 globalThis.applicationSdkFixture={};const app=createApplicationUi({navigation,executeActions:async()=>{},canCall:()=>ready});
 try{
  await new Promise(resolve=>setTimeout(resolve,20));assert.deepEqual(calls,[]);assert.equal(globalThis.applicationSdkFixture.ready,undefined);
  ready=true;
  for(let i=0;i<400&&!globalThis.applicationSdkFixture.ready;i++)await new Promise(resolve=>setTimeout(resolve,5));
  assert.equal(globalThis.applicationSdkFixture.ready,true);assert.equal(calls.filter(x=>x==='plugin.list').length,1);
  await new Promise(resolve=>setTimeout(resolve,1600));assert.equal(calls.filter(x=>x==='plugin.list').length,1,'Do not activate twice on subsequent polls');
 }finally{app.destroy();plugins=[];delete globalThis.applicationSdkFixture;}
});
test("plugin callbacks retain the owning SDK and failed activation rolls back contributions",async()=>{
 const frontend='web/'+fileURLToPath(new URL('./fixtures/application-sdk-plugin.mjs',import.meta.url)).replaceAll('\\','/').replace(/^\/+/, '');
 const navigation={preferredProject:()=>"default",workspace:()=>({projectId:"default",sessionId:"s"}),get:()=>({projectId:"default",sessionId:"s"})};
 plugins=[{id:"qa-scope",enabled:true,frontend,revision:"scope"}];writes.length=0;
 globalThis.applicationSdkFixture={};let app=createApplicationUi({navigation,executeActions:async()=>{}});
 try{
  for(let i=0;i<100&&!globalThis.applicationSdkFixture.ready;i++)await new Promise(resolve=>setTimeout(resolve,5));
  assert.equal(globalThis.applicationSdkFixture.ready,true);
  const plugin=globalThis.applicationSdkFixture.sdk;
  await plugin.ui.openPanel('qa.fixture.panel');
  await sidebar.children[0].onclick();
  await plugin.ui.render('qa.fixture.renderer',document.body,{});
  const event=new Event('keydown');Object.assign(event,{code:'KeyK',ctrlKey:true});document.dispatchEvent(event);await new Promise(resolve=>setTimeout(resolve,0));
  assert.deepEqual(writes.map(row=>row.text),['activate','panel','menu','renderer','keybinding']);
  assert.ok(writes.every(row=>row.extension==='plugin.qa-scope'),writes);
 }finally{app.destroy();}
 assert.equal(sidebar.children.length,0);assert.equal(document.body.children.length,0);
 assert.throws(()=>globalThis.applicationSdkFixture.sdk.ui.registerAction('late',()=>{}),/disposed/);
 const originalError=console.error;console.error=()=>{};
 globalThis.applicationSdkFixture={fail:true};app=createApplicationUi({navigation,executeActions:async()=>{}});
 try{
  for(let i=0;i<100&&!globalThis.applicationSdkFixture.attempted;i++)await new Promise(resolve=>setTimeout(resolve,5));
  await new Promise(resolve=>setTimeout(resolve,0));
  assert.equal(globalThis.applicationSdkFixture.attempted,true);assert.equal(sidebar.children.length,0);
  await assert.rejects(()=>app.sdk.ui.openPanel('qa.fixture.panel'),/not registered/);
 }finally{app.destroy();console.error=originalError;plugins=[];delete globalThis.applicationSdkFixture;}
});

test("plugin activation can await its own UI receipt without blocking the window queue",async()=>{
 const frontend='web/'+fileURLToPath(new URL('./fixtures/application-sdk-plugin.mjs',import.meta.url)).replaceAll('\\','/').replace(/^\/+/, '');
 const navigation={preferredProject:()=>"default",workspace:()=>({projectId:"default",sessionId:"s"}),get:()=>({projectId:"default",sessionId:"s"})};
 const originalFetch=globalThis.fetch;let action=null,state='pending',executed=0;
 globalThis.fetch=async(url,options={})=>{
  const input=options.body?JSON.parse(options.body):null;let data;
  if(input?.method==='ui.invoke')data=action={id:'activation-ui',action:input.params.action,params:{},context:{project:'default',session:'s'}};
  else if(input?.method==='client.poll')data=action&&state==='pending'?[action]:[];
  else if(input?.method==='ui.result')data={state};
  else if(input?.method==='client.ack'){state='succeeded';data={state};}
  else return originalFetch(url,options);
  return Response.json({ok:true,data});
 };
 plugins=[{id:'qa-scope',enabled:true,frontend,revision:'receipt'}];
 globalThis.applicationSdkFixture={waitForUi:async sdk=>{
  sdk.ui.registerAction('qa.activation',()=>{executed++;});
  await sdk.call('ui.invoke',{client:sdk.clientId,action:'qa.activation'});
  for(let i=0;i<600;i++){if((await sdk.call('ui.result',{id:'activation-ui'})).state==='succeeded')return;await new Promise(resolve=>setTimeout(resolve,5));}
  throw Error('Plugin activation prevented UI receipt delivery');
 }};
 const app=createApplicationUi({navigation,executeActions:async()=>{}});
 try{
  for(let i=0;i<700&&!applicationSdkFixture.ready;i++)await new Promise(resolve=>setTimeout(resolve,5));
  assert.equal(applicationSdkFixture.ready,true);assert.equal(executed,1);
 }finally{app.destroy();plugins=[];globalThis.fetch=originalFetch;delete globalThis.applicationSdkFixture;}
});
