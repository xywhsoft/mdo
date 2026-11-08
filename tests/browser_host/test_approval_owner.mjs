import assert from 'node:assert/strict';
import test from 'node:test';
import {approvalBelongsToSession as belongs} from '../../app/web/js/features/approvals/approval-owner.js';

test('SDK approvals belong to the exact validated session without a registered Agent run',()=>{
  assert(belongs({run_id:0,project_id:'default',session_id:'one'},'default','one',new Set()));
  assert(!belongs({run_id:0,project_id:'default',session_id:'one'},'default','two',new Set()));
  assert(!belongs({run_id:0,project_id:'other',session_id:'one'},'default','one',new Set()));
});
test('explicit foreign or partial metadata never falls back to a colliding run',()=>{
  const runs=new Set(['17','0']);
  for(const row of [{project_id:'other',session_id:'one'}, {project_id:'default'}, {session_id:'one'}])
    assert(!belongs({...row,run_id:17},'default','one',runs));
});
test('legacy approvals require a real matching run and a selected conversation',()=>{
  const runs=new Set(['17','0','-1','undefined']);
  assert(belongs({run_id:17},'default','one',runs));
  assert(!belongs({run_id:18},'default','one',runs));
  for(const run_id of [0,'0',-1,undefined,null,'','01'])
    assert(!belongs({run_id},'default','one',runs));
  assert(!belongs({run_id:17},'default','',runs));
});
