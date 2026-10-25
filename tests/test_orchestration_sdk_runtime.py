"""Public orchestration SDK through the real product and native C modules."""
from concurrent.futures import ThreadPoolExecutor
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from test_api_runtime import ROOT, free_port, request, wait_ready, write_site

MODULE = r'''
#include "mdo/execution.h"
#include "mdo/state.h"
#include <string.h>
static mdo_result Capture(void* U,const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
 const mdo_values_v1* V=C->Api->Values;(void)U;
 if(V->Get(P,"state"))return mdoStateGet(C,P,R,E);
 if(V->Get(P,"cancel")){E->Status=499;strcpy(E->Code,"qa_cancelled");return MDO_RESULT_CANCELLED;}
 if(V->Get(P,"timeout")){E->Status=504;strcpy(E->Code,"qa_timeout");return MDO_RESULT_TIMEOUT;}
 if(V->Get(P,"uncertain")){E->Status=503;E->MayHaveCompleted=true;strcpy(E->Code,"qa_uncertain");return MDO_RESULT_ERROR;}
 xvalue *Before=NULL,*Nested=NULL,*After=NULL;
 if(mdoExecutionContext(C,&Before,E)!=MDO_RESULT_OK)return MDO_RESULT_ERROR;
 if(!strcmp(C->Method,"qa.trace")){
  if(mdoCall(C,"qa.child",NULL,&Nested,E)!=MDO_RESULT_OK){V->Release(Before);return MDO_RESULT_ERROR;}
 }
 if(mdoExecutionContext(C,&After,E)!=MDO_RESULT_OK){V->Release(Before);V->Release(Nested);return MDO_RESULT_ERROR;}
 *R=V->Object();V->Set(*R,"before",Before);V->Set(*R,"after",After);if(Nested)V->Set(*R,"nested",Nested);
 V->Release(Before);V->Release(Nested);V->Release(After);return MDO_RESULT_OK;
}
static mdo_result Register(const mdo_host_services_v1* H,const mdo_registrar_v1* R,void** D,char* E,size_t N){
 (void)H;(void)D;mdo_service_v1 S={sizeof(S),1,"qa.trace","Trace fixture","{}","{}",MDO_TOOL_EFFECT_READ,NULL,Capture};
 if(R->AddService(R->Context,&S,E,N)!=MDO_RESULT_OK)return MDO_RESULT_ERROR;
 S.Id="qa.child";return R->AddService(R->Context,&S,E,N);
}
MDO_EXPORT const mdo_module_v1* mdoModuleEntry(void){
 static const mdo_module_v1 M={sizeof(M),1,"qa.orchestration","Orchestration QA","QA","1",MDO_CAPABILITY_ALL,NULL,0,Register,NULL};return &M;
}'''


