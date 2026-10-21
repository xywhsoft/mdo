#include "skill_scope.inc.h"
#include "../../include/mdo/project_lifecycle.h"

static const char* MdoSkillProviders[]={"agents","claude","codex","zcode","opencode","mdo"};

static char* SkillSourceRoot(cstr Origin,cstr Workspace)
{
    if(!strcmp(Origin,"project"))return Workspace?xrtPathReal(Workspace):NULL;
    if(strcmp(Origin,"user"))return NULL;
    char* Root=xrtEnvGet("USERPROFILE");
    if(!Root || !Root[0]){xrtFree(Root);Root=xrtEnvGet("HOME");}
    char* Real=Root&&strlen(Root)<=4096?xrtPathReal(Root):NULL;xrtFree(Root);return Real;
}

static MdoSkillCatalog* SkillSourceCatalog(cstr Origin,cstr Provider,cstr Workspace)
{
    if(!strcmp(Origin,"personal")&&!strcmp(Provider,"mdo"))return MdoSkillCatalogSnapshot();
    bool Known=false;
    for(size_t i=0;i<sizeof(MdoSkillProviders)/sizeof(*MdoSkillProviders);i++)
        if(!strcmp(Provider,MdoSkillProviders[i]))Known=true;
    if(!Known)return NULL;
    char* Root=SkillSourceRoot(Origin,Workspace);
    char Relative[80];snprintf(Relative,sizeof(Relative),".%s/skills",Provider);
    MdoSkillCatalog* Catalog=Root?MdoSkillCatalogDirectory(Root,Relative):NULL;
    xrtFree(Root);return Catalog;
}

bool MdoApiSkillSourcesRoute(MdoApiContext* C)
{
    char* Workspace=NULL;if(!MdoApiSkillScope(C,&Workspace))return true;
    xvalue *Data=xrtValueObject(),*Items=xrtValueArray(),*Issues=xrtValueArray();
    bool Ok=Data&&Items&&Issues,Truncated=false;const char* Origins[]={"personal","project","user"};
    for(size_t o=0;Ok&&o<3;o++)for(size_t p=0;Ok&&p<6;p++) {
        if(o==0&&p!=5)continue;
        if(o==1&&!Workspace)continue;
        MdoSkillCatalog* Catalog=SkillSourceCatalog(Origins[o],MdoSkillProviders[p],Workspace);
        if(!Catalog) {
            xvalue* Issue=xrtValueObject();Ok=Issue&&MdoApiValueSetString(Issue,"origin",Origins[o])&&
                MdoApiValueSetString(Issue,"provider",MdoSkillProviders[p])&&
                MdoApiValueSetString(Issue,"message","Source directory is unavailable or exceeds limits")&&xrtValueArrayAppend(Issues,Issue);
            xrtValueRelease(Issue);xrtClearError();continue;
        }
        for(size_t i=0;Ok&&i<MdoSkillCatalogCount(Catalog);i++) {
            if(xrtValueCount(Items)>=128){Truncated=true;break;}
            MdoSkillInfo Info={0};Info.Size=sizeof(Info);xvalue* Item=xrtValueObject();
            Ok=Item&&MdoSkillCatalogAt(Catalog,i,&Info)&&xrtValueCount(Items)<256&&
                MdoApiValueSetString(Item,"id",Info.Id)&&MdoApiValueSetString(Item,"name",Info.Name)&&
                MdoApiValueSetString(Item,"origin",Origins[o])&&MdoApiValueSetString(Item,"provider",MdoSkillProviders[p])&&
                MdoApiValueSetString(Item,"source_path",Info.SourcePath)&&
                MdoApiValueSetString(Item,"revision",Info.MetadataHash)&&
                MdoApiValueSetUInt(Item,"resource_count",Info.ResourceCount)&&xrtValueArrayAppend(Items,Item);
            xrtValueRelease(Item);
        }
        MdoSkillDiagnostics* Ds=MdoSkillCatalogDiagnostics(Catalog);
        for(size_t i=0;Ok&&i<MdoSkillDiagnosticsCount(Ds);i++) {
            if(xrtValueCount(Issues)>=128){Truncated=true;break;}
            MdoSkillDiagnosticInfo Info={0};Info.Size=sizeof(Info);xvalue* Issue=xrtValueObject();
            Ok=Issue&&MdoSkillDiagnosticsAt(Ds,i,&Info)&&MdoApiValueSetString(Issue,"source_path",Info.SourcePath)&&
                MdoApiValueSetString(Issue,"message",Info.Message)&&xrtValueArrayAppend(Issues,Issue);xrtValueRelease(Issue);
        }
        MdoSkillDiagnosticsRelease(Ds);MdoSkillCatalogRelease(Catalog);
    }
    char* UserDir=MdoHomeExternalPath("skills");char* ProjectDir=Workspace?xrtPathJoin(Workspace,".mdo/skills"):NULL;
    if(Ok)Ok=MdoApiValueSetString(Data,"user_directory",UserDir?UserDir:"")&&MdoApiValueSetString(Data,"project_directory",ProjectDir?ProjectDir:"");
    xrtFree(UserDir);xrtFree(ProjectDir);xrtFree(Workspace);
    if(Ok)Ok=MdoApiValueSetBool(Data,"truncated",Truncated)&&xrtValueObjectSet(Data,XRT_STR_LITERAL("items"),Items)&&xrtValueObjectSet(Data,XRT_STR_LITERAL("diagnostics"),Issues);
    xrtValueRelease(Items);xrtValueRelease(Issues);
    if(!Ok){xrtValueRelease(Data);return MdoApiReplyError(C,422,"skill_sources_limit","Skill sources exceed the discovery limit",NULL);}
    return MdoApiReplySuccessTake(C,200,Data,NULL);
}

