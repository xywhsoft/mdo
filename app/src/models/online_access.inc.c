void MdoModelManagerSetOnlineAuthority(const MdoModelOnlineAuthority* Authority)
{g_MdoModelsOnline=Authority?*Authority:(MdoModelOnlineAuthority){0};}

const char* MdoModelLoginStateName(MdoModelLoginState State)
{
    switch(State){
    case MDO_MODEL_LOGIN_PENDING:return "pending";
    case MDO_MODEL_LOGIN_REQUIRED:return "signed_out";
    case MDO_MODEL_LOGIN_UNAVAILABLE:return "unavailable";
    default:return "ready";
    }
}

MdoModelLoginState MdoModelCatalogLoginState(const MdoModelCatalog* Catalog)
{
    if(!g_MdoModelsOnline.State)return MDO_MODEL_LOGIN_READY;
    MdoModelLoginState State=g_MdoModelsOnline.State();
    /* An old snapshot cannot prove removal after a new catalog is published. */
    if(State==MDO_MODEL_LOGIN_READY&&(!Catalog||!Catalog->LoginCatalogReady))
        return MDO_MODEL_LOGIN_PENDING;
    return State;
}

bool MdoModelCatalogCheckLogin(const MdoModelCatalog* Catalog,const char* ModelId,xllm_error* Error)
{
    const char* Id=ModelId&&*ModelId?ModelId:
        (Catalog&&Catalog->DefaultModelIndex<Catalog->ModelCount?Catalog->Models[Catalog->DefaultModelIndex].Id:"");
    const MdoModelEntry* Model=Catalog?MdoModelsLookup(Catalog,Id):NULL;
    /* Online entries are transient; saved identities survive a restart.
     * An exact personal model takes precedence over the online namespace. */
    MdoModelLoginState State=MdoModelCatalogLoginState(Catalog);
    bool AwaitingDefault=Catalog&&!Catalog->ModelCount&&!*Id&&
        (State==MDO_MODEL_LOGIN_PENDING||State==MDO_MODEL_LOGIN_UNAVAILABLE);
    bool Required=AwaitingDefault||(Model?(Model->RequiresLogin||MdoModelIsOnline(Catalog,Model->Id)):
        (!strncmp(Id,"mdo-online.",11) || MdoModelsRetiredOnlineId(Id)));
    if(!Required)return true;
    if(State==MDO_MODEL_LOGIN_READY)return true;
    /* A retained profile is structural information, not an access grant.
     * Background renewal must not reject an established route before its
     * completion boundary can wait/retry credentials. Signed-out still fails. */
    if(Model&&Catalog->LoginCatalogReady&&
        (State==MDO_MODEL_LOGIN_PENDING||State==MDO_MODEL_LOGIN_UNAVAILABLE||State==MDO_MODEL_LOGIN_REQUIRED))return true;
    const char* Kind=State==MDO_MODEL_LOGIN_PENDING?"profile_login_pending":
        State==MDO_MODEL_LOGIN_REQUIRED?"profile_login_required":"profile_login_unavailable";
    MdoModelProfileErrorInit(Error,Kind,"account model catalog is not ready");
    return false;
}

bool MdoModelManagerSetOnlineCatalog(const xvalue* Catalog)
{
    if(!g_MdoModels.Initialized)return false;
    xvalue* Next=Catalog?xrtValueClone(Catalog):NULL;if(Catalog&&!Next)return false;
    xrtMutexLock(g_MdoModels.OnlineLock);
    xrtMutexLock(g_MdoModels.Lock);xvalue* Old=g_MdoModels.OnlineCatalog;g_MdoModels.OnlineCatalog=Next;xrtMutexUnlock(g_MdoModels.Lock);
    bool Ok=MdoModelManagerReload();
    if(!Ok){xrtMutexLock(g_MdoModels.Lock);if(g_MdoModels.OnlineCatalog==Next){g_MdoModels.OnlineCatalog=Old;Old=NULL;}xrtMutexUnlock(g_MdoModels.Lock);xrtValueRelease(Next);}
    xrtValueRelease(Old);xrtMutexUnlock(g_MdoModels.OnlineLock);return Ok;
}

bool MdoModelIsOnline(const MdoModelCatalog* Catalog,const char* ModelId)
{
    const MdoModelEntry* Model=Catalog&&ModelId?MdoModelsLookup(Catalog,ModelId):NULL;
    return Model&&g_MdoModelsOnline.Acquire&&!strcmp(Model->ProviderId,"mdo-online");
}

xllm_client* MdoModelClientCreate(const MdoModelCatalog* Catalog,const MdoModelClientOptions* Options,MdoModelClientInfo* Info,xllm_error* Error)
{return MdoModelClientCreateAuth(Catalog,Options,Info,Error,NULL);}

