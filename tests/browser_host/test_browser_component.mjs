import { wireI18n } from './fixtures/i18n.mjs';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import test from 'node:test';
import {ensureNativeBrowser,createNativeBrowserExecutor} from '../../app/web/js/features/workbench/native-view.js';
import {browserScopeKey} from '../../app/web/js/features/workbench/browser-ownership.js';
import {browserLocation,browserPhase,safeBrowserUrl} from '../../app/web/js/features/workbench/model.js';

// Run the actual applied UI component with controlled bridge/broker delivery.
// The DOM scaffold supplies layout only; it does not reproduce component logic.
const source=readFileSync(new URL('../../app/web/js/features/workbench/browser-view.js',import.meta.url),'utf8');
const factory=new Function('ensureNativeBrowser','createNativeBrowserExecutor','createBrowserDownloads','createBrowserChoosers','createBrowserDialogs',
  'browserLocation','browserPhase','safeBrowserUrl','browserScopeKey','make','icon','iconButton','popupMenu','toast','isRemoteTarget',
  'document','navigator','ResizeObserver','devicePixelRatio','setTimeout','clearTimeout',
  source.replace(/^import .+;\r?$/gm,'').replace('export async function','async function')+';return mountBrowserView;');
const helperFactory=name=>new Function('document',readFileSync(new URL(`../../app/web/js/features/workbench/${name==='Dialogs'?'dialog':'chooser'}-ui.js`,import.meta.url),'utf8')
  .replace('export function','function')+`;return createBrowser${name};`);
function fixture({realHelpers=false}={}){
  const trace=[],nodes=[],timers=new Map(),controller=new AbortController();let serial=0,current=true,cleanup=true,hook=()=>{},menu,options,onResize;
  let geometry={x:0,y:0,width:800,height:600};
  const responses=new Map();
  const node=(tag='',className='',text='')=>{
    const classes=new Set(className.split(/\s+/).filter(Boolean));
    const value={tag,className,textContent:text,hidden:false,style:{},dataset:{},children:[],value:'',
      classList:{add(...names){for(const name of names)classes.add(name);},contains(name){return classes.has(name);},
        toggle(name,enabled=!classes.has(name)){if(enabled)classes.add(name);else classes.delete(name);return enabled;}},setAttribute(name,value){this[name]=value;},append(...values){this.children.push(...values);},
      replaceChildren(...values){this.children=values;},getBoundingClientRect:()=>({...geometry}),getClientRects:()=>[{}],click(){return this.onclick?.();}};
    nodes.push(value);return value;
  };
  const row={id:'panel',resource:{id:'browser'},owner:'extension',client:'display',context:{device:'device',project:'project',session:'task'}};
  let browser={id:'browser',owner:row.owner,client:'display',context:{...row.context},revision:1,url:'https://example.test/',title:'Original'};
  let state={url:browser.url,title:browser.title,loading:false,visible:true,can_go_back:true,can_go_forward:true};
  const root=node('section'),mount={},sdk={async call(method,params){trace.push({method,params});await hook(method,params);return{...browser,context:{...browser.context}};}},
    bridge={available:()=>true,async call(method,params){trace.push({method,params});const sampled={...state};await hook(method,params);
      if(responses.has(method))return responses.get(method);
      if(method==='takeover'||method==='release')return{manual:method==='takeover'};
      if(method==='state')return sampled;if(method==='snapshot')return{ok:true,value:{text:'Page contents'}};
      if(method==='create')return{created:true};if(method==='capabilities')return{initial_download_interception:true};return{};}};
  const helper=name=>value=>{const element=node('section');return{element,async refresh(){trace.push({method:name});await hook(name);},toggle(){return true;},dispose(){trace.push({method:name+'.dispose'});}};};
  const document={hidden:false,activeElement:null,querySelectorAll:()=>[],createElement:tag=>node(tag)};
  const run=factory(ensureNativeBrowser,createNativeBrowserExecutor,helper('downloads'),realHelpers?helperFactory('Choosers')(document):helper('choosers'),
    realHelpers?helperFactory('Dialogs')(document):helper('dialogs'),browserLocation,browserPhase,safeBrowserUrl,browserScopeKey,node,name=>node('span',name),
    (name,label,action,onError)=>{const button=node('button','',label);button.onclick=()=>Promise.resolve().then(action).catch(onError);return button;},
    (button,items)=>{menu=items;return{dispose(){}};},(...values)=>trace.push({method:'toast',values}),()=>false,document,
    {clipboard:{async writeText(value){trace.push({method:'clipboard',value});}}},class{constructor(callback){onResize=callback;}observe(){}},1,
    callback=>{const id=++serial;timers.set(id,callback);return id;},id=>timers.delete(id));
  const start=()=>run(root,options={row,sdk,bridge,signal:controller.signal,mount,clientId:'display',shown:()=>true,alive:()=>current,
    canHide:()=>cleanup,onBind:view=>{trace.push({method:'bind',id:view.id});return current;},onRetired:()=>trace.push({method:'remount'}),
    updateTitle:async title=>{trace.push({method:'title',title});await hook('title');},openExternal:async url=>trace.push({method:'external',url}),
    onEnd:async id=>trace.push({method:'end',id}),addContext:value=>trace.push({method:'context',value})});
  return{trace,nodes,root,mount,controller,timers,start,row,options:()=>options,retire:()=>{current=false;},cleanup:value=>{cleanup=value;},
    hook:value=>{hook=value;},geometry:value=>{geometry={...geometry,...value};},resize:()=>onResize(),browser:value=>{browser={...browser,...value};},state:value=>{state={...state,...value};},
    response:(method,value)=>{responses.set(method,value);},
    button:label=>nodes.find(n=>n.tag==='button'&&n.textContent===label),menu:label=>menu().find(item=>item.label===label),
    async flush(){for(let i=0;i<30;i++)await Promise.resolve();},async tick(){const entry=[...timers][0];if(entry){timers.delete(entry[0]);entry[1]();}await this.flush();}};
}

