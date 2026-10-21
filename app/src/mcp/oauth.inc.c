#include "../../include/mdo/time.h"
#include "../../include/mdo/http_url.h"
#include <xoauth2.h>

#define MDO_MCP_OAUTH_LIMIT 512u
#define MDO_MCP_OAUTH_CALLBACK "/api/v1/mcp/oauth/callback"

typedef struct MdoMcpOAuthEntry {
    char* Source;
    char* ServerId;
    char* Endpoint;
    char* Config;
    char Key[65];
    bool Active;
    xmutex* Lock;
    xmutex* Tokens;
    xcancel* Cancel;
    xthread* Thread;
    bool Busy;
    bool HasToken;
    bool NeedsAuthorization;
    uint64 Expires;
    unsigned Status;
    uint16 HttpStatus;
    char Message[256];
    char* AuthorizationUrl;
} MdoMcpOAuthEntry;

enum { MDO_MCP_AUTH_IDLE, MDO_MCP_AUTH_DISCOVERY, MDO_MCP_AUTH_READY,
    MDO_MCP_AUTH_EXCHANGE, MDO_MCP_AUTH_ERROR };

static struct {
    xmutex* Lock;
    MdoMcpOAuthEntry* Entries[MDO_MCP_OAUTH_LIMIT];
    size_t Count;
    MdoMcpOAuthEntry* Candidate[128];
    size_t CandidateCount;
    const char* CandidateSource;
    size_t Running;
    bool Stopping;
} g_MdoMcpOAuth;

static cstr McpOAuthText(const xvalue* Object,cstr Key)
{
    xstrview Text={0};
    if(!xrtValueGetString(xrtValueObjectGet(Object,xrtStrView(Key)),&Text)||Text.Size>8192||memchr(Text.Data,0,Text.Size))return NULL;
    return Text.Data;
}

static bool McpOAuthSet(xvalue* Object,cstr Key,cstr Text)
{
    return xrtValueObjectSetNew(Object,xrtStrView(Key),xrtValueString(xrtStrView(Text?Text:"")));
}

static uint64 McpOAuthUInt(const xvalue* Object,cstr Key)
{
    uint64 Result=0;int64 Signed=0;const xvalue* Value=xrtValueObjectGet(Object,xrtStrView(Key));
    if(xrtValueGetUInt(Value,&Result))return Result;
    return xrtValueGetInt(Value,&Signed)&&Signed>0?(uint64)Signed:0;
}

static bool McpOAuthBool(const xvalue* Object,cstr Key)
{
    bool Value=false;(void)xrtValueGetBool(xrtValueObjectGet(Object,xrtStrView(Key)),&Value);return Value;
}

static void McpOAuthSecretRelease(xvalue* Object,unsigned Depth)
{
    if(!Object||Depth>20)return;
    if(xrtValueType(Object)==XVALUE_STRING) {
        xstrview Text={0};if(xrtValueGetString(Object,&Text))xrtSecureZero((void*)Text.Data,Text.Size);
    } else if(xrtValueType(Object)==XVALUE_OBJECT) {
        for(size_t i=0;i<xrtValueCount(Object);i++)McpOAuthSecretRelease(xrtValueObjectAt(Object,i,NULL),Depth+1);
    } else if(xrtValueType(Object)==XVALUE_ARRAY) {
        for(size_t i=0;i<xrtValueCount(Object);i++)McpOAuthSecretRelease(xrtValueArrayGet(Object,i),Depth+1);
    }
}

static void McpOAuthSecretFree(xvalue* Object)
{
    McpOAuthSecretRelease(Object,0);xrtValueRelease(Object);
}

static char* McpOAuthOrigin(cstr Url)
{
    if(!Url||!MdoHttpUrlValid(xrtStrView(Url),true))return NULL;
    const char* At=strstr(Url,"://");if(!At)return NULL;
    const char* End=At+3;while(*End&&*End!='/'&&*End!='?')End++;
    char* Origin=xrtStrDupN(Url,(size_t)(End-Url));if(!Origin)return NULL;
    for(char* P=Origin;*P;P++)if(*P>='A'&&*P<='Z')*P=(char)(*P+32);
    size_t N=strlen(Origin);
    if(N>4&&!strncmp(Origin,"https:",6)&&!strcmp(Origin+N-4,":443"))Origin[N-4]=0;
    if(N>3&&!strncmp(Origin,"http:",5)&&!strcmp(Origin+N-3,":80"))Origin[N-3]=0;
    return Origin;
}

