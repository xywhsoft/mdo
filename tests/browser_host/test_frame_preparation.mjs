import assert from 'node:assert/strict';
import test from 'node:test';
import {readFile} from 'node:fs/promises';

// Actual shared dispatcher/driver/reader; native UI/provider is controlled.
// This fixture does not enable or qualify Android product admission.
const source=async name=>(await readFile(new URL('../../app/web/js/features/workbench/'+name+'.js',import.meta.url),'utf8'))
  .replace(/from (["'])\.\/automation-runtime\.js\1/g,`from ${JSON.stringify(new URL('../../app/web/js/features/workbench/automation-runtime.js',import.meta.url).href)}`);
const url=code=>`data:text/javascript;base64,${Buffer.from(code).toString('base64')}`;
let dispatcher=await source('automation-dispatch');
for(const name of ['capture-image','visual-snapshot','visual-dispatch','upload-dispatch','download-dispatch','download-save'])
  dispatcher=dispatcher.replace(`from "./${name}.js"`,`from ${JSON.stringify(new URL('../../app/web/js/features/workbench/'+name+'.js',import.meta.url).href)}`);
const {executeBrowserAutomation}=await import(url(dispatcher));
const reader=url(await source('frame-reader'));
const driver=(await source('frame-driver')).replace("from './frame-reader.js'",`from ${JSON.stringify(reader)}`)
  .replace("from './frame-capture.js'",`from ${JSON.stringify(new URL('../../app/web/js/features/workbench/frame-capture.js',import.meta.url).href)}`);
const {createFrameDriver}=await import(url(driver));
const id='00000000-0000-4000-8000-000000000169',document='d-'+id,frameDocument='fd-'+id;

function fixture(mode='ready'){
  const trace={calls:[],evaluations:[],mutations:0,preparations:0,restores:0,inputs:0};
  let ready=false,armed=false,cancelled=false,live=true,manual=false,navigation=1;
  const execute=async(command,params={})=>{
    trace.calls.push(command==='frames'?`frames:${params.action}`:command);
    if(command==='state')return{loading:false,automation:true,platform:'android-webview',navigation,manual,
      popup_preparation_required:mode!=='windows',popup_preparation_capability:trace.capability!==false,
      isolated_world:{adapter:'android-native-frames-v1',ready,phase:ready?'ready':'suspended'},
      chooser_capability:true,chooser_upload_capability:true,chooser_armed:true,download_capability:true,downloads_armed:true};
    if(command==='popup_prepare'){
      trace.preparations++;assert.equal(params.navigation,1);
      if(!ready)return{ok:false,ready:false};
      await trace.onPrepare?.();
      armed=true;return{ok:true,ready:true,navigation:1};
    }
    if(command==='downloads')return{ok:true,downloads:[]};
    if(command==='input'){trace.inputs++;throw Error('An unexpected native input was reached');}
    assert.equal(command,'frames');
    if(params.action==='list'){
      if(!ready){ready=true;trace.restores++;}
      return await trace.onInventory?.()||{ok:true,complete:true,frames:[{id:'f0',document:frameDocument,parent:null,isolated:true,loading:false,url:'https://fixture.invalid/'}]};
    }
    assert.equal(params.action,'evaluate');assert.equal(params.frame,'f0');assert.equal(params.document,frameDocument);
    assert(ready&&(armed||mode==='windows'),'Page script reached an unprepared original source');
    const tail=params.script.split('({local_frame:true}).').at(-1),method=tail.slice(0,tail.indexOf('('));trace.evaluations.push(method);
    const rejected=await trace.onExpression?.(method,params);if(rejected)return rejected;
    const element={ref:'e1',frame:'f0',name:'Options',role:'combobox',editable:false};
    let value={ok:true,document,url:'https://fixture.invalid/',ready:'complete',viewport:{width:320,height:240}};
    if(method==='observe')value={...value,elements:[],frames:[{id:'f0',dom:true}],text:'Restored original page',visited:1,truncated:false};
    if(method==='locate')value={ok:true,document,element,visited:1};
    if(method==='prepare')value={ok:true,document,element,point:{x:20,y:30},geometry:[10,20,20,20,20,30]};
    if(method==='selectOptions'){trace.mutations++;value={ok:true,document,dispatched:true,verified:true};}
    await trace.onReply?.(method);
    return{ok:true,frame:'f0',frame_document:frameDocument,value};
  };
  const options={command:'observe',params:{timeout_ms:1000},browser:'restore-order-'+mode,action:{id:'restore-order-'+mode},
    api:{async call(){return{state:cancelled?'cancelled':'pending'};}},alive:()=>live,view:{execute},pageDriver:createFrameDriver({execute})};
  return{trace,options,execute,setReady(value){ready=value;},setCancelled(){cancelled=true;},setLive(value){live=value;},setManual(){manual=true;},setNavigation(value){navigation=value;}};
}

test('fresh popup observation restores its native inventory before preparing its first page expression',async()=>{
  const f=fixture(),out=await executeBrowserAutomation(f.options);
  assert(out.result.ok,JSON.stringify(out.result));assert.equal(out.result.text,'Restored original page');
  assert.equal(f.trace.restores,1);assert.equal(f.trace.inputs,0);assert.equal(f.trace.mutations,0);
  assert(f.trace.calls.indexOf('frames:list')<f.trace.calls.indexOf('popup_prepare'));
});

test('waiting for a suspended popup uses the same restore then protection order',async()=>{
  const f=fixture('wait');f.options.command='wait_for';f.options.params.until='text';f.options.params.text='Restored original page';
  const out=await executeBrowserAutomation(f.options);assert(out.result.ok&&out.result.matched,JSON.stringify(out.result));
  assert.equal(f.trace.restores,1);assert.equal(f.trace.inputs,0);assert.equal(f.trace.mutations,0);
});

test('every page expression is preceded by its own original preparation acknowledgment',async()=>{
  const f=fixture();await executeBrowserAutomation(f.options);
  const effects=f.trace.calls.filter(value=>value==='frames:evaluate'||value==='popup_prepare');
  assert(f.trace.evaluations.length>=2);assert.deepEqual(effects,f.trace.evaluations.flatMap(()=>['popup_prepare','frames:evaluate']));
});

test('preparation loss after inventory preserves cancellation, manual ownership and navigation boundaries',async()=>{
  for(const [name,alter,code] of [['cancel',f=>f.setCancelled(),'cancelled'],['owner',f=>f.setLive(false),'context_changed'],
    ['manual',f=>f.setManual(),'manual_control'],['navigation',f=>f.setNavigation(2),'stale_document']]){
    const f=fixture(name);f.trace.onPrepare=()=>alter(f);const out=await executeBrowserAutomation(f.options);
    assert.equal(out.result.error.code,code);assert.equal(out.result.error.effect_may_have_occurred,false);
    assert.equal(f.trace.restores,1);assert.equal(f.trace.preparations,1);assert.equal(f.trace.evaluations.length,0);assert.equal(f.trace.inputs,0);
  }
});

test('unavailable protection and invalid inventory execute no page expressions',async()=>{
  for(const mode of ['unsupported','inventory']){
    const f=fixture(mode);if(mode==='unsupported')f.trace.capability=false;
    else f.trace.onInventory=()=>({ok:false,error:{code:'frame_limit'}});
    const out=await executeBrowserAutomation(f.options);assert.equal(out.result.error.code,mode==='unsupported'?'unsupported_capability':'frame_limit');
    assert.equal(out.result.error.effect_may_have_occurred,false);assert.equal(f.trace.preparations,0);assert.equal(f.trace.evaluations.length,0);
  }
});

test('the original action deadline bounds protection after inventory without evaluating a late reply',async()=>{
  const f=fixture('deadline');f.options.params.timeout_ms=60;f.trace.onPrepare=()=>new Promise(resolve=>setTimeout(resolve,90));
  const out=await executeBrowserAutomation(f.options);assert.equal(out.result.error.code,'timed_out');
  assert.equal(out.result.error.effect_may_have_occurred,false);assert.equal(f.trace.evaluations.length,0);assert.equal(f.trace.preparations,1);
});

test('a stale pinned target is not selected after fresh inventory restoration',async()=>{
  const f=fixture('old-pin');f.options.command='act';f.options.params={op:'select',document:'d-00000000-0000-4000-8000-000000000001',target:{name:'Options'},values:['one'],timeout_ms:1000};
  const out=await executeBrowserAutomation(f.options);assert.equal(out.result.error.code,'stale_document');
  assert.equal(f.trace.mutations,0);assert.equal(f.trace.inputs,0);assert.equal(out.result.error.effect_may_have_occurred,false);
});

test('native selection marks dispatch only at the mutating expression and never replays a lost result',async()=>{
  for(const mode of ['success','cancel-before','cancel-after','lost','refused']){
    const f=fixture(mode);f.options.command='act';f.options.params={op:'select',document,target:{name:'Options'},values:['one'],timeout_ms:1500};
    const original=f.options.pageDriver;let readMethod;
    f.options.pageDriver={...original,async read(method,options,control){readMethod=method;return original.read(method,options,control);}};
    if(mode==='cancel-before')f.trace.onPrepare=()=>{if(readMethod==='selectOptions')f.setCancelled();};
    if(mode==='cancel-after')f.trace.onReply=method=>{if(method==='selectOptions')f.setCancelled();};
    if(mode==='lost')f.trace.onReply=method=>{if(method==='selectOptions')throw Error('Controlled lost selection acknowledgment');};
    if(mode==='refused')f.trace.onExpression=method=>method==='selectOptions'?{ok:false,error:{code:'cdp_worker_cancelled',evaluation_may_have_occurred:false}}:null;
    const out=await executeBrowserAutomation(f.options);assert.equal(f.trace.inputs,0);
    if(mode==='success'){assert(out.result.ok,JSON.stringify(out.result));assert.equal(out.result.dispatched,true);assert.equal(f.trace.mutations,1);}
    else{
      assert.equal(out.result.ok,false);assert.equal(out.result.error.effect_may_have_occurred,mode==='lost'||mode==='cancel-after');
      assert.equal(f.trace.mutations,mode==='lost'||mode==='cancel-after'?1:0);
      if(mode.startsWith('cancel'))assert.equal(out.result.error.code,'cancelled');
    }
    assert(f.trace.mutations<=1);
  }
});

test('the direct mutating driver gate preserves its original thrown boundary before any dispatch',async()=>{
  const success=fixture('direct-success');success.setReady(true);let gates=0,dispatches=0;
  const control={beforeEvaluate:async()=>{gates++;await success.execute('popup_prepare',{navigation:1});},onDispatch:()=>{dispatches++;}};
  const out=await success.options.pageDriver.read('selectOptions',{document,target:{name:'Options'},values:['one']},control);
  assert(out.ok);assert.equal(dispatches,1);assert.equal(success.trace.mutations,1);assert.equal(gates,success.trace.evaluations.length);
  const denied=fixture('direct-denied');denied.setReady(true);let calls=0,sent=0;const boundary={browserBoundary:{handled:true,result:{ok:false,error:{code:'cancelled',effect_may_have_occurred:false}}}};
  await assert.rejects(denied.options.pageDriver.read('selectOptions',{document,target:{name:'Options'},values:['one']},{beforeEvaluate:async()=>{
    if(++calls===gates)throw boundary;await denied.execute('popup_prepare',{navigation:1});},onDispatch:()=>{sent++;}}),error=>error===boundary);
  assert.equal(denied.trace.mutations,0);assert.equal(sent,0);assert.equal(calls,gates);
});

test('hosts without popup preparation retain native frame execution without preparation RPCs',async()=>{
  const f=fixture('windows'),out=await executeBrowserAutomation(f.options);assert(out.result.ok,JSON.stringify(out.result));
  assert.equal(f.trace.preparations,0);assert(f.trace.evaluations.length>0);
});
