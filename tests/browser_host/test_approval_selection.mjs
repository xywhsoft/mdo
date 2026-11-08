import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import test from 'node:test';
import {createResourceStore} from '../../app/web/js/state/store.js';

const source=readFileSync(new URL('../../app/web/js/state/approvals.js',import.meta.url),'utf8');
const factory=new Function('api','resourceId','createResourceStore','isTransientReadError','t','runDecisionAction',
  source.replace(/^import .+;\r?$/gm,'').replace(/^export /gm,'')+
  ';return {approvalsStore,selectApprovals,clearApprovals,loadApprovals};');
function fixture(read){
  const calls=[];
  const api={get(path,options){calls.push(path);return read(path,options);}};
  const value=factory(api,x=>x,createResourceStore,()=>false,x=>x,()=>{throw Error('No decisions expected');});
  return {...value,calls};
}
const reply=(session,pending=1)=>({data:{project_id:'default',session_id:session,
  total:1,pending_total:pending,limit:4,truncated:false,items:[{id:session}]}});
test('selected approvals use server scope before the response limit and preserve global pending count',async()=>{
  const f=fixture(async()=>reply('one',6));await f.selectApprovals('default','one');
  assert.deepEqual(f.calls,['/approvals?project_id=default&session_id=one']);
  assert.equal(f.approvalsStore.get().data.total,6);assert.equal(f.approvalsStore.get().data.scopeTotal,1);
  assert.equal(f.approvalsStore.get().data.sessionId,'one');
});
test('obsolete response ignoring cancellation cannot publish into the next conversation',async()=>{
  let finish;const f=fixture(path=>path.endsWith('one')?new Promise(resolve=>{finish=resolve;}):Promise.resolve(reply('two')));
  const first=f.selectApprovals('default','one');await f.selectApprovals('default','two');
  finish(reply('one'));await first;await Promise.resolve();
  assert.equal(f.approvalsStore.get().data.sessionId,'two');
  assert.deepEqual(f.approvalsStore.get().data.items,[{id:'two'}]);
  f.clearApprovals();assert.deepEqual(f.approvalsStore.get().data.items,[]);
});
test('a mismatched scoped snapshot fails instead of publishing foreign approvals',async()=>{
  const f=fixture(async()=>reply('other'));await f.selectApprovals('default','one');
  assert.equal(f.approvalsStore.get().status,'error');assert.deepEqual(f.approvalsStore.get().data.items,[]);
});