static bool McpOAuthLoopback(cstr Url)
{
    char* Origin=McpOAuthOrigin(Url);bool Result=false;
    if(Origin) {
        cstr Host=strstr(Origin,"://")+3;
        Result=(!strncmp(Host,"127.0.0.1",9)&&(Host[9]==0||Host[9]==':'))||
            (!strncmp(Host,"[::1]",5)&&(Host[5]==0||Host[5]==':'))||
            (!strncmp(Host,"localhost",9)&&(Host[9]==0||Host[9]==':'));
    }
    xrtFree(Origin);return Result;
}

static bool McpOAuthUrl(cstr Url,cstr Resource)
{
    if(!Url||strlen(Url)>4096||!MdoHttpUrlValid(xrtStrView(Url),true))return false;
    if(MdoHttpUrlAsciiEqual(Url,"https://",8))return true;
    if(!McpOAuthLoopback(Url)||!McpOAuthLoopback(Resource))return false;
    char* A=McpOAuthOrigin(Url),*B=McpOAuthOrigin(Resource);
    bool Ok=A&&B&&!strcmp(A,B);xrtFree(A);xrtFree(B);return Ok;
}

static char* McpOAuthCanonical(cstr Url)
{
    char* Origin=McpOAuthOrigin(Url);if(!Origin)return NULL;
    cstr Path=strstr(Url,"://")+3;while(*Path&&*Path!='/'&&*Path!='?')Path++;
    if(!strcmp(Path,"/"))Path="";
    char* Result=xrtFormat("%s%s",Origin,Path);xrtFree(Origin);return Result;
}

static bool McpOAuthHash(cstr Text,char Output[65])
{
    unsigned char Digest[32];if(!Text||!xrtSha256(Text,strlen(Text),Digest))return false;
    for(size_t i=0;i<32;i++)snprintf(Output+2*i,3,"%02x",Digest[i]);
    return true;
}

static char* McpOAuthConfig(const xvalue* Value,char Error[1024])
{
    bool Enabled=true;
    if(!Value)return NULL;
    if(xrtValueType(Value)==XVALUE_BOOL) {
        xrtValueGetBool(Value,&Enabled);if(!Enabled)return NULL;
    } else if(xrtValueType(Value)!=XVALUE_OBJECT)goto invalid;
    else if(xrtValueObjectHas(Value,XRT_STR_LITERAL("enabled"))&&
        (!xrtValueGetBool(xrtValueObjectGet(Value,XRT_STR_LITERAL("enabled")),&Enabled)))goto invalid;
    if(!Enabled)return NULL;
    const char* Keys[]={"enabled","client_id","client_metadata_url","client_secret_ref","scope",
        "resource_metadata_url","authorization_server","redirect_uri","auth_method"};
    if(xrtValueType(Value)==XVALUE_OBJECT)for(size_t i=0;i<xrtValueCount(Value);i++) {
        xstrview Key={0};(void)xrtValueObjectAt(Value,i,&Key);bool Known=false;
        for(size_t j=0;j<sizeof(Keys)/sizeof(*Keys);j++)if(Key.Size==strlen(Keys[j])&&!memcmp(Key.Data,Keys[j],Key.Size))Known=true;
        if(!Known)goto invalid;
    }
    xvalue* Canonical=xrtValueObject();bool Ok=Canonical&&xrtValueObjectSetNew(Canonical,XRT_STR_LITERAL("enabled"),xrtValueBool(true));
    for(size_t i=1;Ok&&i<sizeof(Keys)/sizeof(*Keys);i++) {
        const xvalue* Field=xrtValueObjectGet(Value,xrtStrView(Keys[i]));
        if(!Field||xrtValueType(Field)==XVALUE_NULL)continue;
        cstr Text=McpOAuthText(Value,Keys[i]);
        if(!Text||strlen(Text)>4096){Ok=false;break;}
        for(cstr P=Text;*P;P++)if((unsigned char)*P<32||(unsigned char)*P==127)Ok=false;
        if(!strcmp(Keys[i],"client_secret_ref")&&!MdoSecretReferenceSyntaxValid(xrtStrView(Text)))Ok=false;
        if((!strcmp(Keys[i],"client_metadata_url")||!strcmp(Keys[i],"resource_metadata_url")||
            !strcmp(Keys[i],"authorization_server")||!strcmp(Keys[i],"redirect_uri"))&&Text[0]&&!MdoHttpUrlValid(xrtStrView(Text),true))Ok=false;
        if(!strcmp(Keys[i],"auth_method")&&strcmp(Text,"none")&&strcmp(Text,"client_secret_basic")&&strcmp(Text,"client_secret_post"))Ok=false;
        if(Ok&&Text[0])Ok=McpOAuthSet(Canonical,Keys[i],Text);
    }
    char* Result=Ok?xrtJsonStringify(Canonical,false,NULL):NULL;xrtValueRelease(Canonical);
    if(Result)return Result;
invalid:
    snprintf(Error,1024,"Invalid OAuth configuration; use known fields and secret references, not a plaintext client secret");return NULL;
}

