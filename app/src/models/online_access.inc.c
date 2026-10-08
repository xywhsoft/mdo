void MdoModelManagerSetOnlineAuthority(const MdoModelOnlineAuthority* Authority)
{g_MdoModelsOnline=Authority?*Authority:(MdoModelOnlineAuthority){0};}

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
    return Model&&g_MdoModelsOnline.Acquire&&(!strcmp(Model->ProviderId,"mdo-online") ||
        (Model->Builtin&&!strcmp(Model->Id,MDO_BUILTIN_MODEL_ID)));
}

xllm_client* MdoModelClientCreate(const MdoModelCatalog* Catalog,const MdoModelClientOptions* Options,MdoModelClientInfo* Info,xllm_error* Error)
{return MdoModelClientCreateAuth(Catalog,Options,Info,Error,NULL);}

typedef struct MdoModelsOnlineCall {
    const MdoModelCatalog* Catalog;
    const MdoModelClientOptions* Options;
    MdoModelOnlineAuthority Authority;
    xllm_client* Client;
} MdoModelsOnlineCall;

static void MdoModelsLoginRequired(xllm_error* Error)
{
    MdoModelsProfileError(Error,"Sign in to use online models, or wait for login renewal");
    Error->eCode=XLLM_ERROR_AUTH;
    snprintf(Error->sProviderCode,sizeof(Error->sProviderCode),"%s","login_required");
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
        if(Result==XLLM_RESULT_OK){MdoModelsLoginRequired(Error);Result=XLLM_RESULT_ERROR;}
        return Result;
    }
    Call->Client=MdoModelClientCreateAuth(Call->Catalog,Call->Options,NULL,Error,Access.Token);
    *Client=Call->Client;
    if(Call->Client){
        xllm_request Borrowed=*Request;Borrowed.pCancel=Access.Cancel;
        Result=xllmClientComplete(Call->Client,&Borrowed,Callbacks,Response,Error);
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
    if(Status==401&&strcmp(MdoModelErrorKind(Error),"login_required"))Status=0;
    Call->Authority.Release(&Access,Status);
    return Result;
}

/* The account lease covers renewal, all attempts, and request cancellation.
 * Generation retries use the same six-attempt and recovery-time budget as
 * configured providers, rather than nesting a second completion loop. */
xllm_result MdoModelOnlineComplete(const MdoModelCatalog* Catalog,const MdoModelClientOptions* Options,
    const xllm_request* Request,const xllm_stream_callbacks* Callbacks,xllm_response** Response,xllm_error* Error)
{
    if(Response)*Response=NULL;
    xllm_error LocalError;if(!Error)Error=&LocalError;xllmErrorInit(Error);
    MdoModelOnlineAccess Pinned={0};xllm_result Result=XLLM_RESULT_ERROR;
    MdoModelsOnlineCall Call={Catalog,Options,g_MdoModelsOnline,NULL};
    if(!Request||!Response||!Options||!g_MdoModelsOnline.Acquire||!g_MdoModelsOnline.Release)return Result;
    Result=MdoModelsScope(Request,Error);if(Result!=XLLM_RESULT_OK)return Result;
    if(!Call.Authority.Acquire(Request->pCancel,Request->uDeadline,&Pinned)){
        Result=MdoModelsScope(Request,Error);
        if(Result==XLLM_RESULT_OK){MdoModelsLoginRequired(Error);Result=XLLM_RESULT_ERROR;}
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
            Options->RestartableStream,MdoModelsOnlineAttempt,&Call);
        else Result=XLLM_RESULT_ERROR;
        xrtFree(ExtraJson);xllmClientDestroy(Call.Client);
    }
    /* The per-attempt lease already reports authentication failures. Releasing
     * the pinned lease must not schedule a second renewal for the same reply. */
    Call.Authority.Release(&Pinned,0);return Result;
}
