import test from 'node:test';
import assert from 'node:assert/strict';
import {connectorJobError} from '../../app/web/js/api/connector-error.js';

test('device job error retains its own HTTP receipt when background relay status changes',()=>{
  const error=connectorJobError({error:'',status:200,job:{id:'original',error:'remote_service_unavailable',status:0,
    transport:{phase:'transport',attempts:3}}},'refresh');
  assert.equal(error.status,0);assert.equal(error.code,'remote_service_unavailable');assert.equal(error.jobId,'original');
  assert.match(error.message,/有限重试/);assert.equal(error.transport.attempts,3);
});
test('an uncertain ticket/write receives inspection guidance and no replay instruction',()=>{
  const error=connectorJobError({job:{error:'remote_service_unavailable',status:0,transport:{phase:'transport',attempts:1}}},'connect');
  assert.match(error.message,/结果尚未确认/);assert.match(error.message,/避免重复操作/);assert.doesNotMatch(error.message,/有限重试/);
});
test('account acquisition failure is not presented as a proven network outage',()=>{
  assert.match(connectorJobError({job:{error:'remote_service_unavailable',status:0,transport:{phase:'account'}}},'list').message,/登录状态暂时无法就绪/);
});
for(const [status,pattern] of [[401,/重新登录/],[403,/权限/],[429,/请求过多/],[503,/服务暂时异常/]])
test(`definitive HTTP ${status} has actionable guidance`,()=>{
  const error=connectorJobError({job:{error:'remote_service_unavailable',status}},'list');assert.equal(error.status,status);assert.match(error.message,pattern);
});
test('older host snapshots retain compatibility',()=>{
  const error=connectorJobError({status:401,error:'legacy',job:{id:'old'}});assert.equal(error.code,'legacy');assert.match(error.message,/重新登录/);
});