for(const geometry of [{width:0,height:0},{width:0},{height:0},{width:NaN}])
test(`initial collapsed or invalid layout never creates a native document: ${JSON.stringify(geometry)}`,async()=>{
  const f=fixture();f.geometry(geometry);const pending=f.start();await f.flush();
  try{
    assert(!f.trace.some(x=>['bind','create','state','bounds'].includes(x.method)));
    f.geometry({width:360,height:640});f.resize();await pending;await f.flush();
    const creations=f.trace.filter(x=>x.method==='create');assert.equal(creations.length,1);
    assert.equal(creations[0].params.bounds.width,360);assert.equal(creations[0].params.bounds.height,640);
    f.resize();await f.mount.visible(true);await f.flush();
    assert.equal(f.trace.filter(x=>x.method==='create').length,1);
  }finally{f.controller.abort();f.mount.dispose();await pending;}
});

for(const finish of ['dispose','abort','retire'])
test(`initial layout wait stops without native effects on ${finish}`,async()=>{
  const f=fixture();f.geometry({width:0,height:0});const pending=f.start();await f.flush();
  if(finish==='dispose')f.mount.dispose();
  else if(finish==='abort')f.controller.abort();
  else{f.retire();await f.mount.visible(false);}
  await pending;f.geometry({width:360,height:640});f.resize();await f.flush();
  assert(!f.trace.some(x=>['bind','create','state','bounds','navigate'].includes(x.method)));
  assert.equal(f.timers.size,0);
});

test('layout collapsing during first native create retains its valid starting viewport',async()=>{
  const f=fixture();f.geometry({width:420,height:640});let complete;
  f.hook(method=>{if(method==='create')return new Promise(resolve=>{complete=resolve;});});
  const pending=f.start();await f.flush();assert(complete);
  f.geometry({width:0,height:0});f.resize();complete();await pending;await f.flush();
  const hidden=f.trace.filter(x=>x.method==='bounds').at(-1).params;
  assert.equal(hidden.visible,false);assert.equal(hidden.bounds.width,420);assert.equal(hidden.bounds.height,640);
  assert.equal(f.trace.filter(x=>x.method==='create').length,1);
});