static bool SkillParents(xroot Root,cstr Path)
{
    char Copy[1400];if(strlen(Path)>=sizeof(Copy))return false;strcpy(Copy,Path);
    for(char* P=Copy;*P;P++)if(*P=='/') {
        *P=0;xfileinfo Info={0};
        if(!xrtRootStat(Root,Copy,false,&Info)) {
            if(!xrtGetError()||xrtErrorKind(xrtGetError())!=XERR_NOT_FOUND)return false;
            xrtClearError();if(!xrtRootDirCreate(Root,Copy,0700u))return false;
        } else if(Info.Type!=XFILE_TYPE_DIRECTORY)return false;
        *P='/';
    }
    return true;
}

static bool SkillNonce(char Out[33])
{
    unsigned char Bytes[16];if(!xrtSecureRandom(Bytes,sizeof(Bytes)))return false;
    for(size_t i=0;i<16;i++)snprintf(Out+i*2,3,"%02x",Bytes[i]);
    return true;
}

static char* SkillRead(xroot Root,cstr Path,bool* Missing)
{
    *Missing=false;xfileoptions Options;xrtFileOptionsInit(&Options);Options.Flags=XFILE_READ|XFILE_NOFOLLOW;
    xfile File=xrtRootFileOpen(Root,Path,&Options);
    if(!File){if(xrtGetError()&&xrtErrorKind(xrtGetError())==XERR_NOT_FOUND){*Missing=true;xrtClearError();}return NULL;}
    uint64 Size=0;size_t Got=0;char* Text=NULL;
    bool Ok=xrtFileSize(File,&Size)&&Size<=MDO_SKILL_TEXT_LIMIT;
    if(Ok)Text=xrtMalloc((size_t)Size+1);
    Ok=Ok&&Text&&xrtReadAtFull(File,0,Text,(size_t)Size,&Got)&&Got==Size;
    if(Ok){Text[Got]=0;Ok=!memchr(Text,0,Got)&&xrtUtf8Valid(xrtStrViewN(Text,Got),NULL);}
    if(!xrtClose(File))Ok=false;
    if(!Ok){xrtFree(Text);return NULL;}return Text;
}

/* Publish an owned temporary file only after flushing. Rooted opens reject
 * links; the parent is validated before the same-volume native rename. */
