import { wireI18n } from './fixtures/i18n.mjs';
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { test } from "node:test";
import { ensureNativeBrowser } from "../../app/web/js/features/workbench/native-view.js";
import { browserScopeKey, browserViewKey } from "../../app/web/js/features/workbench/browser-ownership.js";

const original = await readFile(new URL("../../app/web/js/features/workbench/popup-manager.js",import.meta.url),"utf8");
const source = original.replace('import { safeBrowserUrl } from "./model.js";',
  'const safeBrowserUrl = value => {const u=new URL(value);if(!["http:","https:"].includes(u.protocol)||u.username||u.password)throw Error("Unsafe URL");return u.href;};')
  .replace('import { ensureNativeBrowser } from "./native-view.js";',`const ensureNativeBrowser=${ensureNativeBrowser.toString()};`)
  .replace('import { browserScopeKey as scopeKey, browserViewKey as viewKey } from "./browser-ownership.js";',
    `const browserScopeKey=${browserScopeKey.toString()};const scopeKey=browserScopeKey,viewKey=${browserViewKey.toString()};`);
const { createBrowserPopups } = await import(`data:text/javascript;base64,${Buffer.from(source).toString("base64")}`);
const ticket = "popup-{00000000-0000-0000-0000-000000000001}";
const context = { device:"original-device",project:"original-project",session:"original-session",run:"" };
const descriptor = { id:ticket,opener:"source",client:"display" };

test("ephemeral native tickets require the original display client and stop on a device change",async()=>{
  const calls=[];let live=true;
  const options={browser:{id:"child",opener:"source",client:"display",runtime:{popup_request:descriptor}},clientId:"display",
    bridge:{async call(command){calls.push(command);live=false;return{created:true};}},
    sdk:{call(){throw Error("A changed device must not settle its old admission");}},alive:()=>live};
  await assert.rejects(ensureNativeBrowser(options),{name:"AbortError"});assert.deepEqual(calls,["create"]);
  live=true;calls.length=0;
  await assert.rejects(ensureNativeBrowser({...options,clientId:"another-display"}),/之前的窗口/);
  assert.equal(calls.length,0);
});

function fixture({settled=false,queued=[],request={}}={}){
  const calls=[],opened=[],closed=[],errors=[];
  let performOpen=async(child,sdk,reveal)=>{opened.push({child,reveal});};
  const scope={...context};
  let pending=[{id:ticket,url:"https://example.com/popup",user_initiated:true,...request}],closeRequested=false;
  const row={id:"child",owner:"original-extension",opener:"source",client:"display",context:scope,
    ...(settled?{}:{runtime:{popup_request:descriptor}})};
  const states=new Map([["child",{opener:"source"}]]);
  const api={bind(bound){assert.deepEqual(bound,{...context,extension:"original-extension"});return{async call(method,params){
    calls.push({method,params});if(method==="browser.popup.admit")return row;
    if(method==="browser.popup.settle")return{settled:true};if(method==="browser.close")return{};
    if(method==="client.poll"){assert.equal(params.client,"display");return queued;}
    throw Error("Unexpected SDK call "+method);
  }}}};
  const views=new Map([["source",{row:{id:"source-panel",owner:"original-extension",client:"display",context:scope},
    async execute(){return{popup_capability:true,closed_by_page:closeRequested};}}]]);
  const bridge={async call(method,params){calls.push({method,params});
    if(method==="popups"&&params.action==="list")return{popups:pending};
    if(method==="popups"&&params.action==="dismiss"){pending=[];return{dismissed:true};}
    if(method==="state")return states.get(params.browser);
    if(method==="create"){assert.equal(params.popup,ticket);assert.equal(params.opener,"source");pending=[];return{created:true};}
    if(method==="close")return{};throw Error("Unexpected native call "+method);
  }};
  const manager=createBrowserPopups({api,bridge,views,clientId:"display",alive:()=>true,
    open:(...args)=>performOpen(...args),async closePanels(id){closed.push(id);},onError:error=>errors.push(error)});
  return{manager,calls,opened,closed,errors,views,bridge,api,setOpen(value){performOpen=value;},setClose(){closeRequested=true;},clear(){pending=[];},row};
}

