#include "../../include/mdo/projects.h"
/* Export only the selected project C command. Rooted, no-follow reads and a
 * project lease prevent aliases or a concurrent workspace move from escaping
 * the project. Command compilation is deliberately deferred until invocation. */
static bool EcoExportProjectCommand(cstr Project,cstr Id,xvalue* Resource)
{
    if(!MdoExtensionIdValid(Id)||!MdoPackageId(Project))return false;
    xwork_error Error;MdoProjectLease* Lease=MdoProjectLeaseAcquire(Project,MDO_PROJECT_LEASE_SHARED,&Error);
    char* Workspace=NULL;
    if(Lease&&!strcmp(Project,"default"))Workspace=MdoHomeDefaultWorkspacePath(false);
    else if(Lease){MdoProjectInfo Info={0};Info.Size=sizeof(Info);bool Found=false;if(MdoProjectGet(Project,&Info,&Found,&Error)&&Found)Workspace=xrtPathAbs(Info.WorkspaceRoot);}
    xroot Root=Workspace?xrtRootOpen(Workspace):NULL;xrtFree(Workspace);
    char Path[160];snprintf(Path,sizeof(Path),".mdo/commands/%s.c",Id);
    xfileoptions Options;xrtFileOptionsInit(&Options);Options.Flags=XFILE_READ|XFILE_NOFOLLOW;
    char* Text=Root?CommandReadFile(xrtRootFileOpen(Root,Path,&Options)):NULL;
    char Why[512];xvalue* Metadata=Text?MdoCommandMetadata(Id,Text,Why):NULL;
    bool Ok=Metadata&&MdoApiValueSetString(Resource,"kind","commands")&&MdoApiValueSetString(Resource,"id",Id)&&MdoApiValueSetString(Resource,"content",Text);
    xrtValueRelease(Metadata);xrtFree(Text);if(Root)xrtRootClose(Root);MdoProjectLeaseRelease(Lease);return Ok;
}
static bool EcoExportProjectMcp(cstr Project,cstr Id,xvalue* Resource)
{
    if(!MdoExtensionIdValid(Id)||!MdoPackageId(Project))return false;
    xwork_error Error;MdoProjectLease* Lease=MdoProjectLeaseAcquire(Project,MDO_PROJECT_LEASE_SHARED,&Error);char* Workspace=NULL;
    if(Lease&&!strcmp(Project,"default"))Workspace=MdoHomeDefaultWorkspacePath(false);else if(Lease){MdoProjectInfo Info={0};Info.Size=sizeof(Info);bool Found=false;if(MdoProjectGet(Project,&Info,&Found,&Error)&&Found)Workspace=xrtPathAbs(Info.WorkspaceRoot);}
    xroot Root=Workspace?xrtRootOpen(Workspace):NULL;xrtFree(Workspace);char Path[160];snprintf(Path,sizeof(Path),".mdo/mcp/%s.json",Id);xfileoptions O;xrtFileOptionsInit(&O);O.Flags=XFILE_READ|XFILE_NOFOLLOW;
    char* Original=Root?CommandReadFile(xrtRootFileOpen(Root,Path,&O)):NULL;char* Text=Original?McpTemplateExport(Original,Id):NULL;
    bool Ok=Text&&MdoApiValueSetString(Resource,"kind","mcp")&&MdoApiValueSetString(Resource,"id",Id)&&MdoApiValueSetString(Resource,"content",Text);xrtFree(Text);xrtFree(Original);if(Root)xrtRootClose(Root);MdoProjectLeaseRelease(Lease);return Ok;
}
/* Select a project by ID, and export only its .mdo Skill using rooted catalog
 * reads. Absolute paths, credentials and neighboring project files never enter
 * the portable work. */
