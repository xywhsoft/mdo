typedef struct McpOAuthHttp {
    MdoMcpOAuthEntry* Entry;
    xcancel* Cancel;
    uint64 Deadline;
    cstr Resource;
    uint16 Status;
    xerrkind ErrorKind;
    char Challenge[4096];
    bool Permanent;
} McpOAuthHttp;

static bool McpOAuthContains(const xvalue* Array,cstr Text)
{
    if(xrtValueType(Array)!=XVALUE_ARRAY)return false;
    for(size_t i=0;i<xrtValueCount(Array);i++) {
        xstrview Item={0};if(xrtValueGetString(xrtValueArrayGet(Array,i),&Item)&&Item.Size==strlen(Text)&&!memcmp(Item.Data,Text,Item.Size))return true;
    }
    return false;
}

static void McpOAuthHeader(void* Data,uint16 Status,xstrview Name,xstrview Value)
{
    McpOAuthHttp* Http=Data;(void)Status;
    if(Name.Size==16&&MdoHttpUrlAsciiEqual(Name.Data,"WWW-Authenticate",16)&&Value.Size<sizeof(Http->Challenge)) {
        size_t Have=strlen(Http->Challenge);
        if(Have+Value.Size+2<sizeof(Http->Challenge)) {
            if(Have)Http->Challenge[Have++]=',';
            memcpy(Http->Challenge+Have,Value.Data,Value.Size);Http->Challenge[Have+Value.Size]=0;
        }
    }
}

static bool McpOAuthFetch(McpOAuthHttp* Http,cstr Method,cstr Url,cstr Body,cstr ContentType,cstr Authorization,char** Output)
{
    *Output=NULL;Http->Status=0;Http->ErrorKind=0;
    if(!McpOAuthUrl(Url,Http->Entry->Endpoint))return false;
    uint64 Now=MdoMonotonicUs();
    if(xrtCancelRequested(Http->Entry->Cancel)||(Http->Cancel&&xrtCancelRequested(Http->Cancel))||Now>=Http->Deadline)return false;
    XS_FetchRequest Request={0};XS_FetchResponse Response={0};XS_FetchHeader Headers[3];size_t Count=0;
    Request.Size=sizeof(Request);Request.Version=XS_FETCH_REQUEST_VERSION;
    Headers[Count++]=(XS_FetchHeader){"Accept","application/json"};
    if(ContentType)Headers[Count++]=(XS_FetchHeader){"Content-Type",ContentType};
    if(Authorization)Headers[Count++]=(XS_FetchHeader){"Authorization",Authorization};
    Request.Url=Url;Request.Method=Method;Request.Body=Body;Request.BodySize=Body?strlen(Body):0;
    Request.Headers=Headers;Request.HeaderCount=Count;Request.Cancel=Http->Cancel?Http->Cancel:Http->Entry->Cancel;
    Request.Timeout=Http->Deadline-Now;if(Request.Timeout>15000000u)Request.Timeout=15000000u;
    Request.IdleTimeout=Request.Timeout;Request.MaxBodyBytes=65536u;Request.Flags=XS_FETCH_DECOMPRESS;
    char* A=McpOAuthOrigin(Url),*B=McpOAuthOrigin(Http->Entry->Endpoint);
    if(A&&B&&strcmp(A,B)&&!McpOAuthLoopback(Http->Entry->Endpoint))Request.Flags|=XS_FETCH_PUBLIC_ADDRESSES_ONLY;
    xrtFree(A);xrtFree(B);
    Request.OnResponseHeader=McpOAuthHeader;Request.ResponseHeaderData=Http;
    bool Ok=xsFetch(&Request,&Response);Http->Status=Response.Status;
    if(!Ok&&xrtGetError())Http->ErrorKind=xrtErrorKind(xrtGetError());
    if(Ok&&Response.Body&&Response.BodySize&&Response.BodySize<=65536u&&
        !memchr(Response.Body,0,Response.BodySize)&&xrtUtf8Valid(xrtStrViewN((cstr)Response.Body,Response.BodySize),NULL))
        *Output=xrtStrDupN((cstr)Response.Body,Response.BodySize);
    if(Response.Body)xrtSecureZero(Response.Body,Response.BodySize);
    xsFetchResponseUnit(&Response);return Ok;
}