static xvalue* McpOAuthRead(MdoMcpOAuthEntry* Entry)
{
    char Path[120],Purpose[100];snprintf(Path,sizeof(Path),"data/mcp/oauth/%s.bin",Entry->Key);
    snprintf(Purpose,sizeof(Purpose),"mdo.mcp.oauth:%s",Entry->Key);
    xfile File=MdoHomeOpenRead(Path);
    if(!File){bool Missing=xrtGetError()&&xrtErrorKind(xrtGetError())==XERR_NOT_FOUND;
        if(Missing)xrtClearError();
        return Missing?xrtValueObject():NULL;}
    uint64 Size=0;bytes Data=NULL;bytes Plain=NULL;size_t PlainSize=0;
    bool Ok=xrtFileSize(File,&Size)&&Size<=65536u&&Size>0;
    if(Ok)Data=xrtMalloc((size_t)Size);
    Ok=Ok&&Data&&xrtReadFull(File,Data,(size_t)Size,NULL);
    if(!xrtClose(File))Ok=false;
    if(Ok)Ok=xsCredentialUnseal(Purpose,Data,(size_t)Size,&Plain,&PlainSize);
    xvalue* Result=NULL;
    if(Ok) {
        xjsonreadconfig Config;xrtJsonReadConfigInit(&Config);Config.MaxInputBytes=XS_CREDENTIAL_PLAIN_LIMIT;
        Config.MaxDepth=12;Config.MaxValues=512;Config.MaxStringBytes=8192;
        Result=xrtJsonRead(xrtStrViewN((cstr)Plain,PlainSize),&Config);
        if(xrtValueType(Result)!=XVALUE_OBJECT){McpOAuthSecretFree(Result);Result=NULL;}
    }
    if(Plain){xrtSecureZero(Plain,PlainSize);xrtFree(Plain);}xrtFree(Data);return Result;
}

static void McpOAuthCache(MdoMcpOAuthEntry* Entry,const xvalue* Record)
{
    xrtMutexLock(Entry->Lock);
    const xvalue* Token=xrtValueObjectGet(Record,XRT_STR_LITERAL("tokens"));
    Entry->HasToken=McpOAuthText(Token,"access_token")!=NULL;
    Entry->Expires=McpOAuthUInt(Token,"expires_at");
    Entry->NeedsAuthorization=McpOAuthBool(Record,"needs_authorization");
    xrtMutexUnlock(Entry->Lock);
}

/* The caller owns Tokens. Disk publication precedes cache publication. */
static bool McpOAuthSave(MdoMcpOAuthEntry* Entry,const xvalue* Record)
{
    char Path[120],Purpose[100];snprintf(Path,sizeof(Path),"data/mcp/oauth/%s.bin",Entry->Key);
    snprintf(Purpose,sizeof(Purpose),"mdo.mcp.oauth:%s",Entry->Key);
    size_t Size=0,SealedSize=0;char* Plain=xrtJsonStringify(Record,false,&Size);bytes Sealed=NULL;
    bool Ok=Plain&&Size<=XS_CREDENTIAL_PLAIN_LIMIT&&xsCredentialSeal(Purpose,Plain,Size,&Sealed,&SealedSize)&&
        MdoHomeAtomicWrite(Path,Sealed,SealedSize,false);
    if(Plain){xrtSecureZero(Plain,Size);xrtFree(Plain);}xrtFree(Sealed);
    if(Ok)McpOAuthCache(Entry,Record);
    return Ok;
}

static MdoMcpOAuthEntry* McpOAuthFind(cstr Id,cstr Key)
{
    MdoMcpOAuthEntry* Result=NULL;
    if(!g_MdoMcpOAuth.Lock||!xrtMutexLock(g_MdoMcpOAuth.Lock))return NULL;
    if(!g_MdoMcpOAuth.Stopping)for(size_t i=0;i<g_MdoMcpOAuth.Count;i++) {
        MdoMcpOAuthEntry* Entry=g_MdoMcpOAuth.Entries[i];
        if(Entry->Active&&((Id&&!strcmp(Entry->ServerId,Id))||(Key&&!strcmp(Entry->Key,Key)))){Result=Entry;break;}
    }
    xrtMutexUnlock(g_MdoMcpOAuth.Lock);return Result;
}