static bool SkillWrite(xroot Root,cstr Path,const void* Bytes,size_t Size)
{
    char Nonce[33],Temp[1450];if(!SkillNonce(Nonce)||!SkillParents(Root,Path))return false;
    snprintf(Temp,sizeof(Temp),"%s.%s.tmp",Path,Nonce);
    xfileoptions Options;xrtFileOptionsInit(&Options);Options.Flags=XFILE_WRITE|XFILE_CREATE|XFILE_EXCLUSIVE|XFILE_NOFOLLOW|XFILE_SYNC;Options.Mode=0600;
    xfile File=xrtRootFileOpen(Root,Temp,&Options);if(!File)return false;
    bool Ok=xrtWriteFull(File,Bytes,Size,NULL)&&xrtFlush(File);
    if(!xrtClose(File))Ok=false;
    char* From=xrtPathJoin(xrtRootPath(Root),Temp),*To=xrtPathJoin(xrtRootPath(Root),Path);
    if(Ok)Ok=From&&To&&xrtPathRename(From,To,true);
    if(!Ok)xrtRootRemove(Root,Temp);
    xrtFree(From);xrtFree(To);return Ok;
}

static xfile SkillWriterLock(xroot Root)
{
    if(!SkillParents(Root,".mdo/.skills.lock"))return NULL;
    xfileoptions Options;xrtFileOptionsInit(&Options);Options.Flags=XFILE_READ|XFILE_WRITE|XFILE_CREATE|XFILE_NOFOLLOW;
    xfile Lock=xrtRootFileOpen(Root,".mdo/.skills.lock",&Options);
    if(Lock&&!xrtFileLock(Lock,XFILE_LOCK_EXCLUSIVE,false)){xrtClose(Lock);return NULL;}
    return Lock;
}

static bool SkillCopy(MdoSkillCatalog* Source,cstr Id,cstr RootPath,cstr Directory,cstr Target)
{
    char* Document=MdoSkillCatalogDocument(Source,Id);MdoSkillInfo Info={0};Info.Size=sizeof(Info);
    xroot Root=xrtRootOpen(RootPath);char Stage[256]={0},Final[256],Path[1400],Nonce[33];bool Created=false;
    snprintf(Final,sizeof(Final),"%s/%s",Directory,Target);
    xfileinfo Existing={0};bool Exists=Root&&xrtRootStat(Root,Final,false,&Existing);
    bool Missing=!Exists&&xrtGetError()&&xrtErrorKind(xrtGetError())==XERR_NOT_FOUND;
    xrtClearError();bool Ok=Root&&Document&&Missing&&MdoSkillCatalogFind(Source,Id,&Info)&&SkillNonce(Nonce);
    if(Ok){snprintf(Stage,sizeof(Stage),"%s/.import-%s",Directory,Nonce);snprintf(Path,sizeof(Path),"%s/SKILL.md",Stage);Ok=SkillWrite(Root,Path,Document,strlen(Document));Created=true;}
    for(size_t i=0;Ok&&i<Info.ResourceCount;i++) {
        MdoSkillResourceInfo Resource={0};Resource.Size=sizeof(Resource);MdoSkillResourceContent Content={0};Content.Size=sizeof(Content);
        Ok=MdoSkillCatalogResourceAt(Source,Id,i,&Resource)&&MdoSkillCatalogLoadResource(Source,Id,Resource.Path,MDO_SKILL_FILE_LIMIT,&Content);
        if(Ok){snprintf(Path,sizeof(Path),"%s/%s",Stage,Resource.Path);Ok=SkillWrite(Root,Path,Content.Data,Content.Bytes);}
#if !defined(_WIN32) && !defined(_WIN64)
        if(Ok&&Resource.Kind==MDO_SKILL_RESOURCE_SCRIPT)Ok=xrtRootSetMode(Root,Path,false,0700);
#endif
        MdoSkillResourceContentUnit(&Content);
    }
    /* Validate the whole staged Skill before it is discoverable. */
    if(Ok){MdoSkillCatalog* Check=MdoSkillCatalogSingle(RootPath,Stage,Target);Ok=Check!=NULL;MdoSkillCatalogRelease(Check);
        char* Before=xrtPathJoin(RootPath,Stage),*After=xrtPathJoin(RootPath,Final);
        Ok=Ok&&Before&&After&&xrtPathRename(Before,After,false);xrtFree(Before);xrtFree(After);
        if(Ok)Created=false;
    }
    if(Created){size_t Count=0;ExtensionTreeRemove(Root,Stage,0,&Count);}
    if(Root)xrtRootClose(Root);
    xrtFree(Document);return Ok;
}

