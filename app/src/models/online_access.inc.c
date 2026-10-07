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

/* Fresh access on each turn avoids keeping a fifteen-minute bearer in a
 * long-lived Agent client. Logout/account switch cancels the active call. */
xllm_result MdoModelOnlineComplete(const MdoModelCatalog* Catalog,const MdoModelClientOptions* Options,
    const xllm_request* Request,const xllm_stream_callbacks* Callbacks,xllm_response** Response,xllm_error* Error)
{
    if(Response)*Response=NULL;
    MdoModelOnlineAccess Access={0};xllm_result Result=XLLM_RESULT_ERROR;xllm_client* Client=NULL;
    if(!Request||!Response||!Options||!g_MdoModelsOnline.Acquire||!g_MdoModelsOnline.Release)return Result;
    if(!g_MdoModelsOnline.Acquire(Request->pCancel,&Access)){
        MdoModelsProfileError(Error,"Sign in to use online models, or wait for login renewal");if(Error)Error->eCode=XLLM_ERROR_AUTH;return Result;}
    Client=MdoModelClientCreateAuth(Catalog,Options,NULL,Error,Access.Token);
    if(Client){
        xllm_request Borrowed=*Request;Borrowed.pCancel=Access.Cancel;
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
        if(Ready)Result=xllmClientComplete(Client,&Borrowed,Callbacks,Response,Error);
        xrtFree(ExtraJson);xllmClientDestroy(Client);
    }
    g_MdoModelsOnline.Release(&Access,Error?Error->iHttpStatus:0);return Result;
}
