import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import test from 'node:test';
import {interruptedToolNotice} from '../../app/web/js/features/chat/tool-recovery.js';
const source=readFileSync(new URL('../../app/web/js/features/chat/timeline.js',import.meta.url),'utf8');
const project=new Function('t','interruptedToolNotice',source.replace(/^import .+;\r?$/gm,'').replace(/^export /gm,'')+';return eventsToTimeline;')
  ((key,args,fallback)=>fallback||key,interruptedToolNotice);
const event={event_id:1,kind:'tool_start',agent_depth:0,agent_id:1,run_id:0,
  tool_call_id:'sdk-'+'a'.repeat(32),tool_name:'browser_open',text:'{"action":"create"}',
  tool_outcome:{state:'interrupted',code:'sdk_operation_interrupted',may_have_completed:true,message:'Outcome unknown; inspect effects.'}};
// Actual restart receipts are verified by the existing-Home integration gate.
test('qualified restart status closes the display without inventing an output event',()=>{
  const rows=project([event]);assert.equal(rows.length,1);assert.equal(rows[0].state,'failed');
  assert.equal(rows[0].inputText,event.text);assert.equal(rows[0].outputText,event.tool_outcome.message);
  assert.equal(rows[0].recoveryState,'interrupted');assert.equal(rows[0].outputEventId,undefined);
});
test('a real retained tool result takes precedence over derived operation status',()=>{
  const done={...event,event_id:2,kind:'tool_done',success:true,text:'actual result'};
  const row=project([event,done])[0];assert.equal(row.state,'done');assert.equal(row.outputText,'actual result');
  assert.equal(row.recoveryState,undefined);assert.equal(row.outputEventId,2);
});
test('unqualified or ambiguous metadata never claims an interrupted result',()=>{
  for(const patch of [{run_id:1},{agent_depth:1},{kind:'tool_done'},{tool_call_id:'unknown'},
      {tool_outcome:{...event.tool_outcome,may_have_completed:'true'}},
      {tool_outcome:{...event.tool_outcome,code:'different'}}, {tool_outcome:undefined}])
    assert.equal(interruptedToolNotice({...event,...patch}),'');
});
test('local C/JS error adaptation preserves interrupted uncertainty details',async()=>{
  const apiSource=readFileSync(new URL('../../app/web/js/api/application.js',import.meta.url),'utf8');
  let calls=0;
  const fetch=async path=>{
    calls++;if(path==='/api/v1/bootstrap')return{ok:true,headers:{get:()=> 'synthetic-test-token'}};
    return{ok:false,status:409,json:async()=>({ok:false,request_id:'qa',error:{code:'sdk_operation_interrupted',
      message:'Outcome unknown',details:{may_have_completed:true,stage:'operation_recovery'}}})};
  };
  const make=new Function('api','workbenchServices','fetch',apiSource.replace(/^import .+;\r?$/gm,'').replace(/^export /gm,'')+';return createLocalApplicationClient;')({},()=>({}),fetch);
  await assert.rejects(make().result(event.tool_call_id),error=>error.code==='sdk_operation_interrupted'&&
    error.status===409&&error.requestId==='qa'&&error.details.may_have_completed===true);
  assert.equal(calls,2);
});