bool MdoApiSkillMigrateRoute(MdoApiContext* C)
{
    char* Workspace=NULL;if(!MdoApiSkillScope(C,&Workspace))return true;
    MdoApiJsonBody Body={0};MdoApiBodyStatus Status=MdoApiJsonBodyRead(C,&Body);
    if(Status!=MDO_API_BODY_OK){xrtFree(Workspace);return MdoApiReplyBodyError(C,Status);}
    xstrview Origin={0},Provider={0},Id={0},Target={0},Scope={0};
    bool Ok=ExtensionString(Body.Value,"origin",&Origin)&&ExtensionString(Body.Value,"provider",&Provider)&&
        ExtensionString(Body.Value,"id",&Id)&&ExtensionString(Body.Value,"target_id",&Target)&&
        ExtensionString(Body.Value,"scope",&Scope)&&MdoExtensionIdValid(Target.Data)&&Target.Data[0]>='a'&&Target.Data[0]<='z'&&
        Id.Size>0&&Id.Size<=192&&(!strcmp(Scope.Data,"user")||(!strcmp(Scope.Data,"project")&&Workspace));
    MdoProjectLease* Lease=NULL;xwork_error Error;
    if(Ok&&Workspace){char Project[65];size_t N=0;xrtPercentDecode(xrtStrViewN(C->Target.Query.Data+8,C->Target.Query.Size-8),Project,64,&N);Project[N]=0;
        Lease=MdoProjectLeaseAcquire(Project,MDO_PROJECT_LEASE_SHARED,&Error);Ok=Lease!=NULL;}
    xrtMutexLock(g_MdoExtensionLock);
    MdoSkillCatalog* Source=Ok?SkillSourceCatalog(Origin.Data,Provider.Data,Workspace):NULL;
    char* Root=Ok?(!strcmp(Scope.Data,"user")?MdoHomeExternalPath("skills"):xrtStrDup(Workspace)):NULL;
    if(Ok&&!strcmp(Scope.Data,"user")) {
        xroot Skills=MdoHomeOpenStorageDirectory("skills");Ok=Skills&&Root;
        if(Skills)xrtRootClose(Skills);
        char* Parent=Root?xrtPathParent(Root):NULL;xrtFree(Root);Root=Parent;
    }
    xfile Writer=NULL;
    if(Ok&&Root&&!strcmp(Scope.Data,"project")) {
        xroot Anchor=xrtRootOpen(Root);Writer=Anchor?SkillWriterLock(Anchor):NULL;
        if(Anchor)xrtRootClose(Anchor);
        Ok=Writer!=NULL;
    }
    Ok=Ok&&Source&&Root&&SkillCopy(Source,Id.Data,Root,!strcmp(Scope.Data,"user")?"skills":".mdo/skills",Target.Data);
    if(Ok&&!strcmp(Scope.Data,"user")&&!MdoSkillManagerReload()) {
        xroot Anchor=xrtRootOpen(Root);char TargetPath[160];size_t Count=0;
        snprintf(TargetPath,sizeof(TargetPath),"skills/%s",Target.Data);
        bool Restored=Anchor&&ExtensionTreeRemove(Anchor,TargetPath,0,&Count);
        if(Anchor)xrtRootClose(Anchor);
        if(!Restored||!MdoSkillManagerReload())MdoHomeRequireRestart("Skill import rollback failed; restart before further edits");
        Ok=false;
    }
    if(Writer)xrtClose(Writer);
    xrtFree(Root);MdoSkillCatalogRelease(Source);xrtMutexUnlock(g_MdoExtensionLock);
    MdoProjectLeaseRelease(Lease);xrtFree(Workspace);MdoApiJsonBodyUnit(&Body);
    return Ok?MdoApiReplySuccessTake(C,200,xrtValueObject(),NULL):MdoApiReplyError(C,409,"skill_migration_failed",
        "Skill could not be copied. Target must be new; check source diagnostics, limits, disk space and project availability. Original files are preserved.",NULL);
}