/* The manager serializes source transactions. Never hold this lock across
 * native registry replacement: HTTP retirement may call the token provider. */
static bool McpOAuthBeginSource(cstr Source,const MdoMcpCatalog* Catalog)
{
    if(!xrtMutexLock(g_MdoMcpOAuth.Lock))return false;
    g_MdoMcpOAuth.CandidateCount=0;g_MdoMcpOAuth.CandidateSource=Source;
    for(size_t i=0;i<Catalog->Count;i++) {
        const MdoMcpServerEntry* Server=&Catalog->Entries[i];if(!Server->OAuthJson)continue;
        cstr Id=Server->RuntimeId?Server->RuntimeId:Server->Id;
        char* Identity=xrtFormat("%s\n%s\n%s\n%s",Source,Id,Server->Endpoint,Server->OAuthJson);char Key[65];
        bool Ok=Identity&&McpOAuthHash(Identity,Key);xrtFree(Identity);
        MdoMcpOAuthEntry* Entry=NULL;
        for(size_t j=0;Ok&&j<g_MdoMcpOAuth.Count;j++)if(!strcmp(g_MdoMcpOAuth.Entries[j]->Key,Key))Entry=g_MdoMcpOAuth.Entries[j];
        if(Ok&&!Entry&&g_MdoMcpOAuth.Count<MDO_MCP_OAUTH_LIMIT) {
            Entry=xrtCalloc(1,sizeof(*Entry));
            if(Entry) {
                Entry->Source=xrtStrDup(Source);Entry->ServerId=xrtStrDup(Id);Entry->Endpoint=xrtStrDup(Server->Endpoint);Entry->Config=xrtStrDup(Server->OAuthJson);
                Entry->Lock=xrtMutexCreate();Entry->Tokens=xrtMutexCreate();Entry->Cancel=xrtCancelCreate();strcpy(Entry->Key,Key);
                if(!Entry->Source||!Entry->ServerId||!Entry->Endpoint||!Entry->Config||!Entry->Lock||!Entry->Tokens||!Entry->Cancel) {
                    xrtFree(Entry->Source);xrtFree(Entry->ServerId);xrtFree(Entry->Endpoint);xrtFree(Entry->Config);
                    if(Entry->Lock)xrtMutexDestroy(Entry->Lock);
                    if(Entry->Tokens)xrtMutexDestroy(Entry->Tokens);
                    if(Entry->Cancel)xrtCancelDestroy(Entry->Cancel);
                    xrtFree(Entry);Entry=NULL;
                } else {
                    g_MdoMcpOAuth.Entries[g_MdoMcpOAuth.Count++]=Entry;
                    xvalue* Record=McpOAuthRead(Entry);McpOAuthCache(Entry,Record);McpOAuthSecretFree(Record);
                }
            }
        }
        if(!Ok||!Entry){g_MdoMcpOAuth.CandidateCount=0;xrtMutexUnlock(g_MdoMcpOAuth.Lock);return false;}
        g_MdoMcpOAuth.Candidate[g_MdoMcpOAuth.CandidateCount++]=Entry;
    }
    xrtMutexUnlock(g_MdoMcpOAuth.Lock);
    return true;
}

static void McpOAuthEndSource(bool Commit)
{
    xrtMutexLock(g_MdoMcpOAuth.Lock);
    if(Commit) {
        for(size_t i=0;i<g_MdoMcpOAuth.Count;i++)if(!strcmp(g_MdoMcpOAuth.Entries[i]->Source,g_MdoMcpOAuth.CandidateSource))g_MdoMcpOAuth.Entries[i]->Active=false;
        for(size_t i=0;i<g_MdoMcpOAuth.CandidateCount;i++)g_MdoMcpOAuth.Candidate[i]->Active=true;
    }
    g_MdoMcpOAuth.CandidateCount=0;g_MdoMcpOAuth.CandidateSource=NULL;xrtMutexUnlock(g_MdoMcpOAuth.Lock);
}

static void McpOAuthState(MdoMcpOAuthEntry* Entry,unsigned State,cstr Message,uint16 HttpStatus)
{
    xrtMutexLock(Entry->Lock);Entry->Status=State;Entry->HttpStatus=HttpStatus;
    snprintf(Entry->Message,sizeof(Entry->Message),"%s",Message?Message:"");xrtMutexUnlock(Entry->Lock);
}

#include "oauth_flow.inc.c"