test('automatic popup close waits for its final UI acknowledgement',async()=>{
  const queued=[{action:'workbench.browser',params:{id:'source',command:'download',action:'release'}}];
  const f=fixture({queued});f.setClose();await f.manager.reconcile('source');
  assert(f.views.has('source'));assert.equal(f.closed.length,0);assert(!f.calls.some(call=>call.method==='browser.close'));
  queued[0].params.id='another-browser';await f.manager.reconcile('source');
  assert(!f.views.has('source'));assert.deepEqual(f.closed,['source']);
  f.manager.dispose();
});

function replaceSource(f){
  const next={row:{...f.views.get('source').row,context:{...context,session:'replacement-task'}},execute(){throw Error('Replacement must stay untouched');}};
  f.views.set('source',next);return next;
}
function sdkAfter(f,method,after){
  const bind=f.api.bind;f.api.bind=context=>{const sdk=bind(context);return{...sdk,async call(name,params){const result=await sdk.call(name,params);if(name===method)after();return result;}};};
}
for(const stage of ['state','list','admit','capabilities','create','settle'])test(`popup ownership loss during ${stage} stops the original chain`,async()=>{
  const f=fixture();let replacement;
  const lose=()=>{replacement=replaceSource(f);};
  if(stage==='state'){const view=f.views.get('source'),read=view.execute;view.execute=async()=>{const value=await read();lose();return value;};}
  if(stage==='admit'||stage==='settle')sdkAfter(f,stage==='admit'?'browser.popup.admit':'browser.popup.settle',lose);
  const native=f.bridge.call;f.bridge.call=async(method,params)=>{
    if(stage==='capabilities'&&method==='capabilities'){lose();return{initial_download_interception:true};}
    const result=await native(method,params);if(stage==='list'&&method==='popups'&&params.action==='list'||stage==='create'&&method==='create')lose();return result;
  };
  if(stage==='capabilities')f.row.download_policy='intercept';
  await f.manager.reconcile('source');
  assert.equal(f.views.get('source'),replacement);assert.equal(f.opened.length,0);assert.equal(f.errors.length,0);
  assert.equal(f.views.has('child'),false);assert.equal(f.calls.filter(row=>row.method==='browser.close'||row.method==='close').length,0);
  if(['state','list','admit','capabilities'].includes(stage))assert(!f.calls.some(row=>row.method==='create'));
  if(stage==='create')assert(!f.calls.some(row=>row.method==='browser.popup.settle'));
  f.manager.dispose();
});

for(const stage of ['client.poll','browser.close','close'])test(`automatic page close preserves a replacement after ${stage}`,async()=>{
  const f=fixture();f.setClose();let replacement;
  const lose=()=>{replacement=replaceSource(f);};
  if(stage==='close'){const native=f.bridge.call;f.bridge.call=async(method,params)=>{const result=await native(method,params);if(method==='close')lose();return result;};}
  else sdkAfter(f,stage,lose);
  await f.manager.reconcile('source');
  assert.equal(f.views.get('source'),replacement);assert.equal(f.closed.length,0);assert.equal(f.errors.length,0);
  if(stage==='client.poll')assert(!f.calls.some(row=>row.method==='browser.close'||row.method==='close'));
  if(stage==='browser.close')assert(!f.calls.some(row=>row.method==='close'));f.manager.dispose();
});

test('popup scope is captured before an asynchronous native state read',async()=>{
  const f=fixture(),view=f.views.get('source'),read=view.execute;
  view.execute=async()=>{const state=await read();view.row.context.session='another-task';return state;};
  await f.manager.reconcile('source');assert.equal(f.calls.length,0);assert.equal(f.opened.length,0);assert.equal(f.errors.length,0);f.manager.dispose();
});

