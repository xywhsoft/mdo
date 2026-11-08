import {test} from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {browserScriptExpression,executeBrowserScript} from '../../app/web/js/features/workbench/script-dispatch.js';

const document='d-11111111-1111-4111-8111-111111111111';
const root='fd-11111111-1111-4111-8111-111111111111';
const child='f-22222222-2222-4222-8222-222222222222',childDocument='fd-22222222-2222-4222-8222-222222222222';
async function evaluate(script,maximum=4096){
  return JSON.parse(JSON.stringify(await vm.runInNewContext(browserScriptExpression(script,maximum),{TextEncoder,document:{title:'Fixture'},pageValue:42},{timeout:1000})));
}
test('script bodies await async results and return JSON values rather than remote objects',async()=>{
  assert.deepEqual(await evaluate('return {title:document.title,value:await Promise.resolve(pageValue)};'),{ok:true,value:{title:'Fixture',value:42},value_type:'object'});
  assert.deepEqual(await evaluate('return;'),{ok:true,value:null,value_type:'undefined'});
});
test('JavaScript exceptions and unsupported JSON values retain bounded failure results',async()=>{
  assert.equal((await evaluate('throw new Error("fixture failure")')).error.message,'fixture failure');
  assert.equal((await evaluate('return 1n')).error.code,'script_result_not_json');
  assert.equal((await evaluate('return ()=>1')).error.code,'script_result_not_json');
  assert.equal((await evaluate('const a={};a.a=a;return a')).ok,false);
});
test('script result budgets cover repeated objects, node counts and multibyte strings',async()=>{
  for(const script of ['return "x".repeat(600)','return "汉".repeat(200)','return Array.from({length:5000},()=>1)'])
    assert.equal((await evaluate(script,256)).error.code,'script_result_limit');
});

function fixture(patch={}){
  let calls=0,armed=0,possible=false;const requests=[];
  const params={document,script:'return document.title;',...patch};
  const fail=(code,message)=>({handled:true,result:{ok:false,error:{code,message,effect_may_have_occurred:possible}}});
  const options={params,action:{id:'original'},deadline:performance.now()+10000,fail,
    setDispatched:value=>{possible=value;},guard:async()=>({state:{platform:'webview2',script_execution:true}}),
    arm:async()=>{armed++;return null;},
    read:async()=>({ok:true,document,frames:[{id:'f0',document:root,dom:true,loading:false},{id:child,document:childDocument,dom:true,loading:false}]}),
    view:{execute:async(command,request)=>{calls++;requests.push({command,...request});return{ok:true,frame:request.frame,frame_document:request.document,world:request.world,value:{ok:true,value:'Fixture',value_type:'string'}};}}};
  return{options,requests,get calls(){return calls;},get armed(){return armed;}};
}
test('default page execution pins the main document and dispatches exactly once',async()=>{
  const f=fixture();const result=await executeBrowserScript(f.options);
  assert.equal(result.result.value,'Fixture');assert.equal(f.calls,1);assert.equal(f.armed,1);
  assert.equal(f.requests[0].frame,'f0');assert.equal(f.requests[0].document,root);assert.equal(f.requests[0].world,'page');
});
test('cross-origin iframe execution carries its observed native identity and explicit world',async()=>{
  const f=fixture({frame:child,frame_document:childDocument,world:'isolated'});const result=await executeBrowserScript(f.options);
  assert.equal(result.result.frame,child);assert.equal(f.requests[0].world,'isolated');assert.equal(f.requests[0].document,childDocument);
});
test('stale root or iframe identity cannot execute the script',async()=>{
  for(const patch of [{document:'old'},{frame:child},{frame:child,frame_document:root},{frame:'raw-protocol-id'}]){
    const f=fixture(patch);assert.equal((await executeBrowserScript(f.options)).result.ok,false);assert.equal(f.calls,0);assert.equal(f.armed,0);
  }
});
test('unavailable hosts and pre-dispatch cancellation cannot run a script',async()=>{
  const f=fixture();f.options.guard=async()=>({state:{platform:'android-webview',script_execution:false}});
  assert.equal((await executeBrowserScript(f.options)).result.error.code,'unsupported_capability');assert.equal(f.calls,0);
  f.options.guard=async()=>f.options.fail('cancelled','Cancelled');
  assert.equal((await executeBrowserScript(f.options)).result.error.code,'cancelled');assert.equal(f.calls,0);
});
test('unfinished native input prevents JavaScript dispatch',async()=>{
  const f=fixture();f.options.guard=async()=>({state:{platform:'webview2',script_execution:true,input_busy:true}});
  assert.equal((await executeBrowserScript(f.options)).result.error.code,'browser_busy');assert.equal(f.calls,0);
});
test('native rejection preserves certainty and never replays a failed evaluation',async()=>{
  const f=fixture();f.options.view.execute=async()=>{f.requests.push({});return{ok:false,error:{code:'stale_frame',evaluation_may_have_occurred:false}};};
  const result=await executeBrowserScript(f.options);assert.equal(result.result.error.code,'stale_frame');assert.equal(result.result.error.effect_may_have_occurred,false);assert.equal(f.requests.length,1);
});
test('a lost or cancelled post-dispatch result remains uncertain',async()=>{
  const f=fixture();let checks=0;
  f.options.guard=async()=>++checks===3?f.options.fail('cancelled','Cancelled'):({state:{platform:'webview2',script_execution:true}});
  const result=await executeBrowserScript(f.options);assert.equal(f.calls,1);assert.equal(result.result.error.effect_may_have_occurred,true);
});
test('foreign frame replies and oversized results are refused after original execution',async()=>{
  for(const patch of [{frame:'f0',frame_document:childDocument},{value:{ok:true,value:'x'.repeat(20000)}}]){
    const f=fixture();const execute=f.options.view.execute;f.options.view.execute=async(...args)=>({...await execute(...args),...patch});
    const result=await executeBrowserScript(f.options);assert.equal(result.result.ok,false);assert.equal(result.result.error.effect_may_have_occurred,true);assert.equal(f.calls,1);
  }
});
