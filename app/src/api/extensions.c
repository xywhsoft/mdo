#include <stdio.h>
#include <string.h>
#include "internal.h"
#include "../../include/mdo/subagent_file.h"
#include "../../include/mdo/skills.h"
#include "../../include/mdo/mcp.h"
#include "../../include/mdo/secrets.h"

/* File-backed editors share only storage/HTTP mechanics. Runtime ownership,
 * transport, execution and permissions remain with their existing managers. */
static xmutex* g_MdoExtensionLock;
bool MdoApiExtensionsInit(void) { g_MdoExtensionLock = xrtMutexCreate(); return g_MdoExtensionLock != NULL; }
void MdoApiExtensionsUnit(void) { if (g_MdoExtensionLock) xrtMutexDestroy(g_MdoExtensionLock); g_MdoExtensionLock = NULL; }

static bool ExtensionKind(cstr Kind)
{
    return strcmp(Kind,"subagents") == 0 || strcmp(Kind,"skills") == 0 ||
        strcmp(Kind,"mcp") == 0 || strcmp(Kind,"commands") == 0;
}
static bool ExtensionParams(MdoApiContext* C, char Kind[16], char Id[65])
{
    size_t i;
    if (C->ParamCount < 1u || C->Params[0].Size >= 16u) return false;
    memcpy(Kind,C->Params[0].Data,C->Params[0].Size); Kind[C->Params[0].Size] = 0;
    if (!ExtensionKind(Kind)) return false;
    Id[0] = 0;
    if (C->ParamCount >= 2u) {
        if (C->Params[1].Size > 64u) return false;
        memcpy(Id,C->Params[1].Data,C->Params[1].Size); Id[C->Params[1].Size] = 0;
        if (!MdoExtensionIdValid(Id)) return false;
    }
    for (i=0u;i<C->Target.Query.Size;++i) if ((unsigned char)C->Target.Query.Data[i] < 32u) return false;
    return true;
}
static void ExtensionPath(cstr Kind, cstr Id, char Path[160])
{
    snprintf(Path,160u,"%s/%s%s",Kind,Id,strcmp(Kind,"skills")==0?"/SKILL.md":strcmp(Kind,"mcp")==0?".json":".md");
}
static bool ExtensionString(const xvalue* Object,cstr Key,xstrview* Text)
{
    return xrtValueGetString(xrtValueObjectGet(Object,xrtStrView(Key)),Text) &&
        memchr(Text->Data,0,Text->Size)==NULL && xrtUtf8Valid(*Text,NULL);
}
static bool ExtensionValidate(cstr Kind,cstr Id,cstr Text,char Error[1024])
{
    bool Ok;
    Error[0]=0;
    if (strcmp(Kind,"skills")==0) return MdoSkillValidateText(Text,Error,1024u);
    if (strcmp(Kind,"mcp")==0) return MdoMcpValidateText(Id,Text,Error,1024u);
    if (strcmp(Kind,"subagents")==0) {
        MdoSubagentFile File;
        Ok=MdoSubagentFileParse(Id,Text,&File,Error,1024u);
        MdoSubagentFileUnit(&File); return Ok;
    }
    /* Built-ins always own their names; a saved prompt cannot shadow /stop. */
    {
        static const char* Reserved[]={"new","model","fork","export","clear","stop","settings","theme","help"};
        size_t i;
        xvalue* Doc;
        xstrview Prompt;
        for (i=0u;i<sizeof(Reserved)/sizeof(Reserved[0]);++i) if (!strcmp(Id,Reserved[i])) {
            snprintf(Error,1024u,"Command name is reserved by mdo"); return false;
        }
        Doc=MdoPromptParse(Text,false,Error,1024u);
        Ok=Doc != NULL && ExtensionString(Doc,"prompt",&Prompt) && xrtStrTrim(Prompt).Size != 0u;
        xrtValueRelease(Doc);
        if (!Ok && !Error[0]) snprintf(Error,1024u,"Command instructions must not be empty");
        return Ok;
    }
}
static bool ExtensionReload(cstr Kind,cstr Id,bool Expected)
{
    bool Found=false;
    if (!strcmp(Kind,"subagents")) {
        MdoModuleAgentInfo Info={0}; MdoModuleCatalog* Catalog;
        char Agent[96]; Info.Size=sizeof(Info);
        if (!MdoModuleManagerReload()) return false;
        Catalog=MdoModuleCatalogSnapshot(); snprintf(Agent,sizeof(Agent),"subagent.%s",Id);
        Found=MdoModuleCatalogAgentFind(Catalog,Agent,&Info); MdoModuleCatalogRelease(Catalog);
    } else if (!strcmp(Kind,"skills")) {
        MdoSkillInfo Info={0}; MdoSkillCatalog* Catalog; Info.Size=sizeof(Info);
        if (!MdoSkillManagerReload()) return false;
        Catalog=MdoSkillCatalogSnapshot(); Found=MdoSkillCatalogFind(Catalog,Id,&Info); MdoSkillCatalogRelease(Catalog);
    } else if (!strcmp(Kind,"mcp")) {
        MdoMcpServerInfo Info={0}; MdoMcpCatalog* Catalog; Info.Size=sizeof(Info);
        if (!MdoMcpManagerReload()) return false;
        Catalog=MdoMcpCatalogSnapshot(); Found=MdoMcpCatalogFind(Catalog,Id,&Info); MdoMcpCatalogRelease(Catalog);
    } else return true;
    return Found==Expected;
}
static xvalue* ExtensionItem(cstr Kind,cstr Id,bool IncludeSource)
{
    char Path[160],Hash[65],Error[1024];
    char* Text;
    bool Missing=false,Enabled=true,External=false,Valid;
    xfileinfo Info;
    xvalue* Item=xrtValueObject();
    xvalue* Doc=NULL;
    xstrview Name={0},Description={0};
    ExtensionPath(Kind,Id,Path);
    Text=MdoExtensionRead(Path,false,MDO_EXTENSION_TEXT_LIMIT,&Missing);
    if (Text==NULL || Item==NULL || !MdoHomeExternalStat(Path,&External,&Info) ||
        !MdoExtensionEnabled(Kind,Id,&Enabled)) goto fail;
    Valid=ExtensionValidate(Kind,Id,Text,Error);
    if (!strcmp(Kind,"mcp")) {
        Doc=xrtJsonParse(xrtStrView(Text));
        if (Doc) (void)xrtValueGetBool(xrtValueObjectGet(Doc,XRT_STR_LITERAL("enabled")),&Enabled);
    } else Doc=MdoPromptParse(Text,strcmp(Kind,"commands")!=0,NULL,0u);
    if (Doc) { (void)ExtensionString(Doc,"name",&Name); (void)ExtensionString(Doc,"description",&Description); }
    if (!MdoExtensionHash(Text,Hash) || !MdoApiValueSetString(Item,"id",Id) ||
        !MdoApiValueSetString(Item,"kind",Kind) || !MdoApiValueSetStringView(Item,"name",Name.Size?Name:xrtStrView(Id)) ||
        !MdoApiValueSetStringView(Item,"description",Description) || !MdoApiValueSetString(Item,"path",Path) ||
        !MdoApiValueSetString(Item,"revision",Hash) || !MdoApiValueSetBool(Item,"external",External) ||
        !MdoApiValueSetBool(Item,"enabled",Enabled) || !MdoApiValueSetBool(Item,"valid",Valid) ||
        !MdoApiValueSetString(Item,"error",Valid?"":Error) ||
        (IncludeSource && !MdoApiValueSetString(Item,"content",Text))) goto fail;
    if (!strcmp(Kind,"mcp")) {
        MdoMcpServerStatus Status={0}; Status.Size=sizeof(Status);
        if (MdoMcpManagerGetStatus(Id,&Status)) {
            cstr State=Status.State==XWORK_MCP_SERVER_READY?"ready":Status.State==XWORK_MCP_SERVER_FAILED?"failed":
                Status.State==XWORK_MCP_SERVER_DISABLED?"disabled":"disconnected";
            if (!MdoApiValueSetString(Item,"state",State) || !MdoApiValueSetBool(Item,"connected",Status.Connected) ||
                !MdoApiValueSetUInt(Item,"tool_count",Status.DiscoveredToolCount)) goto fail;
        }
    }
    if (!strcmp(Kind,"commands") && Doc) {
        xstrview Prompt,Hint={0};
        (void)ExtensionString(Doc,"argument-hint",&Hint);
        if (ExtensionString(Doc,"prompt",&Prompt)) {
            if ((IncludeSource && !MdoApiValueSetStringView(Item,"prompt",Prompt)) || !MdoApiValueSetStringView(Item,"argument_hint",Hint)) goto fail;
        }
    }
    xrtFree(Text); xrtValueRelease(Doc); return Item;
fail:
    xrtFree(Text); xrtValueRelease(Doc); xrtValueRelease(Item); return NULL;
}
static bool ExtensionList(MdoApiContext* C,cstr Kind)
{
    char Root[80]; xdir Dir; xdirentry Entry; xdirnext Next;
    xvalue* Data=xrtValueObject(); xvalue* Items=xrtValueArray();
    size_t Count=0u; bool Ok=Data != NULL && Items != NULL;
    snprintf(Root,sizeof(Root),"/app/default-home/%s",Kind);
    Dir=xrtVfsDirOpen(xsApplicationVfs(),Root,XDIR_STAT);
    if (Dir==NULL) {
        const xerror* E=xrtGetError();
        if (E && xrtErrorKind(E)==XERR_NOT_FOUND) xrtClearError(); else Ok=false;
    }
    while (Ok && Dir && (Next=xrtDirNext(Dir,&Entry))==XDIR_NEXT_ITEM) {
        char Id[65]; size_t Length=Entry.Name.Size; bool Skill=!strcmp(Kind,"skills"); xvalue* Item;
        cstr Suffix=!strcmp(Kind,"mcp")?".json":".md"; size_t SuffixSize=strlen(Suffix);
        if (!(Entry.Flags&XDIR_ENTRY_UTF8)) continue;
        if (Skill) { if (Entry.Info.Type!=XFILE_TYPE_DIRECTORY) continue; }
        else { if (Entry.Info.Type!=XFILE_TYPE_FILE || Length<=SuffixSize ||
            memcmp(Entry.Name.Data+Length-SuffixSize,Suffix,SuffixSize)) continue; Length-=SuffixSize; }
        if (Length==0u || Length>64u) continue;
        memcpy(Id,Entry.Name.Data,Length); Id[Length]=0;
        if (!MdoExtensionIdValid(Id)) continue;
        /* README is not a resource; lowercase IDs keep that invariant portable. */
        if (++Count>256u) { Ok=false; break; }
        Item=ExtensionItem(Kind,Id,false);
        if (Item) { Ok=xrtValueArrayAppend(Items,Item); xrtValueRelease(Item); }
        else xrtClearError(); /* missing SKILL.md folders are not installed Skills */
    }
    if (Dir) { if (Ok && Next==XDIR_NEXT_ERROR) Ok=false; (void)xrtDirClose(Dir); }
    if (Ok) Ok=MdoApiValueSetTake(Data,"items",&Items);
    xrtValueRelease(Items);
    if (!Ok) { xrtValueRelease(Data); return MdoApiReplyError(C,503,"extensions_unavailable","Resource directory cannot be read",NULL); }
    return MdoApiReplySuccessTake(C,200,Data,NULL);
}
static bool ExtensionMatch(MdoApiContext* C,cstr Text)
{
    const xhttpfield* Field=NULL; char Tag[72]; char Hash[65]; xstrview Value;
    xhttpnext Next=xrtHttpFieldGetUnique(C->Request->head->Fields,C->Request->head->FieldCount,
        XRT_STR_LITERAL("If-Match"),&Field);
    if (Next==XHTTP_NEXT_END) { (void)MdoApiReplyError(C,428,"precondition_required","Resource revision is required",NULL); return false; }
    if (Text) { if (!MdoExtensionHash(Text,Hash)) return false; snprintf(Tag,sizeof(Tag),"\"%s\"",Hash); }
    else strcpy(Tag,"\"new\"");
    Value=Field?xrtStrTrim(Field->Value):xrtStrView("");
    if (Next!=XHTTP_NEXT_ITEM || Value.Size!=strlen(Tag) || memcmp(Value.Data,Tag,Value.Size)) {
        (void)MdoApiReplyError(C,412,"revision_conflict","Resource changed; reload before saving or deleting",NULL); return false;
    }
    return true;
}
/* Cleanup is rooted at an already checked directory, bounded, and never follows
 * links. Used only for importer staging/deleted Skill or owned MCP secrets. */