for(const stage of ['existing','create'])test(`a ${stage} foreign child view is never opened or replaced`,async()=>{
  const f=fixture(),replacement={row:{...f.row,context:{...context,session:'foreign-child'}},execute(){throw Error('Human replacement must stay untouched');}};
  if(stage==='existing')f.views.set('child',replacement);
  else {const native=f.bridge.call;f.bridge.call=async(method,params)=>{const result=await native(method,params);if(method==='create')f.views.set('child',replacement);return result;};}
  await f.manager.reconcile('source');assert.equal(f.views.get('child'),replacement);assert.equal(f.opened.length,0);assert.equal(f.errors.length,0);
  assert(!f.calls.some(row=>row.method==='browser.close'||row.method==='close'||row.method==='browser.popup.settle'));
  if(stage==='existing')assert(!f.calls.some(row=>row.method==='create'));f.manager.dispose();
});

test('popup source metadata refresh does not revoke the same task owner',async()=>{
  const f=fixture(),view=f.views.get('source'),read=view.execute;
  view.execute=async()=>{const state=await read();view.row={...view.row,title:'Refreshed',context:{...view.row.context}};return state;};
  await f.manager.reconcile('source');assert.equal(f.opened.length,1);assert.equal(f.errors.length,0);f.manager.dispose();
});

test('acknowledged child publication has its own guarded lifetime',async()=>{
  const f=fixture();let entered=0;
  f.setOpen(async(child,sdk,reveal,current)=>{
    entered++;assert.equal(typeof current,'function');assert(current());f.views.delete('source');assert(current(),'Independent adopted child followed its closed parent');
    f.views.set(child.id,{row:{...child,context:{...context,session:'replacement'}}});assert(!current(),'Panel publication followed a replacement child');
  });
  await f.manager.reconcile('source');assert.equal(entered,1);assert.equal(f.errors.length,0);f.manager.dispose();
});

for(const field of ['owner','context','opener'])test(`admission cannot change the popup ${field}`,async()=>{
  const f=fixture();f.row[field]=field==='context'?{...context,session:'foreign'}:'foreign';
  await f.manager.reconcile('source');assert(!f.calls.some(row=>row.method==='create'));assert.equal(f.opened.length,0);assert.equal(f.errors.length,1);f.manager.dispose();
});

const panels=await readFile(new URL('../../app/web/js/features/workbench/panel-manager.js',import.meta.url),'utf8');
function panelOpening(){
  const body=panels.slice(panels.indexOf('  async function open('),panels.indexOf('  async function newTab('));
  const entries=new Map(),layout={order:[],active:'',open:false},effects=[];let resolve,requests=0;
  const api={bind(){return{call(){requests++;return new Promise(done=>{resolve=done;});}};}};
  const factory=new Function('api','definitions','entries','layout','clientId','reveal','remember','render','handle',
    `let stopped=false;${body};return{open,stop(){stopped=true;}};`);
  return{...factory(api,new Map(),entries,layout,'display',id=>effects.push(['reveal',id]),()=>effects.push('remember'),()=>effects.push('render'),id=>({id})),
    entries,layout,effects,resolve:()=>resolve({id:'panel'}),requests:()=>requests};
}
for(const cause of ['owner','stop'])test(`actual panel open ignores its late reply after ${cause} loss`,async()=>{
  const f=panelOpening();let current=true;
  const opening=f.open('mdo.browser',{},undefined,undefined,()=>current);assert.equal(f.requests(),1);
  if(cause==='owner')current=false;else f.stop();f.resolve();
  await assert.rejects(opening,cause==='owner'?{name:'AbortError'}:/工作区已退出/);
  assert.equal(f.entries.size,0);assert.deepEqual(f.layout.order,[]);assert.equal(f.effects.length,0);
});