for(const action of ['hide','dispose'])test(`native ${action} retains the original viewport when its DOM surface collapses`,async()=>{
  const f=fixture();await f.start();await f.flush();
  const original=f.trace.filter(row=>row.method==='bounds'&&row.params.visible).at(-1).params.bounds;
  f.geometry({width:0,height:0});
  if(action==='dispose')f.mount.dispose();else await f.mount.visible(false);
  await f.flush();const hidden=f.trace.filter(row=>row.method==='bounds').at(-1).params;
  assert.equal(hidden.visible,false);assert.deepEqual(hidden.bounds,original);
  if(action==='hide'){
    f.geometry({width:420,height:320});await f.mount.visible(true);await f.flush();
    const restored=f.trace.filter(row=>row.method==='bounds').at(-1).params;
    assert.equal(restored.visible,true);assert.equal(restored.bounds.width,420);assert.equal(restored.bounds.height,320);
  }
});

for(const reason of ['isolated_world_unsupported','android_adapter_incomplete'])
test(`automatically displayed Browser Use tabs retain ${reason} admission before native create`,async()=>{
  const f=fixture();f.browser({automation_required:true,download_policy:'intercept'});
  f.response('capabilities',{initial_download_interception:true,browser_use:{available:false,reason}});
  await assert.rejects(f.start(),{code:'unsupported_capability',capability_reason:reason});await f.flush();
  assert(!f.trace.some(row=>['create','navigate'].includes(row.method)));
});

test('the retained Browser Use creation policy requests native automation on its first displayed navigation',async()=>{
  const f=fixture();f.browser({automation_required:true});
  await f.start();await f.flush();
  assert.equal(f.trace.find(row=>row.method==='create').params.automation,true);
});

test('an older browser record still honors its Browser Use panel automation policy before native creation',async()=>{
  const f=fixture();f.row.params={automation_required:true};
  f.response('capabilities',{initial_download_interception:true,browser_use:{available:false,reason:'android_adapter_incomplete'}});
  await assert.rejects(f.start(),{code:'unsupported_capability',capability_reason:'android_adapter_incomplete'});
  assert(!f.trace.some(row=>['create','navigate'].includes(row.method)));
});