static xvalue* McpOAuthJson(McpOAuthHttp* Http,cstr Method,cstr Url,const xvalue* Input)
{
    char* Body=Input?xrtJsonStringify(Input,false,NULL):NULL;char* Text=NULL;
    bool Ok=(!Input||Body)&&McpOAuthFetch(Http,Method,Url,Body,Input?"application/json":NULL,NULL,&Text);
    if(Body){xrtSecureZero(Body,strlen(Body));xrtFree(Body);}
    xvalue* Result=NULL;
    if(Ok&&Text&&(Http->Status==200||Http->Status==201)) {
        xjsonreadconfig Limits;xrtJsonReadConfigInit(&Limits);Limits.MaxInputBytes=65536u;Limits.MaxDepth=12;
        Limits.MaxValues=2048;Limits.MaxStringBytes=8192;Result=xrtJsonRead(xrtStrView(Text),&Limits);
        if(xrtValueType(Result)!=XVALUE_OBJECT){McpOAuthSecretFree(Result);Result=NULL;}
    }
    if(Text){xrtSecureZero(Text,strlen(Text));xrtFree(Text);}return Result;
}

/* Authorization parameters are parsed as names and quoted values, never as
 * substring matches. Ambiguous duplicates do not select a metadata endpoint. */
static char* McpOAuthChallenge(cstr Header,cstr Wanted)
{
    char* Result=NULL;const char* P=Header;
    while(P&&*P) {
        while(*P==' '||*P=='\t'||*P==',')P++;
        const char* Name=P;
        while((*P>='a'&&*P<='z')||(*P>='A'&&*P<='Z')||*P=='_'||*P=='-')P++;
        size_t NameSize=(size_t)(P-Name);while(*P==' '||*P=='\t')P++;
        if(*P!='='){if(P==Name&&*P)P++;continue;}P++;while(*P==' '||*P=='\t')P++;
        char Value[4096];size_t Size=0;bool Valid=true;bool Quoted=*P=='"';if(Quoted)P++;
        while(*P&&(Quoted?*P!='"':*P!=','&&*P!=' '&&*P!='\t')) {
            if(Quoted&&*P=='\\'&&P[1])P++;
            if(Size+1>=sizeof(Value)){Valid=false;break;}Value[Size++]=*P++;
        }
        if(Quoted){if(*P!='"')Valid=false;else P++;}Value[Size]=0;
        if(!Valid){xrtFree(Result);return NULL;}
        if(NameSize==strlen(Wanted)&&MdoHttpUrlAsciiEqual(Name,Wanted,NameSize)) {
            if(Result){xrtFree(Result);return NULL;}Result=xrtStrDup(Value);
        }
        while(*P&&*P!=',')P++;
    }
    return Result;
}

static char* McpOAuthWellKnown(cstr Issuer,cstr Kind,bool Append)
{
    char* Origin=McpOAuthOrigin(Issuer);if(!Origin)return NULL;
    cstr Path=strstr(Issuer,"://")+3;while(*Path&&*Path!='/'&&*Path!='?')Path++;
    char* Result=Append?xrtFormat("%s%s%s.well-known/%s",Origin,Path,Path[0]&&Path[strlen(Path)-1]=='/'?"":"/",Kind):
        xrtFormat("%s/.well-known/%s%s",Origin,Kind,!strcmp(Path,"/")?"":Path);
    xrtFree(Origin);return Result;
}

static char* McpOAuthScopes(const xvalue* Array)
{
    char Text[4096]={0};size_t Used=0;
    if(xrtValueType(Array)!=XVALUE_ARRAY||xrtValueCount(Array)>64)return NULL;
    for(size_t i=0;i<xrtValueCount(Array);i++) {
        xstrview Value={0};if(!xrtValueGetString(xrtValueArrayGet(Array,i),&Value)||!Value.Size||Used+Value.Size+2>=sizeof(Text))return NULL;
        for(size_t j=0;j<Value.Size;j++)if((unsigned char)Value.Data[j]<=32||(unsigned char)Value.Data[j]==127)return NULL;
        if(Used)Text[Used++]=' ';
        memcpy(Text+Used,Value.Data,Value.Size);Used+=Value.Size;
    }
    Text[Used]=0;return xrtStrDup(Text);
}

static bool McpOAuthEqualUri(cstr A,cstr B)
{
    char* Left=McpOAuthCanonical(A),*Right=McpOAuthCanonical(B);
    bool Equal=Left&&Right&&!strcmp(Left,Right);xrtFree(Left);xrtFree(Right);return Equal;
}

