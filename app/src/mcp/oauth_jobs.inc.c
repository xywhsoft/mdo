typedef struct McpOAuthJob {
    MdoMcpOAuthEntry* Entry;
    char* Redirect;
    char* Code;
    char* State;
    char* Issuer;
    bool Denied;
    xvalue* Revocation;
} McpOAuthJob;

static void McpOAuthPendingPath(cstr State,char Path[128])
{
    char Hash[65];
    if(!McpOAuthHash(State,Hash)){Path[0]=0;return;}
    snprintf(Path,128,"data/mcp/oauth-pending/%s",Hash);
}

static bool McpOAuthTokenSave(MdoMcpOAuthEntry* Entry,xvalue* Record,const xoauth2token* Token)
{
    const xvalue* Old=xrtValueObjectGet(Record,XRT_STR_LITERAL("tokens"));
    cstr Refresh=Token->RefreshToken?Token->RefreshToken:McpOAuthText(Old,"refresh_token");
    cstr Scope=Token->Scope?Token->Scope:McpOAuthText(Old,"scope");
    if(!Token->AccessToken||!Token->AccessToken[0]||strlen(Token->AccessToken)>7900||
        !Token->TokenType||strcmp(Token->TokenType,"bearer"))return false;
    for(cstr P=Token->AccessToken;*P;P++)if((unsigned char)*P<=32||(unsigned char)*P>=127)return false;
    xvalue* Value=xrtValueObject();
    bool Ok=Value&&McpOAuthSet(Value,"access_token",Token->AccessToken)&&McpOAuthSet(Value,"refresh_token",Refresh)&&
        McpOAuthSet(Value,"scope",Scope)&&xrtValueObjectSetNew(Value,XRT_STR_LITERAL("expires_at"),
            xrtValueUInt(Token->ExpiresIn>0&&Token->ExpiresAt>0?(uint64)Token->ExpiresAt:0));
    if(!Ok){McpOAuthSecretFree(Value);return false;}
    if(!xrtValueObjectSetNew(Record,XRT_STR_LITERAL("tokens"),Value))return false;
    return xrtValueObjectSetNew(Record,XRT_STR_LITERAL("needs_authorization"),xrtValueBool(false))&&McpOAuthSave(Entry,Record);
}