test('initial broker result cannot initialize a retired browser mount',async()=>{
  const f=fixture();f.hook(method=>{if(method==='browser.get')f.retire();});await f.start();await f.flush();
  assert(!f.trace.some(x=>['browser.attach','create','bounds','state','browser.report'].includes(x.method)));
  assert.equal(f.root.children.length,0);assert.equal(f.timers.size,0);
});
for(const changed of [{id:'foreign'},{context:{project:'project',session:'foreign'}},{owner:'other-extension'}])
test(`initial broker rejects foreign identity ${JSON.stringify(changed)}`,async()=>{
  const f=fixture();f.browser(changed);await assert.rejects(f.start(),/归属|identity/i);
  assert(!f.trace.some(x=>['browser.attach','create','bounds','state'].includes(x.method)));
});
test('attach result cannot initialize a retired mount',async()=>{
  const f=fixture();f.browser({client:'previous-display'});f.hook(method=>{if(method==='browser.attach')f.retire();});await f.start();await f.flush();
  assert.equal(f.trace.filter(x=>x.method==='browser.attach').length,1);assert(!f.trace.some(x=>x.method==='create'));
});
test('native capabilities cannot create after UI ownership changes',async()=>{
  const f=fixture();f.browser({download_policy:'intercept'});f.hook(method=>{if(method==='capabilities')f.retire();});await f.start();await f.flush();
  assert(!f.trace.some(x=>x.method==='create'));assert.equal(f.timers.size,0);
});
for(const phase of ['state','dialogs','choosers','downloads','browser.update','title','browser.report'])
test(`poll delivery stops at retired ${phase}`,async()=>{
  const f=fixture();await f.start();await f.flush();f.trace.length=0;f.state({url:'https://changed.test/',title:'Changed'});
  f.hook(method=>{if(method===phase)f.retire();});await f.tick();
  const at=f.trace.findIndex(x=>x.method===phase);assert(at>=0,`phase not exercised: ${phase}`);
  assert.equal(f.trace.slice(at+1).filter(x=>!x.method.endsWith('.dispose')).length,0,JSON.stringify(f.trace));assert.equal(f.timers.size,0);
});
test('foreign conflict refresh cannot redirect the retained dispatcher',async()=>{
  const f=fixture(),view=await f.start();await f.flush();f.state({url:'https://changed.test/',title:'Changed'});f.trace.length=0;
  f.hook(method=>{if(method==='browser.update')throw Object.assign(Error('Conflict'),{status:409});if(method==='browser.get')f.browser({id:'replacement'});});
  await f.tick();assert(!f.trace.some(x=>['title','browser.report'].includes(x.method)));
  assert.equal(f.timers.size,0);assert(!f.trace.some(x=>x.method==='bounds'));
  await view.execute('state',{browser:'replacement'});assert.equal(f.trace.at(-1).params.browser,'browser');
  await f.mount.visible(true);assert(!f.trace.some(x=>x.method==='remount'));
  f.trace.length=0;f.mount.dispose();await f.flush();assert(!f.trace.some(x=>x.method==='bounds'));
});
test('retired snapshot result cannot be added to another task',async()=>{
  const f=fixture();await f.start();await f.flush();f.trace.length=0;f.hook(method=>{if(method==='snapshot')f.retire();});
  await f.menu('引用页面文字到对话').execute().catch(()=>{});assert(!f.trace.some(x=>x.method==='context'));
});
test('end browser stops after broker ownership changes',async()=>{
  const f=fixture();await f.start();await f.flush();f.trace.length=0;f.hook(method=>{if(method==='browser.close')f.retire();});
  await f.menu('结束浏览').execute().catch(()=>{});assert(!f.trace.some(x=>['close','end'].includes(x.method)));
});
test('recovery reads current broker ownership before closing or recreating',async()=>{
  const f=fixture();f.state({error:'Lost process',process_failed:true});await f.start();await f.flush();f.trace.length=0;f.browser({client:'another-display'});
  await f.button('刷新').click();await f.flush();assert(f.trace.some(x=>x.method==='browser.get'));
  assert(!f.trace.some(x=>['close','create','navigate'].includes(x.method)));
});
test('retired layout callbacks neither hide nor control a newer browser',async()=>{
  const f=fixture();await f.start();await f.flush();f.trace.length=0;f.retire();f.cleanup(false);
  await f.mount.visible(true);await f.button('后退').click();await f.button('接管浏览器').click();f.mount.dispose();await f.flush();
  assert(!f.trace.some(x=>['bounds','back','takeover','toast'].includes(x.method)));
});
test('ordinary disposal hides its own surface but preserves the task dispatcher',async()=>{
  const f=fixture(),view=await f.start();await f.flush();f.trace.length=0;f.controller.abort();f.mount.dispose();await f.flush();
  assert.equal(f.trace.filter(x=>x.method==='bounds'&&x.params.visible===false).length,1);
  await view.execute('state');assert.equal(f.trace.at(-1).params.browser,'browser');assert.equal(f.timers.size,0);
});

