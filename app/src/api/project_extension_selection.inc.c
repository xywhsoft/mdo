#include "skill_scope.inc.h"
#include "../../include/mdo/scoped_files.h"

/* A project selects installed Home extensions. Only boolean preferences live
 * in the workspace; no second executable or document installation is made. */
static bool MdoProjectExtensionSelection(MdoApiContext* C,cstr Kind){
    xhttpmethod Method=C->Request->head->MethodCode;bool Write=Method!=XHTTP_METHOD_GET;
    if(Method!=XHTTP_METHOD_GET&&Method!=XHTTP_METHOD_POST&&Method!=XHTTP_METHOD_DELETE)return MdoApiReplyError(C,405,"extension_project_selection_only","Edit extension files in Home; projects only select their activation",NULL);
    if(C->ParamCount>1&&!xrtStrEqual(C->Params[1],XRT_STR_LITERAL("enabled")))return MdoApiReplyError(C,404,"extension_project_action","Unknown project selection action",NULL);
    char Id[97]={0};if(C->ParamCount){if(C->Params[0].Size>96)return MdoApiReplyError(C,400,"extension_project_id","Invalid directory identity",NULL);memcpy(Id,C->Params[0].Data,C->Params[0].Size);if(!MdoExtensionDirectoryId(Id))return MdoApiReplyError(C,400,"extension_project_id","Invalid directory identity",NULL);}
    if(Write&&!Id[0])return MdoApiReplyError(C,400,"extension_project_id","Choose an installed Home extension",NULL);
    char* Workspace=NULL;if(!MdoApiSkillScopeMode(C,&Workspace,true))return true;if(!Workspace)return MdoApiReplyError(C,400,"extension_project_required","A project ID is required",NULL);
    char Project[65]={0};size_t N=0;xrtPercentDecode(xrtStrViewN(C->Target.Query.Data+8,C->Target.Query.Size-8),Project,64,&N);Project[N]=0;xwork_error Failure;
    MdoProjectLease* Lease=MdoProjectLeaseAcquire(Project,MDO_PROJECT_LEASE_SHARED,&Failure);xroot Root=Lease?xrtRootOpen(Workspace):NULL;xrtFree(Workspace);if(!Root){MdoProjectLeaseRelease(Lease);return MdoApiReplyError(C,409,"extension_project_busy","Project directory is busy or unavailable",NULL);}
    xrtMutexLock(g_MdoExtensionLock);char Path[80],LockPath[80];snprintf(Path,sizeof(Path),".mdo/%s.json",Kind);snprintf(LockPath,sizeof(LockPath),".mdo/.%s.lock",Kind);xfile Lock=Write?MdoScopedLock(Root,LockPath):NULL;
    xfileoptions O;xrtFileOptionsInit(&O);O.Flags=XFILE_READ|XFILE_NOFOLLOW;xfile F=xrtRootFileOpen(Root,Path,&O);bool Missing=!F&&CommandMissing();char* Raw=CommandReadFile(F);bool Ok=(Raw||Missing)&&(!Write||Lock);xvalue* Settings=Raw?xrtJsonParse(xrtStrView(Raw)):!strcmp(Kind,"commands")?SdkParse("{\"schema_version\":1,\"disabled\":{}}"):xrtValueObject();
    xvalue* Flags=!strcmp(Kind,"commands")?xrtValueObjectGet(Settings,XRT_STR_LITERAL("disabled")):Settings;Ok=Ok&&(!Raw||strlen(Raw)<=65536)&&xrtValueType(Settings)==XVALUE_OBJECT&&xrtValueType(Flags)==XVALUE_OBJECT&&xrtValueCount(Flags)<=8192;
    if(Ok&&!strcmp(Kind,"commands")){uint64 Schema=0;Ok=xrtValueCount(Settings)==2&&CommandUInt(xrtValueObjectGet(Settings,XRT_STR_LITERAL("schema_version")),&Schema)&&Schema==1;}
    for(size_t I=0;Ok&&I<xrtValueCount(Flags);I++){xstrview K={0};const xvalue* V=xrtValueObjectAt(Flags,I,&K);bool B;Ok=K.Size&&K.Size<=64&&!memchr(K.Data,0,K.Size)&&MdoExtensionAlias(K.Data)&&xrtValueGetBool(V,&B);}
    char Error[512]={0},Revision[65]="new";bool Preferences=Ok;if(!Ok)snprintf(Error,sizeof(Error),"Project preferences are unreadable, invalid or locked; no extension files were changed");if(Raw)Ok=Ok&&MdoExtensionHash(Raw,Revision);xvalue* Rows=Ok?MdoExtensionsList(Kind,Error):NULL,*Items=xrtValueArray(),*Data=xrtValueObject();Ok=Ok&&Rows&&Items&&Data;bool Found=!Id[0]||!Preferences,Replied=false;
    for(size_t I=0;Ok&&I<xrtValueCount(Rows);I++){
        const xvalue* Item=xrtValueArrayGet(Rows,I);cstr Physical=MdoExtensionText(Item,"id",96),Alias=MdoExtensionText(Item,"alias",64);if(Id[0]&&(!Physical||strcmp(Id,Physical)))continue;if(!Alias&&!Id[0])continue;Found=true;if(!Alias){Ok=false;snprintf(Error,sizeof(Error),"Repair this extension's Home manifest before selecting it for a project");break;}
        if(Write){
            if(!ExtensionMatch(C,Raw)){Replied=true;Ok=false;break;}
            if(Method==XHTTP_METHOD_DELETE)xrtValueObjectRemove(Flags,xrtStrView(Alias));
            else{MdoApiJsonBody Body={0};MdoApiBodyStatus Read=MdoApiJsonBodyRead(C,&Body);bool Enabled=false;Ok=Read==MDO_API_BODY_OK&&xrtValueType(Body.Value)==XVALUE_OBJECT&&xrtValueCount(Body.Value)==1&&xrtValueGetBool(xrtValueObjectGet(Body.Value,XRT_STR_LITERAL("enabled")),&Enabled)&&xrtValueObjectSetNew(Flags,xrtStrView(Alias),xrtValueBool(!strcmp(Kind,"commands")?!Enabled:Enabled));MdoApiJsonBodyUnit(&Body);}
            char* Json=Ok?xrtJsonStringify(Settings,false,NULL):NULL;Ok=Ok&&Json&&strlen(Json)<=65536&&MdoScopedWrite(Root,Path,Json)&&MdoExtensionHash(Json,Revision);xrtFree(Json);
        }
        xvalue* Copy=Ok?xrtValueClone(Item):NULL;bool Selected=true,Global=false;xrtValueGetBool(xrtValueObjectGet(Item,XRT_STR_LITERAL("enabled")),&Global);const xvalue* Flag=xrtValueObjectGet(Flags,xrtStrView(Alias));if(Flag){bool B=false;Ok=Ok&&xrtValueGetBool(Flag,&B);Selected=!strcmp(Kind,"commands")?!B:B;}
        Ok=Ok&&Copy&&MdoApiValueSetString(Copy,"revision",Revision)&&MdoApiValueSetString(Copy,"scope","project")&&MdoApiValueSetString(Copy,"project",Project)&&MdoApiValueSetBool(Copy,"project_enabled",Selected)&&MdoApiValueSetBool(Copy,"enabled",Global&&Selected)&&MdoDirectoryApiItemFlags(Copy)&&xrtValueArrayAppend(Items,Copy);xrtValueRelease(Copy);
    }
    if(Ok&&Id[0]){xrtValueRelease(Data);Data=Found?xrtValueClone(xrtValueArrayGet(Items,0)):NULL;Ok=Data!=NULL;}else if(Ok)Ok=xrtValueObjectSet(Data,XRT_STR_LITERAL("items"),Items)&&MdoApiValueSetString(Data,"revision",Revision);
    xrtValueRelease(Settings);xrtValueRelease(Rows);xrtValueRelease(Items);xrtFree(Raw);if(Lock)xrtClose(Lock);xrtRootClose(Root);xrtMutexUnlock(g_MdoExtensionLock);MdoProjectLeaseRelease(Lease);
    if(Replied){xrtValueRelease(Data);return true;}if(!Ok){xrtValueRelease(Data);return MdoApiReplyError(C,Found?409:404,"extension_project_selection_failed",Error[0]?Error:Found?"Project preferences are invalid or changed; refresh before retrying":"Extension is not installed in Home",NULL);}return MdoApiReplySuccessTake(C,200,Data,NULL);
}