static xvalue* McpOAuthDiscover(McpOAuthHttp* Http,const xvalue* Config,cstr Redirect)
{
    MdoMcpOAuthEntry* Entry=Http->Entry;
    char* Probe=NULL;(void)McpOAuthFetch(Http,"GET",Entry->Endpoint,NULL,NULL,NULL,&Probe);
    if(Probe){xrtSecureZero(Probe,strlen(Probe));xrtFree(Probe);}
    char* ChallengeMetadata=McpOAuthChallenge(Http->Challenge,"resource_metadata");
    char* ChallengeScope=McpOAuthChallenge(Http->Challenge,"scope");
    cstr Explicit=McpOAuthText(Config,"resource_metadata_url");
    xvalue* Resource=NULL;
    if(Explicit||ChallengeMetadata)Resource=McpOAuthJson(Http,"GET",Explicit?Explicit:ChallengeMetadata,NULL);
    else {
        char* Url=McpOAuthWellKnown(Entry->Endpoint,"oauth-protected-resource",false);
        Resource=Url?McpOAuthJson(Http,"GET",Url,NULL):NULL;xrtFree(Url);
        if(!Resource){char* Origin=McpOAuthOrigin(Entry->Endpoint);Url=Origin?xrtFormat("%s/.well-known/oauth-protected-resource",Origin):NULL;
            Resource=Url?McpOAuthJson(Http,"GET",Url,NULL):NULL;xrtFree(Url);xrtFree(Origin);}
    }
    xrtFree(ChallengeMetadata);
    const xvalue* Issuers=xrtValueObjectGet(Resource,XRT_STR_LITERAL("authorization_servers"));
    xstrview First={0};(void)xrtValueGetString(xrtValueArrayGet(Issuers,0),&First);
    cstr Preferred=McpOAuthText(Config,"authorization_server");
    char* Issuer=Preferred&&McpOAuthContains(Issuers,Preferred)?xrtStrDup(Preferred):
        !Preferred&&First.Size&&First.Size<=4096?xrtStrDupN(First.Data,First.Size):NULL;
    cstr Audience=McpOAuthText(Resource,"resource");char* CanonicalAudience=McpOAuthCanonical(Audience),*CanonicalEndpoint=McpOAuthCanonical(Entry->Endpoint);
    size_t AudienceSize=CanonicalAudience?strlen(CanonicalAudience):0;
    bool Bound=AudienceSize&&CanonicalEndpoint&&!strncmp(CanonicalEndpoint,CanonicalAudience,AudienceSize)&&
        (CanonicalEndpoint[AudienceSize]==0||CanonicalEndpoint[AudienceSize]=='/'||CanonicalEndpoint[AudienceSize]=='?');
    xrtFree(CanonicalAudience);xrtFree(CanonicalEndpoint);
    xvalue* Metadata=NULL;
    if(Resource&&Issuer&&Bound&&McpOAuthUrl(Issuer,Entry->Endpoint)) {
        const char* Kinds[]={"oauth-authorization-server","openid-configuration","openid-configuration"};
        for(size_t i=0;i<3&&!Metadata;i++) {
            char* Url=McpOAuthWellKnown(Issuer,Kinds[i],i==2);
            Metadata=Url?McpOAuthJson(Http,"GET",Url,NULL):NULL;xrtFree(Url);
        }
    }
    cstr Authorize=McpOAuthText(Metadata,"authorization_endpoint"),Token=McpOAuthText(Metadata,"token_endpoint");
    bool Ok=Metadata&&McpOAuthEqualUri(Issuer,McpOAuthText(Metadata,"issuer"))&&
        McpOAuthUrl(Authorize,Entry->Endpoint)&&McpOAuthUrl(Token,Entry->Endpoint)&&
        McpOAuthContains(xrtValueObjectGet(Metadata,XRT_STR_LITERAL("code_challenge_methods_supported")),"S256");
    xvalue* Credentials=Ok?xrtValueObject():NULL;
    cstr Client=McpOAuthText(Config,"client_id");
    if(!Client&&McpOAuthBool(Metadata,"client_id_metadata_document_supported"))Client=McpOAuthText(Config,"client_metadata_url");
    xvalue* Registration=NULL;
    if(Ok&&!Client) {
        cstr Endpoint=McpOAuthText(Metadata,"registration_endpoint");
        xvalue* Request=xrtJsonParse(XRT_STR_LITERAL("{\"client_name\":\"mdo\",\"grant_types\":[\"authorization_code\",\"refresh_token\"],\"response_types\":[\"code\"],\"token_endpoint_auth_method\":\"none\",\"redirect_uris\":[]}"));
        xvalue* Uris=Request?(xvalue*)xrtValueObjectGet(Request,XRT_STR_LITERAL("redirect_uris")):NULL;
        Ok=Request&&Uris&&xrtValueArrayAppendNew(Uris,xrtValueString(xrtStrView(Redirect)));
        Registration=Ok&&Endpoint?McpOAuthJson(Http,"POST",Endpoint,Request):NULL;xrtValueRelease(Request);
        Client=McpOAuthText(Registration,"client_id");Ok=Client&&Client[0];
    }
    cstr Scope=McpOAuthText(Config,"scope");char* Supported=NULL;
    if(!Scope)Scope=ChallengeScope;
    if(!Scope){Supported=McpOAuthScopes(xrtValueObjectGet(Resource,XRT_STR_LITERAL("scopes_supported")));Scope=Supported;}
    cstr SecretRef=McpOAuthText(Config,"client_secret_ref");char* Secret=NULL;
    if(Ok&&SecretRef)Ok=MdoSecretResolve(xrtStrView(SecretRef),4096,&Secret);
    cstr Method=McpOAuthText(Config,"auth_method");
    if(!Method)Method=Secret?"client_secret_basic":McpOAuthText(Registration,"token_endpoint_auth_method");
    if(!Method)Method="none";
    cstr DynamicSecret=McpOAuthText(Registration,"client_secret");
    Ok=Ok&&Credentials&&McpOAuthSet(Credentials,"client_id",Client)&&McpOAuthSet(Credentials,"client_secret",Secret?Secret:DynamicSecret)&&
        McpOAuthSet(Credentials,"auth_method",Method)&&McpOAuthSet(Credentials,"resource",Audience)&&
        McpOAuthSet(Credentials,"issuer",Issuer)&&McpOAuthSet(Credentials,"authorize_url",Authorize)&&
        McpOAuthSet(Credentials,"token_url",Token)&&McpOAuthSet(Credentials,"redirect_uri",Redirect)&&
        McpOAuthSet(Credentials,"scope",Scope);
    cstr Revoke=McpOAuthText(Metadata,"revocation_endpoint");
    if(Ok&&Revoke&&McpOAuthUrl(Revoke,Entry->Endpoint))Ok=McpOAuthSet(Credentials,"revocation_url",Revoke);
    MdoSecretRelease(&Secret);xrtFree(Supported);xrtFree(ChallengeScope);xrtFree(Issuer);
    McpOAuthSecretFree(Registration);xrtValueRelease(Metadata);xrtValueRelease(Resource);
    if(!Ok){McpOAuthSecretFree(Credentials);return NULL;}return Credentials;
}