typedef struct MdoModelsOnlineCall {
    const MdoModelCatalog* Catalog;
    const MdoModelClientOptions* Options;
    MdoModelOnlineAuthority Authority;
    xllm_client* Client;
} MdoModelsOnlineCall;

static void MdoModelsLoginRequired(const MdoModelOnlineAuthority* Authority,xllm_error* Error)
{
    MdoModelLoginState State=Authority->State?Authority->State():MDO_MODEL_LOGIN_REQUIRED;
    /* A catalog being ready is not proof that a fresh bearer was acquired.
     * Preserve recoverable acquisition failures even in that race. Only an
     * explicitly signed-out authority needs user login. */
    MdoModelProfileErrorInit(Error,State==MDO_MODEL_LOGIN_REQUIRED?"profile_login_required":
        State==MDO_MODEL_LOGIN_PENDING?"profile_login_pending":"profile_login_unavailable",
        State==MDO_MODEL_LOGIN_REQUIRED?"account login is required":
            "account credentials are temporarily unavailable");
}

/* Acquire a fresh bearer for every HTTP attempt, including after backoff.
 * The enclosing call keeps its first account lease alive: this cancel parent
 * prevents a retry from acquiring a different account after logout/switch. */
static xllm_result MdoModelsOnlineAttempt(void* Data,xllm_client** Client,
    const xllm_request* Request,const xllm_stream_callbacks* Callbacks,
    xllm_response** Response,xllm_error* Error)
{
    MdoModelsOnlineCall* Call=Data;
    MdoModelOnlineAccess Access={0};xllm_result Result=XLLM_RESULT_ERROR;
    xllmErrorInit(Error);
    xllmClientDestroy(Call->Client);Call->Client=NULL;*Client=NULL;
    if(!Call->Authority.Acquire(Request->pCancel,Request->uDeadline,&Access)){
        Result=MdoModelsScope(Request,Error);
        if(Result==XLLM_RESULT_OK){MdoModelsLoginRequired(&Call->Authority,Error);Result=XLLM_RESULT_ERROR;}
        return Result;
    }
    MdoModelCatalog* Current=MdoModelCatalogSnapshot();
    Call->Client=MdoModelClientCreateAuth(Current?Current:Call->Catalog,Call->Options,NULL,Error,Access.Token);
    xllm_request Defaulted=*Request;
    MdoModelApplyRequestDefaults(Current?Current:Call->Catalog,Call->Options->ModelId,&Defaulted);
    MdoModelCatalogRelease(Current);
    *Client=Call->Client;
    if(Call->Client){
        xllm_request Borrowed=Defaulted;Borrowed.pCancel=Access.Cancel;
        Result=MdoModelsClientComplete(Call->Client,&Borrowed,Callbacks,Response,Error);
        if(Error->iHttpStatus==401 &&
            !strcmp(MdoModelErrorKind(Error),"authentication_failed")){
            /* Online account authentication is distinct from a user-entered
             * provider key. Preserve status/diagnostics and business codes. */
            snprintf(Error->sProviderCode,sizeof(Error->sProviderCode),"%s","login_required");
        }
    }
    /* A specific provider/business error can also carry HTTP 401. It does not
     * invalidate the member bearer or justify rotating the login session. */
    int Status=Error->iHttpStatus;
    if(!strcmp(MdoModelErrorKind(Error),"login_required"))Status=401;
    else if(Status==401)Status=0;
    Call->Authority.Release(&Access,Status);
    return Result;
}

/* The account lease covers renewal, all attempts, and request cancellation.
 * Generation retries use the same persistent connection recovery and bounded
 * draft repair as configured providers, with no nested completion loop. */
