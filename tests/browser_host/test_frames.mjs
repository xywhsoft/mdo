import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {test} from 'node:test';

const code=(await readFile(new URL('../../app/web/js/features/workbench/frame-reader.js',import.meta.url),'utf8'))
  .replace("import {browserRuntime} from './automation-runtime.js';",'function browserRuntime(){}');
const {createFrameReader}=await import(`data:text/javascript;base64,${Buffer.from(code).toString('base64')}`);
const guid=n=>`00000000-0000-4000-8000-${String(n).padStart(12,'0')}`;
const rootDoc='d-'+guid(1),childDoc='d-'+guid(2),child='f-'+guid(2);
const initial=[{id:'f0',document:'fd-'+guid(1),parent:null,url:'https://user:password@root.test/',name:'',isolated:true,loading:false},
  {id:child,document:'fd-'+guid(2),parent:'f0',url:'https://child.test/',name:'child',isolated:true,loading:false,separate_process:true}];
const element=name=>({ref:'e1',frame:'f0',role:'button',name,bounds:{x:1,y:2,width:30,height:20},coordinate_space:'frame-css'});
function fixture(){
  const state={rows:structuredClone(initial),calls:[],lists:0,labels:['Root action','Child action'],text:['Root text','Child text'],
    visited:3,beforeList:null,onRead:null,checkpoint:null};
  const reader=createFrameReader({checkpoint:async()=>{await state.checkpoint?.();},execute:async(command,params)=>{
    assert.equal(command,'frames');state.calls.push(params);
    if(params.action==='list'){state.lists++;await state.beforeList?.(state.lists);if(state.listResult)return await state.listResult(state.lists);return{ok:true,complete:true,frames:structuredClone(state.rows)};}
    assert.equal(params.action,'evaluate');assert(params.script.includes('({local_frame:true}).'));
    const tail=params.script.split('({local_frame:true}).').at(-1),method=tail.slice(0,tail.indexOf('('));
    const options=JSON.parse(tail.slice(tail.indexOf('(')+1,-1));assert(['describe','observe','locate'].includes(method));
    const index=params.frame==='f0'?0:1,document=index?childDoc:rootDoc;
    let value={ok:true,document,url:initial[index].url,title:'Fixture',ready:'complete',viewport:{width:800,height:600}};
    if(method==='observe')value={...value,text:state.text[index],frames:[{id:'f0'}],elements:options.limit===0?[]:[element(state.labels[index])],visited:state.visited,truncated:false};
    if(method==='locate')value=options.target.ref==='e1'||options.target.name===state.labels[index]?
      {ok:true,document,element:element(state.labels[index]),visited:options.target.ref?0:state.visited}:
      {ok:false,error:{code:options.target.ref?'element_detached':'element_not_found'},visited:state.visited};
    const custom=await state.onRead?.({params,method,options,value});if(custom)return custom;
    return{ok:true,frame:params.frame,frame_document:params.document,value};
  }});
  return{state,reader};
}

test('only an unpinned observation can retry an inventory invalidated by navigation',async()=>{
  const f=fixture();f.state.listResult=()=>({ok:false,error:{code:'stale_frame'}});
  const observed=await f.reader.observe({});assert.equal(observed.error.code,'frame_tree_changed');assert.equal(observed.error.effect_may_have_occurred,false);
  const located=await f.reader.locate({document:rootDoc,target:{name:'Root action'}});assert.equal(located.error.code,'stale_frame');
  assert.equal(f.state.calls.filter(x=>x.action==='evaluate').length,0);
});
test('a navigation-invalidated final inventory discards an unpinned partial observation',async()=>{
  const f=fixture();f.state.listResult=count=>count===1?{ok:true,complete:true,frames:structuredClone(f.state.rows)}:{ok:false,error:{code:'stale_frame'}};
  const result=await f.reader.observe({});assert.equal(result.error.code,'frame_tree_changed');assert.equal(result.text,undefined);assert.equal(result.document,undefined);
  assert(f.state.calls.some(x=>x.action==='evaluate'));
});
for(const code of ['stale_document','context_changed','frame_interrupted','frame_response_invalid'])
test(`inventory ${code} is never relabelled as a transient navigation`,async()=>{
  const f=fixture();f.state.listResult=()=>({ok:false,error:{code}});const result=await f.reader.observe({});assert.equal(result.error.code,code);
});

test('one-frame observations merge text and namespace identical local references',async()=>{
  const {reader,state}=fixture(),out=await reader.observe({});assert(out.ok);
  assert.equal(out.document,rootDoc);assert.equal(out.text,'Root text\nChild text');assert.equal(out.frames.length,2);
  assert.equal(out.visited,6);assert(!out.truncated);assert(out.elements.every(row=>row.ref.startsWith('r1|')));
  assert.notEqual(out.elements[0].ref,out.elements[1].ref);assert.equal(out.elements[1].frame,child);
  assert.equal(out.elements[1].coordinate_space,'frame-css');assert(!JSON.stringify(out).includes('password'));
  assert(state.calls.filter(row=>row.action==='evaluate').every(row=>row.agent&&row.document&&row.frame));
});