static xvalue* SkillProjectItem(xroot Root,cstr Id)
{
    char Path[200],Directory[160],Hash[65];bool Missing=false;
    snprintf(Directory,sizeof(Directory),".mdo/skills/%s",Id);snprintf(Path,sizeof(Path),"%s/SKILL.md",Directory);
    char* Document=SkillRead(Root,Path,&Missing);if(!Document)return NULL;
    MdoSkillCatalog* Catalog=MdoSkillCatalogSingle(xrtRootPath(Root),Directory,Id);
    MdoSkillInfo Info={0};Info.Size=sizeof(Info);bool Valid=Catalog&&MdoSkillCatalogFind(Catalog,Id,&Info);
    bool Enabled=true;char* Settings=SkillRead(Root,".mdo/skills.json",&Missing);
    xvalue* Config=Settings?xrtJsonParse(xrtStrView(Settings)):NULL;
    const xvalue* Flag=xrtValueObjectGet(Config,xrtStrView(Id));
    bool Ok=(Settings?(Config&&xrtValueType(Config)==XVALUE_OBJECT):Missing)&&(!Flag||xrtValueGetBool(Flag,&Enabled));
    xvalue* Item=xrtValueObject();
    Ok=Ok&&Item&&MdoExtensionHash(Document,Hash)&&MdoApiValueSetString(Item,"id",Id)&&
        MdoApiValueSetString(Item,"name",Valid?Info.Name:Id)&&MdoApiValueSetString(Item,"path",Path)&&
        MdoApiValueSetString(Item,"content",Document)&&MdoApiValueSetString(Item,"revision",Hash)&&
        MdoApiValueSetBool(Item,"enabled",Enabled)&&MdoApiValueSetBool(Item,"valid",Valid);
    MdoSkillCatalogRelease(Catalog);xrtValueRelease(Config);xrtFree(Settings);xrtFree(Document);
    if(!Ok){xrtValueRelease(Item);return NULL;}return Item;
}

static bool SkillProjectList(MdoApiContext* C,xroot Root)
{
    xvalue* Data=xrtValueObject(),*Items=xrtValueArray();bool Ok=Data&&Items;
    xdir Dir=xrtRootDirOpen(Root,".mdo/skills",XDIR_STAT);xdirentry Item;xdirnext Next=XDIR_NEXT_END;
    if(!Dir){Ok=Ok&&xrtGetError()&&xrtErrorKind(xrtGetError())==XERR_NOT_FOUND;xrtClearError();}
    while(Ok&&Dir&&(Next=xrtDirNext(Dir,&Item))==XDIR_NEXT_ITEM) {
        if(!(Item.Flags&XDIR_ENTRY_UTF8)||Item.Info.Type!=XFILE_TYPE_DIRECTORY)continue;
        char* Id=xrtStrDupN(Item.Name.Data,Item.Name.Size);
        if(Id&&MdoExtensionIdValid(Id)) {
            char Path[200];snprintf(Path,sizeof(Path),".mdo/skills/%s/SKILL.md",Id);
            xvalue* Value=SkillProjectItem(Root,Id);
            if(!Value){Value=xrtValueObject();MdoApiValueSetString(Value,"id",Id);MdoApiValueSetString(Value,"path",Path);MdoApiValueSetBool(Value,"valid",false);}
            xrtValueObjectRemove(Value,XRT_STR_LITERAL("content"));
            Ok=xrtValueCount(Items)<256&&Value&&xrtValueArrayAppend(Items,Value);xrtValueRelease(Value);
        }
        xrtFree(Id);
    }
    if(Next==XDIR_NEXT_ERROR)Ok=false;
    if(Dir&&!xrtDirClose(Dir))Ok=false;
    if(Ok)Ok=xrtValueObjectSet(Data,XRT_STR_LITERAL("items"),Items);
    xrtValueRelease(Items);
    if(!Ok){xrtValueRelease(Data);return MdoApiReplyError(C,503,"skill_project_unreadable","Project Skill directory cannot be read",NULL);}
    return MdoApiReplySuccessTake(C,200,Data,NULL);
}