static bool EcoExportProjectSkill(cstr Project,cstr Id,xvalue* Resource)
{
    if(!MdoExtensionIdValid(Id)||!Project||!Project[0])return false;
    char* Workspace=NULL;
    if(!strcmp(Project,"default"))Workspace=MdoHomeDefaultWorkspacePath(false);
    else {MdoProjectInfo Info={0};Info.Size=sizeof(Info);bool Found=false;xwork_error Error;if(MdoProjectGet(Project,&Info,&Found,&Error)&&Found)Workspace=xrtPathAbs(Info.WorkspaceRoot);}
    MdoSkillCatalog* Catalog=Workspace?MdoSkillCatalogDirectory(Workspace,".mdo/skills"):NULL;xrtFree(Workspace);
    char* Text=Catalog?MdoSkillCatalogDocument(Catalog,Id):NULL;MdoSkillInfo Info={0};Info.Size=sizeof(Info);
    xvalue* Extras=xrtValueArray();bool Ok=Text&&Extras&&MdoSkillCatalogFind(Catalog,Id,&Info)&&Info.ResourceCount<=128&&MdoApiValueSetString(Resource,"kind","skills")&&MdoApiValueSetString(Resource,"id",Id)&&MdoApiValueSetString(Resource,"content",Text);size_t Total=0;
    for(size_t i=0;Ok&&i<Info.ResourceCount;i++){
        MdoSkillResourceInfo R={0};R.Size=sizeof(R);MdoSkillResourceContent Content={0};Content.Size=sizeof(Content);
        Ok=MdoSkillCatalogResourceAt(Catalog,Id,i,&R)&&MdoSkillCatalogLoadResource(Catalog,Id,R.Path,MDO_SKILL_FILE_LIMIT,&Content)&&Content.Bytes<=64u*1024u*1024u-Total;
        char* B64=Ok?xrtBase64EncodeNew(Content.Data,Content.Bytes,NULL):NULL;xvalue* F=xrtValueObject();Ok=Ok&&B64&&F&&MdoApiValueSetString(F,"path",R.Path)&&MdoApiValueSetString(F,"base64",B64)&&xrtValueArrayAppend(Extras,F);
        if(Ok)Total+=Content.Bytes;
        xrtFree(B64);xrtValueRelease(F);MdoSkillResourceContentUnit(&Content);
    }
    if(Ok)Ok=xrtValueObjectSet(Resource,XRT_STR_LITERAL("files"),Extras);
    xrtValueRelease(Extras);xrtFree(Text);MdoSkillCatalogRelease(Catalog);return Ok;
}
/* Local authoring is available without a service session. Export exact files,
 * never machine credentials, absolute roots or executable installation hooks. */
