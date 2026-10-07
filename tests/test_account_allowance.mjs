import assert from 'node:assert/strict';
import { allowanceView } from '../app/web/js/features/account/allowance.js';
const q={model_id:'ornith',title:'Ornith',used_tokens:25000000,reserved_tokens:5000000,
  resets_at:2000,unlimited:false,limit_tokens:100000000,remaining_tokens:70000000};
const state={group_id:'free',plan_id:'',is_vip:false,plan_expires_at:0,updated_at:1000,daily_quotas:[q]};
assert.equal(allowanceView(state,1000).quotas[0].percentText,'70%');
assert.equal(allowanceView({...state,is_vip:true,group_id:'vip',plan_expires_at:2000},1000).vip,true);
assert.equal(allowanceView({...state,is_vip:true,group_id:'vip',plan_expires_at:999},1000).group,'free');
assert.equal(allowanceView({...state,daily_quotas:[{...q,remaining_tokens:1}]},1000).quotas[0].percentText,'<1%');
assert.equal(allowanceView({...state,daily_quotas:[{...q,remaining_tokens:0}]},1000).quotas[0].percentage,0);
assert.equal(allowanceView({...state,daily_quotas:[{...q,unlimited:true,limit_tokens:null,remaining_tokens:null}]},1000).quotas[0].percentText,'∞');
assert.equal(allowanceView(state,1100).stale,true);
assert.equal(allowanceView(state,2000).quotas[0].stale,true);
assert.equal(allowanceView({...state,daily_quotas:[{...q,remaining_tokens:100000001}]},1000).quotas.length,0);
assert.equal(allowanceView(null),null);
console.log('PASS allowance percentage, unlimited, VIP expiry, stale data and invalid values');
