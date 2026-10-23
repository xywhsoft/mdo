#include "internal.h"
#include "../../include/mdo/time.h"
typedef struct SdkClient {char* Id;xvalue* Scope;uint64 Expires;} SdkClient;
typedef struct SdkUiAction {char Id[37];char* Client;xvalue* Value;uint64 Expires;bool Terminal;} SdkUiAction;
static struct {xmutex* Lock;SdkClient Clients[32];SdkUiAction Actions[128];} SdkUi;
static const cstr SdkUiMethods[]={"client.hello","client.poll","client.ack","ui.clients","ui.invoke","ui.result"};
bool MdoApplicationUiInit(void){SdkUi.Lock=xrtMutexCreate();return SdkUi.Lock!=NULL;}
static void SdkUiActionFree(SdkUiAction* A){xrtFree(A->Client);xrtValueRelease(A->Value);memset(A,0,sizeof(*A));}
void MdoApplicationUiUnit(void){if(!SdkUi.Lock)return;for(size_t i=0;i<32;i++){xrtFree(SdkUi.Clients[i].Id);xrtValueRelease(SdkUi.Clients[i].Scope);}for(size_t i=0;i<128;i++)SdkUiActionFree(&SdkUi.Actions[i]);xrtMutexDestroy(SdkUi.Lock);memset(&SdkUi,0,sizeof(SdkUi));}
xvalue* MdoApplicationUiCatalog(void){xvalue* A=xrtValueArray();for(size_t i=0;i<sizeof(SdkUiMethods)/sizeof(*SdkUiMethods);i++){xvalue* V=xrtValueObject();SdkPut(V,"id",SdkUiMethods[i]);SdkPut(V,"domain","ui");SdkPut(V,"transport","native");xrtValueArrayAppendNew(A,V);}return A;}
bool MdoApplicationUiCall(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E,bool* Found){
    *Found=false;for(size_t i=0;i<sizeof(SdkUiMethods)/sizeof(*SdkUiMethods);i++)if(!strcmp(Method,SdkUiMethods[i]))*Found=true;if(!*Found)return true;
    if(!SdkUi.Lock)return MdoApplicationError(E,"sdk_unavailable","UI bridge unavailable","ui",503,false);
    cstr ClientId=SdkField(P,"client"),Id=SdkField(P,"id");
    xrtMutexLock(SdkUi.Lock);SdkClient* Client=NULL;SdkUiAction* Action=NULL;
    for(size_t i=0;i<32;i++)if(ClientId&&SdkUi.Clients[i].Id&&!strcmp(ClientId,SdkUi.Clients[i].Id))Client=&SdkUi.Clients[i];
    for(size_t i=0;i<128;i++){SdkUiAction* A=&SdkUi.Actions[i];if(A->Id[0]&&MdoDeadlineExpired(A->Expires))SdkUiActionFree(A);if(Id&&!strcmp(Id,A->Id))Action=A;}
    bool Ok=true;
    if(!strcmp(Method,"ui.clients")){
        *R=xrtValueArray();for(size_t i=0;i<32;i++)if(SdkUi.Clients[i].Id&&!MdoDeadlineExpired(SdkUi.Clients[i].Expires)){xvalue* V=xrtValueObject();SdkPut(V,"id",SdkUi.Clients[i].Id);xrtValueObjectSetNew(V,XRT_STR_LITERAL("context"),xrtValueClone(SdkUi.Clients[i].Scope));xrtValueArrayAppendNew(*R,V);}
    }else if(!strcmp(Method,"client.hello")){
        if(!SdkIdValid(ClientId)){Ok=MdoApplicationError(E,"sdk_arguments_invalid","A stable client identity is required","ui",400,false);goto done;}
        if(!Client)for(size_t i=0;i<32;i++)if(!SdkUi.Clients[i].Id||MdoDeadlineExpired(SdkUi.Clients[i].Expires)){Client=&SdkUi.Clients[i];xrtFree(Client->Id);Client->Id=xrtStrDup(ClientId);break;}
        if(!Client){Ok=MdoApplicationError(E,"sdk_client_limit","Too many live UI clients","ui",429,false);goto done;}
        xrtValueRelease(Client->Scope);Client->Scope=SdkGet(P,"context")?xrtValueClone(SdkGet(P,"context")):xrtValueObject();Client->Expires=MdoDeadlineAfterUs(45000000);*R=xrtValueObject();SdkPut(*R,"id",ClientId);
    }else if(!strcmp(Method,"ui.invoke")){
        cstr Type=SdkField(P,"action");const xvalue* Params=SdkGet(P,"params");
        MdoApplicationInvocation* Invocation=C->Opaque;
        if(!ClientId&&Invocation&&Invocation->Actions){
            if(!SdkIdValid(Type)||xrtValueCount(Invocation->Actions)>=16){Ok=MdoApplicationError(E,"sdk_ui_action_invalid","Invalid action or command action limit","ui",400,false);goto done;}
            xvalue* V=xrtValueObject();SdkPut(V,"type",Type);if(Params)xrtValueObjectSetNew(V,XRT_STR_LITERAL("params"),xrtValueClone(Params));Ok=xrtValueArrayAppendNew(Invocation->Actions,V);*R=xrtValueObject();SdkPut(*R,"state","queued_in_command");goto done;
        }
        if(!Client||MdoDeadlineExpired(Client->Expires)){Ok=MdoApplicationError(E,"sdk_client_missing","Select an active client from ui.clients; the SDK never guesses a window","ui",404,false);goto done;}
        if(!SdkIdValid(Type)){Ok=MdoApplicationError(E,"sdk_arguments_invalid","A UI action name is required","ui",400,false);goto done;}
        char* Json=Params?SdkJson(Params):NULL;if(Json&&strlen(Json)>16384){xrtFree(Json);Ok=MdoApplicationError(E,"sdk_ui_action_limit","UI arguments exceed 16 KiB","ui",413,false);goto done;}xrtFree(Json);
        Action=NULL;for(size_t i=0;i<128;i++)if(!SdkUi.Actions[i].Id[0]){Action=&SdkUi.Actions[i];break;}
        if(!Action){Ok=MdoApplicationError(E,"sdk_ui_action_limit","UI action receipts are full; wait for their lease to expire","ui",429,false);goto done;}
        if(!SdkRandomId("uia-",Action->Id)){Ok=false;goto done;}Action->Client=xrtStrDup(ClientId);Action->Value=xrtValueObject();Action->Expires=MdoDeadlineAfterUs(600000000);
        SdkPut(Action->Value,"id",Action->Id);SdkPut(Action->Value,"action",Type);SdkPut(Action->Value,"state","pending");
        xrtValueObjectSetNew(Action->Value,XRT_STR_LITERAL("params"),Params?xrtValueClone(Params):xrtValueObject());
        xvalue* Scope=SdkGet(P,"context")?xrtValueClone(SdkGet(P,"context")):xrtValueObject();
        if(!SdkGet(Scope,"project")&&C->ProjectId&&C->ProjectId[0])SdkPut(Scope,"project",C->ProjectId);
        if(!SdkGet(Scope,"session")&&C->SessionId&&C->SessionId[0])SdkPut(Scope,"session",C->SessionId);
        xrtValueObjectSetNew(Action->Value,XRT_STR_LITERAL("context"),Scope);*R=xrtValueClone(Action->Value);
    }else if(!strcmp(Method,"ui.result")){
        if(!Action){Ok=MdoApplicationError(E,"sdk_ui_receipt_expired","UI result is not retained; inspect the client before repeating effects","ui",410,true);goto done;}*R=xrtValueClone(Action->Value);
    }else{
        if(!Client||MdoDeadlineExpired(Client->Expires)){Ok=MdoApplicationError(E,"sdk_client_missing","Register or renew the client lease first","ui",404,false);goto done;}
        Client->Expires=MdoDeadlineAfterUs(45000000);
        if(!strcmp(Method,"client.poll")){*R=xrtValueArray();for(size_t i=0;i<128;i++)if(SdkUi.Actions[i].Id[0]&&!SdkUi.Actions[i].Terminal&&!strcmp(SdkUi.Actions[i].Client,ClientId))xrtValueArrayAppendNew(*R,xrtValueClone(SdkUi.Actions[i].Value));}
        else {if(!Action||strcmp(Action->Client,ClientId)){Ok=MdoApplicationError(E,"sdk_ui_receipt_expired","Action does not belong to this client","ui",410,false);goto done;}
            if(!Action->Terminal){
                const xvalue* Result=SdkGet(P,"result");char* Json=Result?SdkJson(Result):NULL;
                if(Json&&strlen(Json)>1048576){xrtFree(Json);Ok=MdoApplicationError(E,"sdk_ui_result_limit","UI result exceeds 1 MiB; use a stream for larger results","ui",413,false);goto done;}
                xrtFree(Json);if(Result)xrtValueObjectSet(Action->Value,XRT_STR_LITERAL("result"),Result);
                bool Success=false;xrtValueGetBool(SdkGet(P,"success"),&Success);SdkPut(Action->Value,"state",Success?"succeeded":"failed");if(SdkField(P,"error"))SdkPut(Action->Value,"error",SdkField(P,"error"));Action->Terminal=true;}
            *R=xrtValueClone(Action->Value);}
    }
done:xrtMutexUnlock(SdkUi.Lock);return Ok;
}