function panelClosing(){
  const body=panels.slice(panels.indexOf('  const close = async'),panels.indexOf('  const reveal ='));
  const original={id:'panel'},entries=new Map([['panel',original]]),effects=[];let resolve,requests=0;
  const factory=new Function('entries','mounts','bound','disposeMount','selectRemaining','remember','render',
    `let stopped=false;${body};return{close,stop(){stopped=true;}};`);
  return{...factory(entries,new Map(),()=>({call(){requests++;return new Promise(done=>{resolve=done;});}}),id=>effects.push(['dispose',id]),()=>effects.push('select'),()=>effects.push('remember'),()=>effects.push('render')),
    entries,original,effects,requests:()=>requests,resolve:()=>resolve({})};
}
for(const cause of ['replacement','owner','stop'])test(`actual panel close preserves UI after ${cause} changes during its reply`,async()=>{
  const f=panelClosing();let current=true;const closing=f.close('panel',()=>current);assert.equal(f.requests(),1);
  let expected=f.original;if(cause==='replacement'){expected={id:'panel',revision:2};f.entries.set('panel',expected);}else if(cause==='owner')current=false;else f.stop();f.resolve();await closing;
  assert.equal(f.entries.get('panel'),expected);assert.equal(f.effects.length,0);
});

test('actual panel lifetime guards admit unchanged owners',async()=>{
  const opened=panelOpening(),opening=opened.open('mdo.browser',{},undefined,undefined,()=>true);opened.resolve();assert.equal((await opening).id,'panel');assert(opened.entries.has('panel'));
  const closed=panelClosing(),closing=closed.close('panel',()=>true);closed.resolve();await closing;assert.equal(closed.entries.size,0);assert.equal(closed.effects.length,4);
});

test('the actual popup panel adapters forward the original lifetime and row identity',async()=>{
  const body=panels.slice(panels.indexOf('  const popupManager = createBrowserPopups('),panels.indexOf('  const nativePopup ='));
  const entries=new Map([['one',{resource:{id:'child'}}],['two',{resource:{id:'child'}}]]),opened=[],closed=[];let current=true;
  const factory=new Function('createBrowserPopups','bridge','api','browserViews','clientId','open','close','entries','toast','isRemoteTarget','targetState',
    `let stopped=false,displayTarget='local';${body};return popupManager;`);
  const adapters=factory(options=>options,{}, {},new Map(),'display',(...args)=>opened.push(args),async(id,guard)=>{assert(guard());closed.push(id);entries.set(id,{resource:{id:'child'}});assert(!guard());current=false;},entries,()=>{},()=>false,()=>({}));
  const sdk={},guard=()=>true;await adapters.open({id:'child'},sdk,'background',guard);
  assert.equal(opened[0][0],'mdo.browser');assert.equal(opened[0][2],sdk);assert.equal(opened[0][4],guard);
  await adapters.closePanels('child',()=>current);assert.deepEqual(closed,['one']);
});

test('an existing panel row can attach the same acknowledged child browser',async()=>{
  const f=fixture(),original={row:{id:'child-panel',resource:{id:'child'},owner:f.row.owner,context:{...context},client:'display'}};
  f.views.set('child',original);await f.manager.reconcile('source');
  assert.equal(f.views.get('child'),original);assert.equal(f.opened.length,1);assert.equal(f.errors.length,0);f.manager.dispose();
});

test("duplicate native notifications adopt one child in the original task",async()=>{
  const f=fixture();await Promise.all([f.manager.reconcile("source"),f.manager.reconcile("source")]);
  assert.equal(f.errors.length,0);assert.equal(f.calls.filter(c=>c.method==="create").length,1);
  assert.equal(f.calls.filter(c=>c.method==="browser.popup.admit").length,1);
  assert.equal(f.opened.length,1);assert.equal(f.opened[0].reveal,"activate");assert.ok(f.views.has("child"));
  f.manager.dispose();
});

