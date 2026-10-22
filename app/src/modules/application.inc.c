/* Generation-owned providers and hooks. A catalog reference pins callback code;
 * external callbacks always run after the module manager lock was released. */
static mdo_result MdoModulesRegistrarAddService(void* U,const mdo_service_v1* D,char* Error,size_t Cap){
    MdoModuleRegistrarContext* R=U;MdoModuleGeneration* M=R?R->Generation:NULL;
    if(!M||!D||D->Size<sizeof(*D)||D->Version!=MDO_SDK_VERSION||!SdkIdValid(D->Id)||
        !strchr(D->Id,'.')||!strncmp(D->Id,"sdk.",4)||!strncmp(D->Id,"product.",8)||!D->Execute||M->ServiceCount>=64){
        MdoModulesCopyError(Error,Cap,"Invalid service descriptor, namespace or limit");return MDO_RESULT_ERROR;}
    for(size_t i=0;i<M->ServiceCount;i++)if(!strcmp(D->Id,M->Services[i].Id)){MdoModulesCopyError(Error,Cap,"Duplicate service ID");return MDO_RESULT_ERROR;}
    mdo_service_v1 Copy=*D;
    Copy.Id=xrtStrDup(D->Id);Copy.Description=xrtStrDup(D->Description?D->Description:"");
    Copy.InputSchema=xrtStrDup(D->InputSchema?D->InputSchema:"{}");Copy.OutputSchema=xrtStrDup(D->OutputSchema?D->OutputSchema:"{}");
    xvalue* In=SdkParse(Copy.InputSchema),*Out=SdkParse(Copy.OutputSchema);
    bool Ok=Copy.Id&&Copy.Description&&In&&Out&&xrtValueType(In)==XVALUE_OBJECT&&xrtValueType(Out)==XVALUE_OBJECT;
    xrtValueRelease(In);xrtValueRelease(Out);
    mdo_service_v1* Next=Ok?xrtRealloc(M->Services,(M->ServiceCount+1)*sizeof(*Next)):NULL;
    if(!Next){xrtFree((void*)Copy.Id);xrtFree((void*)Copy.Description);xrtFree((void*)Copy.InputSchema);xrtFree((void*)Copy.OutputSchema);MdoModulesCopyError(Error,Cap,"Service schema or allocation is invalid");return MDO_RESULT_ERROR;}
    M->Services=Next;Next[M->ServiceCount++]=Copy;return MDO_RESULT_OK;
}
static mdo_result MdoModulesRegistrarAddHook(void* U,const mdo_hook_v1* D,char* Error,size_t Cap){
    MdoModuleRegistrarContext* R=U;MdoModuleGeneration* M=R?R->Generation:NULL;
    if(!M||!D||D->Size<sizeof(*D)||D->Version!=MDO_SDK_VERSION||!SdkIdValid(D->Id)||!SdkIdValid(D->Topic)||!D->Execute||M->HookCount>=64){
        MdoModulesCopyError(Error,Cap,"Invalid hook descriptor or limit");return MDO_RESULT_ERROR;}
    for(size_t i=0;i<M->HookCount;i++)if(!strcmp(D->Id,M->Hooks[i].Id)){MdoModulesCopyError(Error,Cap,"Duplicate hook ID");return MDO_RESULT_ERROR;}
    mdo_hook_v1 Copy=*D;Copy.Id=xrtStrDup(D->Id);Copy.Topic=xrtStrDup(D->Topic);
    mdo_hook_v1* Next=Copy.Id&&Copy.Topic?xrtRealloc(M->Hooks,(M->HookCount+1)*sizeof(*Next)):NULL;
    if(!Next){xrtFree((void*)Copy.Id);xrtFree((void*)Copy.Topic);return MDO_RESULT_ERROR;}
    M->Hooks=Next;Next[M->HookCount++]=Copy;return MDO_RESULT_OK;
}
xvalue* MdoApplicationCustomCatalog(void){
    xvalue* A=xrtValueArray();MdoModuleCatalog* Catalog=MdoModuleCatalogSnapshot();
    for(size_t i=0;Catalog&&i<Catalog->ModuleCount;i++){
        MdoModuleGeneration* M=Catalog->Modules[i];
        for(size_t j=0;M&&j<M->ServiceCount;j++){
            const mdo_service_v1* S=&M->Services[j];xvalue* V=xrtValueObject();
            SdkPut(V,"id",S->Id);SdkPut(V,"domain",M->Id);SdkPut(V,"description",S->Description);
            xrtValueObjectSetNew(V,XRT_STR_LITERAL("input_schema"),SdkParse(S->InputSchema));
            xrtValueObjectSetNew(V,XRT_STR_LITERAL("output_schema"),SdkParse(S->OutputSchema));SdkPutUInt(V,"generation",M->Generation);
            xrtValueArrayAppendNew(A,V);
        }
    }MdoModuleCatalogRelease(Catalog);return A;
}
bool MdoApplicationCustomCall(const mdo_sdk_context_v1* C,cstr Id,const xvalue* P,xvalue** R,mdo_error_v1* E,bool* Found){
    *Found=false;MdoModuleCatalog* Catalog=MdoModuleCatalogSnapshot();mdo_service_v1* Service=NULL;
    for(size_t i=0;Catalog&&i<Catalog->ModuleCount;i++){
        MdoModuleGeneration* M=Catalog->Modules[i];
        for(size_t j=0;M&&j<M->ServiceCount;j++)if(!strcmp(M->Services[j].Id,Id)){
            if(Service){*Found=true;MdoModuleCatalogRelease(Catalog);return MdoApplicationError(E,"sdk_service_conflict","Multiple providers registered this service","dispatch",409,false);}
            Service=&M->Services[j];
        }
    }
    bool Ok=true;if(Service){*Found=true;Ok=Service->Execute(Service->UserData,C,P,R,E)==MDO_RESULT_OK;}
    MdoModuleCatalogRelease(Catalog);return Ok;
}
typedef struct SdkHookBinding {const mdo_hook_v1* Hook;cstr Module;} SdkHookBinding;
static int SdkHookCompare(const void* L,const void* R){
    const SdkHookBinding *A=L,*B=R;
    if(A->Hook->Priority!=B->Hook->Priority)return A->Hook->Priority<B->Hook->Priority?-1:1;
    int N=strcmp(A->Module,B->Module);return N?N:strcmp(A->Hook->Id,B->Hook->Id);
}
bool MdoApplicationHooks(const mdo_sdk_context_v1* C,cstr Topic,xvalue** Value,mdo_error_v1* Error){
    MdoModuleCatalog* Catalog=MdoModuleCatalogSnapshot();SdkHookBinding* List=NULL;size_t Count=0;bool Ok=true;
    for(size_t i=0;Ok&&Catalog&&i<Catalog->ModuleCount;i++){
        MdoModuleGeneration* M=Catalog->Modules[i];
        for(size_t j=0;M&&j<M->HookCount;j++)if(!strcmp(M->Hooks[j].Topic,Topic)){
            SdkHookBinding* Next=xrtRealloc(List,(Count+1)*sizeof(*Next));if(!Next){Ok=false;break;}
            List=Next;List[Count++]=(SdkHookBinding){&M->Hooks[j],M->Id};
        }
    }
    if(Count>1)qsort(List,Count,sizeof(*List),SdkHookCompare);
    for(size_t i=0;Ok&&i<Count;i++){
        const mdo_hook_v1* H=List[i].Hook;xvalue* Replacement=NULL;mdo_error_v1 E={0};
        mdo_result Result=H->Execute(H->UserData,C,Topic,*Value,&Replacement,&E);
        if(H->Transform&&Result!=MDO_RESULT_OK){if(Error)*Error=E;Ok=false;}
        else if(H->Transform&&Replacement){xrtValueRelease(*Value);*Value=Replacement;Replacement=NULL;}
        else if(Result!=MDO_RESULT_OK)fprintf(stderr,"[mdo] observer %s:%s failed (%s)\n",List[i].Module,H->Id,E.Code);
        xrtValueRelease(Replacement);
    }
    xrtFree(List);MdoModuleCatalogRelease(Catalog);return Ok;
}
