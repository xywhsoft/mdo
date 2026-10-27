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
    bool OwnershipKnown=false;xvalue* Receipts=ExtensionStoreRead(&OwnershipKnown);
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
            if(Ok&&o==0){char Path[160];ExtensionPath("skills",Info.Id,Path);Ok=ExtensionStoreInfo(Item,Path,Receipts,OwnershipKnown,false);}
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
    xrtFree(UserDir);xrtFree(ProjectDir);xrtFree(Workspace);xrtValueRelease(Receipts);
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
    if(Created){size_t Count=0;ExtensionTreeRemove(Root,Stage,&Count);}
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
    if(Ok&&!strcmp(Origin.Data,"personal")&&!strcmp(Provider.Data,"mdo")&&!ExtensionStoreCanCustomize(C,"skills",Id.Data)){
        xrtMutexUnlock(g_MdoExtensionLock);MdoProjectLeaseRelease(Lease);xrtFree(Workspace);MdoApiJsonBodyUnit(&Body);return true;
    }
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
        bool Restored=Anchor&&ExtensionTreeRemove(Anchor,TargetPath,&Count);
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

bool MdoApiSkillProjectRoute(MdoApiContext* C){return MdoProjectExtensionSelection(C,"skills");}
