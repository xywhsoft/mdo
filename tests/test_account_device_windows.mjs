import assert from "node:assert/strict";
import test from "node:test";
import { readFileSync } from "node:fs";
import { TestElement, testDocument } from "./helpers/extension-dom.mjs";
import { loadLocale } from "../app/web/js/i18n.js";

const originalFetch=globalThis.fetch;
globalThis.fetch=async path=>Response.json(JSON.parse(readFileSync(new URL(`../app/web${path}`,import.meta.url),"utf8")));
await loadLocale("en-US");globalThis.fetch=originalFetch;
const flush=()=>new Promise(resolve=>setImmediate(resolve));
const signedOut=()=>({state:"signed_out",persistence_available:true});
const signedIn=id=>({state:"signed_in",busy:false,persistence_available:true,profile:{id,username:`user-${id}`,nickname:`User ${id}`}});
let serial=0;
async function sourceModule(path,replacements){
 const url=new URL(path,import.meta.url);let source=readFileSync(url,"utf8");
 for(const [from,to] of replacements)source=source.replace(from,to);
 source=source.replace(/from "(\.[^"]+)"/g,(_,relative)=>`from ${JSON.stringify(new URL(relative,url).href)}`);
 return import("data:text/javascript;base64,"+Buffer.from(source+`\n// instance ${++serial}`).toString("base64"));
}
async function setup(t){
 const previous={document:globalThis.document,window:globalThis.window};
 const doc=Object.assign(new EventTarget(),testDocument()),win=new EventTarget();globalThis.document=doc;globalThis.window=win;
 const ids=new Map(),add=(id,tag="div",parent=doc.body)=>{const n=new TestElement(tag);n.id=id;ids.set(id,n);parent.append(n);return n;};
 const identity=add("identity");identity.className="sidebar-identity-row";
 const sidebar=add("open-account","button",identity),avatar=new TestElement("span"),name=new TestElement("span");
 avatar.setAttribute("data-account-avatar","");name.setAttribute("data-account-name","");sidebar.append(avatar,name);
 const devices=add("open-devices","button",identity);devices.append(new TestElement("span"));
 const panel=add("account-panel"),body=new TestElement("div");body.setAttribute("data-account-body","");panel.append(body);
 add("account-search-notice");add("settings-target","button");add("update-target","button");add("mobile-target-label","span");
 const banner=add("target-connection-notice");banner.append(new TestElement("p"));
 for(const key of ["local","retry","choose"]){const button=new TestElement("button");button.setAttribute(`data-target-${key}`,"");banner.append(button);}
 doc.querySelector=selector=>selector.startsWith("#")?ids.get(selector.slice(1))||doc.body.querySelector(selector):doc.body.querySelector(selector);
 const calls=[],listeners=new Set(),navigations=[];let target={selected:null,connected:true};
 const model={local:signedOut(),target:signedOut(),listing:{device_id:"this",listing:{devices:[{id:"peer",name:"Other desktop",platform:"windows",online:true,allow_remote:true,viewers:0}]}},failLogin:false,job:null};
 const notify=next=>{target=next;for(const fn of listeners)fn();};
 const request=async(channel,path,body)=>{
  calls.push({channel,path,body:body&&{...body}});
  if(path==="/account/login"){
   if(model.failLogin)throw Error("Wrong password");
   model[channel]=signedIn(channel==="local"?7:9);return model[channel];
  }
  if(path==="/account/cancel"||path==="/account/callback")return model[channel];
  if(path==="/account"||path==="/account/refresh")return model[channel];
  if(path==="/connector/state")return model.listing;
  throw Error(`Unexpected ${channel} ${path}`);
 };
 const stub={api:{get:async path=>({data:await request("target",path)}),post:async(path,body)=>({data:await request("target",path,body)})},
  connectorRequest:(path,body)=>request("local",path,body),connectorJob:async(body,options)=>{calls.push({channel:"connector-job",body});return model.job?model.job(body,options):model.listing;},
  targetState:()=>target,isRemoteTarget:()=>!!target.selected,initializeTarget:async()=>{},
  subscribeTarget:fn=>{listeners.add(fn);fn();return()=>listeners.delete(fn);},requestTargetSwitch:async selected=>{calls.push({channel:"switch",selected});notify({selected,connected:true});}};
 globalThis.__accountWindows=stub;
 const loginModule=await sourceModule("../app/web/js/features/account/login-window.js",[['import { subscribeTarget } from "../../api/target.js";',"const { subscribeTarget } = globalThis.__accountWindows;"]]);
 stub.createLoginWindow=loginModule.createLoginWindow;
 const accountModule=await sourceModule("../app/web/js/features/account/account.js",[
  ['import { api } from "../../api/client.js";',"const { api } = globalThis.__accountWindows;"],
  ['import { isRemoteTarget, targetState } from "../../api/target.js";',"const { isRemoteTarget, targetState } = globalThis.__accountWindows;"],
  ['import { connectorRequest } from "../../api/connector.js";',"const { connectorRequest } = globalThis.__accountWindows;"],
  ['import { createLoginWindow } from "./login-window.js";',"const { createLoginWindow } = globalThis.__accountWindows;"]]);
 const devicesModule=await sourceModule("../app/web/js/features/shell/devices.js",[
  ['import { connectorRequest, connectorJob } from "../../api/connector.js";',"const { connectorRequest, connectorJob } = globalThis.__accountWindows;"],
  ['import { targetState, subscribeTarget, requestTargetSwitch, initializeTarget } from "../../api/target.js";',"const { targetState, subscribeTarget, requestTargetSwitch, initializeTarget } = globalThis.__accountWindows;"],
  ['import { createLoginWindow } from "../account/login-window.js";',"const { createLoginWindow } = globalThis.__accountWindows;"]]);
 const modelChanges=[];
 const account=accountModule.createAccount({navigation:{openSettings:page=>navigations.push(page)},onModelsChange:()=>modelChanges.push(true)}),chooser=devicesModule.createDevices();
 t.after(()=>{account.dispose();chooser.dispose();Object.assign(globalThis,previous);delete globalThis.__accountWindows;});
 await flush();return {doc,ids,model,calls,navigations,account,chooser,stub,notify,modelChanges};
}
function submit(doc){const dialog=doc.body.querySelector("dialog");dialog.querySelector("[name=identifier]").value="fixture-user";const password=dialog.querySelector("[name=password]");password.value="fixture-password";dialog.querySelector("form").dispatchEvent(new Event("submit",{cancelable:true}));assert.equal(password.value,"");return dialog;}