test('Android unknown popup URLs use the retained ticket without inventing or loading a URL',async()=>{
  const f=fixture({request:{url:'about:blank',url_known:false,user_initiated:false}});
  await f.manager.reconcile('source');
  assert.equal(f.errors.length,0);assert.equal(f.opened.length,1);assert.equal(f.opened[0].reveal,'background');
  const admission=f.calls.find(row=>row.method==='browser.popup.admit');
  assert.deepEqual(admission.params,{opener:'source',client:'display',popup:ticket,url:'about:blank'});
  const created=f.calls.filter(row=>row.method==='create');assert.equal(created.length,1);
  assert.equal(created[0].params.popup,ticket);assert.equal(created[0].params.opener,'source');
  assert(!f.calls.some(row=>['navigate','reload'].includes(row.method)));f.manager.dispose();
});

test("settled admissions are inspected instead of recreating a native window",async()=>{
  const f=fixture({settled:true});await f.manager.reconcile("source");
  assert.equal(f.errors.length,0);assert.equal(f.calls.filter(c=>c.method==="create").length,0);
  assert.equal(f.opened.length,1);f.manager.dispose();
});

test("uncertain native effects retain their admitted receipt without replacement",async()=>{
  const f=fixture();const original=f.bridge.call;
  f.bridge.call=async(method,params)=>{if(method==="create")throw Error("Native reply lost");return original(method,params);};
  await f.manager.reconcile("source");await f.manager.reconcile("source");
  assert.equal(f.calls.filter(c=>c.method==="browser.popup.admit").length,1);
  assert.equal(f.calls.filter(c=>c.method==="browser.close").length,0);
  assert.equal(f.opened.length,0);assert.equal(f.errors.length,1);f.manager.dispose();
});

test("page close requests remove exactly their scoped browser and panels",async()=>{
  const f=fixture();f.setClose();await f.manager.reconcile("source");
  assert.deepEqual(f.closed,["source"]);assert.ok(!f.views.has("source"));
  assert.deepEqual(f.calls.map(c=>c.method),["client.poll","browser.close","close"]);f.manager.dispose();
});

test("the actual native bridge serializes concurrent creation and rejects a changed popup identity",async()=>{
  const code=await readFile(new URL("../../app/web/js/features/workbench/native-browser.js",import.meta.url),"utf8");
  const { createNativeBrowserBridge }=await import(`data:text/javascript;base64,${Buffer.from(wireI18n(code)).toString("base64")}`);
  const savedWindow=globalThis.window,savedChrome=globalThis.chrome;
  const events=new EventTarget(),sent=[];let receive;
  globalThis.window=events;globalThis.chrome={webview:{addEventListener(name,listener){receive=listener;},removeEventListener(){},postMessage(json){sent.push(JSON.parse(json));}}};
  const bridge=createNativeBrowserBridge();
  try{
    const params={browser:"child",popup:ticket,opener:"source"};
    const first=bridge.call("create",params),second=bridge.call("create",params);
    assert.equal(sent.length,1);
    await assert.rejects(bridge.call("create",{...params,intercept_downloads:true}),/下载拦截策略/);
    assert.equal(sent.length,1,"A conflicting policy must not dispatch another native creation");
    await assert.rejects(bridge.call("create",{...params,opener:"another-source"}),/另一个弹出请求/);
    receive({data:{channel:"mdo-browser",id:sent[0].id,result:{created:true}}});await first;
    assert.equal((await second).created,true);
    assert.equal(sent.length,1,"A concurrent reader must not reset the newly displayed view's visibility or bounds");
    const start=sent.length;
    const cancelled=bridge.call("create",{browser:"replace"}).catch(error=>error);
    const closing=bridge.call("close",{browser:"replace"});
    receive({data:{channel:"mdo-browser",id:sent[start+1].id,result:{}}});await closing;
    const replacement=bridge.call("create",{browser:"replace"});
    assert.equal(sent.length,start+3,"Close did not detach the old in-flight creation promise");
    receive({data:{channel:"mdo-browser",id:sent[start].id,error:"Browser creation was cancelled"}});
    assert.equal((await cancelled).message,"Browser creation was cancelled");
    receive({data:{channel:"mdo-browser",id:sent[start+2].id,result:{created:true}}});
    assert.equal((await replacement).created,true);
    const uploadIndex=sent.length;
    const upload=bridge.call("upload",{browser:"replace",action:"chunk"});
    assert.equal(bridge.busy(),true,"A pending native file chunk was omitted from the bridge busy state");
    receive({data:{channel:"mdo-browser",id:sent[uploadIndex].id,result:{}}});await upload;
    assert.equal(bridge.busy(),false);
  }finally{bridge.dispose();globalThis.window=savedWindow;globalThis.chrome=savedChrome;}
});