static bool ExtensionTreeRemove(xroot Root,cstr Path,unsigned Depth,size_t* Count)
{
    xdir Dir; xdirentry Entry; xdirnext Next; bool Ok=true;
    if (Depth>12u || ++*Count>1024u) return false;
    Dir=xrtRootDirOpen(Root,Path,XDIR_STAT); if (!Dir) return false;
    while ((Next=xrtDirNext(Dir,&Entry))==XDIR_NEXT_ITEM) {
        char Child[1200]; int Length;
        if (!(Entry.Flags&XDIR_ENTRY_UTF8)) { Ok=false; break; }
        if ((Entry.Name.Size==1u && Entry.Name.Data[0]=='.') ||
            (Entry.Name.Size==2u && !memcmp(Entry.Name.Data,"..",2u))) continue;
        if (++*Count>1024u) { Ok=false; break; }
        Length=snprintf(Child,sizeof(Child),"%s/%.*s",Path,(int)Entry.Name.Size,Entry.Name.Data);
        if (Length<0 || (size_t)Length>=sizeof(Child)) { Ok=false; break; }
        if (Entry.Info.Type==XFILE_TYPE_DIRECTORY) Ok=ExtensionTreeRemove(Root,Child,Depth+1u,Count);
        else Ok=xrtRootRemove(Root,Child);
        if (!Ok) break;
    }
    if (Next==XDIR_NEXT_ERROR) Ok=false;
    (void)xrtDirClose(Dir);
    return Ok && xrtRootRemove(Root,Path);
}
static bool ExtensionRemoveDirectory(cstr Path)
{
    xroot Root; size_t Count=0u; bool Ok; char Parent[64];
    const char* Slash=strchr(Path,'/'); size_t Length;
    if (!Slash || (Length=(size_t)(Slash-Path))>=sizeof(Parent)) return false;
    memcpy(Parent,Path,Length); Parent[Length]=0;
    Root=MdoHomeOpenStorageDirectory(Parent); if (!Root) return false;
    Ok=ExtensionTreeRemove(Root,Slash+1u,0u,&Count); (void)xrtRootClose(Root); return Ok;
}
static bool ExtensionFilePath(xstrview Path)
{
    size_t Start=0u,i;
    if (Path.Size==0u || Path.Size>1024u) return false;
    for (i=0u;i<=Path.Size;++i) {
        unsigned char c=i==Path.Size?0:(unsigned char)Path.Data[i];
        if (i<Path.Size && (c<32u || c=='\\' || c==':' || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|')) return false;
        if (c=='/' || c==0) {
            if (i==Start || Path.Data[Start]=='.' || Path.Data[i-1u]=='.' || Path.Data[i-1u]==' ') return false;
            Start=i+1u;
        }
    }
    return memchr(Path.Data,0,Path.Size)==NULL && xrtUtf8Valid(Path,NULL);
}
static bool ExtensionImportFiles(cstr Stage,const xvalue* Files)
{
    size_t i,j,Total=0u;
    if (xrtValueType(Files)!=XVALUE_ARRAY || xrtValueCount(Files)>128u) return false;
    for (i=0u;i<xrtValueCount(Files);++i) {
        const xvalue* Item=xrtValueArrayGet(Files,i); xstrview Path,Encoded; char Output[1200];
        bytes Data; size_t Bytes;
        if (!ExtensionString(Item,"path",&Path) || !ExtensionFilePath(Path) ||
            (Path.Size==8u && !memcmp(Path.Data,"SKILL.md",8u)) || !ExtensionString(Item,"base64",&Encoded)) return false;
        for (j=0u;j<i;++j) {
            xstrview Other; (void)ExtensionString(xrtValueArrayGet(Files,j),"path",&Other);
            if (Path.Size==Other.Size && !memcmp(Path.Data,Other.Data,Path.Size)) return false;
        }
        Data=xrtBase64DecodeNew(Encoded.Data,Encoded.Size,&Bytes,NULL);
        if (!Data || Bytes>128u*1024u || Total>128u*1024u-Bytes) { xrtFree(Data); return false; }
        Total+=Bytes;
        snprintf(Output,sizeof(Output),"%s/%.*s",Stage,(int)Path.Size,Path.Data);
        if (!MdoHomeAtomicWrite(Output,Data,Bytes,false)) { xrtFree(Data); return false; }
        xrtFree(Data);
    }
    return true;
}
/* Imported ordinary MCP credentials become device-sealed references. The
 * caller discards newly stored credentials unless the file transaction commits. */
static void ExtensionBodyUnit(MdoApiJsonBody* Body)
{
    const xvalue* Secrets=Body->Value?xrtValueObjectGet(Body->Value,XRT_STR_LITERAL("secrets")):NULL;
    size_t i;
    if (Secrets && xrtValueType(Secrets)==XVALUE_ARRAY) for (i=0u;i<xrtValueCount(Secrets);++i) {
        xstrview Value;
        if (xrtValueGetString(xrtValueArrayGet(Secrets,i),&Value)) xrtSecureZero((void*)Value.Data,Value.Size);
    }
    if (Body->Document) xrtSecureZero(Body->Document,Body->Size);
    MdoApiJsonBodyUnit(Body);
}

static bool ExtensionMcpCredentials(char** Text,const xvalue* Inputs,
    char References[32][MDO_SECRET_VAULT_REFERENCE_CAPACITY],size_t* Count)
{
    xvalue* Doc=xrtJsonParse(xrtStrView(*Text));
    const xvalue* Transport=Doc?xrtValueObjectGet(Doc,XRT_STR_LITERAL("transport")):NULL;
    const char* Keys[]={"environment","headers"}; size_t k,i;
    bool Ok=Doc!=NULL;
    if (Inputs && (xrtValueType(Inputs)!=XVALUE_ARRAY || xrtValueCount(Inputs)>32u)) Ok=false;
    for (k=0u;Ok && k<2u;++k) {
        const xvalue* Array=xrtValueObjectGet(Transport,xrtStrView(Keys[k]));
        for (i=0u;Array && Ok && i<xrtValueCount(Array);++i) {
            xvalue* Item=(xvalue*)xrtValueArrayGet(Array,i); xstrview Ref;
            if (!ExtensionString(Item,"secret_ref",&Ref) || Ref.Size<6u || memcmp(Ref.Data,"input:",6u)) continue;
            {
                size_t n=0u,j; xstrview Value; char* Owned;
                if (Ref.Size==6u) { Ok=false; break; }
                for (j=6u;j<Ref.Size;++j) {
                    if (Ref.Data[j]<'0' || Ref.Data[j]>'9' || n>32u) { Ok=false; break; }
                    n=n*10u+(size_t)(Ref.Data[j]-'0');
                }
                if (!Ok || !Inputs || n>=xrtValueCount(Inputs) || *Count>=32u ||
                    !xrtValueGetString(xrtValueArrayGet(Inputs,n),&Value) || Value.Size==0u || Value.Size>8192u ||
                    memchr(Value.Data,0,Value.Size)) { Ok=false; break; }
                Owned=xrtStrDupN(Value.Data,Value.Size); if (!Owned) { Ok=false; break; }
                Ok=MdoSecretStore(Owned,References[*Count]);
                xrtSecureZero(Owned,strlen(Owned)); xrtFree(Owned);
                if (Ok) { ++*Count; Ok=xrtValueObjectSetNew(Item,XRT_STR_LITERAL("secret_ref"),xrtValueString(xrtStrView(References[*Count-1u]))); }
            }
        }
    }
    if (Ok) {
        char* Json=xrtJsonStringify(Doc,true,NULL);
        if (!Json) Ok=false; else { xrtFree(*Text); *Text=Json; }
    }
    xrtValueRelease(Doc); return Ok;
}

static bool ExtensionWrite(MdoApiContext* C,cstr Kind,cstr Id,cstr Path,char* Old)
{
    MdoApiJsonBody Body; xstrview Content; char Error[1024]={0}; char* Text=NULL;
    const xvalue* Files=NULL; bool Enabled=true,Ok=false,Created=false,Published=false;
    char Stage[160]={0},Directory[160];
    char References[32][MDO_SECRET_VAULT_REFERENCE_CAPACITY]; size_t ReferenceCount=0u,i;
    MdoApiBodyStatus Status=MdoApiJsonBodyRead(C,&Body);
    if (Status!=MDO_API_BODY_OK) return MdoApiReplyBodyError(C,Status);
    if (!ExtensionString(Body.Value,"content",&Content) || Content.Size>MDO_EXTENSION_TEXT_LIMIT) goto invalid;
    Text=xrtStrDupN(Content.Data,Content.Size); if (!Text) goto failed;
    if (!strcmp(Kind,"mcp") && !ExtensionMcpCredentials(&Text,
        xrtValueObjectGet(Body.Value,XRT_STR_LITERAL("secrets")),References,&ReferenceCount)) {
        snprintf(Error,sizeof(Error),"MCP credentials could not be stored; use env:/file: references or valid imported values"); goto invalid;
    }
    if (!ExtensionValidate(Kind,Id,Text,Error)) {
        goto invalid;
    }
    Files=xrtValueObjectGet(Body.Value,XRT_STR_LITERAL("files"));
    if (Files && (strcmp(Kind,"skills") || Old)) {
        snprintf(Error,sizeof(Error),"Folder import requires a new Skill ID"); goto invalid;
    }
    if (!MdoExtensionEnabled(Kind,Id,&Enabled)) goto failed;
    if (!strcmp(Kind,"mcp")) Enabled=true; /* disabled MCP servers remain in the catalog */
    if (Files) {
        char Hash[65],StageFile[190]; bool Exists; xfileinfo Info;
        snprintf(Directory,sizeof(Directory),"skills/%s",Id);
        if (!MdoHomeExternalStat(Directory,&Exists,&Info) || Exists) { snprintf(Error,sizeof(Error),"Skill directory already exists"); goto invalid; }
        if (!MdoExtensionHash(Text,Hash)) goto failed;
        snprintf(Stage,sizeof(Stage),"skills/.import-%s-%.12s",Id,Hash);
        if (!MdoHomeCreateDirectory(Stage)) goto failed;
        Created=true;
        snprintf(StageFile,sizeof(StageFile),"%s/SKILL.md",Stage);
        if (!ExtensionImportFiles(Stage,Files) || !MdoHomeAtomicWrite(StageFile,Text,strlen(Text),false)) goto failed;
        if (!MdoHomeRenameNoReplace(Stage,Directory)) goto failed;
        Created=false; Published=true;
    } else {
        if (!MdoHomeAtomicWrite(Path,Text,strlen(Text),false)) goto failed;
        Published=true;
    }
    Ok=ExtensionReload(Kind,Id,Enabled);
    if (!Ok) goto failed;
    xrtFree(Text); ExtensionBodyUnit(&Body);
    return MdoApiReplySuccessTake(C,200,ExtensionItem(Kind,Id,true),NULL);
invalid:
    for (i=0u;i<ReferenceCount;++i) (void)MdoSecretDiscard(References[i]);
    xrtFree(Text); ExtensionBodyUnit(&Body);
    return MdoApiReplyError(C,422,"extension_invalid",Error[0]?Error:"Invalid content or import files",NULL);
failed:
    if (Created) (void)ExtensionRemoveDirectory(Stage);
    if (Published) {
        bool Restored;
        if (Files) Restored=ExtensionRemoveDirectory(Directory);
        else Restored=Old?MdoHomeAtomicWrite(Path,Old,strlen(Old),false):MdoHomeRemove(Path,false);
        if (!Restored || !ExtensionReload(Kind,Id,Old?Enabled:false))
            (void)MdoHomeRequireRestart("Resource update rollback failed; restart before editing again");
    }
    for (i=0u;i<ReferenceCount;++i) (void)MdoSecretDiscard(References[i]);
    xrtFree(Text); ExtensionBodyUnit(&Body);
    return MdoApiReplyError(C,503,"extension_publish_failed","Resource could not be published; previous files restored. Check resource references and filesystem permissions.",NULL);
}
static bool ExtensionMcpEnable(MdoApiContext* C,cstr Id,bool Enabled)
{
    char Path[160],Error[1024]; char* Old; char* Json=NULL; xvalue* Doc; bool Missing=false,Ok=false,Written=false;
    ExtensionPath("mcp",Id,Path); Old=MdoExtensionRead(Path,false,MDO_EXTENSION_TEXT_LIMIT,&Missing);
    Doc=Old?xrtJsonParse(xrtStrView(Old)):NULL;
    if (Doc && xrtValueObjectSetNew(Doc,XRT_STR_LITERAL("enabled"),xrtValueBool(Enabled))) Json=xrtJsonStringify(Doc,true,NULL);
    if (Json && ExtensionValidate("mcp",Id,Json,Error) && MdoHomeAtomicWrite(Path,Json,strlen(Json),false)) {
        Written=true; Ok=ExtensionReload("mcp",Id,true);
    }
    if (!Ok && Written) {
        if (!MdoHomeAtomicWrite(Path,Old,strlen(Old),false) || !ExtensionReload("mcp",Id,true))
            (void)MdoHomeRequireRestart("MCP enable rollback failed");
    }
    xrtFree(Old); xrtFree(Json); xrtValueRelease(Doc);
    return Ok?MdoApiReplySuccessTake(C,200,ExtensionItem("mcp",Id,true),NULL):
        MdoApiReplyError(C,409,"extension_enable_failed","MCP configuration could not be changed",NULL);
}

static bool ExtensionEnable(MdoApiContext* C,cstr Kind,cstr Id)
{
    MdoApiJsonBody Body; bool Enabled,Missing=false; char* Old;
    xvalue* Config=NULL; xvalue* Disabled=NULL; char Key[96]; char* Json=NULL; size_t Bytes;
    bool Ok=false,Written=false;
    MdoApiBodyStatus Status=MdoApiJsonBodyRead(C,&Body);
    if (Status!=MDO_API_BODY_OK) return MdoApiReplyBodyError(C,Status);
    if (!xrtValueGetBool(xrtValueObjectGet(Body.Value,XRT_STR_LITERAL("enabled")),&Enabled)) {
        ExtensionBodyUnit(&Body); return MdoApiReplyError(C,422,"extension_invalid","enabled must be boolean",NULL);
    }
    if (!strcmp(Kind,"mcp")) { ExtensionBodyUnit(&Body); return ExtensionMcpEnable(C,Id,Enabled); }
    Old=MdoExtensionRead("config/extensions.json",true,64u*1024u,&Missing);
    Config=Old?xrtJsonParse(xrtStrView(Old)):Missing?xrtValueObject():NULL;
    if (!Config || xrtValueType(Config)!=XVALUE_OBJECT) goto done;
    {
        const xvalue* Existing=xrtValueObjectGet(Config,XRT_STR_LITERAL("disabled"));
        Disabled=Existing?xrtValueRetain(Existing):xrtValueObject();
    }
    snprintf(Key,sizeof(Key),"%s:%s",Kind,Id);
    if (!Disabled || xrtValueType(Disabled)!=XVALUE_OBJECT ||
        !xrtValueObjectSetNew(Disabled,xrtStrView(Key),xrtValueBool(!Enabled)) ||
        !xrtValueObjectSet(Config,XRT_STR_LITERAL("disabled"),Disabled)) goto done;
    Json=xrtJsonStringify(Config,true,&Bytes);
    if (!Json || Bytes>64u*1024u || !MdoHomeAtomicWrite("config/extensions.json",Json,Bytes,false)) goto done;
    Written=true;
    Ok=ExtensionReload(Kind,Id,Enabled);
done:
    if (!Ok && Written) {
        bool Restored=Old?MdoHomeAtomicWrite("config/extensions.json",Old,strlen(Old),false):MdoHomeRemove("config/extensions.json",false);
        if (!Restored || !ExtensionReload(Kind,Id,!Enabled)) (void)MdoHomeRequireRestart("Extension enable rollback failed");
    }
    xrtFree(Old); xrtFree(Json); xrtValueRelease(Disabled); xrtValueRelease(Config); ExtensionBodyUnit(&Body);
    if (!Ok) return MdoApiReplyError(C,409,"extension_enable_failed","Resource could not be enabled or disabled; check dependent resources",NULL);
    return MdoApiReplySuccessTake(C,200,ExtensionItem(Kind,Id,true),NULL);
}
static bool ExtensionDelete(MdoApiContext* C,cstr Kind,cstr Id,cstr Path,cstr Old)
{
    bool External; xfileinfo Info; char Directory[160],Trash[190],Hash[65]; bool Moved=false,Ok;
    if (!MdoHomeExternalStat(Path,&External,&Info) || !External) return MdoApiReplyError(C,409,"builtin_resource","Built-in files cannot be deleted; disable or customize them instead",NULL);
    if (!strcmp(Kind,"skills")) {
        snprintf(Directory,sizeof(Directory),"skills/%s",Id); MdoExtensionHash(Old,Hash);
        snprintf(Trash,sizeof(Trash),"skills/.deleted-%s-%.12s",Id,Hash);
        if (!MdoHomeRenameNoReplace(Directory,Trash)) goto failed;
        Moved=true;
    } else if (!MdoHomeRemove(Path,false)) goto failed;
    /* Removing an override may reveal a packaged default. */
    {
        bool Missing=false; char* Fallback=MdoExtensionRead(Path,false,MDO_EXTENSION_TEXT_LIMIT,&Missing); bool Enabled=true;
        if (strcmp(Kind,"mcp")) (void)MdoExtensionEnabled(Kind,Id,&Enabled);
        Ok=ExtensionReload(Kind,Id,Fallback != NULL && Enabled); xrtFree(Fallback);
    }
    if (!Ok) {
        bool Restored=Moved?MdoHomeRenameNoReplace(Trash,Directory):MdoHomeAtomicWrite(Path,Old,strlen(Old),false);
        bool Enabled=true; (void)MdoExtensionEnabled(Kind,Id,&Enabled);
        if (!Restored || !ExtensionReload(Kind,Id,Enabled)) (void)MdoHomeRequireRestart("Resource deletion rollback failed");
        goto failed;
    }
    if (Moved && !ExtensionRemoveDirectory(Trash)) return MdoApiReplyError(C,503,"extension_cleanup_pending","Skill removed from runtime, but hidden deleted files require filesystem cleanup",NULL);
    return MdoApiReplySuccessTake(C,200,xrtValueObject(),NULL);
failed:
    return MdoApiReplyError(C,409,"extension_delete_failed","Resource could not be deleted; check dependent resources",NULL);
}

static bool ExtensionBundleFiles(cstr Directory,cstr Prefix,bool External,
    xvalue* Files,size_t* Total,unsigned Depth)
{
    char Root[1200]; xdir Dir; xdirentry Item; xdirnext Next; bool Ok=true;
    if (Depth>8u) return false;
    snprintf(Root,sizeof(Root),"%s%s%s",Directory,Prefix[0]?"/":"",Prefix);
    if (External) Dir=MdoHomeOpenDirectory(Root,XDIR_STAT);
    else { char Virtual[1280]; snprintf(Virtual,sizeof(Virtual),"/app/default-home/%s",Root); Dir=xrtVfsDirOpen(xsApplicationVfs(),Virtual,XDIR_STAT); }
    if (!Dir) return false;
    while ((Next=xrtDirNext(Dir,&Item))==XDIR_NEXT_ITEM) {
        char Path[1025],Full[1200]; int Length;
        if (!(Item.Flags&XDIR_ENTRY_UTF8) || Item.Name.Size==0u || Item.Name.Data[0]=='.') continue;
        Length=snprintf(Path,sizeof(Path),"%s%s%.*s",Prefix,Prefix[0]?"/":"",(int)Item.Name.Size,Item.Name.Data);
        if (Length<0 || (size_t)Length>=sizeof(Path)) { Ok=false; break; }
        if (Item.Info.Type==XFILE_TYPE_DIRECTORY) Ok=ExtensionBundleFiles(Directory,Path,External,Files,Total,Depth+1u);
        else if (Item.Info.Type==XFILE_TYPE_FILE && strcmp(Path,"SKILL.md")) {
            xfile File; xfileinfo Info; bytes Bytes=NULL; char* Encoded=NULL; xvalue* Value=NULL;
            if (!ExtensionFilePath(xrtStrView(Path)) || xrtValueCount(Files)>=128u) { Ok=false; break; }
            snprintf(Full,sizeof(Full),"%s/%s",Directory,Path);
            File=External?MdoHomeOpenRead(Full):MdoResourceOpenRead(Full);
            Ok=File != NULL && xrtFileStat(File,&Info) && (Info.Available&XFILE_INFO_SIZE) &&
                Info.Type==XFILE_TYPE_FILE && Info.Size<=128u*1024u && *Total<=128u*1024u-(size_t)Info.Size;
            if (Ok) Bytes=(bytes)xrtMalloc((size_t)Info.Size+1u);
            Ok=Ok && Bytes != NULL && xrtReadFull(File,Bytes,(size_t)Info.Size,NULL);
            if (File && !xrtClose(File)) Ok=false;
            if (Ok) { Encoded=xrtBase64EncodeNew(Bytes,(size_t)Info.Size,NULL); Value=xrtValueObject(); }
            if (Ok) Ok=Encoded && Value && MdoApiValueSetString(Value,"path",Path) &&
                MdoApiValueSetString(Value,"base64",Encoded) && xrtValueArrayAppend(Files,Value);
            if (Ok) *Total+=(size_t)Info.Size;
            xrtFree(Bytes); xrtFree(Encoded); xrtValueRelease(Value);
        }
        if (!Ok) break;
    }
    if (Next==XDIR_NEXT_ERROR) Ok=false;
    (void)xrtDirClose(Dir); return Ok;
}
static bool ExtensionBundle(MdoApiContext* C,cstr Id)
{
    xvalue* Item=ExtensionItem("skills",Id,true); xvalue* Files=xrtValueArray();
    char Directory[160]; size_t Total=0u; bool External=false; xfileinfo Info;
    snprintf(Directory,sizeof(Directory),"skills/%s",Id);
    if (!Item || !Files || !MdoHomeExternalStat(Directory,&External,&Info) ||
        !ExtensionBundleFiles(Directory,"",External,Files,&Total,0u) ||
        !xrtValueObjectSet(Item,XRT_STR_LITERAL("files"),Files)) {
        xrtValueRelease(Item); xrtValueRelease(Files);
        return MdoApiReplyError(C,413,"extension_bundle_limit","Folder export exceeds 128 files or 128 KiB; copy the Skill directory directly",NULL);
    }
    xrtValueRelease(Files); return MdoApiReplySuccessTake(C,200,Item,NULL);
}

bool MdoApiExtensionsRoute(MdoApiContext* C)
{
    char Kind[16],Id[65],Path[160]; char* Old=NULL; bool Missing=false,Result;
    xhttpmethod Method=C->Request->head->MethodCode;
    if (!ExtensionParams(C,Kind,Id)) return MdoApiReplyError(C,400,"extension_path_invalid","Unknown resource kind or non-portable ID",NULL);
    xrtMutexLock(g_MdoExtensionLock);
    if (!Id[0]) Result=ExtensionList(C,Kind);
    else {
        ExtensionPath(Kind,Id,Path);
        if (Method==XHTTP_METHOD_GET || Method==XHTTP_METHOD_HEAD) {
            bool Bundle=C->Target.Path.Size>=7u && !memcmp(C->Target.Path.Data+C->Target.Path.Size-7u,"/bundle",7u);
            xvalue* Item=Bundle?NULL:ExtensionItem(Kind,Id,true);
            if (Bundle) Result=!strcmp(Kind,"skills")?ExtensionBundle(C,Id):MdoApiReplyError(C,400,"extension_action_invalid","Bundles are only supported for Skills",NULL);
            else Result=Item?MdoApiReplySuccessTake(C,200,Item,NULL):MdoApiReplyError(C,404,"extension_not_found","Resource not found",NULL);
        } else {
            Old=MdoExtensionRead(Path,false,MDO_EXTENSION_TEXT_LIMIT,&Missing);
            if (!Old && !Missing) Result=MdoApiReplyError(C,503,"extension_unreadable","Existing resource cannot be read safely",NULL);
            else if (!ExtensionMatch(C,Old)) Result=true;
            else if (Method==XHTTP_METHOD_PUT) Result=ExtensionWrite(C,Kind,Id,Path,Old);
            else if (!Old) Result=MdoApiReplyError(C,404,"extension_not_found","Resource not found",NULL);
            else if (Method==XHTTP_METHOD_DELETE) Result=ExtensionDelete(C,Kind,Id,Path,Old);
            else Result=ExtensionEnable(C,Kind,Id);
            xrtFree(Old);
        }
    }
    xrtMutexUnlock(g_MdoExtensionLock); return Result;
}
