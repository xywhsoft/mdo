#include "internal.h"
#include "skill_scope.inc.h"
#include "../../include/mdo/commands.h"
#include "../../include/mdo/scoped_files.h"

bool MdoApiCommandsRoute(MdoApiContext* C) {
    char* Workspace=NULL;if(!MdoApiSkillScopeMode(C,&Workspace,false))return false;
    xvalue* Catalog=MdoCommandCatalog(Workspace);xrtFree(Workspace);
    bool Known=false;xvalue* Records=ExtensionStoreRead(&Known);xvalueiter It={0};xvaluekey Key;xvalue* Item;
    if(xrtValueIterBegin(Catalog,&It)){while((Item=xrtValueIterNext(&It,&Key))){
        cstr Scope=MdoPackageText(Item,"scope",16),Path=MdoPackageText(Item,"path",255);
        if(Scope&&!strcmp(Scope,"user")&&Path)ExtensionStoreInfo(Item,Path,Records,Known,false);
    }xrtValueIterEnd(&It);}xrtValueRelease(Records);
    return Catalog?MdoApiReplySuccessTake(C,200,Catalog,NULL):
        MdoApiReplyError(C,503,"commands_unavailable","Command catalog is unavailable",NULL);
}
bool MdoApiCommandInvocationRoute(MdoApiContext* C) {
    if(C->ParamCount){
        char Id[37];xstrview P=C->Params[0];
        if(P.Size!=36)return MdoApiReplyError(C,400,"invalid_command_id","Invalid command invocation ID",NULL);
        memcpy(Id,P.Data,P.Size);Id[P.Size]=0;
        if(C->Request->head->MethodCode==XHTTP_METHOD_DELETE)(void)MdoCommandCancel(Id);
        xvalue* V=MdoCommandGet(Id);
        return V?MdoApiReplySuccessTake(C,200,V,NULL):MdoApiReplyError(C,404,"command_missing","Command invocation was not found",NULL);
    }
    MdoApiJsonBody Body;MdoApiBodyStatus Status=MdoApiJsonBodyRead(C,&Body);
    if(Status!=MDO_API_BODY_OK)return MdoApiReplyBodyError(C,Status);
    char* Workspace=NULL;char Project[65]="default";bool Ok=true;
    MdoApiContext Scope=*C;if(!Scope.Target.Query.Size)Scope.Target.Query=XRT_STR_LITERAL("project=default");
    if(!MdoApiSkillScopeMode(&Scope,&Workspace,false)){MdoApiJsonBodyUnit(&Body);return false;}
    size_t N=0;Ok=xrtPercentDecode(xrtStrViewN(Scope.Target.Query.Data+8,Scope.Target.Query.Size-8),Project,64,&N);
    Project[N]=0;
    const char* Fields[]={"invocation_id","command_id","raw_arguments","session_id","run_id","revision"};
    Ok=Ok&&xrtValueType(Body.Value)==XVALUE_OBJECT;
    for(size_t i=0;Ok&&i<xrtValueCount(Body.Value);i++){
        xstrview Key={0};xrtValueObjectAt(Body.Value,i,&Key);bool Known=false;
        for(size_t j=0;j<6;j++)if(xrtStrEqual(Key,xrtStrView(Fields[j])))Known=true;
        if(!Known)Ok=false;
    }
    xvalue* Input=xrtValueObject();
    for(size_t i=0;Ok&&i<6;i++){
        cstr T=CommandText(Body.Value,Fields[i]);
        if(!T&&xrtValueObjectHas(Body.Value,xrtStrView(Fields[i])))Ok=false;
        size_t Limit=i==2?MDO_COMMAND_ARGS_LIMIT:i==0?36:i==4?48:64;
        Ok=Ok&&(!T||strlen(T)<=Limit)&&CommandSet(Input,Fields[i],T);
    }
    char Error[512]={0};cstr Raw=CommandText(Input,"raw_arguments");
    xvalue* Arguments=Ok?MdoCommandArguments(Raw,Error):NULL;
    Ok=Ok&&Arguments&&CommandSet(Input,"project_id",Project);
    if(Ok)Ok=xrtValueObjectSetNew(Input,XRT_STR_LITERAL("call_origin"),xrtValueUInt(C->ApplicationContext?mdoCallOrigin(C->ApplicationContext):MDO_ORIGIN_USER));
    if(Ok){Ok=xrtValueObjectSetNew(Input,XRT_STR_LITERAL("arguments"),Arguments);Arguments=NULL;}
    xrtValueRelease(Arguments);
    MdoApiJsonBodyUnit(&Body);xvalue* Result=Ok?MdoCommandStart(Input,Workspace,Error):NULL;
    xrtValueRelease(Input);xrtFree(Workspace);
    if(!Result)return MdoApiReplyError(C,409,"command_rejected",Error[0]?Error:"Invalid command request",NULL);
    return MdoApiReplySuccessTake(C,202,Result,NULL);
}

bool MdoApiCommandProjectRoute(MdoApiContext* C){return MdoProjectExtensionSelection(C,"commands");}