test('UI binding precedes native create and popup reconciliation stays outside it',async()=>{
  const f=fixture();await f.start();await f.flush();const methods=f.trace.map(x=>x.method);
  assert(methods.indexOf('browser.get')<methods.indexOf('bind'));assert(methods.indexOf('bind')<methods.indexOf('create'));
  assert(methods.indexOf('create')<methods.indexOf('bounds'));
});
test('an initial native navigation survives UI retirement without replay or task cancellation',async()=>{
  const f=fixture();f.browser({download_policy:'intercept'});f.hook(method=>{if(method==='create')f.retire();});
  // Exercise the shared native helper receipt path rather than a plain create result.
  let live=true;const calls=[],native={async call(method){calls.push(method);if(method==='capabilities')return{initial_download_interception:true};
    if(method==='create'){live=false;return{created:true,downloads_intercepted:true,initial_navigation_pending:true,navigation_ref:'nv-original'};}}};
  await assert.rejects(ensureNativeBrowser({bridge:native,sdk:{call(){throw Error('No settlement after retirement');}},browser:{id:'browser',download_policy:'intercept'},
    clientId:'display',alive:()=>live,cancelOnDetach:false}),{name:'AbortError'});
  assert.deepEqual(calls,['capabilities','create']);await f.start();await f.flush();
  assert.equal(f.trace.filter(x=>x.method==='create').length,1);assert(!f.trace.some(x=>['state','bounds','navigation_cancel'].includes(x.method)));assert.equal(f.timers.size,0);
});
test('explicit recovery checks ownership again after native close',async()=>{
  const f=fixture();f.state({error:'Lost process',process_failed:true});await f.start();await f.flush();f.trace.length=0;
  f.hook(method=>{if(method==='close')f.browser({client:'another-display'});});await f.button('刷新').click();await f.flush();
  assert.equal(f.trace.filter(x=>x.method==='browser.get').length,2);assert.equal(f.trace.filter(x=>x.method==='close').length,1);
  assert(!f.trace.some(x=>['create','navigate','bounds'].includes(x.method)));assert.equal(f.timers.size,0);
});
test('valid recovery, navigation, page quotation and end still execute in order',async()=>{
  const f=fixture();f.state({error:'Lost process',process_failed:true});await f.start();await f.flush();f.trace.length=0;
  await f.button('刷新').click();await f.flush();const methods=f.trace.map(x=>x.method);
  assert(methods.indexOf('browser.get')<methods.indexOf('close'));assert(methods.indexOf('close')<methods.indexOf('create'));assert(methods.indexOf('create')<methods.indexOf('navigate'));
  await f.menu('引用页面文字到对话').execute();assert.equal(f.trace.at(-1).method,'context');assert.equal(f.trace.at(-1).value.context.session,'task');
  f.trace.length=0;await f.menu('结束浏览').execute();assert.deepEqual(f.trace.map(x=>x.method),['browser.close','close','end']);
});
test('retired pane asks the workbench to remount only on explicit visible layout',async()=>{
  const f=fixture();await f.start();await f.flush();f.retire();f.trace.length=0;
  await f.mount.visible(false);assert.equal(f.trace.length,0);await f.mount.visible(true);assert.deepEqual(f.trace.map(x=>x.method),['remount']);
});

test('routine loading keeps its live announcement without a visible status row',async()=>{
  const f=fixture();f.state({loading:true});await f.start();await f.flush();
  const status=f.nodes.find(n=>n.className==='wb-browser-status');
  assert.equal(status.role,'status');assert.equal(status.hidden,false);assert.equal(status.textContent,'正在加载…');
  assert(status.classList.contains('sr-only'));assert.equal(f.mount.status.status,'loading');
  f.state({loading:false});await f.tick();
  assert.equal(status.hidden,true);assert.equal(status.classList.contains('sr-only'),false);
});

test('loading cannot hide pending control or a failed control notice',async()=>{
  const f=fixture();f.state({loading:true});await f.start();await f.flush();
  const status=f.nodes.find(n=>n.className==='wb-browser-status');let finish;
  f.response('takeover',new Promise(resolve=>{finish=resolve;}));
  const pending=f.button('接管浏览器').click();await f.flush();
  assert.equal(status.classList.contains('sr-only'),false);assert.match(status.textContent,/正在接管/);
  finish({manual:true});await pending;await f.flush();
  f.response('release',{ok:false,manual:true,error:{code:'network_owner_changed'},network_cleanup_confirmed:false});
  await f.button('接管浏览器').click();await f.flush();
  assert.equal(status.hidden,false);assert.equal(status.classList.contains('sr-only'),false);
  assert.match(status.textContent,/交还控制失败/);assert.match(status.textContent,/清理尚未确认/);
});
test('initialization never advertises actionable UI before native creation completes',async()=>{
  const f=fixture();let finish;f.hook(method=>{if(method==='create')return new Promise(resolve=>{finish=resolve;});});
  const pending=f.start();await f.flush();assert(finish);f.trace.length=0;
  await f.button('后退').click();await f.button('刷新').click();await f.menu('结束浏览').execute().catch(()=>{});await f.flush();
  assert.equal(f.trace.length,0);finish();await pending;await f.flush();assert(f.trace.some(x=>x.method==='state'));
});