static bool McpOAuthTokenHttp(cstr Method,cstr Url,cstr Body,cstr Header,char** Output,int* Status,void* Data)
{
    McpOAuthHttp* Http=Data;char* Resource=xoauth2UrlEncode(Http->Resource);
    char* Form=Body&&Resource?xrtFormat("%s&resource=%s",Body,Resource):NULL;xrtFree(Resource);
    bool Ok=Form&&McpOAuthFetch(Http,Method,Url,Form,"application/x-www-form-urlencoded",Header,Output);
    if(Form){xrtSecureZero(Form,strlen(Form));xrtFree(Form);}
    if(Ok&&*Output&&(Http->Status==400||Http->Status==401)) {
        xvalue* Error=xrtJsonParse(xrtStrView(*Output));cstr Kind=McpOAuthText(Error,"error");
        Http->Permanent=Kind&&(!strcmp(Kind,"invalid_grant")||!strcmp(Kind,"invalid_client"));
        McpOAuthSecretFree(Error);
    }
    *Status=Http->Status;return Ok;
}

static void McpOAuthClient(xoauth2client* Client,const xvalue* Credentials,McpOAuthHttp* Http)
{
    xoauth2config Config;xoauth2ConfigInit(&Config);
    Config.AuthorizeUrl=McpOAuthText(Credentials,"authorize_url");Config.TokenUrl=McpOAuthText(Credentials,"token_url");
    Config.ClientId=McpOAuthText(Credentials,"client_id");Config.ClientSecret=McpOAuthText(Credentials,"client_secret");
    if(Config.ClientSecret&&!Config.ClientSecret[0])Config.ClientSecret=NULL;
    Config.RedirectUri=McpOAuthText(Credentials,"redirect_uri");Config.Scope=McpOAuthText(Credentials,"scope");Config.UsePkce=true;
    cstr Method=McpOAuthText(Credentials,"auth_method");
    if(Method&&!strcmp(Method,"none"))Config.ClientSecret=NULL;
    Config.AuthStyle=Method&&!strcmp(Method,"client_secret_basic")?XOAUTH2_AUTH_BASIC:XOAUTH2_AUTH_BODY;
    Config.Http=McpOAuthTokenHttp;Config.HttpContext=Http;Http->Resource=McpOAuthText(Credentials,"resource");
    xoauth2UseCustom(Client,&Config);
}

static bool McpOAuthLock(MdoMcpOAuthEntry* Entry,xcancel* Cancel,uint64 Deadline)
{
    while(!xrtMutexTryLock(Entry->Tokens)) {
        if(xrtCancelRequested(Entry->Cancel)||(Cancel&&xrtCancelRequested(Cancel))||MdoMonotonicUs()>=Deadline)return false;
        xrtSleep(5u);
    }
    return true;
}

#include "oauth_jobs.inc.c"