static bool McpOAuthAuthorize(void* Data,cstr Id,cstr Endpoint,bool Force,xcancel* Cancel,
    uint64 Deadline,char* Header,size_t Capacity,xwork_error* Error)
{
    (void)Data;Header[0]=0;
    MdoMcpOAuthEntry* Entry=McpOAuthFind(Id,NULL);
    if(!Entry)return true;
    bool Ok=false;uint16 Status=401;
    cstr Message="Authorize this MCP server in Settings > MCP before connecting";
    if(strcmp(Entry->Endpoint,Endpoint)||!McpOAuthLock(Entry,Cancel,Deadline))goto failed;
    xvalue* Record=McpOAuthRead(Entry);
    if(!Record){Message="Saved MCP credentials cannot be read; unlock the system credential store or authorize again";goto unlock;}
    const xvalue* Credentials=xrtValueObjectGet(Record,XRT_STR_LITERAL("credentials"));
    const xvalue* Tokens=xrtValueObjectGet(Record,XRT_STR_LITERAL("tokens"));
    cstr Access=McpOAuthText(Tokens,"access_token");uint64 Expiry=McpOAuthUInt(Tokens,"expires_at");
    uint64 Now=(uint64)(MdoUtcUs()/1000000);
    if(McpOAuthBool(Record,"needs_authorization"))goto record_done;
    if(Force||(Expiry&&Expiry<=Now+30)) {
        cstr Refresh=McpOAuthText(Tokens,"refresh_token");
        if(!Refresh||!Refresh[0])goto record_done;
        McpOAuthHttp Http={0};Http.Entry=Entry;Http.Cancel=Cancel;Http.Deadline=Deadline;
        xoauth2client Client={0};McpOAuthClient(&Client,Credentials,&Http);
        xoauth2token* Token=xoauth2Refresh(&Client,Refresh);
        if(Token) {
            Ok=McpOAuthTokenSave(Entry,Record,Token);xoauth2TokenFree(Token);
            if(!Ok){Message="Refreshed MCP credentials could not be saved";xoauth2ClientUnit(&Client);goto record_done;}
            Tokens=xrtValueObjectGet(Record,XRT_STR_LITERAL("tokens"));Access=McpOAuthText(Tokens,"access_token");
        } else {
            Status=Http.Status;Message="MCP token refresh failed temporarily; saved credentials were retained";
            if(Http.Permanent) {
                (void)xrtValueObjectSetNew(Record,XRT_STR_LITERAL("needs_authorization"),xrtValueBool(true));
                (void)McpOAuthSave(Entry,Record);Message="MCP authorization expired or was revoked; authorize this server again";
            }
            xoauth2ClientUnit(&Client);Ok=false;goto record_done;
        }
        xoauth2ClientUnit(&Client);
    }
    Ok=Access&&Access[0]&&strlen(Access)+8<Capacity;
    if(Ok) {
        for(cstr P=Access;*P;P++)if((unsigned char)*P<=32||(unsigned char)*P>=127)Ok=false;
        if(Ok)snprintf(Header,Capacity,"Bearer %s",Access);
    }
record_done:
    McpOAuthSecretFree(Record);
unlock:
    xrtMutexUnlock(Entry->Tokens);
failed:
    if(!Ok) {
        McpOAuthState(Entry,MDO_MCP_AUTH_ERROR,Message,Status);
        if(Error){xworkErrorInit(Error);Error->eCode=Status==401?XWORK_ERROR_POLICY:XWORK_ERROR_TOOL;
            Error->uHttpStatus=Status;snprintf(Error->sMessage,sizeof(Error->sMessage),"%s",Message);}
    }
    return Ok;
}

static bool McpOAuthRevoke(MdoMcpOAuthEntry* Entry,const xvalue* Record);