for(const cleanup of [true,false,undefined])
test(`failed native return keeps the webpage and its manual control, cleanup=${cleanup}`,async()=>{
  const f=fixture();f.state({manual:true});await f.start();await f.flush();f.trace.length=0;
  f.response('release',{ok:false,manual:true,error:{code:'network_owner_changed'},network_cleanup_confirmed:cleanup,url:'https://foreign.test/'});
  await f.button('接管浏览器').click();await f.flush();
  assert.equal(f.button('接管浏览器')['aria-pressed'],'true');assert.equal(f.root.dataset.browserPhase,'ready');
  assert.equal(f.nodes.find(n=>n.className==='wb-browser-message').hidden,true);
  assert.match(f.nodes.find(n=>n.className==='wb-browser-status').textContent,/状态已变化/);
  assert.equal(f.mount.status.status,'ready');assert.equal(typeof f.mount.status.message,'string');
  const toast=f.trace.find(r=>r.method==='toast');assert.match(toast.values[0],/交还控制失败/);
  if(cleanup===false)assert.match(toast.values[0],/清理尚未确认/);
  if(cleanup===true)assert.match(toast.values[0],/本次网络操作已清理/);
  await f.menu('复制诊断信息').execute();const diagnostic=JSON.parse(f.trace.at(-1).value);
  assert.equal(diagnostic.control.code,'network_owner_changed');assert.equal(diagnostic.control.network_cleanup_confirmed,cleanup);
  await f.tick();assert.equal(f.trace.filter(r=>r.method==='release').length,1);
  assert(!f.trace.some(r=>['navigate','close','create'].includes(r.method)));assert.equal(f.root.dataset.browserPhase,'ready');
});

for(const [code,text] of [['network_retiring',/仍在清理/],['network_rearm_page_loading',/加载结束/],['network_return_blocked',/文件选择/],
  ['network_control_timeout',/接管确认超时/],['network_control_cancelled',/接管已停止/],['network_control_cleanup_unconfirmed',/清理尚未确认/],
  ['cdp_popup_human_owner_invalid',/控制通道已变化/],['cdp_popup_human_resource_cancel_failed',/尚未完成清理/],
  ['popup_takeover_unqualified',/暂不支持接管此弹出页/],['unknown_private_reason',/控制结果未确认/]])
test(`scalar control rejection has a bounded useful message: ${code}`,async()=>{
  const f=fixture();await f.start();await f.flush();f.trace.length=0;
  f.hook(method=>{if(method==='takeover')throw Error(code);});await f.button('接管浏览器').click();await f.flush();
  assert.match(f.trace.find(r=>r.method==='toast').values[0],text);assert.equal(f.root.dataset.browserPhase,'ready');
  assert.equal(f.button('接管浏览器')['aria-pressed'],'false');assert(!f.button('接管浏览器').disabled);
});

for(const value of [null,[],{},true,{manual:'false'},{manual:true},{manual:false,ok:'true'},{manual:false,error:{code:'bad_reply'}}])
test(`malformed return cannot grant UI authority: ${JSON.stringify(value)}`,async()=>{
  const f=fixture();f.state({manual:true});await f.start();await f.flush();f.trace.length=0;f.response('release',value);
  await f.button('接管浏览器').click();await f.flush();assert.equal(f.button('接管浏览器')['aria-pressed'],'true');
  assert.match(f.trace.find(r=>r.method==='toast').values[0],/未返回有效的控制权确认/);assert.equal(f.root.dataset.browserPhase,'ready');
});

test('control waits for its original receipt, serializes toolbar/menu and ignores polled early grants',async()=>{
  const f=fixture();f.state({manual:true});await f.start();await f.flush();f.trace.length=0;let settle;
  f.hook(method=>{if(method==='release')return new Promise(resolve=>{settle=resolve;});});
  const pending=f.button('接管浏览器').click();await f.flush();const button=f.button('接管浏览器');
  assert.equal(button['aria-pressed'],'true');assert(button.disabled);assert.equal(button['aria-busy'],'true');
  assert.match(f.nodes.find(n=>n.className==='wb-browser-status').textContent,/正在交还控制/);
  assert(f.menu('交还给 Agent').disabled);await assert.rejects(f.menu('交还给 Agent').execute(),/正在进行/);
  f.state({manual:false});await f.tick();assert.equal(button['aria-pressed'],'true');
  settle();await pending;await f.flush();assert.equal(button['aria-pressed'],'false');assert(!button.disabled);
  assert.equal(f.trace.filter(r=>r.method==='release').length,1);assert(!f.trace.some(r=>r.method==='toast'));
});

