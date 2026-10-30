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
            if(Ok&&Info.DirectoryId){xvalue* Installed=MdoExtensionsRead("skills",Info.DirectoryId,NULL);if(Installed)Ok=MdoApiValueSetString(Item,"directory_id",Info.DirectoryId)&&MdoApiValueSetString(Item,"source",MdoExtensionText(Installed,"source",16));xrtValueRelease(Installed);}
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
    char* UserDir=MdoHomeExternalPath("skills");
    if(Ok)Ok=MdoApiValueSetString(Data,"user_directory",UserDir?UserDir:"");
    xrtFree(UserDir);xrtFree(Workspace);
    if(Ok)Ok=MdoApiValueSetBool(Data,"truncated",Truncated)&&xrtValueObjectSet(Data,XRT_STR_LITERAL("items"),Items)&&xrtValueObjectSet(Data,XRT_STR_LITERAL("diagnostics"),Issues);
    xrtValueRelease(Items);xrtValueRelease(Issues);
    if(!Ok){xrtValueRelease(Data);return MdoApiReplyError(C,422,"skill_sources_limit","Skill sources exceed the discovery limit",NULL);}
    return MdoApiReplySuccessTake(C,200,Data,NULL);
}

bool MdoApiSkillMigrateRoute(MdoApiContext* C)
{
    char* Workspace=NULL;if(!MdoApiSkillScopeMode(C,&Workspace,false))return true;
    MdoApiJsonBody Body={0};MdoApiBodyStatus Read=MdoApiJsonBodyRead(C,&Body);if(Read!=MDO_API_BODY_OK){xrtFree(Workspace);return MdoApiReplyBodyError(C,Read);}
    cstr Origin=MdoExtensionText(Body.Value,"origin",16),Provider=MdoExtensionText(Body.Value,"provider",16),Id=MdoExtensionText(Body.Value,"id",192),Target=MdoExtensionText(Body.Value,"target_id",64),Scope=MdoExtensionText(Body.Value,"scope",16);
    bool Ok=Origin&&Provider&&Id&&MdoExtensionAlias(Target)&&MdoExtensionDirectoryId(Target)&&Scope&&!strcmp(Scope,"user");char Error[512]={0};MdoSkillCatalog* Source=Ok?SkillSourceCatalog(Origin,Provider,Workspace):NULL;MdoSkillInfo Info={0};Info.Size=sizeof(Info);Ok=Ok&&Source&&MdoSkillCatalogFind(Source,Id,&Info);
    char Entry[4097]="SKILL.md";xvalue* Owner=Ok&&Info.DirectoryId?MdoExtensionsRead("skills",Info.DirectoryId,Error):NULL;
    if(Owner){cstr InstalledSource=MdoExtensionText(Owner,"source",16),SourceEntry=MdoExtensionText(Owner,"entry",4096);if(SourceEntry)snprintf(Entry,sizeof(Entry),"%s",SourceEntry);if(InstalledSource&&!strcmp(InstalledSource,"store")){Ok=false;snprintf(Error,sizeof(Error),"Store Skill directories cannot be copied or republished");}}
    char* Directory=Ok?MdoSkillCatalogBaseDirectory(Source,Id):NULL;xvalue* Manifest=xrtValueObject(),*Platforms=SdkParse("[\"any\"]"),*Input=xrtValueObject();
    char* Seed=xrtXidMakeString();char IdentityHash[65],Identity[96];bool IdentityOk=Seed&&MdoExtensionHash(Seed,IdentityHash);xrtFree(Seed);if(IdentityOk)snprintf(Identity,sizeof(Identity),"skill:local.%s",IdentityHash);
    Ok=Ok&&Directory&&Manifest&&Platforms&&Input&&MdoApiValueSetString(Manifest,"kind","skills")&&MdoApiValueSetString(Manifest,"name",Info.Name?Info.Name:Target)&&MdoApiValueSetString(Manifest,"alias",Target)&&MdoApiValueSetString(Manifest,"entry",Entry)&&MdoApiValueSetString(Manifest,"description",Info.Description?Info.Description:"")&&xrtValueObjectSet(Manifest,XRT_STR_LITERAL("platforms"),Platforms);
    Ok=Ok&&IdentityOk&&MdoApiValueSetString(Manifest,"id",Identity);
    const cstr CreditFields[]={"author","copyright","homepage","readme","platforms","dependencies","conflicts"};for(size_t I=0;Ok&&I<7;I++){const xvalue* Value=xrtValueObjectGet(Owner,xrtStrView(CreditFields[I]));if(Value)Ok=xrtValueObjectSet(Manifest,xrtStrView(CreditFields[I]),Value);}xrtValueRelease(Owner);
    if(Ok&&Info.License)Ok=MdoApiValueSetString(Manifest,"license",Info.License);
    if(Ok&&Info.Version)Ok=MdoApiValueSetString(Manifest,"version",Info.Version);
    Ok=Ok&&MdoApiValueSetString(Input,"action","import")&&MdoApiValueSetString(Input,"kind","skills")&&MdoApiValueSetString(Input,"id",Target)&&MdoApiValueSetString(Input,"directory",Directory)&&MdoApiValueSetString(Input,"expected_revision","")&&xrtValueObjectSet(Input,XRT_STR_LITERAL("manifest"),Manifest);
    MdoApiJsonBodyUnit(&Body);MdoSkillCatalogRelease(Source);xrtFree(Directory);xrtFree(Workspace);xrtValueRelease(Manifest);xrtValueRelease(Platforms);
    bool Sent=Ok?MdoApiDirectoryOperation(C,Input,false):MdoApiReplyError(C,422,"skill_import_invalid",Error[0]?Error:"Choose an explicit source and new Home directory; project pages only select installed extensions",NULL);xrtValueRelease(Input);return Sent;
}

bool MdoApiSkillProjectRoute(MdoApiContext* C){return MdoProjectExtensionSelection(C,"skills");}