static int32 McpOAuthWorker(void* Data)
{
    McpOAuthJob* Job=Data;MdoMcpOAuthEntry* Entry=Job->Entry;
    McpOAuthHttp Http={0};Http.Entry=Entry;Http.Cancel=Entry->Cancel;Http.Deadline=MdoDeadlineAfterUs(60000000u);
    bool Locked=McpOAuthLock(Entry,Entry->Cancel,Http.Deadline),Ok=false;
    xvalue* Record=Locked?(Job->Revocation?Job->Revocation:McpOAuthRead(Entry)):NULL;
    cstr Message=Record?"MCP authorization failed; check discovery metadata, S256 support and client registration":"Saved MCP authorization is unreadable";
    if(Record&&Job->Revocation) {
        Ok=McpOAuthRevoke(Entry,Record);Message="Local credentials removed; provider revocation was not acknowledged";
    } else if(Record&&Job->Redirect) {
        xvalue* Config=xrtJsonParse(xrtStrView(Entry->Config));
        xvalue* Credentials=Config?McpOAuthDiscover(&Http,Config,Job->Redirect):NULL;
        xrtValueRelease(Config);
        if(Credentials) {
            xoauth2client Client={0};McpOAuthClient(&Client,Credentials,&Http);
            char* Url=xoauth2BeginLogin(&Client);char* Resource=xoauth2UrlEncode(Http.Resource);
            char* Complete=Url&&Resource?xrtFormat("%s&resource=%s",Url,Resource):NULL;
            xrtFree(Url);xrtFree(Resource);
            xvalue* Pending=xrtValueObject();char Path[128];McpOAuthPendingPath(Client.sState,Path);
            Ok=Complete&&Pending&&Path[0]&&McpOAuthSet(Pending,"state",Client.sState)&&McpOAuthSet(Pending,"verifier",Client.sVerifier)&&
                McpOAuthSet(Pending,"challenge",Client.sChallenge)&&xrtValueObjectSetNew(Pending,XRT_STR_LITERAL("expires_at"),
                    xrtValueUInt((uint64)(MdoUtcUs()/1000000)+600));
            if(Ok){Ok=xrtValueObjectSet(Record,XRT_STR_LITERAL("credentials"),Credentials)&&
                xrtValueObjectSet(Record,XRT_STR_LITERAL("pending"),Pending)&&
                xrtValueObjectSetNew(Record,XRT_STR_LITERAL("needs_authorization"),xrtValueBool(true))&&McpOAuthSave(Entry,Record)&&
                MdoHomeAtomicWrite(Path,Entry->Key,64,false);}
            if(Ok){xrtMutexLock(Entry->Lock);xrtFree(Entry->AuthorizationUrl);Entry->AuthorizationUrl=Complete;Complete=NULL;xrtMutexUnlock(Entry->Lock);}
            xrtFree(Complete);McpOAuthSecretFree(Pending);xoauth2ClientUnit(&Client);McpOAuthSecretFree(Credentials);
        }
    } else if(Record) {
        const xvalue* Credentials=xrtValueObjectGet(Record,XRT_STR_LITERAL("credentials"));
        const xvalue* Pending=xrtValueObjectGet(Record,XRT_STR_LITERAL("pending"));
        cstr Expected=McpOAuthText(Pending,"state"),Verifier=McpOAuthText(Pending,"verifier"),Challenge=McpOAuthText(Pending,"challenge");
        bool Valid=Expected&&Job->State&&!strcmp(Expected,Job->State)&&McpOAuthUInt(Pending,"expires_at")>(uint64)(MdoUtcUs()/1000000)&&
            (!Job->Issuer||!Job->Issuer[0]||McpOAuthEqualUri(Job->Issuer,McpOAuthText(Credentials,"issuer")));
        if(Valid&&Verifier&&strlen(Verifier)<128&&Challenge&&strlen(Challenge)<128&&strlen(Expected)<128) {
            xoauth2client Client={0};McpOAuthClient(&Client,Credentials,&Http);
            strcpy(Client.sState,Expected);strcpy(Client.sVerifier,Verifier);strcpy(Client.sChallenge,Challenge);
            char Path[128];McpOAuthPendingPath(Expected,Path);
            /* Persist consumption before transmitting the single-use code. */
            bool Consumed=xrtValueObjectSetNew(Record,XRT_STR_LITERAL("pending"),xrtValueNull())&&McpOAuthSave(Entry,Record);
            if(Consumed&&Path[0])(void)MdoHomeRemove(Path,false);
            if(Consumed&&!Job->Denied) {
                xoauth2token* Token=xoauth2CompleteLogin(&Client,Job->Code,Job->State);
                if(Token){Ok=McpOAuthTokenSave(Entry,Record,Token);xoauth2TokenFree(Token);}
                Message="MCP authorization code exchange failed; start authorization again";
            } else Message=Job->Denied?"MCP authorization was declined":"Cannot persist authorization result";
            xoauth2ClientUnit(&Client);
        } else Message="MCP authorization callback is expired or does not match this server";
    }
    if(!Locked)McpOAuthSecretFree(Job->Revocation);
    McpOAuthSecretFree(Record);if(Locked)xrtMutexUnlock(Entry->Tokens);
    if(Ok&&!Job->Redirect&&!Job->Revocation){xwork_error Ignored;(void)MdoMcpManagerDisconnect(Entry->ServerId,&Ignored);}
    McpOAuthState(Entry,Job->Revocation?MDO_MCP_AUTH_IDLE:Ok?(Job->Redirect?MDO_MCP_AUTH_READY:MDO_MCP_AUTH_IDLE):MDO_MCP_AUTH_ERROR,Ok?"":Message,Http.Status);
    if(Job->Code){xrtSecureZero(Job->Code,strlen(Job->Code));xrtFree(Job->Code);}
    xrtFree(Job->State);xrtFree(Job->Issuer);xrtFree(Job->Redirect);xrtFree(Job);
    xrtMutexLock(g_MdoMcpOAuth.Lock);xrtMutexLock(Entry->Lock);Entry->Busy=false;xrtMutexUnlock(Entry->Lock);
    g_MdoMcpOAuth.Running--;xrtMutexUnlock(g_MdoMcpOAuth.Lock);return 0;
}