for(const failedRead of [false,true])
test(`stale poll cannot overwrite a newer control receipt, rejected read=${failedRead}`,async()=>{
  const f=fixture();await f.start();await f.flush();let settle;f.trace.length=0;
  f.hook(method=>{if(method==='state')return new Promise((resolve,reject)=>{settle=()=>failedRead?reject(Error('Old read failed')):resolve();});});
  await f.tick();assert(settle);await f.button('接管浏览器').click();await f.flush();
  assert.equal(f.button('接管浏览器')['aria-pressed'],'true');settle();await f.flush();
  assert.equal(f.button('接管浏览器')['aria-pressed'],'true');assert.equal(f.root.dataset.browserPhase,'ready');
  assert(!f.trace.some(r=>r.method==='toast'));assert.equal(f.trace.filter(r=>r.method==='takeover').length,1);
});

test('successful return clears only control notice and ignores unrelated receipt fields',async()=>{
  const f=fixture();f.state({manual:true});await f.start();await f.flush();
  f.response('release',{ok:false,manual:true,error:{code:'network_return_blocked'}});await f.button('接管浏览器').click();await f.flush();
  f.response('release',{manual:false,url:'https://foreign.test/',loading:true,process_failed:true});
  await f.button('接管浏览器').click();await f.flush();assert.equal(f.button('接管浏览器')['aria-pressed'],'false');
  assert.equal(f.root.dataset.browserPhase,'ready');assert.equal(f.nodes.find(n=>n.className==='wb-address').value,'https://example.test/');
  assert.equal(f.nodes.find(n=>n.className==='wb-browser-status').hidden,true);assert.equal(f.mount.status.message,'');
});

test('retired control delivery cannot repaint or toast into another mount',async()=>{
  const f=fixture();f.state({manual:true});await f.start();await f.flush();let settle;
  f.hook(method=>{if(method==='release')return new Promise(resolve=>{settle=resolve;});});
  const pending=f.button('接管浏览器').click();await f.flush();const original=JSON.stringify(f.mount.status);
  f.retire();f.response('release',{ok:false,manual:true,error:{code:'network_owner_changed'}});f.trace.length=0;settle();await pending;await f.flush();
  assert.equal(JSON.stringify(f.mount.status),original);assert(!f.trace.some(r=>['toast','bounds','navigate','create'].includes(r.method)));
});

for(const kind of ['chooser','dialog'])
test(`actual ${kind} UI does not continue after failed takeover`,async()=>{
  const f=fixture({realHelpers:true});f.state({chooser_open:kind==='chooser',dialog_open:kind==='dialog'});
  f.response(kind,kind==='chooser'?{choosers:[{id:'chooser-1'}]}:{dialogs:[{id:'dialog-1',kind:'confirm',message:'Confirm',url:'https://example.test/'}]});
  await f.start();await f.flush();f.response('takeover',{ok:false,manual:false,error:{code:'popup_takeover_unqualified'}});f.trace.length=0;
  const button=f.nodes.filter(n=>n.tag==='button'&&n.textContent===(kind==='chooser'?'接管浏览器':'确定')).at(-1);
  await button.onclick();await f.flush();assert.equal(f.trace.filter(r=>r.method==='takeover').length,1);
  assert(!f.trace.some(r=>r.method===kind&&r.params.action!=='list'));
  assert.equal(f.button('接管浏览器')['aria-pressed'],'false');assert.match(f.trace.find(r=>r.method==='toast').values[0],/暂不支持接管/);
  assert.equal(f.nodes.find(n=>n['aria-label']===(kind==='chooser'?'等待处理的文件选择器':'等待处理的网页对话框')).hidden,false);
});
