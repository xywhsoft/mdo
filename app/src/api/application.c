#include "internal.h"
#include "../../include/mdo/projects.h"
bool MdoApiApplicationRoute(MdoApiContext* C){
    bool Services=xrtStrEqual(C->Target.Path,XRT_STR_LITERAL("/api/v1/application/services"));
    if(Services)return MdoApiReplySuccessTake(C,200,MdoApplicationCatalog(),NULL);
    if(C->Request->head->MethodCode==XHTTP_METHOD_GET){
        uint64 After=0;xvalue* P=xrtValueObject();
        xstrview Query=C->Target.Query;bool Valid=!Query.Size;
        if(Query.Size>6&&Query.Size<=26&&!memcmp(Query.Data,"after=",6)){
            Valid=true;for(size_t i=6;i<Query.Size;i++){unsigned char Ch=(unsigned char)Query.Data[i];
                if(Ch<'0'||Ch>'9'||After>(UINT64_MAX-(Ch-'0'))/10){Valid=false;break;}After=After*10+(Ch-'0');}
        }
        if(!Valid){xrtValueRelease(P);return MdoApiReplyError(C,400,"sdk_arguments_invalid","after must be an unsigned sequence",NULL);}
        SdkPutUInt(P,"after",After);mdo_error_v1 E={0};xvalue* R=MdoApplicationEvents(P,&E);xrtValueRelease(P);
        return R?MdoApiReplySuccessTake(C,200,R,NULL):MdoApiReplyError(C,(uint16)E.Status,E.Code,E.Message,NULL);
    }
    MdoApiJsonBody Body;MdoApiBodyStatus Status=MdoApiJsonBodyRead(C,&Body);if(Status!=MDO_API_BODY_OK)return MdoApiReplyBodyError(C,Status);
    cstr Method=SdkField(Body.Value,"method");const xvalue* Scope=SdkGet(Body.Value,"context");
    MdoApplicationInvocation I={0};mdo_sdk_context_v1 Native;
    const xhttpfield* Token=NULL;
    if(xrtHttpFieldGetUnique(C->Request->head->Fields,C->Request->head->FieldCount,XRT_STR_LITERAL("X-Mdo-Write-Token"),&Token)==XHTTP_NEXT_ITEM&&Token)
        snprintf(I.WriteToken,sizeof(I.WriteToken),"%.*s",(int)Token->Value.Size,Token->Value.Data);
    cstr Extension=SdkField(Scope,"extension");if(Extension&&!SdkIdValid(Extension)){MdoApiJsonBodyUnit(&Body);return MdoApiReplyError(C,400,"sdk_context_invalid","Invalid extension identity",NULL);}
    cstr Project=SdkField(Scope,"project");char* Workspace=NULL;
    if(!Project||!Project[0]||!strcmp(Project,"default"))Workspace=MdoHomeDefaultWorkspacePath(false);
    else {MdoProjectInfo Info={0};Info.Size=sizeof(Info);bool Found=false;xwork_error Error;
        if(!MdoProjectGet(Project,&Info,&Found,&Error)||!Found){MdoApiJsonBodyUnit(&Body);return MdoApiReplyError(C,404,"sdk_project_missing","The selected project is unavailable",NULL);}
        Workspace=xrtPathAbs(Info.WorkspaceRoot);
    }
    MdoApplicationContext(&Native,&I,Extension?Extension:"local-client",C->RequestId,Project,SdkField(Scope,"session"),SdkField(Scope,"run"),Workspace,0);
    Native.Origin=MDO_ORIGIN_USER;
    xvalue* R=NULL;mdo_error_v1 E={0};mdo_result Code=MdoApplicationCall(C->ApplicationContext?C->ApplicationContext:&Native,Method,SdkGet(Body.Value,"params"),&R,&E);
    MdoApiJsonBodyUnit(&Body);
    xrtFree(Workspace);
    if(Code==MDO_RESULT_OK)return MdoApiReplySuccessTake(C,200,R?R:xrtValueObject(),NULL);
    xvalue* Details=xrtValueObject();SdkPut(Details,"stage",E.Stage);
    SdkPut(Details,"cause",E.Cause);
    xrtValueObjectSetNew(Details,XRT_STR_LITERAL("retryable"),xrtValueBool(E.Retryable));
    xrtValueObjectSetNew(Details,XRT_STR_LITERAL("may_have_completed"),xrtValueBool(E.MayHaveCompleted));
    if(R)xrtValueObjectSetNew(Details,XRT_STR_LITERAL("result"),R);
    return MdoApiReplyErrorDetailsTake(C,(uint16)(E.Status?E.Status:500),E.Code,E.Message,Details);
}