static bool McpOAuthLaunch(McpOAuthJob* Job)
{
    MdoMcpOAuthEntry* Entry=Job->Entry;bool Ok=false;
    xrtMutexLock(g_MdoMcpOAuth.Lock);xrtMutexLock(Entry->Lock);
    if(!g_MdoMcpOAuth.Stopping&&Entry->Active&&!Entry->Busy&&g_MdoMcpOAuth.Running<4) {
        if(Entry->Thread){xrtThreadDestroy(Entry->Thread);Entry->Thread=NULL;}
        Entry->Busy=true;Entry->Status=Job->Redirect?MDO_MCP_AUTH_DISCOVERY:MDO_MCP_AUTH_EXCHANGE;
        Entry->Message[0]=0;g_MdoMcpOAuth.Running++;
        Entry->Thread=xrtThreadCreate(McpOAuthWorker,Job,0);Ok=Entry->Thread!=NULL;
        if(!Ok){Entry->Busy=false;g_MdoMcpOAuth.Running--;}
    }
    xrtMutexUnlock(Entry->Lock);xrtMutexUnlock(g_MdoMcpOAuth.Lock);
    if(!Ok){McpOAuthSecretFree(Job->Revocation);xrtFree(Job->Redirect);xrtFree(Job->Code);xrtFree(Job->State);xrtFree(Job->Issuer);xrtFree(Job);}return Ok;
}

xvalue* MdoMcpAuthorizationStatus(cstr Id)
{
    MdoMcpOAuthEntry* Entry=McpOAuthFind(Id,NULL);if(!Entry)return NULL;
    xrtMutexLock(Entry->Lock);xvalue* Out=xrtValueObject();
    cstr States[]={"idle","discovery","ready","exchange","error"};
    bool Ok=Out&&McpOAuthSet(Out,"state",States[Entry->Status])&&McpOAuthSet(Out,"message",Entry->Message)&&
        McpOAuthSet(Out,"authorization_url",Entry->Status==MDO_MCP_AUTH_READY?Entry->AuthorizationUrl:NULL)&&
        xrtValueObjectSetNew(Out,XRT_STR_LITERAL("busy"),xrtValueBool(Entry->Busy))&&
        xrtValueObjectSetNew(Out,XRT_STR_LITERAL("authorized"),xrtValueBool(Entry->HasToken&&!Entry->NeedsAuthorization))&&
        xrtValueObjectSetNew(Out,XRT_STR_LITERAL("http_status"),xrtValueUInt(Entry->HttpStatus))&&
        xrtValueObjectSetNew(Out,XRT_STR_LITERAL("expires_at"),xrtValueUInt(Entry->Expires));
    xrtMutexUnlock(Entry->Lock);if(!Ok){xrtValueRelease(Out);return NULL;}return Out;
}

bool MdoMcpAuthorizationStart(cstr Id,cstr LocalOrigin)
{
    MdoMcpOAuthEntry* Entry=McpOAuthFind(Id,NULL);if(!Entry)return false;
    xvalue* Config=xrtJsonParse(xrtStrView(Entry->Config));cstr Configured=McpOAuthText(Config,"redirect_uri");
    char* Redirect=Configured?xrtStrDup(Configured):LocalOrigin&&McpOAuthLoopback(LocalOrigin)?xrtFormat("%s%s",LocalOrigin,MDO_MCP_OAUTH_CALLBACK):NULL;
    xrtValueRelease(Config);
    if(!Redirect||(!McpOAuthLoopback(Redirect)&&strncmp(Redirect,"https://",8))){xrtFree(Redirect);return false;}
    McpOAuthJob* Job=xrtCalloc(1,sizeof(*Job));if(!Job){xrtFree(Redirect);return false;}
    Job->Entry=Entry;Job->Redirect=Redirect;return McpOAuthLaunch(Job);
}