test('semantic ambiguity is global while exact frame and reference selection remain stable',async()=>{
  const {reader,state}=fixture();state.labels=['Shared action','Shared action'];
  const ambiguous=await reader.locate({document:rootDoc,target:{name:'Shared action'}});
  assert.equal(ambiguous.error.code,'element_ambiguous');assert.equal(ambiguous.error.candidates.length,2);
  const exact=await reader.locate({document:rootDoc,target:{name:'Shared action',frame:child}});assert(exact.ok);
  assert.equal(exact.route.frame,child);assert.equal(exact.route.document,childDoc);assert.equal(exact.route.ref,'e1');
  const pinned=await reader.locate({document:rootDoc,target:{ref:exact.element.ref}});assert(pinned.ok);assert.equal(pinned.element.ref,exact.element.ref);
});

test('old root or child documents are rejected before local reference lookup',async()=>{
  const {reader,state}=fixture(),observed=await reader.observe({}),ref=observed.elements[1].ref;
  state.rows[1].document='fd-'+guid(3);state.calls=[];
  assert.equal((await reader.locate({document:rootDoc,target:{ref}})).error.code,'stale_frame');
  assert(!state.calls.some(row=>row.script?.includes('}).locate(')));
  state.calls=[];assert.equal((await reader.locate({document:'d-'+guid(99),target:{name:'Root action'}})).error.code,'stale_document');
  assert(!state.calls.some(row=>row.script?.includes('}).locate(')));
});

test('a same-size tree replacement during observation invalidates the entire combined result',async()=>{
  const {reader,state}=fixture();state.beforeList=n=>{if(n===2)state.rows[1]={...state.rows[1],id:'f-'+guid(7)};};
  assert.equal((await reader.observe({})).error.code,'stale_frame');
});

test('an unreadable frame prevents a false unique match and is explicit in partial observations',async()=>{
  const {reader,state}=fixture();state.rows[1].loading=true;
  assert.equal((await reader.locate({document:rootDoc,target:{name:'Root action'}})).error.code,'frame_not_ready');
  const root=await reader.locate({document:rootDoc,target:{name:'Root action',frame:'f0'}});assert(root.ok);
  const out=await reader.observe({});assert(out.ok&&out.truncated);assert.equal(out.frames[1].reason,'frame_loading');assert(!out.frames[1].dom);
});

test('aggregate node budget prevents further scans or a false unique match',async()=>{
  const {reader,state}=fixture();state.visited=6000;
  const out=await reader.observe({});assert(out.ok&&out.truncated);assert.equal(out.visited,6000);assert.equal(out.frames[1].reason,'observation_budget');
  state.calls=[];assert.equal((await reader.locate({document:rootDoc,target:{name:'Root action'}})).error.code,'observation_incomplete');
  assert(!state.calls.some(row=>row.frame===child&&row.action==='evaluate'));
});

test('combined text obeys its byte budget without splitting UTF-8',async()=>{
  const {reader,state}=fixture();state.text=['🦉'.repeat(63),'后续文本'.repeat(100)];
  const out=await reader.observe({max_bytes:256});assert(out.ok&&out.truncated);assert(new TextEncoder().encode(out.text).length<=256);
  assert(!out.text.includes('\uFFFD'));assert.equal(out.elements.length,2);
});

test('cancellation between native calls stops additional frame reads',async()=>{
  const {reader,state}=fixture();let checks=0;state.checkpoint=()=>{if(++checks===7)throw Object.assign(Error('Original action cancelled'),{code:'cancelled'});};
  const out=await reader.observe({});assert.equal(out.error.code,'cancelled');assert.equal(out.error.effect_may_have_occurred,false);
  assert(!state.calls.some(row=>row.frame===child&&row.action==='evaluate'));
});

test('one read deadline bounds multiple native frame calls',async()=>{
  const {reader,state}=fixture();state.checkpoint=()=>new Promise(resolve=>setTimeout(resolve,5));
  const out=await reader.observe({timeout_ms:1});assert.equal(out.error.code,'timed_out');assert.equal(state.calls.length,0);
});

test('malformed or conflicting references never become protocol identities or script input',async()=>{
  const {reader,state}=fixture(),out=await reader.observe({}),ref=out.elements[1].ref;
  for(const target of [{ref:'raw-session-target'},{ref,frame:'f0'},{ref,name:'Child action'}]){
    state.calls=[];assert.equal((await reader.locate({document:rootDoc,target})).error.code,'target_invalid');
    assert(!state.calls.some(row=>row.script?.includes('}).locate(')));
  }
});

test('mismatched native receipts and foreign ancestry fail explicitly',async()=>{
  const first=fixture();first.state.onRead=()=>({ok:true,frame:'f0',frame_document:'fd-'+guid(999),value:{ok:true}});
  assert.equal((await first.reader.observe({})).error.code,'frame_response_invalid');
  const second=fixture();second.state.rows[1].parent='f-'+guid(999);
  assert.equal((await second.reader.observe({})).error.code,'frame_response_invalid');
});

test('native failures remain failures and oversized metadata is bounded',async()=>{
  const first=fixture();first.state.onRead=()=>({ok:false,error:{code:'frame_timed_out',evaluation_may_have_occurred:true}});
  assert.equal((await first.reader.observe({})).error.code,'frame_timed_out');
  const second=fixture();second.state.labels[0]='x'.repeat(270000);
  assert.equal((await second.reader.observe({})).error.code,'frame_result_limit');
});
