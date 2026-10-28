/* MCP distribution owns a directory. Connection values and approvals never
 * enter that directory; these bounded records live only below data/mcp. */
#define MDO_MCP_CONNECTIONS "data/mcp/connections"
static char* McpDirectoryReplace(cstr Text,cstr Root)
{
    size_t Count=0;for(cstr P=Text;(P=strstr(P,"{$path}"));P+=7)Count++;
    if(strlen(Text)>4096||strlen(Root)>4096||Count>256)return NULL;
    size_t Bytes=strlen(Text)-Count*7u+Count*strlen(Root)+1;if(Bytes>4097u)return NULL;
    char* Out=xrtMalloc(Bytes),*Next=Out;if(!Out)return NULL;
    cstr At=Text,Slot;while((Slot=strstr(At,"{$path}"))){size_t N=(size_t)(Slot-At);memcpy(Next,At,N);Next+=N;N=strlen(Root);memcpy(Next,Root,N);Next+=N;At=Slot+7;}strcpy(Next,At);return Out;
}
static xvalue* McpDirectoryInput(const xvalue* Template,const xvalue* Values,cstr Id)
{
    const xvalue* Input=MdoMcpTemplateInput(Template,Id),*Value=Id?xrtValueObjectGet(Values,xrtStrView(Id)):NULL;
    if(!Value)Value=xrtValueObjectGet(Input,XRT_STR_LITERAL("default"));
    cstr Text=MdoExtensionText(Values,Id?Id:"",4096);if(!Text)Text=MdoExtensionText(Input,"default",4096);
    if(!Input||!Value||!Text||!MdoMcpTemplateString(Text)||(!Text[0]&&MdoMcpTemplateFlag(Input,"required",true)))return NULL;
    if(MdoMcpTemplateFlag(Input,"secret",false)&&Text[0]&&!MdoSecretReferenceSyntaxValid(xrtStrView(Text)))return NULL;
    cstr Type=MdoExtensionText(Input,"type",16);
    if(Type&&!strcmp(Type,"select")){bool Found=false;const xvalue* Options=xrtValueObjectGet(Input,XRT_STR_LITERAL("options"));
        for(size_t I=0;
        I<xrtValueCount(Options);
        I++)if(xrtValueEqual(Value,xrtValueArrayGet(Options,I)))Found=true;
        if(!Found)return NULL;
        }
    if(Type&&!strcmp(Type,"boolean")&&strcmp(Text,"true")&&strcmp(Text,"false"))return NULL;
    if(Type&&!strcmp(Type,"number")){
        cstr P=Text;bool Negative=*P=='-';if(Negative)P++;uint64 N=0;if(!*P)return NULL;
        for(;*P;P++){if(*P<'0'||*P>'9'||N>((uint64)INT64_MAX-(uint64)(*P-'0'))/10)return NULL;N=N*10+(*P-'0');}
        int64 Number=Negative?-(int64)N:(int64)N,Bound=0;
        if((MdoExtensionNumber(Input,"min",&Bound)&&Number<Bound)||(MdoExtensionNumber(Input,"max",&Bound)&&Number>Bound))return NULL;
    }
    return xrtValueClone(Value);
}
static xvalue* McpDirectoryRender(const xvalue* Template,const xvalue* Value,const xvalue* Values,cstr Root,unsigned Depth)
{
    if(Depth>20)return NULL;
    if(xrtValueType(Value)==XVALUE_OBJECT){
        cstr Input=MdoExtensionText(Value,"input",64);if(Input)return McpDirectoryInput(Template,Values,Input);
        xvalue* Out=xrtValueObject();bool Ok=Out!=NULL;
        for(size_t I=0;Ok&&I<xrtValueCount(Value);I++){xstrview Key={0};const xvalue* Child=xrtValueObjectAt(Value,I,&Key);
            xvalue* Rendered=McpDirectoryRender(Template,Child,Values,Root,Depth+1);Ok=Rendered&&xrtValueObjectSet(Out,Key,Rendered);xrtValueRelease(Rendered);}
        if(!Ok){xrtValueRelease(Out);return NULL;}return Out;
    }
    if(xrtValueType(Value)==XVALUE_ARRAY){xvalue* Out=xrtValueArray();bool Ok=Out!=NULL;
        for(size_t I=0;Ok&&I<xrtValueCount(Value);I++){xvalue* Child=McpDirectoryRender(Template,xrtValueArrayGet(Value,I),Values,Root,Depth+1);Ok=Child&&xrtValueArrayAppend(Out,Child);xrtValueRelease(Child);}
        if(!Ok){xrtValueRelease(Out);return NULL;}return Out;}
    xstrview Text={0};if(xrtValueGetString(Value,&Text)){
        if(Text.Size>6&&!strncmp(Text.Data,"input:",6))return McpDirectoryInput(Template,Values,Text.Data+6);
        char* Expanded=McpDirectoryReplace(Text.Data,Root);xvalue* Out=Expanded?xrtValueString(xrtStrView(Expanded)):NULL;xrtFree(Expanded);return Out;
    }return xrtValueClone(Value);
}
static bool McpDirectoryPlatform(const xvalue* Variant)
{
#if defined(__ANDROID__)
    cstr Platform="android-arm64-v8a";
#elif defined(_WIN32) || defined(_WIN64)
    cstr Platform="windows-x86_64";
#elif defined(__aarch64__) && defined(__APPLE__)
    cstr Platform="macos-arm64";
#elif defined(__aarch64__)
    cstr Platform="linux-arm64";
#else
    cstr Platform="linux-x86_64";
#endif
    const xvalue* Platforms=xrtValueObjectGet(Variant,XRT_STR_LITERAL("platforms"));
    for(size_t I=0;I<xrtValueCount(Platforms);I++){xstrview P={0};if(xrtValueGetString(xrtValueArrayGet(Platforms,I),&P)&&
        (xrtStrEqual(P,XRT_STR_LITERAL("any"))||xrtStrEqual(P,xrtStrView(Platform))))return true;}return false;
}
static bool McpDirectoryPut(xvalue* O,cstr Key,cstr Text)
{return Text&&xrtValueObjectSetNew(O,xrtStrView(Key),xrtValueString(xrtStrView(Text)));}
static bool McpDirectoryReservedEnvironment(xvalue* Transport)
{
    xvalue* A=xrtValueObjectGet(Transport,XRT_STR_LITERAL("environment"));
    if(!A){A=xrtValueArray();if(!A||!xrtValueObjectSetNew(Transport,XRT_STR_LITERAL("environment"),A))return false;}
    for(size_t I=0;I<xrtValueCount(A);I++){
        cstr Name=MdoExtensionText(xrtValueArrayGet(A,I),"name",256);
        if(!Name||!strcmp(Name,"MDO_EXTENSION_ROOT")||!strcmp(Name,"MDO_EXTENSION_ID")||!strcmp(Name,"MDO_EXTENSION_DIGEST"))return false;
    }return true;
}
static char* McpDirectoryResolveDocument(MdoMcpSource* Source,const xvalue* Connection,char Error[MDO_MCP_ERROR_LIMIT])
{
    char Why[512]={0},Digest[65]={0};char* Text=NULL;
    cstr DirectoryId=MdoExtensionText(Connection,"extension_id",96),Id=MdoExtensionText(Connection,"id",96),VariantId=MdoExtensionText(Connection,"variant",64);
    cstr Approved=MdoExtensionText(Connection,"approval_digest",64),Project=MdoExtensionText(Connection,"project_id",64);
    const xvalue* Values=xrtValueObjectGet(Connection,XRT_STR_LITERAL("values"));bool Enabled=false;
    bool Ok=Connection&&MdoMcpTemplateKeys(Connection,"|id||extension_id||variant||values||enabled||name||project_id||approval_digest|")&&Id&&!strcmp(Id,Source->Id)&&DirectoryId&&MdoExtensionDirectoryId(DirectoryId)&&VariantId&&
        xrtValueType(Values)==XVALUE_OBJECT&&xrtValueCount(Values)<=32&&xrtValueGetBool(xrtValueObjectGet(Connection,XRT_STR_LITERAL("enabled")),&Enabled)&&(!Project||!Project[0]||MdoExtensionIdValid(Project));
    xvalue* Meta=Ok?MdoExtensionsRead("mcp",DirectoryId,Why):NULL;bool ExtensionEnabled=false;
    xrtValueGetBool(xrtValueObjectGet(Meta,XRT_STR_LITERAL("enabled")),&ExtensionEnabled);Enabled=Enabled&&ExtensionEnabled;
    cstr Root=MdoExtensionText(Meta,"root",MDO_EXTENSION_PATH_BYTES),Entry=MdoExtensionText(Meta,"entry",MDO_EXTENSION_PATH_BYTES),Alias=MdoExtensionText(Meta,"alias",64);
    char* Path=Root&&Entry?xrtFormat("%s/%s",Root,Entry):NULL,*Native=Root?MdoHomeExternalPath(Root):NULL;
    Text=Path?MdoExtensionRead(Path,true,MDO_MCP_FILE_LIMIT,NULL):NULL;xvalue* Template=Text?xrtJsonParse(xrtStrView(Text)):NULL;xrtFree(Text);
    Ok=Ok&&Meta&&Native&&Template&&MdoMcpTemplateValidate(Template,Alias,Why,sizeof(Why))&&MdoExtensionsVerify("mcp",DirectoryId,NULL,Why)&&MdoExtensionsFingerprint("mcp",DirectoryId,Digest,NULL,Why);
    if(Ok&&Enabled&&(!Approved||strcmp(Approved,Digest))){snprintf(Why,sizeof(Why),"MCP directory changed or has not been approved; review its launch/target and confirm the connection again");Ok=false;}
    const xvalue* Variants=xrtValueObjectGet(Template,XRT_STR_LITERAL("variants")),*Selected=NULL;
    for(size_t I=0;I<xrtValueCount(Variants);I++){const xvalue* V=xrtValueArrayGet(Variants,I);cstr Name=MdoExtensionText(V,"id",64);if(Name&&VariantId&&!strcmp(Name,VariantId))Selected=V;}
    Ok=Ok&&Selected&&McpDirectoryPlatform(Selected);
    xvalue* Config=Ok?McpDirectoryRender(Template,xrtValueObjectGet(Selected,XRT_STR_LITERAL("config")),Values,Native,0):NULL;
    xvalue* Transport=xrtValueObjectGet(Config,XRT_STR_LITERAL("transport"));cstr Type=MdoExtensionText(Transport,"type",32);
    Ok=Ok&&Config&&McpDirectoryPut(Config,"id",Source->Id)&&xrtValueObjectSetNew(Config,XRT_STR_LITERAL("enabled"),xrtValueBool(Enabled));
    cstr Name=MdoExtensionText(Connection,"name",256);if(Ok&&Name&&Name[0])Ok=McpDirectoryPut(Config,"name",Name);
    if(Ok&&Type&&!strcmp(Type,"stdio")){
        cstr Program=MdoExtensionText(Transport,"program",4096),Cwd=MdoExtensionText(Transport,"working_directory",4096);
        if(Program&&!xrtPathIsRooted(Program)){
            char* Relative=xrtFormat("%s/%s",Root,Program);bool Exists=false;xfileinfo Info;
            Ok=Relative&&MdoHomeExternalStat(Relative,&Exists,&Info);
            if(Ok&&Exists){char* Full=MdoHomeExternalPath(Relative);Ok=Info.Type==XFILE_TYPE_FILE&&Full&&McpDirectoryPut(Transport,"program",Full);xrtFree(Full);}
            else if(Ok&&strchr(Program,'/'))Ok=false;
            xrtFree(Relative);
        }
        if(Ok&&(!Cwd||!strcmp(Cwd,".")))Ok=McpDirectoryPut(Transport,"working_directory",Native);
        else if(Ok&&Cwd&&!xrtPathIsRooted(Cwd)){
            char* Relative=xrtFormat("%s/%s",Root,Cwd),*Full=Relative?MdoHomeExternalPath(Relative):NULL;
            Ok=MdoExtensionRelativePath(Cwd)&&Full&&McpDirectoryPut(Transport,"working_directory",Full);xrtFree(Relative);xrtFree(Full);
        }
        if(Ok)Ok=McpDirectoryReservedEnvironment(Transport);
#if defined(__ANDROID__)
        /* Bundled ELF execution belongs to APK preinstallation. */
        if(Ok){cstr ProgramNow=MdoExtensionText(Transport,"program",4096);if(ProgramNow&&strstr(ProgramNow,Native)){snprintf(Why,sizeof(Why),"Android native MCP programs must be preinstalled by the full APK");Ok=false;}}
#endif
    }
    if(Ok){Source->Directory=xrtValueClone(Meta);Source->ProjectId=xrtStrDup(Project?Project:"");snprintf(Source->DirectoryDigest,sizeof(Source->DirectoryDigest),"%s",Digest);
        if(Enabled)Source->DirectoryLease=MdoExtensionsAcquire("mcp",DirectoryId,Why);
        Ok=Source->Directory&&Source->ProjectId&&(!Enabled||Source->DirectoryLease);}
    char* Result=Ok?xrtJsonStringify(Config,false,NULL):NULL;if(Result&&strlen(Result)>MDO_MCP_FILE_LIMIT){xrtFree(Result);Result=NULL;}
    if(!Result)snprintf(Error,MDO_MCP_ERROR_LIMIT,"%s",Why[0]?Why:"MCP connection requires an installed directory, valid private values, matching approval and a supported variant");
    xrtFree(Path);xrtFree(Native);xrtValueRelease(Meta);xrtValueRelease(Template);xrtValueRelease(Config);return Result;
}
static char* McpDirectoryResolve(MdoMcpSource* Source,char Error[MDO_MCP_ERROR_LIMIT])
{
    char* Text=MdoExtensionRead(Source->RelativePath,true,MDO_MCP_FILE_LIMIT,NULL);
    xvalue* Connection=Text?xrtJsonParse(xrtStrView(Text)):NULL;xrtFree(Text);
    char* Result=McpDirectoryResolveDocument(Source,Connection,Error);xrtValueRelease(Connection);return Result;
}
#include "../../include/mdo/extension_files.h"
/* Host-owned connection values never belong to the distributed directory.
 * Remove only bindings for the explicitly uninstalled installation; shared
 * credential-vault entries are retained rather than guessed at here. */