class Fixture:
    def __init__(self, host, base):
        self.base = base
        self.home = base / 'home'
        self.port = free_port()
        self.config = write_site(base, self.port)
        module = self.home / 'modules/tools/orchestration.c'
        module.parent.mkdir(parents=True)
        module.write_text(MODULE, encoding='utf-8')
        self.host = host

    def start(self):
        self.log = (self.base/'host.log').open('ab')
        self.child = subprocess.Popen([str(self.host), str(self.config), '--', '--home', str(self.home)],
            cwd=self.base, env=dict(os.environ, USE_WEBVIEW='0'), stdout=self.log, stderr=self.log,
            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        try:
            wait_ready(self.port, self.child)
        except Exception as error:
            self.stop()
            raise RuntimeError((self.base/'host.log').read_text(encoding='utf-8', errors='replace')) from error

    def stop(self):
        self.child.terminate()
        self.child.wait(timeout=12)
        self.log.close()

    def call(self, method, params=None, status=200, context=None):
        actual, _, text = request(self.port, 'POST', '/api/v1/application/call',
            body=json.dumps(dict(method=method, params=params or {}, context=context or {'project':'default'})).encode(),
            headers={'Content-Type':'application/json'})
        result = json.loads(text)
        assert actual == status, (method, actual, result)
        return result['data'] if status==200 else result['error']


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--host', type=Path, required=True)
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(dir=ROOT/'.build', prefix='orchestration-sdk-') as tmp:
        fixture=Fixture(args.host.resolve(),Path(tmp))
        fixture.start()
        try:
            result=fixture.call('qa.trace')
            before,after,nested=result['before'],result['after'],result['nested']['before']
            assert len(before['trace_id'])==32 and before['trace_id']==after['trace_id']==nested['trace_id']
            assert before['parent_call_id']==after['parent_call_id']
            assert before['call_id']!=after['call_id'] and nested['parent_call_id']!=before['parent_call_id']
            assert fixture.call('qa.trace')['before']['trace_id']!=before['trace_id']
            assert before['deadline_unit']=='monotonic_microseconds'
            assert fixture.call('qa.trace', {'cancel':True},499)['code']=='qa_cancelled'
            assert fixture.call('qa.trace', {'timeout':True},504)['code']=='qa_timeout'
            uncertain=fixture.call('qa.trace', {'uncertain':True},503)
            assert uncertain['details']['may_have_completed']
            job=fixture.call('operation.start',{'method':'sdk.context'})
            assert fixture.call('operation.wait',{'id':job['id'],'timeout_ms':2000})['outcome']=='succeeded'
            async_context=fixture.call('operation.result',{'id':job['id']})
            events=fixture.call('events.read')['items']
            start=next(e for e in events if e['topic']=='service.before' and e['data'].get('method')=='operation.start')
            assert async_context['trace_id']==start['trace_id'] and async_context['parent_call_id']==start['call_id']
            fixture.call('operation.release',{'id':job['id']})
            assert any(e['topic']=='service.before' and e['trace_id']==before['trace_id'] for e in events)
            for i in range(40):fixture.call('events.emit',{'topic':'qa.page','data':{'n':i,'text':'x'*2048}})
            cursor=0;values=[];pages=0
            while True:
                page=fixture.call('events.read',{'after':cursor,'topic':'qa.page'})
                assert len(json.dumps(page))<40000
                values.extend(e['data']['n'] for e in page['items']);pages+=1;cursor=page['cursor']
                if not page['has_more']:break
            assert values==list(range(40)) and pages>=3
            assert fixture.call('events.emit',{'topic':'qa.large','data':'x'*8193},413)['code']=='sdk_event_limit'
            forged=fixture.call('sdk.context',context={'project':'default','trace_id':'forged'})
            assert forged['trace_id']!='forged'
            def state(method, **params):return fixture.call('state.'+method,dict(namespace='qa.state',**params))
            assert state('get',key='counter')=={'key':'counter','revision':0,'exists':False}
            first=state('put',key='counter',expected_revision=0,value=1)['revision']
            assert state('get',key='counter')['value']==1
            assert fixture.call('qa.trace',{'state':True,'namespace':'qa.state','key':'counter'})['value']==1
            fixture.call('state.put',dict(namespace='qa.state',key='counter',expected_revision=0,value=2),409)
            # Every writer competes for the same observed revision.
            def compete(i):
                actual,_,body=request(fixture.port,'POST','/api/v1/application/call',
                    body=json.dumps({'method':'state.compare_exchange','context':{'project':'default'},
                        'params':dict(namespace='qa.state',key='counter',expected_revision=first,value=i)}).encode(),headers={'Content-Type':'application/json'})
                assert actual in (200,409),(actual,body)
                return actual
            with ThreadPoolExecutor(max_workers=8) as pool:
                statuses=list(pool.map(compete,range(8)))
            assert statuses.count(200)==1 and statuses.count(409)==7
            current=state('get',key='counter');rev=current['revision']
            fixture.call('state.transaction',dict(namespace='qa.state',writes=[
                dict(key='new',expected_revision=0,value=True),dict(key='counter',expected_revision=0,value=99)]),409)
            assert state('get',key='new')['revision']==0 and state('get',key='counter')==current
            transaction=state('transaction',writes=[dict(key='new',expected_revision=0,value=None),dict(key='counter',expected_revision=rev,value=99)])
            assert len(transaction['items'])==2 and len({e['revision'] for e in transaction['items']})==1
            tombstone=state('remove',key='counter',expected_revision=transaction['revision'])['revision']
            assert state('get',key='counter')==dict(key='counter',revision=tombstone,exists=False)
            fixture.call('state.put',dict(namespace='qa.state',key='counter',expected_revision=0,value=7),409)
            recreated=state('put',key='counter',expected_revision=tombstone,value=False)['revision']
            assert state('get',key='counter')['value'] is False
            fixture.call('state.put',dict(namespace='qa.state',key='../escape',expected_revision=0,value=0),400)
            fixture.call('state.put',dict(namespace='qa.state',key='large',expected_revision=0,value='x'*16385),413)
            fixture.call('state.transaction',dict(namespace='qa.state',writes=[dict(key='duplicate',expected_revision=0,value=1)]*2),400)
            assert state('list',limit=1)['has_more']
            assert fixture.call('state.get',{'namespace':'qa.other','key':'counter'})['revision']==0
            fixture.stop();fixture.start()
            assert state('get',key='counter')==dict(key='counter',revision=recreated,exists=True,value=False)
            original_request=dict(namespace='qa.state',key='once',expected_revision=0,value={'a':1,'b':2},request_id='once-1')
            admitted=fixture.call('state.put',original_request)
            assert fixture.call('state.put',dict(request_id='once-1',value={'b':2,'a':1},expected_revision=0,key='once',namespace='qa.state'))==admitted
            fixture.call('state.put',{**original_request,'value':3},409)
            fixture.stop();fixture.start()
            assert fixture.call('state.put',original_request)==admitted
            assert fixture.call('state.receipt.get',{'namespace':'qa.state','request_id':'once-1'})['result']==admitted
            files=list((fixture.home/'data/application/state').glob('*.json'));assert len(files)==1
            original=files[0].read_bytes();files[0].write_bytes(original.replace(b'false',b'true',1))
            assert fixture.call('state.get',{'namespace':'qa.state','key':'counter'},422)['code']=='sdk_state_corrupt'
            fixture.call('state.put',dict(namespace='qa.state',key='counter',expected_revision=recreated,value='overwrite'),422)
            assert files[0].read_bytes()!=original
            files[0].write_bytes(original)


        finally:
            fixture.stop()
    print('PASS orchestration identity/outcomes, paged events, atomic CAS/transactions, tombstones, concurrent claims and corrupt/restart recovery')


if __name__=='__main__':
    main()