bool MdoMcpAuthorizationOpen(cstr Id)
{
    MdoMcpOAuthEntry* Entry=McpOAuthFind(Id,NULL);if(!Entry)return false;
    xrtMutexLock(Entry->Lock);char* Url=Entry->Status==MDO_MCP_AUTH_READY?xrtStrDup(Entry->AuthorizationUrl):NULL;xrtMutexUnlock(Entry->Lock);
    bool Ok=Url&&xsOpenExternalUrl(Url);xrtFree(Url);return Ok;
}

bool MdoMcpAuthorizationCallback(cstr State,cstr Code,cstr Issuer,cstr Error)
{
    if(!State||!State[0]||strlen(State)>=128||(!Code&&!Error))return false;
    char Path[128],Key[65]={0};McpOAuthPendingPath(State,Path);xfile File=Path[0]?MdoHomeOpenRead(Path):NULL;
    uint64 Size=0;bool Read=File&&xrtFileSize(File,&Size)&&Size==64&&xrtReadFull(File,Key,64,NULL);if(File)xrtClose(File);
    MdoMcpOAuthEntry* Entry=Read?McpOAuthFind(NULL,Key):NULL;if(!Entry)return false;
    McpOAuthJob* Job=xrtCalloc(1,sizeof(*Job));if(!Job)return false;
    Job->Entry=Entry;Job->State=xrtStrDup(State);Job->Code=xrtStrDup(Code?Code:"");Job->Issuer=xrtStrDup(Issuer?Issuer:"");Job->Denied=Error&&Error[0];
    if(!Job->State||!Job->Code||!Job->Issuer){xrtFree(Job->State);xrtFree(Job->Code);xrtFree(Job->Issuer);xrtFree(Job);return false;}
    return McpOAuthLaunch(Job);
}

static bool McpOAuthRevoke(MdoMcpOAuthEntry* Entry,const xvalue* Record)
{
    const xvalue* Credentials=xrtValueObjectGet(Record,XRT_STR_LITERAL("credentials"));
    const xvalue* Tokens=xrtValueObjectGet(Record,XRT_STR_LITERAL("tokens"));
    cstr Endpoint=McpOAuthText(Credentials,"revocation_url");
    cstr Token=McpOAuthText(Tokens,"refresh_token");
    if(!Token||!Token[0])Token=McpOAuthText(Tokens,"access_token");
    if(!Endpoint||!Token||!Token[0])return true;
    char* Encoded=xoauth2UrlEncode(Token),*Id=xoauth2UrlEncode(McpOAuthText(Credentials,"client_id"));
    cstr SecretText=McpOAuthText(Credentials,"client_secret");
    char* Secret=xoauth2UrlEncode(SecretText?SecretText:"");
    cstr Method=McpOAuthText(Credentials,"auth_method");bool Basic=Method&&!strcmp(Method,"client_secret_basic");
    char* Joined=Basic&&Id&&Secret?xrtFormat("%s:%s",Id,Secret):NULL;
    char* Base64=Joined?xrtBase64EncodeNew(Joined,strlen(Joined),NULL):NULL;
    char* Header=Base64?xrtFormat("Basic %s",Base64):NULL;
    char* Form=Encoded&&Id&&Secret?xrtFormat("token=%s&client_id=%s%s%s",Encoded,Id,
        !Basic&&Secret[0]?"&client_secret=":"",!Basic?Secret:""):NULL;
    McpOAuthHttp Http={0};Http.Entry=Entry;Http.Deadline=MdoDeadlineAfterUs(3000000u);char* Response=NULL;
    bool Ok=Form&&(!Basic||Header)&&McpOAuthFetch(&Http,"POST",Endpoint,Form,"application/x-www-form-urlencoded",Header,&Response)&&Http.Status>=200&&Http.Status<300;
    char* Values[]={Encoded,Id,Secret,Joined,Base64,Header,Form,Response};
    for(size_t i=0;i<sizeof(Values)/sizeof(*Values);i++)if(Values[i]){xrtSecureZero(Values[i],strlen(Values[i]));xrtFree(Values[i]);}
    return Ok;
}