bool MdoMcpRemoveDirectoryConnections(cstr DirectoryId,char Error[512]){
 if(!MdoExtensionDirectoryId(DirectoryId))return false;
 xdir D=MdoHomeOpenDirectory("data/mcp/connections",XDIR_STAT);
 if(!D){const xerror* E=xrtGetError();if(E&&xrtErrorKind(E)==XERR_NOT_FOUND){xrtClearError();return true;}return false;}
 bool Ok=true;xdirentry E;xdirnext Next=XDIR_NEXT_END;size_t Count=0;
 while((Next=xrtDirNext(D,&E))==XDIR_NEXT_ITEM){
  if(++Count>4096){Ok=false;break;}if(E.Info.Type!=XFILE_TYPE_FILE||!(E.Flags&XDIR_ENTRY_UTF8)||E.Name.Size<6||E.Name.Size>101||memcmp(E.Name.Data+E.Name.Size-5,".json",5))continue;
  char* Path=xrtFormat("data/mcp/connections/%.*s",(int)E.Name.Size,E.Name.Data);char* Text=Path?MdoExtensionRead(Path,true,262144,NULL):NULL;
  xvalue* Doc=Text?xrtJsonParse(xrtStrView(Text)):NULL;cstr Id=MdoExtensionText(Doc,"extension_id",96);
  if(!Doc)Ok=false;else if(Id&&!strcmp(Id,DirectoryId)&&!MdoHomeRemove(Path,false))Ok=false;
  if(Text){xrtSecureZero(Text,strlen(Text));xrtFree(Text);}xrtValueRelease(Doc);xrtFree(Path);
 }if(Next==XDIR_NEXT_ERROR)Ok=false;if(!xrtDirClose(D))Ok=false;if(!Ok&&Error)snprintf(Error,512,"The extension directory was removed, but one or more private MCP bindings require cleanup");return Ok;
}