test("account readiness refreshes models even when login state changes without a catalog generation",async t=>{
 const env=await setup(t),before=env.modelChanges.length;
 env.model.target={...signedIn(9),model_generation:7,model_catalog_state:"pending"};
 await env.account.refresh();assert.equal(env.modelChanges.length,before+1);
 env.model.target={...env.model.target,model_catalog_state:"unavailable"};
 await env.account.refresh();assert.equal(env.modelChanges.length,before+2);
 env.model.target={...env.model.target,model_catalog_state:"ready"};
 await env.account.refresh();assert.equal(env.modelChanges.length,before+3);
 await env.account.refresh();assert.equal(env.modelChanges.length,before+3);
});

test("signed-out sidebar opens the shared login without navigation, then signed-in sidebar opens account settings",async t=>{
 const env=await setup(t);env.ids.get("open-account").click();assert.equal(env.doc.body.querySelector("dialog").id,"account-login-dialog");assert.deepEqual(env.navigations,[]);
 submit(env.doc);await flush();await flush();assert(!env.doc.body.querySelector("dialog"));
 assert.equal(env.ids.get("open-account").querySelector("[data-account-name]").textContent,"User 9");
 env.ids.get("open-account").click();assert.deepEqual(env.navigations,["account"]);
 assert.equal(env.calls.filter(call=>call.path==="/account/login").length,1);
 assert.equal(env.calls.find(call=>call.path==="/account/login").channel,"target");
});