test('required initial interception rejects an old host before sending its URL',async()=>{
  const calls=[];
  await assert.rejects(ensureNativeBrowser({browser:{id:'owned',url:'https://example.com/file',download_policy:'intercept'},clientId:'display',
    bridge:{async call(command){calls.push(command);return{};}},sdk:{call(){throw Error('No admission should be settled');}}}),/打开网址前/);
  assert.deepEqual(calls,['capabilities']);
});

test('unsupported Android automation sends no URL and preserves manual browsing',async()=>{
  for(const reason of ['isolated_world_unsupported','android_adapter_incomplete','unknown-private-provider-error']){
    const calls=[];const bridge={async call(command,params){calls.push(command);if(command==='capabilities')return{browser_use:{available:false,reason}};
      assert.equal(params.automation,undefined);return{created:true};}};
    await assert.rejects(ensureNativeBrowser({browser:{id:'android',url:'https://example.com'},bridge,requireAutomation:true,requireDownloads:true}),error=>{
      assert.equal(error.code,'unsupported_capability');assert(!error.message.includes('unknown-private'));return true;
    });assert.deepEqual(calls,['capabilities']);
    await ensureNativeBrowser({browser:{id:'manual',url:'https://example.com'},bridge});assert.deepEqual(calls,['capabilities','create']);
  }
});

test('automation admission retains download acknowledgement on supported existing hosts',async()=>{
  const calls=[];await ensureNativeBrowser({browser:{id:'windows'},requireAutomation:true,requireDownloads:true,bridge:{async call(command,params){
    calls.push(command);if(command==='capabilities')return{initial_download_interception:true};
    assert.equal(params.automation,true);assert.equal(params.intercept_downloads,true);return{created:true,downloads_intercepted:true};
  }}});assert.deepEqual(calls,['capabilities','create']);
});

test('creation acknowledgement and popup settlement preserve the required policy',async()=>{
  const calls=[];
  const browser={id:'child',opener:'source',client:'display',url:'https://example.com/file',download_policy:'intercept',runtime:{popup_request:descriptor}};
  const result=await ensureNativeBrowser({browser,clientId:'display',bridge:{async call(command,params){calls.push(command);
    if(command==='capabilities')return{initial_download_interception:true};assert.equal(params.intercept_downloads,true);return{created:true,downloads_intercepted:true};}},
    sdk:{async call(method){calls.push(method);return{settled:true};}}});
  assert(result.downloads_intercepted);assert.deepEqual(calls,['capabilities','create','browser.popup.settle']);
});

test('missing policy confirmation does not settle or replay a popup',async()=>{
  const calls=[];
  await assert.rejects(ensureNativeBrowser({browser:{id:'child',opener:'source',client:'display',download_policy:'intercept',runtime:{popup_request:descriptor}},clientId:'display',
    bridge:{async call(command){calls.push(command);return command==='capabilities'?{initial_download_interception:true}:{created:true};}},
    sdk:{call(){throw Error('Unconfirmed adoption must not be settled');}}}),/未确认/);
  assert.deepEqual(calls,['capabilities','create']);
});

test('a manual popup never returns control to the Agent while acknowledging creation',async()=>{
  const calls=[];
  const result=await ensureNativeBrowser({browser:{id:'child',download_policy:'intercept'},clientId:'display',bridge:{async call(command){calls.push(command);
    return command==='capabilities'?{initial_download_interception:true}:{created:true,manual:true,downloads_intercepted:false};}},sdk:{call(){throw Error('No popup admission');}}});
  assert.equal(result.manual,true);assert.deepEqual(calls,['capabilities','create']);
});