static bool EcoNativeSourceList(xvalue* Items)
{
    const char* Dirs[]={"modules/tools","data/ecosystem/toolpacks"};
    for(size_t k=0;k<2;k++){
        xdir Dir=MdoHomeOpenDirectory(Dirs[k],XDIR_STAT);if(!Dir){xrtClearError();continue;}xdirentry E;xdirnext Next;bool Ok=true;
        while((Next=xrtDirNext(Dir,&E))==XDIR_NEXT_ITEM){
            size_t Suffix=k?5:2,N=E.Name.Size;if(E.Info.Type!=XFILE_TYPE_FILE||N<=Suffix||N>64+Suffix||memcmp(E.Name.Data+N-Suffix,k?".json":".c",Suffix))continue;
            char Id[65];memcpy(Id,E.Name.Data,N-Suffix);Id[N-Suffix]=0;if(!MdoExtensionIdValid(Id))continue;
            xvalue* R=xrtValueObject();Ok=R&&MdoApiValueSetString(R,"kind",k?"toolpacks":"modules")&&MdoApiValueSetString(R,"id",Id)&&MdoApiValueSetString(R,"name",Id)&&xrtValueArrayAppend(Items,R);xrtValueRelease(R);if(!Ok)break;
        }
        xrtDirClose(Dir);if(!Ok||Next==XDIR_NEXT_ERROR)return false;
    }
    xvalue* Config=NULL;char Error[256];if(!MdoApplicationPluginsStoreBegin(&Config,Error,sizeof(Error)))return false;MdoApplicationPluginsStoreEnd();
    bool Ok=true;for(size_t i=0;Ok&&i<xrtValueCount(Config);i++){
        cstr Id=MdoPackageText(xrtValueArrayGet(Config,i),"id",64);xvalue* R=xrtValueObject();
        Ok=Id&&R&&MdoApiValueSetString(R,"kind","plugins")&&MdoApiValueSetString(R,"id",Id)&&MdoApiValueSetString(R,"name",Id)&&xrtValueArrayAppend(Items,R);xrtValueRelease(R);
    }xrtValueRelease(Config);return Ok;
}
static bool EcoNativeExportFile(xvalue* Extras,cstr Root,cstr Name,size_t* Total)
{
    if(!MdoPackagePath(Name))return false;
    char Path[512];int N=snprintf(Path,sizeof(Path),"%s/%s",Root,Name);if(N<=0||(size_t)N>=sizeof(Path))return false;
    xfile F=MdoHomeOpenRead(Path);xfileinfo Info={0};bool Ok=F&&xrtFileStat(F,&Info)&&Info.Type==XFILE_TYPE_FILE&&Info.Size<=64u*1024u*1024u&&*Total+Info.Size<=64u*1024u*1024u;
    bytes Data=Ok?xrtMalloc((size_t)Info.Size+1):NULL;Ok=Ok&&Data&&xrtReadFull(F,Data,(size_t)Info.Size,NULL);if(F)xrtClose(F);
    char* B64=Ok?xrtBase64EncodeNew(Data,(size_t)Info.Size,NULL):NULL;xvalue* E=xrtValueObject();
    Ok=Ok&&B64&&E&&MdoApiValueSetString(E,"path",Name)&&MdoApiValueSetString(E,"base64",B64)&&xrtValueArrayAppend(Extras,E);
    if(Ok)*Total+=(size_t)Info.Size;
    xrtFree(B64);xrtFree(Data);xrtValueRelease(E);return Ok;
}
static bool EcoNativeExport(cstr Kind,cstr Id,xvalue* Resource)
{
    if(!MdoExtensionIdValid(Id))return false;
    char Path[256],Root[256];xvalue *Doc=NULL,*Config=NULL;bool Missing=false;char* Raw=NULL;bool Plugin=!strcmp(Kind,"plugins");
    if(Plugin){char Error[256];if(!MdoApplicationPluginsStoreBegin(&Config,Error,sizeof(Error)))return false;MdoApplicationPluginsStoreEnd();
        const xvalue* M=EcoNativeFind(Config,Id);cstr Revision=MdoPackageText(M,"revision",64);
        if(Revision){snprintf(Path,sizeof(Path),"plugins/%s/revisions/%s/manifest.json",Id,Revision);snprintf(Root,sizeof(Root),"plugins/%s/revisions/%s/files",Id,Revision);}
        else {xrtValueRelease(Config);return false;}
    }else snprintf(Path,sizeof(Path),"data/ecosystem/toolpacks/%s.json",Id);
    Raw=MdoExtensionRead(Path,false,1048576u,&Missing);Doc=Raw?xrtJsonParse(xrtStrView(Raw)):NULL;xrtFree(Raw);xrtValueRelease(Config);
    if(!Plugin){cstr R=MdoPackageText(Doc,"root",220);if(!R||strncmp(R,"data/ecosystem/toolpacks/",24)||!MdoPackagePath(R)){xrtValueRelease(Doc);return false;}snprintf(Root,sizeof(Root),"%s",R);}
    const xvalue* Inventory=xrtValueObjectGet(Doc,XRT_STR_LITERAL("files"));xvalue* Extras=xrtValueArray();bool Ok=Doc&&Extras&&xrtValueType(Inventory)==XVALUE_ARRAY&&xrtValueCount(Inventory)<=128;size_t Total=0;
    for(size_t i=0;Ok&&i<xrtValueCount(Inventory);i++)Ok=EcoNativeExportFile(Extras,Root,MdoPackageText(xrtValueArrayGet(Inventory,i),"path",220),&Total);
    xvalue* Descriptor=Plugin?xrtValueObject():xrtValueClone(Doc);
    if(Plugin){const xvalue* F=xrtValueObjectGet(Doc,XRT_STR_LITERAL("frontend"));if(F)Ok=Ok&&xrtValueObjectSet(Descriptor,XRT_STR_LITERAL("frontend"),F);}
    else {const char* Keys[]={"id","root","revision","enabled","files"};for(size_t i=0;i<5;i++)xrtValueObjectRemove(Descriptor,xrtStrView(Keys[i]));}
    char* Content=Descriptor?xrtJsonStringify(Descriptor,false,NULL):NULL;
    Ok=Ok&&Content&&MdoApiValueSetString(Resource,"kind",Kind)&&MdoApiValueSetString(Resource,"id",Id)&&MdoApiValueSetString(Resource,"content",Content)&&xrtValueObjectSet(Resource,XRT_STR_LITERAL("files"),Extras);
    xrtFree(Content);xrtValueRelease(Descriptor);xrtValueRelease(Extras);xrtValueRelease(Doc);return Ok;
}
typedef struct EcoNativeExportContext {cstr Root;xvalue* Extras;size_t* Total;} EcoNativeExportContext;
static bool EcoNativeExportTreeFile(cstr Relative,const xfileinfo* Info,void* Data)
{
    (void)Info;EcoNativeExportContext* C=Data;
    if((!strncmp(C->Root,"skills/",7)&&!strcmp(Relative,"SKILL.md"))||(!strncmp(C->Root,"knowledge/",10)&&!strcmp(Relative,"knowledge.json")))return true;
    return xrtValueCount(C->Extras)<128&&EcoNativeExportFile(C->Extras,C->Root,Relative,C->Total);
}
static bool EcoNativeExportTree(cstr Root,xvalue* Extras,size_t* Total)
{
    EcoNativeExportContext Context={Root,Extras,Total};return MdoPortableTreeWalk(Root,EcoNativeExportTreeFile,&Context);
}

