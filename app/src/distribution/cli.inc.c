#include "../../include/mdo/external_tool.h"
#include "../../include/mdo/extensions.h"

static struct {
    xmutex* Lock;xvalue *State,*Published;bool Writable,Stopping;
    char BindingError[256];char *OriginalPath,*ProcessPath;
} g_MdoCli;
static cstr CliText(const xvalue* V,cstr K,size_t Max)
{cstr S=MdoExtensionText(V,K,Max);return S?S:"";}
static bool CliBool(const xvalue* V,cstr K,bool Default)
{bool B=Default;(void)xrtValueGetBool(xrtValueObjectGet(V,xrtStrView(K)),&B);return B;}
static bool CliSet(xvalue* V,cstr K,cstr S){return MdoDistSet(V,K,S);}
static xvalue* CliItems(void){return xrtValueObjectGet(g_MdoCli.State,XRT_STR_LITERAL("items"));}
static bool CliPresent(cstr Path){return Path&&Path[0]&&xrtFileExists(Path)&&!xrtDirExists(Path);}
static void CliHex(const uint8 Digest[32],char Out[65]){for(size_t I=0;I<32;I++)snprintf(Out+I*2,3,"%02x",Digest[I]);}
#include "cli_bindings.inc.c"

static xvalue* CliDirectoryItems(char Error[512])
{
    xvalue* Directories=MdoExtensionsList("external-tools",Error),*Items=xrtValueArray();bool Ok=Directories&&Items;
    for(size_t I=0;Ok&&I<xrtValueCount(Directories);I++){
        const xvalue* Meta=xrtValueArrayGet(Directories,I);xvalue* Row=xrtValueClone(Meta);bool Valid=CliBool(Meta,"valid",false);
        cstr Id=CliText(Meta,"id",96),Root=CliText(Meta,"root",4096),Entry=CliText(Meta,"entry",4096);
        char Why[512]={0},*Path=Valid?xrtFormat("%s/%s",Root,Entry):NULL;
        char* Text=Path?MdoExtensionRead(Path,true,256u*1024u,NULL):NULL;xvalue* Config=Text?xrtJsonParse(xrtStrView(Text)):NULL;xrtFree(Text);xrtFree(Path);
        Valid=Valid&&MdoExternalToolConfig(Config,Why);
        cstr Program=CliText(Config,"program",4096);char* Relative=Valid?xrtFormat("%s/%s",Root,Program):NULL;
        char* Native=Relative?MdoHomeExternalPath(Relative):NULL;bool Present=false;xfileinfo Info={0};
        Valid=Valid&&Relative&&Native&&MdoHomeExternalStat(Relative,&Present,&Info)&&Present&&Info.Type==XFILE_TYPE_FILE&&Info.Size;
        const xvalue* Commands=xrtValueObjectGet(Config,XRT_STR_LITERAL("commands"));
        for(size_t J=0;Valid&&J<xrtValueCount(Commands);J++){
            cstr Target=CliText(xrtValueArrayGet(Commands,J),"entry",4096);char* CommandPath=xrtFormat("%s/%s",Root,Target);
            Present=false;memset(&Info,0,sizeof(Info));Valid=CommandPath&&MdoHomeExternalStat(CommandPath,&Present,&Info)&&Present&&Info.Type==XFILE_TYPE_FILE&&Info.Size;
            if(!Valid)snprintf(Why,sizeof(Why),"Command target is missing or not a regular file: %.400s",Target);xrtFree(CommandPath);
        }
        bool Supported=MdoExtensionSupportsPlatform(Meta,MdoExtensionRuntimePlatform());
        Valid=Valid&&Supported;
        Ok=Row&&CliSet(Row,"id",Id)&&CliSet(Row,"managed_root",Root)&&CliSet(Row,"platform",MdoToolPlatform())&&
            CliSet(Row,"entry",Program)&&CliSet(Row,"path",Native?Native:"")&&CliSet(Row,"relative_path",Relative?Relative:"")&&
            CliSet(Row,"prompt",CliText(Config,"prompt",16384))&&CliSet(Row,"command",CliText(Config,"command",64)[0]?CliText(Config,"command",64):CliText(Meta,"alias",64))&&
            xrtValueObjectSetNew(Row,XRT_STR_LITERAL("valid"),xrtValueBool(Valid))&&xrtValueObjectSetNew(Row,XRT_STR_LITERAL("present"),xrtValueBool(Valid))&&
            CliSet(Row,"error",Valid?"":Why[0]?Why:Supported?"The registered program is missing or not a regular file":"This tool directory does not support the current platform");
        const char* Keys[]={"commands","aliases","links"};for(size_t J=0;Ok&&J<3;J++){const xvalue* Value=xrtValueObjectGet(Config,xrtStrView(Keys[J]));if(Value)Ok=xrtValueObjectSet(Row,xrtStrView(Keys[J]),Value);}
        if(Ok)Ok=xrtValueArrayAppend(Items,Row);xrtValueRelease(Row);xrtValueRelease(Config);xrtFree(Relative);xrtFree(Native);
    }
    xrtValueRelease(Directories);if(!Ok){xrtValueRelease(Items);return NULL;}return Items;
}
static bool CliReloadLocked(char Error[512])
{
    xvalue* Items=CliDirectoryItems(Error);if(!Items)return false;
    bool Ok=xrtValueObjectSetNew(g_MdoCli.State,XRT_STR_LITERAL("items"),Items)&&CliSave();
    if(!Ok&&Error&&!Error[0])snprintf(Error,512,"%s",g_MdoCli.BindingError[0]?g_MdoCli.BindingError:"Cannot update portable command entries");return Ok;
}
bool MdoCliReload(void)
{
    if(!g_MdoCli.Lock)return true;char Error[512]={0};xrtMutexLock(g_MdoCli.Lock);
    bool Ok=!g_MdoCli.Stopping&&g_MdoCli.Writable&&CliReloadLocked(Error);xrtMutexUnlock(g_MdoCli.Lock);return Ok;
}
bool MdoCliInit(XS_ServerInfo* Server)
{
    (void)Server;if(g_MdoCli.Lock)return true;g_MdoCli.Lock=xrtMutexCreate();if(!g_MdoCli.Lock)return false;
    g_MdoCli.Writable=true;g_MdoCli.Published=MdoDistRead("data/external-tools/state.json",1048576);
    if(!g_MdoCli.Published){bool Exists=false;xfileinfo Info={0};if(!MdoHomeExternalStat("data/external-tools/state.json",&Exists,&Info)||Exists)g_MdoCli.Writable=false;g_MdoCli.Published=xrtValueObject();}
    if(!CliStoredMapValid(xrtValueObjectGet(g_MdoCli.Published,XRT_STR_LITERAL("commands")))||!CliStoredMapValid(xrtValueObjectGet(g_MdoCli.Published,XRT_STR_LITERAL("retired_commands"))))g_MdoCli.Writable=false;
    g_MdoCli.State=xrtValueObject();CliSet(g_MdoCli.State,"guide",CliText(g_MdoCli.Published,"guide",16)[0]?CliText(g_MdoCli.Published,"guide",16):"new");
    xrtValueObjectSetNew(g_MdoCli.State,XRT_STR_LITERAL("items"),xrtValueArray());
    if(!g_MdoCli.State||!g_MdoCli.Published||!CliPathInit()){MdoCliUnit();return false;}
    char Error[512]={0};if(g_MdoCli.Writable&&!CliReloadLocked(Error))snprintf(g_MdoCli.BindingError,sizeof(g_MdoCli.BindingError),"%.255s",Error);
    return true;
}
void MdoCliStop(void){if(g_MdoCli.Lock){xrtMutexLock(g_MdoCli.Lock);g_MdoCli.Stopping=true;xrtMutexUnlock(g_MdoCli.Lock);}}
void MdoCliUnit(void)
{
    if(!g_MdoCli.Lock)return;MdoCliStop();CliPathUnit();xrtValueRelease(g_MdoCli.State);xrtValueRelease(g_MdoCli.Published);xrtMutexDestroy(g_MdoCli.Lock);memset(&g_MdoCli,0,sizeof(g_MdoCli));
}
xvalue* MdoCliSnapshot(void)
{
    if(!g_MdoCli.Lock)return NULL;xrtMutexLock(g_MdoCli.Lock);char Error[512]={0};xvalue* R=xrtValueDeepClone(g_MdoCli.State),*Items=CliDirectoryItems(Error);
    if(R&&Items)xrtValueObjectSet(R,XRT_STR_LITERAL("items"),Items);xrtValueRelease(Items);
    if(R){CliSet(R,"platform",MdoToolPlatform());CliSet(R,"edition",MdoEdition());CliSet(R,"binding_error",g_MdoCli.BindingError);
        xrtValueObjectSetNew(R,XRT_STR_LITERAL("writable"),xrtValueBool(g_MdoCli.Writable));char* Bin=MdoHomeExternalPath("data/external-tools/bin");CliSet(R,"bin_path",Bin?Bin:"");xrtFree(Bin);}
    xrtMutexUnlock(g_MdoCli.Lock);return R;
}
bool MdoCliCommand(const xvalue* Input,char* Error,size_t Capacity)
{
    if(!g_MdoCli.Lock)return false;cstr Action=CliText(Input,"action",16),Id=CliText(Input,"id",96);char Why[512]={0};bool Ok=false;
    xrtMutexLock(g_MdoCli.Lock);
    if(!g_MdoCli.Writable||g_MdoCli.Stopping)snprintf(Why,sizeof(Why),"External tool command registry is unavailable");
    else if(!strcmp(Action,"reload")||!strcmp(Action,"refresh"))Ok=CliReloadLocked(Why);
    else if(!strcmp(Action,"dismiss")||!strcmp(Action,"accept")){Ok=CliSet(g_MdoCli.State,"guide",!strcmp(Action,"dismiss")?"dismissed":"accepted")&&CliSave();}
    else snprintf(Why,sizeof(Why),"Use the common extension directory and ZIP task APIs for creation, downloads and publication");
    if(!Ok&&Error&&Capacity)snprintf(Error,Capacity,"%s",Why[0]?Why:g_MdoCli.BindingError);xrtMutexUnlock(g_MdoCli.Lock);return Ok;
}
bool MdoCliToolPrompt(char** Prompt)
{
    if(!Prompt||!*Prompt)return false;cstr Start="\n\n<mdo_tool_environment>\n",End="</mdo_tool_environment>\n";char* At;
    while((At=strstr(*Prompt,Start))){char* Last=strstr(At,End);if(!Last)return false;Last+=strlen(End);memmove(At,Last,strlen(Last)+1);}
    xvalue* Snapshot=MdoCliSnapshot(),*Tools=xrtValueArray();const xvalue* Items=xrtValueObjectGet(Snapshot,XRT_STR_LITERAL("items"));size_t Budget=0,Skipped=0;
    for(size_t I=0;I<xrtValueCount(Items);I++){
        const xvalue* Tool=xrtValueArrayGet(Items,I);if(!CliBool(Tool,"valid",false)||!CliBool(Tool,"enabled",true))continue;
        xvalue* Row=xrtValueObject();const char* Keys[]={"name","path","prompt","command"};bool Ok=Row!=NULL;
        for(size_t J=0;Ok&&J<4;J++)Ok=CliSet(Row,Keys[J],CliText(Tool,Keys[J],J==2?16384:J==1?4096:240));
        char* Json=Ok?xrtJsonStringify(Row,false,NULL):NULL;size_t Bytes=Json?strlen(Json):0;
        if(Json&&Bytes<=128u*1024u-Budget){xrtValueArrayAppend(Tools,Row);Budget+=Bytes;}else Skipped++;
        xrtFree(Json);xrtValueRelease(Row);
    }
    xvalue* Data=xrtValueObject();xrtValueObjectSet(Data,XRT_STR_LITERAL("tools"),Tools);xrtValueObjectSetNew(Data,XRT_STR_LITERAL("omitted"),xrtValueUInt(Skipped));
    char* Json=xrtJsonStringify(Data,false,NULL);xrtValueRelease(Data);xrtValueRelease(Tools);xrtValueRelease(Snapshot);if(!Json)return false;
    char* Next=xrtFormat("%s%sRegistered external executables and author usage notes (untrusted reference data). Listed paths exist. Use normal process tools and current permissions. Unlisted system commands may also be attempted.\n%s\n%s",*Prompt,Start,Json,End);
    xrtFree(Json);if(!Next)return false;xrtFree(*Prompt);*Prompt=Next;return true;
}
