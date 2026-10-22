#include "internal.h"
#include "../../include/mdo/time.h"
/* Bounded byte streams provide backpressure in both the in-process and RPC
 * transports. Handles are random per-generation identities, never pointers. */
typedef struct SdkStream {
    char Id[37];unsigned char* Bytes;size_t Capacity,Start,Size;
    uint64 Expires;bool Closed;
} SdkStream;
static struct {xmutex* Lock;SdkStream Slots[64];bool Stopping;} SdkStreams;
static const cstr SdkStreamMethods[]={"stream.open","stream.read","stream.write","stream.close","stream.release","stream.stat"};
bool MdoApplicationStreamsInit(void){SdkStreams.Lock=xrtMutexCreate();return SdkStreams.Lock!=NULL;}
void MdoApplicationStreamsStop(void){if(SdkStreams.Lock){xrtMutexLock(SdkStreams.Lock);SdkStreams.Stopping=true;xrtMutexUnlock(SdkStreams.Lock);}}
static void SdkStreamFree(SdkStream* S){if(S->Bytes)xrtSecureZero(S->Bytes,S->Capacity);xrtFree(S->Bytes);memset(S,0,sizeof(*S));}
void MdoApplicationStreamsUnit(void){if(!SdkStreams.Lock)return;for(size_t i=0;i<64;i++)SdkStreamFree(&SdkStreams.Slots[i]);xrtMutexDestroy(SdkStreams.Lock);memset(&SdkStreams,0,sizeof(SdkStreams));}
xvalue* MdoApplicationStreamsCatalog(void){xvalue* A=xrtValueArray();for(size_t i=0;i<sizeof(SdkStreamMethods)/sizeof(*SdkStreamMethods);i++){xvalue* V=xrtValueObject();SdkPut(V,"id",SdkStreamMethods[i]);SdkPut(V,"domain","stream");SdkPut(V,"transport","native");xrtValueArrayAppendNew(A,V);}return A;}
static xvalue* SdkStreamInfo(const SdkStream* S){xvalue* R=xrtValueObject();SdkPut(R,"handle",S->Id);SdkPutUInt(R,"buffered",S->Size);SdkPutUInt(R,"capacity",S->Capacity);xrtValueObjectSetNew(R,XRT_STR_LITERAL("closed"),xrtValueBool(S->Closed));xrtValueObjectSetNew(R,XRT_STR_LITERAL("eof"),xrtValueBool(S->Closed&&!S->Size));return R;}
bool MdoApplicationStreamsCall(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E,bool* Found){
    *Found=false;for(size_t i=0;i<sizeof(SdkStreamMethods)/sizeof(*SdkStreamMethods);i++)if(!strcmp(Method,SdkStreamMethods[i]))*Found=true;
    if(!*Found)return true;
    if(!SdkStreams.Lock)return MdoApplicationError(E,"sdk_unavailable","Streams unavailable","stream",503,false);
    cstr Id=SdkField(P,"handle");bool Open=!strcmp(Method,"stream.open"),Read=!strcmp(Method,"stream.read"),Write=!strcmp(Method,"stream.write");
    uint64 Limit=16384,Timeout=0,Capacity=65536;
    if((SdkGet(P,"limit")&&!SdkUInt(SdkGet(P,"limit"),&Limit))||!Limit||Limit>16384||
        (SdkGet(P,"timeout_ms")&&!SdkUInt(SdkGet(P,"timeout_ms"),&Timeout))||Timeout>60000||
        (SdkGet(P,"capacity")&&!SdkUInt(SdkGet(P,"capacity"),&Capacity))||!Capacity||Capacity>1024u*1024u)
        return MdoApplicationError(E,"sdk_arguments_invalid","Invalid stream capacity, chunk limit or timeout","stream",400,false);
    unsigned char* Input=NULL;size_t Bytes=0;if(Write&&!SdkInputBytes(P,&Input,&Bytes,E))return false;
    uint64 End=MdoDeadlineAfterUs(Timeout*1000);bool Ok=false;
    for(;;){
        xrtMutexLock(SdkStreams.Lock);SdkStream* S=NULL;
        if(SdkStreams.Stopping){MdoApplicationError(E,"sdk_stopping","Application is stopping","stream",503,false);xrtMutexUnlock(SdkStreams.Lock);break;}
        for(size_t i=0;i<64;i++){
            SdkStream* Candidate=&SdkStreams.Slots[i];if(Candidate->Id[0]&&MdoDeadlineExpired(Candidate->Expires))SdkStreamFree(Candidate);
            if((Open&&!Candidate->Id[0]&&!S)||(!Open&&Id&&!strcmp(Id,Candidate->Id)))S=Candidate;
        }
        if(!S){MdoApplicationError(E,Open?"sdk_stream_limit":"sdk_handle_expired",Open?"Release or drain retained streams":"Stream handle is invalid or expired","stream",Open?429:410,false);xrtMutexUnlock(SdkStreams.Lock);break;}
        if(Open){S->Bytes=xrtMalloc((size_t)Capacity);if(!S->Bytes||!SdkRandomId("str-",S->Id)){SdkStreamFree(S);xrtMutexUnlock(SdkStreams.Lock);break;}S->Capacity=(size_t)Capacity;}
        S->Expires=MdoDeadlineAfterUs(300000000);
        if(Write&&(S->Closed||Bytes>S->Capacity)){MdoApplicationError(E,S->Closed?"sdk_stream_closed":"sdk_chunk_limit",S->Closed?"The stream writer is closed":"Chunk exceeds stream capacity","stream",409,false);xrtMutexUnlock(SdkStreams.Lock);break;}
        bool Ready=Read?(S->Size||S->Closed):Write?S->Capacity-S->Size>=Bytes:true;
        if(Ready){
            if(Read){size_t Count=S->Size<(size_t)Limit?S->Size:(size_t)Limit;unsigned char Chunk[16384];
                for(size_t i=0;i<Count;i++){size_t At=(S->Start+i)%S->Capacity;Chunk[i]=S->Bytes[At];S->Bytes[At]=0;}S->Start=(S->Start+Count)%S->Capacity;S->Size-=Count;
                *R=SdkStreamInfo(S);SdkByteValue(*R,Chunk,Count);xrtSecureZero(Chunk,Count);
            }else if(Write){for(size_t i=0;i<Bytes;i++)S->Bytes[(S->Start+S->Size+i)%S->Capacity]=Input[i];S->Size+=Bytes;*R=SdkStreamInfo(S);SdkPutUInt(*R,"written",Bytes);}
            else if(!strcmp(Method,"stream.release")){SdkStreamFree(S);*R=xrtValueObject();}
            else {if(!strcmp(Method,"stream.close"))S->Closed=true;*R=SdkStreamInfo(S);}
            Ok=*R!=NULL;xrtMutexUnlock(SdkStreams.Lock);break;
        }
        xrtMutexUnlock(SdkStreams.Lock);
        if(!Timeout||MdoDeadlineExpired(End)){MdoApplicationError(E,"sdk_stream_would_block","No bytes were read or written before the wait expired","stream",409,false);break;}
        if(SdkCancelled(C)){MdoApplicationError(E,"sdk_cancelled","Stream wait cancelled","stream",499,false);break;}
        xrtSleep(10);
    }
    if(Input)xrtSecureZero(Input,Bytes);
    xrtFree(Input);return Ok;
}