bool MdoMcpAuthorizationForget(cstr Id)
{
    MdoMcpOAuthEntry* Entry=McpOAuthFind(Id,NULL);if(!Entry)return false;
    xrtMutexLock(Entry->Lock);bool Busy=Entry->Busy;xrtMutexUnlock(Entry->Lock);
    if(Busy||!McpOAuthLock(Entry,Entry->Cancel,MdoDeadlineAfterUs(1000000u)))return false;
    xvalue* Record=McpOAuthRead(Entry);
    cstr State=McpOAuthText(xrtValueObjectGet(Record,XRT_STR_LITERAL("pending")),"state");
    char Path[128];if(State){McpOAuthPendingPath(State,Path);if(Path[0])(void)MdoHomeRemove(Path,false);}
    /* Local revocation is authoritative even if a provider is offline. */
    snprintf(Path,sizeof(Path),"data/mcp/oauth/%s.bin",Entry->Key);
    bool Ok=MdoHomeRemove(Path,false);
    if(!Ok&&xrtGetError()&&xrtErrorKind(xrtGetError())==XERR_NOT_FOUND){xrtClearError();Ok=true;}
    McpOAuthJob* Revoke=Ok&&Record?xrtCalloc(1,sizeof(*Revoke)):NULL;
    if(Revoke){Revoke->Entry=Entry;Revoke->Revocation=Record;Record=NULL;}
    McpOAuthSecretFree(Record);
    if(Ok){xrtMutexLock(Entry->Lock);Entry->HasToken=false;Entry->NeedsAuthorization=true;Entry->Expires=0;
        xrtFree(Entry->AuthorizationUrl);Entry->AuthorizationUrl=NULL;xrtMutexUnlock(Entry->Lock);
        McpOAuthState(Entry,MDO_MCP_AUTH_IDLE,"",0);}
    xrtMutexUnlock(Entry->Tokens);
    if(Ok){xwork_error Ignored;(void)MdoMcpManagerDisconnect(Id,&Ignored);}
    if(Revoke)(void)McpOAuthLaunch(Revoke);
    return Ok;
}

static bool McpOAuthInit(xwork_runtime* Runtime)
{
    memset(&g_MdoMcpOAuth,0,sizeof(g_MdoMcpOAuth));g_MdoMcpOAuth.Lock=xrtMutexCreate();
    return g_MdoMcpOAuth.Lock&&xworkRuntimeSetMcpAuthorizationProvider(Runtime,McpOAuthAuthorize,NULL);
}

static void McpOAuthUnit(xwork_runtime* Runtime)
{
    if(!g_MdoMcpOAuth.Lock)return;
    (void)xworkRuntimeSetMcpAuthorizationProvider(Runtime,NULL,NULL);
    xrtMutexLock(g_MdoMcpOAuth.Lock);g_MdoMcpOAuth.Stopping=true;
    for(size_t i=0;i<g_MdoMcpOAuth.Count;i++)xrtCancelRequest(g_MdoMcpOAuth.Entries[i]->Cancel);
    xrtMutexUnlock(g_MdoMcpOAuth.Lock);
    for(size_t i=0;i<g_MdoMcpOAuth.Count;i++) {
        MdoMcpOAuthEntry* Entry=g_MdoMcpOAuth.Entries[i];
        if(Entry->Thread){xrtThreadWait(Entry->Thread);xrtThreadDestroy(Entry->Thread);}
        xrtFree(Entry->Source);xrtFree(Entry->ServerId);xrtFree(Entry->Endpoint);xrtFree(Entry->Config);xrtFree(Entry->AuthorizationUrl);
        xrtMutexDestroy(Entry->Lock);xrtMutexDestroy(Entry->Tokens);xrtCancelDestroy(Entry->Cancel);xrtFree(Entry);
    }
    xrtMutexDestroy(g_MdoMcpOAuth.Lock);memset(&g_MdoMcpOAuth,0,sizeof(g_MdoMcpOAuth));
}