test("signed-out device selection uses local controller login, then opens the shared device picker",async t=>{
 const env=await setup(t);env.notify({selected:{id:"remote",name:"Remote",mode:"control"},connected:true});
 const opened=env.chooser.open();await flush();assert.equal(env.doc.body.querySelector("dialog").id,"account-login-dialog");assert.deepEqual(env.navigations,[]);
 submit(env.doc);await opened;await flush();const dialog=env.doc.body.querySelector("dialog");assert.equal(dialog.id,"devices-dialog");assert(dialog.classList.contains("mdo-layer"));assert(dialog.textContent.includes("Other desktop"));
 assert.equal(env.calls.find(call=>call.path==="/account/login").channel,"local","Connector login must not write the selected remote account");
 const connect=dialog.querySelectorAll("button").find(button=>button.textContent==="Connect");connect.click();await flush();
 assert.equal(env.calls.find(call=>call.channel==="switch").selected.owner,"7");
});

test("login failures keep the window and clear passwords; a target change closes it without replay",async t=>{
 const env=await setup(t);env.model.failLogin=true;env.ids.get("open-account").click();const dialog=submit(env.doc);await flush();
 assert(dialog.open);assert(dialog.textContent.includes("Wrong password"));assert.equal(dialog.querySelector("[name=password]").value,"");
 dialog.querySelector("[name=password]").value="unsubmitted-fixture";
 const before=env.calls.filter(call=>call.path==="/account/login").length;
 env.notify({selected:{id:"new",mode:"control"},connected:true});assert(!env.doc.body.querySelector("dialog"));assert.equal(dialog.querySelector("[name=password]").value,"");
 await flush();assert.equal(env.calls.filter(call=>call.path==="/account/login").length,before);
});

test("closing device selection rejects late listing results and stale actions",async t=>{
 const env=await setup(t);env.model.local=signedIn(7);let resolve;
 const gate=new Promise(done=>{resolve=done;});env.model.job=()=>gate;
 await env.chooser.open();await flush();const old=env.doc.body.querySelector("dialog"),stale=old.querySelectorAll("button").find(button=>button.textContent==="Connect");
 old.querySelector(".mdo-layer-controls").lastChild.click();env.model.job=null;env.model.listing={device_id:"this",listing:{devices:[]}};
 await env.chooser.open();await flush();resolve({listing:{devices:[{name:"Late device"}]}});await flush();
 assert(!env.doc.body.querySelector("dialog").textContent.includes("Late device"));stale.click();await flush();assert(!env.calls.some(call=>call.channel==="switch"));
});

test("browser authorization uses the common form and its cancel guard clears the native attempt",async t=>{
 const env=await setup(t);let state=signedOut();const calls=[];
 const login=env.stub.createLoginWindow({scope:()=>"browser-fixture",allowBrowser:()=>true,request:{
  get:async()=>({data:state}),post:async(path,body)=>{calls.push({path,body:{...body}});state=path.endsWith("cancel")?signedOut():{...signedOut(),state:"authorizing",authorization_url:"https://example.invalid/authorize"};return {data:state};}}});
 t.after(()=>login.dispose());const result=login.open();await flush();const dialog=env.doc.body.querySelector("dialog");
 dialog.querySelector(".mdo-layer-footer").firstChild.click();await flush();assert(dialog.querySelector("form").hidden);assert.equal(dialog.querySelector("a").href,"https://example.invalid/authorize");
 dialog.querySelector(".mdo-layer-controls").lastChild.click();await flush();assert.equal(await result,false);assert.equal(calls.at(-1).path,"/account/cancel");assert(!env.doc.body.querySelector("dialog"));
});

test("native authorization callbacks always address the local controller, even while viewing a remote target",async t=>{
 const env=await setup(t);env.notify({selected:{id:"remote",mode:"view"},connected:true});
 const event=new Event("xs-app-link");event.detail="https://ai.xywhsoft.com/app/mdo/callback?code=fixture&state=fixture";window.dispatchEvent(event);await flush();
 const callback=env.calls.find(call=>call.path==="/account/callback");assert.equal(callback.channel,"local");
});
