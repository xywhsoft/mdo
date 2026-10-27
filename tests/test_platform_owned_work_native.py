"""The platform's owned CPU/file scope releases and reacquires the host lock."""
import argparse
import json
from pathlib import Path
import shutil
import uuid
from native_product_runner import product

ROOT=Path(__file__).resolve().parents[1]
PLATFORM=Path('D:/GIT/home/host/xywhsoft_ai')
PRELUDE=r'''
typedef void* XAdminPluginHandle;
typedef struct QaHostRequest {bool deferred,owned_work,replied;void* raw;} *XS_RequestObject;
typedef int (*XAdminOwnedWorkProc)(void*);
typedef struct PluginInstance {bool started,stopping;size_t activeIo;} PluginInstance;
static PluginInstance G_Plugins[1];static size_t G_PluginCount=1;static xmutex* G_RequestLock;
'''
CONTRACT=r'''
typedef struct QaWorkState {XS_RequestObject Request;bool OtherThread,Guard;} QaWorkState;
static int32 QaLockProbe(void* Data){bool* Result=Data;*Result=xrtMutexTryLock(G_RequestLock);if(*Result)xrtMutexUnlock(G_RequestLock);return 0;}
static int QaWork(void* Data){QaWorkState* S=Data;int Out=0;S->Guard=XAdmin_RunOwnedWork(&G_Plugins[0],S->Request,QaWork,Data,&Out)==-1;
 xthread* Thread=xrtThreadCreate(QaLockProbe,&S->OtherThread,0);if(Thread){xrtThreadWait(Thread);xrtThreadDestroy(Thread);}return 37;}
static void QaFlag(xvalue* R,cstr K,bool V){xrtValueObjectSetNew(R,xrtStrView(K),xrtValueBool(V));}
xvalue* QaProductContract(const xvalue* P,xvfs V){
 (void)P;(void)V;xvalue* R=xrtValueObject();G_RequestLock=xrtMutexCreate();G_Plugins[0].started=true;G_Plugins[0].activeIo=1;struct QaHostRequest Request={0};Request.deferred=true;Request.raw=&Request;QaWorkState State={0};State.Request=&Request;int Result=0;
 xrtMutexLock(G_RequestLock);bool Ran=XAdmin_RunOwnedWork(&G_Plugins[0],&Request,QaWork,&State,&Result)==0;
 QaFlag(R,"other_thread_can_enter_during_owned_work",Ran&&State.OtherThread);QaFlag(R,"nested_scope_rejected",State.Guard);QaFlag(R,"result_and_request_scope_restored",Result==37&&!Request.owned_work);
 bool Acquired=true;xthread* Thread=xrtThreadCreate(QaLockProbe,&Acquired,0);if(Thread){xrtThreadWait(Thread);xrtThreadDestroy(Thread);}QaFlag(R,"host_lock_owned_again_before_return",Thread&&!Acquired);
 Request.deferred=false;bool Guard=XAdmin_RunOwnedWork(&G_Plugins[0],&Request,QaWork,&State,&Result)==-1;Request.deferred=true;G_Plugins[0].activeIo=0;Guard=Guard&&XAdmin_RunOwnedWork(&G_Plugins[0],&Request,QaWork,&State,&Result)==-1;G_Plugins[0].activeIo=1;G_Plugins[0].stopping=true;Guard=Guard&&XAdmin_RunOwnedWork(&G_Plugins[0],&Request,QaWork,&State,&Result)==-1;G_Plugins[0].stopping=false;Request.replied=true;Guard=Guard&&XAdmin_RunOwnedWork(&G_Plugins[0],&Request,QaWork,&State,&Result)==-1;QaFlag(R,"unowned_stopping_and_replied_requests_rejected",Guard);
 xrtMutexUnlock(G_RequestLock);xrtMutexDestroy(G_RequestLock);G_RequestLock=NULL;return R;
}
'''

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--url',required=True);p.add_argument('--home',type=Path,required=True);p.add_argument('--project',default='default');a=p.parse_args()
    out=ROOT/'.build/platform-owned-work-native';out.mkdir(parents=True,exist_ok=True);fixture=out/('home-'+uuid.uuid4().hex[:12]);fixture.mkdir()
    try:
        result=product(a,fixture,PRELUDE+(PLATFORM/'src/net/plugin_owned_work.c').read_text(encoding='utf-8')+CONTRACT,out/'result.json')
        assert len(result)==5 and all(v is True for v in result.values()),result
        print(json.dumps(result,ensure_ascii=False))
    finally:
        if fixture.resolve().parent==out.resolve() and fixture.name.startswith('home-'):shutil.rmtree(fixture)

if __name__=='__main__':main()
