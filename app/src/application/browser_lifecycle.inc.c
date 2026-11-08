/* Called while Workbench.Lock is held. Browser commands enqueue under the same
 * lock, so no command can be admitted between successful closure and cleanup. */
static bool WbIsBrowserPanel(const xvalue* State,const xvalue* Panel){
    const xvalue* Type=WbFind(State,"types",SdkField(Panel,"type"));
    return WbEqual(SdkField(Type,"renderer"),"browser");
}
static bool WbBrowserPrivateBound(const mdo_sdk_context_v1* C,const xvalue* Browser){
    const xvalue* Scope=SdkGet(Browser,"context");
    return WbBound(C,Browser)&&WbEqual(C->Namespace,SdkField(Scope,"namespace"))&&
        WbEqual(C->MemberId,SdkField(Scope,"member"));
}
static bool WbBrowserPanelScopeBound(const mdo_sdk_context_v1* C,const xvalue* Panel){
    const xvalue* Scope=SdkGet(Panel,"context");
    return WbBrowserPrivateBound(C,Panel)&&WbEqual(C->RunId,SdkField(Scope,"run"))&&
        WbEqual(C->Workspace,SdkField(Scope,"workspace"));
}
static bool WbPanelBrowserReference(const mdo_sdk_context_v1* C,const xvalue* State,const xvalue* Panel,mdo_error_v1* E){
    if(!WbIsBrowserPanel(State,Panel))return true;
    const xvalue* Resource=SdkGet(Panel,"resource");
    if(!SdkGet(Resource,"id"))return true; /* Empty panel creates its own tab. */
    cstr Id=SdkField(Resource,"id");
    const xvalue* Browser=WbFind(State,"browsers",Id);
    if(!Browser)return WbFail(E,"browser_missing","Browser session is unavailable; its panel cannot be reopened",404);
    return WbBrowserPrivateBound(C,Browser)||WbFail(E,"context_mismatch","Browser belongs to another extension or context",409);
}
static bool WbBrowserPanelContext(const mdo_sdk_context_v1* C,const xvalue* State,const xvalue* Panel,mdo_sdk_context_v1* Creation,mdo_error_v1* E){
    *Creation=*C;
    if(!WbPanelBrowserReference(C,State,Panel,E))return false;
    if(!WbIsBrowserPanel(State,Panel))return true;
    const xvalue* Browser=WbFind(State,"browsers",SdkField(SdkGet(Panel,"resource"),"id"));
    if(!Browser)return true; /* Empty panel creates a browser in the caller context. */
    const xvalue* Scope=SdkGet(Browser,"context");
    /* A later invocation may have a different run/workspace. Only the display
     * metadata retains the browser's original identity; caller permissions,
     * deadline and action admission still use C unchanged. These strings are
     * borrowed under Workbench.Lock until WbContext copies them into the panel. */
    Creation->RunId=SdkField(Scope,"run");
    Creation->Workspace=SdkField(Scope,"workspace");
    return true;
}
static bool WbCloseBrowserPanels(const mdo_sdk_context_v1* C,xvalue* State,cstr Id,mdo_error_v1* E){
    xvalue* Kept=xrtValueArray();if(!Kept)return WbFail(E,"allocation_failed","Cannot prepare browser panel cleanup",500);
    const xvalue* Panels=SdkGet(State,"panels");
    for(size_t I=0;I<xrtValueCount(Panels);I++){
        const xvalue* Panel=xrtValueArrayGet(Panels,I);
        if(WbBound(C,Panel)&&WbIsBrowserPanel(State,Panel)&&WbEqual(SdkField(SdkGet(Panel,"resource"),"id"),Id))continue;
        if(!xrtValueArrayAppend(Kept,Panel)){xrtValueRelease(Kept);return WbFail(E,"allocation_failed","Cannot retain other panels",500);}
    }
    return xrtValueObjectSetNew(State,XRT_STR_LITERAL("panels"),Kept);
}