xllm_result MdoModelOnlineComplete(const MdoModelCatalog* Catalog,const MdoModelClientOptions* Options,
    const xllm_request* Request,const xllm_stream_callbacks* Callbacks,xllm_response** Response,xllm_error* Error)
{
    if(Response)*Response=NULL;
    xllm_error LocalError;if(!Error)Error=&LocalError;xllmErrorInit(Error);
    MdoModelOnlineAccess Pinned={0};xllm_result Result=XLLM_RESULT_ERROR;
    MdoModelsOnlineCall Call={Catalog,Options,g_MdoModelsOnline,NULL};
    if(!Request||!Response||!Options||!g_MdoModelsOnline.Acquire||!g_MdoModelsOnline.Release)return Result;
    Result=MdoModelsScope(Request,Error);if(Result!=XLLM_RESULT_OK)return Result;
    if(!(Call.Authority.Pin?Call.Authority.Pin(Request->pCancel,&Pinned):
            Call.Authority.Acquire(Request->pCancel,Request->uDeadline,&Pinned))){
        Result=MdoModelsScope(Request,Error);
        if(Result==XLLM_RESULT_OK){MdoModelsLoginRequired(&Call.Authority,Error);Result=XLLM_RESULT_ERROR;}
        return Result;
    }
    {
        xllm_request Borrowed=*Request;Borrowed.pCancel=Pinned.Cancel;
        xllm_model_profile Profile;char* ExtraJson=NULL;bool Ready=true;
        Ready=MdoModelCatalogProfile(Catalog,Options->ModelId,Options->Protocol,&Profile,Error);
        if(Ready&&Profile.eProvider==XLLM_PROVIDER_GLM){
            /* xllm's GLM dialect preserves reasoning_content and enables
             * thinking. GLM-5.3 additionally accepts low/high/max effort;
             * merge this without losing the caller's other extra fields. */
            const MdoModelEntry* Model=MdoModelsLookup(Catalog,Profile.sId);
            const char* Effort=Options->ReasoningEffort&&Options->ReasoningEffort[0]?Options->ReasoningEffort:Model->DefaultReasoningEffort;
            /* An empty effort is meaningful: do not send an unsupported
             * reasoning_effort field to non-reasoning GLM profiles. */
            if(Model->ReasoningEffortCount){
                xvalue* Extra=Request->sExtraBodyJson?xrtJsonParse(xrtStrView(Request->sExtraBodyJson)):xrtValueObject();
                Ready=xrtValueType(Extra)==XVALUE_OBJECT&&xrtValueObjectSetNew(Extra,XRT_STR_LITERAL("reasoning_effort"),xrtValueString(xrtStrView(Effort)));
                if(Ready)ExtraJson=xrtJsonStringify(Extra,false,NULL);
                xrtValueRelease(Extra);Ready=Ready&&ExtraJson!=NULL;Borrowed.sExtraBodyJson=ExtraJson;
                if(!Ready)MdoModelsProfileError(Error,"cannot prepare GLM reasoning controls");
            }
        }
        if(Ready)Result=MdoModelsComplete(NULL,&Borrowed,Callbacks,Response,Error,
            Options->RestartableStream,MdoModelsOnlineAttempt,&Call,Options->RetryObserver);
        else {Result=MdoModelsScope(&Borrowed,Error);if(Result==XLLM_RESULT_OK)Result=XLLM_RESULT_ERROR;}
        xrtFree(ExtraJson);xllmClientDestroy(Call.Client);
    }
    /* The per-attempt lease already reports authentication failures. Releasing
     * the pinned lease must not schedule a second renewal for the same reply. */
    Call.Authority.Release(&Pinned,0);return Result;
}

typedef struct MdoModelsConfiguredCall {
    const MdoModelCatalog* Pinned;
    xllm_client* First;
    xllm_client* Replacement;
    const MdoModelClientOptions* Options;
    bool Attempted;
} MdoModelsConfiguredCall;

static xllm_result MdoModelsConfiguredAttempt(void* Data,xllm_client** Client,
    const xllm_request* Request,const xllm_stream_callbacks* Callbacks,
    xllm_response** Response,xllm_error* Error)
{
    MdoModelsConfiguredCall* Call=Data;
    MdoModelCatalog* Current=MdoModelCatalogSnapshot();
    if(!Call->Attempted&&Call->First&&(!Current||Current==Call->Pinned)){*Client=Call->First;Call->Attempted=true;}
    else{
        Call->Attempted=true;
        xllmClientDestroy(Call->Replacement);Call->Replacement=NULL;*Client=NULL;
        if(Current)Call->Replacement=MdoModelClientCreate(Current,Call->Options,NULL,Error);
        *Client=Call->Replacement;
    }
    xllm_request Defaulted=*Request;MdoModelApplyRequestDefaults(Current?Current:Call->Pinned,Call->Options->ModelId,&Defaulted);
    MdoModelCatalogRelease(Current);
    if(!*Client)return XLLM_RESULT_ERROR;
    return MdoModelsClientComplete(*Client,&Defaulted,Callbacks,Response,Error);
}

xllm_result MdoModelConfiguredComplete(const MdoModelCatalog* Pinned,xllm_client* First,const MdoModelClientOptions* Options,
    const xllm_request* Request,const xllm_stream_callbacks* Callbacks,xllm_response** Response,xllm_error* Error)
{
    if(!Options)return XLLM_RESULT_ERROR;
    MdoModelsConfiguredCall Call={Pinned,First,NULL,Options,false};
    xllm_result Result=MdoModelsComplete(First,Request,Callbacks,Response,Error,
        Options->RestartableStream,MdoModelsConfiguredAttempt,&Call,Options->RetryObserver);
    xllmClientDestroy(Call.Replacement);return Result;
}
