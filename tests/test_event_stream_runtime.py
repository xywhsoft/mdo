"""Complete text events persist once; unfinished previews stay in memory."""
import argparse, json, os, shutil, subprocess, tempfile, time
from test_api_runtime import ROOT, free_port

SOURCE = r'''
#define ServiceInit QaBaseServiceInit
#define ServiceUnit QaBaseServiceUnit
#include "generated/mdo_unity.c"
#undef ServiceInit
#undef ServiceUnit
static unsigned Previews, Completed, Invalid; static size_t ReasonBytes, TextBytes;
static void Watch(cstr P,cstr S,xstrview Json,bool Changed,void* Data){
 (void)Changed;(void)Data;MdoSessionEventOwned E;
 if(!MdoEventsParse(P,S,Json,&E)){++Invalid;return;}
 if(E.Info.StreamVolatile)++Previews;
 else if(E.Info.StreamFirstId){xvalue* Api=NULL;xstrview T;bool Full=MdoApiSessionEventValue(&E.Info,P,S,true,&Api)&&xrtValueGetString(xrtValueObjectGet(Api,XRT_STR_LITERAL("text")),&T)&&T.Size==strlen(E.Text);if(!Full)++Invalid;xrtValueRelease(Api);++Completed;if(E.Info.Kind==XWORK_EVENT_MODEL_REASONING_DELTA)ReasonBytes+=strlen(E.Text);else TextBytes+=strlen(E.Text);}
 MdoEventsOwnedUnit(&E);
}
static uint64 FileSize(MdoSessionEventBridge* B){xfileinfo I;bool Exists=false;return MdoHomeExternalStat(B->Path,&Exists,&I)&&Exists?I.Size:0;}
static bool Emit(MdoSessionEventBridge* B,xwork_event* E){bool Ok=MdoEventsStreamDelta(E)||MdoEventsStreamFinish(B,E);return Ok&&MdoEventsStreamAppend(B,E);}
void ServiceInit(XS_HostInfo* Host){
 (void)Host;bool Ok=MdoHomeInit();g_MdoSessions.Lock=xrtMutexCreate();MdoSessionsObserve(Watch,NULL);
 MdoSessionEventBridge B={0};strcpy(B.ProjectId,"default");strcpy(B.SessionId,"0123456789abcdef0123456789abcdef");MdoEventsPath(B.Path,B.ProjectId,B.SessionId);B.NextEventId=1;strcpy(B.ModelId,"fixture");B.ContextWindowTokens=1024;
 uint64 Next=0;Ok=Ok&&MdoSessionsInternalEventNext(B.ProjectId,B.SessionId,0,&Next)&&Next==1;
 xwork_event E={0};E.uAgentId=1;E.uRunId=1;E.uAgentTurn=1;E.iTextLength=SIZE_MAX;E.eKind=XWORK_EVENT_MODEL_START;E.sText="start";Ok=Ok&&Emit(&B,&E);uint64 Initial=FileSize(&B);
 E.eKind=XWORK_EVENT_MODEL_REASONING_DELTA;E.sText="思";
 for(unsigned I=0;Ok&&I<10000;++I){E.uEventId++;Ok=Emit(&B,&E);}
 Ok=Ok&&FileSize(&B)==Initial&&Completed==0;
 E.eKind=XWORK_EVENT_MODEL_TEXT_DELTA;E.sText="好";Ok=Ok&&Emit(&B,&E)&&Completed==0&&ReasonBytes==0;
 uint64 ReasonDone=FileSize(&B);
 for(unsigned I=0;Ok&&I<9999;++I)Ok=Emit(&B,&E);
 Ok=Ok&&FileSize(&B)==ReasonDone;
 /* A child's start and deltas cannot finish its parent's active reply. */
 E.uAgentId=2;E.uAgentDepth=1;E.eKind=XWORK_EVENT_AGENT_START;E.sText="child";Ok=Ok&&Emit(&B,&E)&&Completed==0;
 E.eKind=XWORK_EVENT_MODEL_TEXT_DELTA;E.sText="child";Ok=Ok&&Emit(&B,&E);
 E.eKind=XWORK_EVENT_MODEL_DONE;E.sText="";Ok=Ok&&Emit(&B,&E)&&Completed==1&&TextBytes==5;
 E.uAgentId=1;E.uAgentDepth=0;E.eKind=XWORK_EVENT_MODEL_DONE;Ok=Ok&&Emit(&B,&E)&&Completed==3&&TextBytes==30005&&ReasonBytes==30000;
 /* Oversized UTF-8/control text is split after completion without loss. */
 E.uAgentTurn=2;E.eKind=XWORK_EVENT_MODEL_TEXT_DELTA;E.sText="\n";
 for(unsigned I=0;Ok&&I<100000;++I){Ok=Emit(&B,&E);if(!Ok)printf("failed_delta=%u\n",I);}
 uint64 BeforeDone=FileSize(&B);Ok=Ok&&Completed==3;
 E.eKind=XWORK_EVENT_AGENT_DONE;E.sText="";bool Done=Emit(&B,&E);Ok=Ok&&Done&&TextBytes==130005&&FileSize(&B)>BeforeDone;
 /* Owner loss deliberately discards the current unfinished event. */
 E.eKind=XWORK_EVENT_MODEL_REASONING_DELTA;E.sText="unfinished";Ok=Ok&&Emit(&B,&E);uint64 BeforeDrop=FileSize(&B);MdoEventsStreamDiscard(&B);Ok=Ok&&FileSize(&B)==BeforeDrop&&B.StreamBytes==0;
 Ok=Ok&&MdoSessionsInternalEventNext(B.ProjectId,B.SessionId,0,&Next)&&Next==B.NextEventId;
 Ok=Ok&&!Invalid&&Previews>0;
 printf("stream_complete=%u previews=%u completed=%u reasoning_bytes=%zu text_bytes=%zu invalid=%u probe_done=1\n",Ok,Previews,Completed,ReasonBytes,TextBytes,Invalid);fflush(stdout);
 MdoSessionsObserve(NULL,NULL);MdoSessionManagerUnit();MdoHomeUnit();
}
void ServiceUnit(XS_HostInfo* Host){(void)Host;}

'''


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--host',required=True)
    host=parser.parse_args().host
    with tempfile.TemporaryDirectory(dir=ROOT/'.build',prefix='stream-events-') as raw:
        from pathlib import Path
        site=Path(raw);shutil.copytree(ROOT/'app',site,dirs_exist_ok=True)
        (site/'probe.c').write_text(SOURCE,encoding='utf-8')
        (site/'xs.json').write_text(json.dumps(dict(services=[dict(enabled=True,**{'class':'http'},name='context',ip='127.0.0.1',port=free_port(),host_default=dict(enabled=True,path='web',devlang='c',devfile='probe.c'))])),encoding='utf-8')
        with (site/'host.log').open('wb') as log:
            process=subprocess.Popen([str(Path(host).resolve()),'xs.json'],cwd=site,stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            try:
                deadline=time.monotonic()+20
                while time.monotonic()<deadline:
                    output=(site/'host.log').read_text(encoding='utf-8',errors='replace')
                    if 'probe_done=1' in output or process.poll() is not None:break
                    time.sleep(.05)
                assert 'stream_complete=1' in output and 'probe_done=1' in output,output
            finally:
                process.terminate()
                try:process.wait(timeout=10)
                except subprocess.TimeoutExpired:process.kill();process.wait()
    print('PASS native completion-only stream: 120000 fragments, UTF-8/control text equality, no disk writes during events, interleaved child, completion and discarded unfinished tail')


if __name__=='__main__':main()