static bool SkillProjectEdit(MdoApiContext* C,xroot Root,cstr Id)
{
    char Directory[160],Path[200],Error[1024]={0};bool Missing=false;
    snprintf(Directory,sizeof(Directory),".mdo/skills/%s",Id);snprintf(Path,sizeof(Path),"%s/SKILL.md",Directory);
    char* Old=SkillRead(Root,Path,&Missing);
    if(!Old&&!Missing)return MdoApiReplyError(C,503,"skill_unreadable","Existing Skill cannot be read safely",NULL);
    if(!ExtensionMatch(C,Old)){xrtFree(Old);return true;}
    if(C->Request->head->MethodCode==XHTTP_METHOD_DELETE) {
        char Nonce[33],Trash[180];bool Ok=Old&&SkillNonce(Nonce);
        if(Ok){snprintf(Trash,sizeof(Trash),".mdo/skill-trash/%s-%s",Id,Nonce);Ok=SkillParents(Root,Trash);}
        char* From=Ok?xrtPathJoin(xrtRootPath(Root),Directory):NULL,*To=Ok?xrtPathJoin(xrtRootPath(Root),Trash):NULL;
        Ok=Ok&&From&&To&&xrtPathRename(From,To,false);xrtFree(From);xrtFree(To);xrtFree(Old);
        xvalue* Data=Ok?xrtValueObject():NULL;
        if(Ok)Ok=Data&&MdoApiValueSetString(Data,"recovery_path",Trash);
        if(!Ok){xrtValueRelease(Data);return MdoApiReplyError(C,409,"skill_remove_failed","Skill could not be moved to the project recovery directory",NULL);}
        return MdoApiReplySuccessTake(C,200,Data,NULL);
    }
    MdoApiJsonBody Body={0};MdoApiBodyStatus Status=MdoApiJsonBodyRead(C,&Body);
    if(Status!=MDO_API_BODY_OK){xrtFree(Old);return MdoApiReplyBodyError(C,Status);}
    bool Ok=false;int Code=422;
    if(C->Request->head->MethodCode==XHTTP_METHOD_POST) {
        bool Enabled=false;
        if(Old&&xrtValueGetBool(xrtValueObjectGet(Body.Value,XRT_STR_LITERAL("enabled")),&Enabled)) {
            char* Current=SkillRead(Root,".mdo/skills.json",&Missing);
            xvalue* Config=Current?xrtJsonParse(xrtStrView(Current)):Missing?xrtValueObject():NULL;
            char* Json=NULL;
            if(Config&&xrtValueType(Config)==XVALUE_OBJECT&&xrtValueObjectSetNew(Config,xrtStrView(Id),xrtValueBool(Enabled)))Json=xrtJsonStringify(Config,true,NULL);
            Ok=Json&&strlen(Json)<=65536&&SkillWrite(Root,".mdo/skills.json",Json,strlen(Json));
            xrtFree(Json);xrtValueRelease(Config);xrtFree(Current);
        }
    } else {
        xstrview Content={0};
        if(ExtensionString(Body.Value,"content",&Content)&&Content.Size<=MDO_SKILL_TEXT_LIMIT&&
            MdoSkillValidateText(Content.Data,Error,sizeof(Error))) {
            if(Old) {
                Ok=SkillWrite(Root,Path,Content.Data,Content.Size);
                MdoSkillCatalog* Check=Ok?MdoSkillCatalogSingle(xrtRootPath(Root),Directory,Id):NULL;
                if(Ok&&!Check){Ok=false;snprintf(Error,sizeof(Error),"Skill resources are missing or invalid; previous document restored");
                    if(!SkillWrite(Root,Path,Old,strlen(Old))){Code=503;snprintf(Error,sizeof(Error),"Rollback failed; restore this Skill from your project version control before using it");}}
                MdoSkillCatalogRelease(Check);
            } else {
                char Nonce[33],Stage[160],Temp[200];bool Created=false;
                Ok=SkillNonce(Nonce);
                if(Ok){snprintf(Stage,sizeof(Stage),".mdo/skills/.create-%s",Nonce);snprintf(Temp,sizeof(Temp),"%s/SKILL.md",Stage);
                    Ok=SkillWrite(Root,Temp,Content.Data,Content.Size);Created=true;}
                MdoSkillCatalog* Check=Ok?MdoSkillCatalogSingle(xrtRootPath(Root),Stage,Id):NULL;Ok=Ok&&Check;
                MdoSkillCatalogRelease(Check);
                char *From=Ok?xrtPathJoin(xrtRootPath(Root),Stage):NULL,*To=Ok?xrtPathJoin(xrtRootPath(Root),Directory):NULL;
                Ok=Ok&&From&&To&&xrtPathRename(From,To,false);
                if(Created&&!Ok){size_t Count=0;ExtensionTreeRemove(Root,Stage,0,&Count);}
                xrtFree(From);xrtFree(To);
            }
        }
    }
    MdoApiJsonBodyUnit(&Body);xrtFree(Old);
    if(!Ok)return MdoApiReplyError(C,Code,"skill_project_save_failed",Error[0]?Error:"Invalid Skill, conflicting directory, enable flags or filesystem error",NULL);
    xvalue* Item=SkillProjectItem(Root,Id);
    return Item?MdoApiReplySuccessTake(C,200,Item,NULL):MdoApiReplyError(C,503,"skill_project_readback_failed","Saved Skill could not be read back; refresh before retrying",NULL);
}

