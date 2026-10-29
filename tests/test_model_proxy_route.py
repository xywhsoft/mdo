"""Run the production Agent route against controllable transport/client boundaries."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SHIM = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t uint32; typedef uint64_t uint64; typedef int MdoModelProtocol;
typedef int xllm_result; typedef int xllm_request; typedef int xllm_response;
typedef int xllm_stream_callbacks; typedef int MdoAgentModelProgress;
typedef struct { int eCode; char sMessage[256]; } xllm_error;
typedef struct { uint64 Revision; bool Destroyed; char Kind[16]; } xllm_client;
typedef struct { void (*Callback)(void*); void* Data; } MdoModelRetryObserver;
typedef struct { const char *ModelId,*ReasoningEffort; int Protocol;
    uint32 MaxOutputTokens; bool RestartableStream; MdoModelRetryObserver* RetryObserver; } MdoModelClientOptions;
typedef struct { uint32 Size;uint64 Revision;char ProxyKind[16]; } MdoConfigTransportSettings;
typedef struct MdoAgentOwner { void *Models,*RouteLock,*ExternalModelData;
    xllm_result (*ExternalComplete)(void*,const xllm_request*,const xllm_stream_callbacks*,xllm_response**,xllm_error*); } MdoAgentOwner;
#define XLLM_RESULT_ERROR -1
#define XLLM_ERROR_HOOK 1
static uint64 Revision=1; static const char* Mode="none";
static unsigned Created,Destroyed,Completed,Locks;static bool Available=true,Online,LockReady=true,CreateFails,ModelSystem;
static xllm_client Clients[32];
static void MdoAgentsRetryProgress(void* P){(void)P;}
static void MdoAgentsProgressSnapshot(MdoAgentOwner* O,MdoAgentModelProgress* P){(void)O;(void)P;}
static bool MdoModelIsOnline(void* M,const char* Id){(void)M;(void)Id;return Online;}
static bool MdoModelUsesSystemProxy(void* M,const char* Id){(void)M;(void)Id;return ModelSystem||!strcmp(Mode,"system");}
static void MdoModelClientOptionsInit(MdoModelClientOptions* O){memset(O,0,sizeof(*O));}
static bool MdoConfigGetTransportSettings(MdoConfigTransportSettings* T)
{T->Revision=Revision;snprintf(T->ProxyKind,sizeof(T->ProxyKind),"%s",Mode);return Available;}
static bool xrtMutexLock(void* Lock){assert(Lock);if(!LockReady)return false;Locks++;return true;}
static bool xrtMutexUnlock(void* Lock){assert(Lock&&Locks);Locks--;return true;}
static void xllmErrorInit(xllm_error* E){memset(E,0,sizeof(*E));}
static xllm_client* MdoModelClientCreate(void* M,const MdoModelClientOptions* O,void* Info,xllm_error* E)
{(void)M;(void)Info;(void)E;assert(!strcmp(O->ModelId,"fixture"));
 if(!Available||CreateFails)return NULL;
 assert(Created<32);xllm_client* C=&Clients[Created++];
 C->Revision=Revision;snprintf(C->Kind,sizeof(C->Kind),"%s",Mode);return C;}
static void xllmClientDestroy(xllm_client* C){assert(C&&!C->Destroyed);C->Destroyed=true;Destroyed++;}
static xllm_result MdoModelCompleteRestartableObserved(xllm_client* C,const xllm_request* R,
 const xllm_stream_callbacks* CB,xllm_response** Response,xllm_error* E,const MdoModelRetryObserver* O)
{(void)R;(void)CB;(void)Response;(void)E;(void)O;assert(!Locks&&!C->Destroyed);
 assert(C->Revision==Revision&&!strcmp(C->Kind,Mode));Completed++;return 0;}
static xllm_result MdoModelOnlineComplete(void* M,const MdoModelClientOptions* O,const xllm_request* R,
 const xllm_stream_callbacks* CB,xllm_response** Response,xllm_error* E)
{(void)M;(void)R;(void)CB;(void)Response;(void)E;assert(O->RestartableStream&&O->RetryObserver);return 7;}
'''
CASES = r'''
int main(void)
{
    MdoAgentOwner Owner={0};Owner.RouteLock=&Owner;
    MdoAgentRoute Route={0};Route.ModelId="fixture";
    xllm_error Error={0};xllm_request Request=0;xllm_response* Response=NULL;
    assert(MdoAgentsRouteComplete(&Owner,&Route,&Request,NULL,&Response,&Error)==0);
    xllm_client* Cached=Route.Client;assert(Cached&&Created==1&&Destroyed==0);
    assert(MdoAgentsRouteComplete(&Owner,&Route,&Request,NULL,&Response,&Error)==0);
    assert(Created==1&&Destroyed==0);
    Mode="system";Revision++;
    assert(MdoAgentsRouteComplete(&Owner,&Route,&Request,NULL,&Response,&Error)==0);
    assert(MdoAgentsRouteComplete(&Owner,&Route,&Request,NULL,&Response,&Error)==0);
    assert(Created==3&&Destroyed==2&&Route.Client==Cached&&!Cached->Destroyed);
    Mode="socks5";Revision++;
    assert(MdoAgentsRouteComplete(&Owner,&Route,&Request,NULL,&Response,&Error)==0);
    Mode="none";Revision++;
    assert(MdoAgentsRouteComplete(&Owner,&Route,&Request,NULL,&Response,&Error)==0);
    assert(Created==5&&Destroyed==4&&!Cached->Destroyed);
    Mode="system";Revision++;CreateFails=true;
    assert(MdoAgentsRouteComplete(&Owner,&Route,&Request,NULL,&Response,&Error)==XLLM_RESULT_ERROR);
    assert(Created==5&&Completed==6&&!Cached->Destroyed);CreateFails=false;
    LockReady=false;
    assert(MdoAgentsRouteComplete(&Owner,&Route,&Request,NULL,&Response,&Error)==XLLM_RESULT_ERROR);
    LockReady=true;Available=false;
    assert(MdoAgentsRouteComplete(&Owner,&Route,&Request,NULL,&Response,&Error)==XLLM_RESULT_ERROR);
    Available=true;Mode="none";Revision++;ModelSystem=true;
    assert(MdoAgentsRouteComplete(&Owner,&Route,&Request,NULL,&Response,&Error)==0);
    assert(MdoAgentsRouteComplete(&Owner,&Route,&Request,NULL,&Response,&Error)==0);
    assert(Created==7&&Destroyed==6&&Completed==8&&!Cached->Destroyed);
    ModelSystem=false;Online=true;
    assert(MdoAgentsRouteComplete(&Owner,&Route,&Request,NULL,&Response,&Error)==7);
    assert(Created==7&&Destroyed==6&&!Locks);
    xllmClientDestroy(Cached);assert(Created==Destroyed);
    puts("Agent proxy refresh, mode switches and shared-client lifetime: PASS");return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=shutil.which('gcc') or r'E:\software\w64devkit\bin\gcc.exe')
    args = parser.parse_args()
    source = (ROOT/'app/src/agents/runtime.c').read_text(encoding='utf-8')
    route = source[source.index('typedef struct MdoAgentRoute {'):source.index('} MdoAgentRoute;')+len('} MdoAgentRoute;')]
    function = source[source.index('static xllm_result MdoAgentsRouteComplete('):source.index('static xllm_result MdoAgentsComplete(')]
    with tempfile.TemporaryDirectory(prefix='model-proxy-route-', dir=ROOT/'.build') as raw:
        base = Path(raw)
        probe = base/'probe.c';probe.write_text(SHIM+route+'\n'+function+CASES, encoding='utf-8')
        binary = base/('probe.exe' if os.name=='nt' else 'probe')
        subprocess.run([args.cc,'-std=gnu11','-Wall','-Wextra','-Werror',str(probe),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True,timeout=10)


if __name__ == '__main__':
    main()