static bool EcoRoleExportReference(xvalue* Spec,cstr Key,cstr Kind,cstr Id,xvalue* Files,size_t* Total)
{
    cstr Original=MdoAgentConfigText(Spec,Key);if(!Original)return true;
    if(!MdoAgentConfigPathValid(Original))return false;
    cstr Origin=MdoAgentConfigText(Spec,"origin");if(!Origin)Origin=Kind;
    char Source[1400],Name[221],Target[320];
    snprintf(Source,sizeof(Source),"%s/%s",Origin,Original);
    size_t N=strlen(Id);int W=!strncmp(Original,Id,N)&&Original[N]=='/'&&!strncmp(Original+N+1,"assets/",7)?
        snprintf(Name,sizeof(Name),"%s",Original+N+1):snprintf(Name,sizeof(Name),"assets/%s",Original);
    if(W<0||(size_t)W>=sizeof(Name)||!MdoPackagePath(Name))return false;
    char* Text=MdoExtensionRead(Source,false,MDO_EXTENSION_TEXT_LIMIT,NULL);if(!Text)return false;
    bool Ok=true;
    if(!MdoPackageRoleFile(Files,Name)){
        char* B64=xrtBase64EncodeNew(Text,strlen(Text),NULL);xvalue* F=xrtValueObject();
        Ok=xrtValueCount(Files)<128&&*Total+strlen(Text)<=64u*1024u*1024u&&B64&&F&&MdoApiValueSetString(F,"path",Name)&&MdoApiValueSetString(F,"base64",B64)&&xrtValueArrayAppend(Files,F);
        if(Ok)*Total+=strlen(Text);xrtFree(B64);xrtValueRelease(F);
    }
    xrtFree(Text);snprintf(Target,sizeof(Target),"%s/%s",Id,Name);
    return Ok&&MdoAgentConfigSetText(Spec,Key,Target)&&(!strcmp(Key,"file")?MdoAgentConfigSetText(Spec,"origin",Kind):true);
}
static bool EcoRoleExport(cstr Kind,cstr Id,xvalue* Resource)
{
    if(!MdoPackageRoleKind(Kind)||!MdoPackageId(Id)||(!strcmp(Kind,"agents")&&!strcmp(Id,"default")))return false;
    char Path[256];snprintf(Path,sizeof(Path),"%s/%s.md",Kind,Id);
    bool Missing=false;char* Raw=MdoExtensionRead(Path,false,MDO_EXTENSION_TEXT_LIMIT,&Missing);
    if(!Raw&&Missing){snprintf(Path,sizeof(Path),"modules/%s/%s.c",Kind,Id);char* Code=MdoExtensionRead(Path,false,MDO_EXTENSION_TEXT_LIMIT,NULL);
        bool Enabled=true;MdoExtensionEnabled(Kind,Id,&Enabled);if(Enabled&&Code)Raw=ExtensionRoleCodeConfig(Kind,Id,Code);xrtFree(Code);}
    xvalue* D=Raw?MdoPackageRoleDocument(Raw,NULL,0):NULL;xrtFree(Raw);
    xvalue* Files=xrtValueArray();bool Ok=D&&Files&&MdoAgentConfigSetText(D,"schema","mdo.agent.v1");size_t Total=0;
    const char* Lists[]={"skills","mcp_servers"};
    for(size_t I=0;Ok&&I<2;I++)if(MdoAgentConfigText(D,Lists[I])){
        char* Items[128]={0};size_t Count=0;xvalue* List=xrtValueArray();
        Ok=List&&MdoAgentFileList(D,Lists[I],Items,&Count);
        for(size_t J=0;J<Count;J++){if(Ok)Ok=xrtValueArrayAppendNew(List,xrtValueString(xrtStrView(Items[J])));xrtFree(Items[J]);}
        if(Ok)Ok=xrtValueObjectSet(D,xrtStrView(Lists[I]),List);xrtValueRelease(List);
    }
    if(Ok)Ok=EcoRoleExportReference(D,"prompt_file",Kind,Id,Files,&Total);
    const xvalue* Context=MdoAgentConfigGet(D,"context");
    for(size_t I=0;Ok&&I<xrtValueCount(Context);I++)Ok=EcoRoleExportReference((xvalue*)xrtValueObjectAt(Context,I,NULL),"file",Kind,Id,Files,&Total);
    if(Ok&&MdoAgentConfigBool(D,"code",false)){
        snprintf(Path,sizeof(Path),"modules/%s",Kind);char Name[80];snprintf(Name,sizeof(Name),"%s.c",Id);
        Ok=EcoNativeExportFile(Files,Path,Name,&Total);
        if(Ok)Ok=MdoApiValueSetString((xvalue*)xrtValueArrayGet(Files,xrtValueCount(Files)-1),"path","program.c");
    }
    char* Content=Ok?xrtJsonStringify(D,true,NULL):NULL;
    Ok=Content&&strlen(Content)<=MDO_EXTENSION_TEXT_LIMIT&&MdoApiValueSetString(Resource,"kind",Kind)&&MdoApiValueSetString(Resource,"id",Id)&&
        MdoApiValueSetString(Resource,"content",Content)&&xrtValueObjectSet(Resource,XRT_STR_LITERAL("files"),Files)&&MdoPackageRoleValidate(Resource,true,NULL,0);
    xrtFree(Content);xrtValueRelease(D);xrtValueRelease(Files);return Ok;
}