bool MdoApiSkillProjectRoute(MdoApiContext* C)
{
    char* Workspace=NULL;if(!MdoApiSkillScope(C,&Workspace))return true;
    if(!Workspace)return MdoApiReplyError(C,400,"skill_project_required","A project ID is required",NULL);
    char Project[65],Id[65]={0};size_t N=0;
    xrtPercentDecode(xrtStrViewN(C->Target.Query.Data+8,C->Target.Query.Size-8),Project,64,&N);Project[N]=0;
    if(C->ParamCount) {
        if(C->Params[0].Size>64){xrtFree(Workspace);return MdoApiReplyError(C,400,"skill_id_invalid","Invalid Skill ID",NULL);}
        memcpy(Id,C->Params[0].Data,C->Params[0].Size);Id[C->Params[0].Size]=0;
        if(!MdoExtensionIdValid(Id)||Id[0]<'a'||Id[0]>'z'){xrtFree(Workspace);return MdoApiReplyError(C,400,"skill_id_invalid","Invalid Skill ID",NULL);}
    }
    xwork_error Error;MdoProjectLease* Lease=MdoProjectLeaseAcquire(Project,MDO_PROJECT_LEASE_SHARED,&Error);
    if(!Lease){xrtFree(Workspace);return MdoApiReplyError(C,409,"skill_project_busy","Project is busy",NULL);}
    xrtMutexLock(g_MdoExtensionLock);xroot Root=xrtRootOpen(Workspace);xfile Lock=NULL;
    bool Write=C->Request->head->MethodCode==XHTTP_METHOD_PUT||C->Request->head->MethodCode==XHTTP_METHOD_POST||C->Request->head->MethodCode==XHTTP_METHOD_DELETE;
    bool Ready=Root!=NULL,Result;
    if(Ready&&Write) {
        Lock=SkillWriterLock(Root);Ready=Lock!=NULL;
    }
    if(!Ready)Result=MdoApiReplyError(C,409,"skill_project_busy","Project Skill files are unavailable or being edited",NULL);
    else if(!Id[0])Result=SkillProjectList(C,Root);
    else if(Write)Result=SkillProjectEdit(C,Root,Id);
    else {xvalue* Item=SkillProjectItem(Root,Id);Result=Item?MdoApiReplySuccessTake(C,200,Item,NULL):
        MdoApiReplyError(C,404,"skill_project_missing","Project Skill not found or unreadable",NULL);}
    if(Lock)xrtClose(Lock);
    if(Root)xrtRootClose(Root);
    xrtMutexUnlock(g_MdoExtensionLock);MdoProjectLeaseRelease(Lease);xrtFree(Workspace);return Result;
}
