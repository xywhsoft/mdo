/* Desired selections are durable. The effective view is retained until every
 * outgoing window acknowledges cleanup. Native/core changes use a full host
 * restart rather than pretending that arbitrary native effects are reversible. */
static bool SdkPluginNative(const xvalue* M){
    if(SdkField(SdkGet(SdkGet(M,"manifest"),"entry"),"native"))return true;
    bool Core=false;if(xrtValueGetBool(SdkGet(M,"source_reload_required"),&Core)&&Core)return true;
    const xvalue* Rows=SdkGet(SdkGet(M,"manifest"),"overrides");
    for(size_t I=0;I<xrtValueCount(Rows);I++){cstr T=SdkField(xrtValueArrayGet(Rows,I),"target");size_t N=T?strlen(T):0;
        if(T&&!strncmp(T,"web/",4)&&((N>3&&!strcmp(T+N-3,".js"))||(N>4&&!strcmp(T+N-4,".css"))||(N>5&&!strcmp(T+N-5,".html"))))return true;
    }return false;
}
static bool SdkPluginNativeChanged(const xvalue* Before,const xvalue* After){
    const xvalue* Sets[]={Before,After};
    for(size_t K=0;K<2;K++)for(size_t I=0;I<xrtValueCount(Sets[K]);I++){
        const xvalue* R=xrtValueArrayGet(Sets[K],I);if(!SdkPluginEnabled(R)||!SdkPluginNative(R))continue;
        const xvalue* Other=SdkPluginFind(Sets[1-K],SdkField(R,"id"));
        if(!Other||!SdkPluginEnabled(Other)||strcmp(SdkField(R,"revision"),SdkField(Other,"revision")))return true;
    }return false;
}
static bool SdkPluginOutgoingWindows(const xvalue* Desired){
    for(size_t I=0;I<xrtValueCount(SdkPlugins.Frontends);I++){
        const xvalue* R=xrtValueArrayGet(SdkPlugins.Frontends,I),*Next=SdkPluginFind(Desired,SdkField(R,"id"));
        if(!Next||!SdkPluginEnabled(Next)||strcmp(SdkField(Next,"revision"),SdkField(R,"revision")))return true;
    }return false;
}
static bool SdkPluginCanPublish(const xvalue* Desired){return !SdkPlugins.Readers&&!SdkPlugins.Recovery&&!SdkPluginNativeChanged(SdkPlugins.Effective,Desired)&&!SdkPluginOutgoingWindows(Desired);}
/* Caller holds the plugin lock. No user callbacks are run here. */
static bool SdkPluginReconcile(void){
    if(!SdkPlugins.Pending||!SdkPluginCanPublish(SdkPlugins.Config))return true;
    xvalue* Map=SdkPluginMapBuild(SdkPlugins.Plugins),*Effective=xrtValueClone(SdkPlugins.Config);
    if(!Map||!Effective){xrtValueRelease(Map);xrtValueRelease(Effective);return false;}
    xrtValueRelease(SdkPlugins.Map);SdkPlugins.Map=Map;xrtValueRelease(SdkPlugins.Effective);SdkPlugins.Effective=Effective;SdkPlugins.Pending=false;return true;
}
xvalue* MdoApplicationPluginNativeSources(void){
    xvalue* Rows=xrtValueArray();if(!SdkPlugins.Lock)return Rows;xrtMutexLock(SdkPlugins.Lock);
    for(size_t I=0;I<xrtValueCount(SdkPlugins.Effective);I++){
        const xvalue* M=xrtValueArrayGet(SdkPlugins.Effective,I);cstr Native=SdkField(SdkGet(SdkGet(M,"manifest"),"entry"),"native");
        if(!Native||!SdkPluginEnabled(M))continue;char Path[600];snprintf(Path,sizeof(Path),"/app/__mdo_plugins/%s/%s/%s",SdkField(M,"id"),SdkField(M,"revision"),Native);
        xvalue* R=xrtValueObject();SdkPut(R,"path",Path);SdkPut(R,"id",SdkField(M,"id"));SdkPut(R,"version",SdkField(SdkGet(M,"manifest"),"version"));const xvalue* Requires=SdkGet(SdkGet(M,"manifest"),"requires");if(Requires)xrtValueObjectSet(R,XRT_STR_LITERAL("requires"),Requires);xrtValueArrayAppendNew(Rows,R);
    }xrtMutexUnlock(SdkPlugins.Lock);return Rows;
}
static bool SdkPluginFrontendCall(cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E){
    cstr Client=SdkField(P,"client"),Page=SdkField(P,"page"),Id=SdkField(P,"id"),Revision=SdkField(P,"revision");
    if(!SdkIdValid(Client)||!SdkIdValid(Page))return MdoApplicationError(E,"sdk_arguments_invalid","A window client and document identity are required","plugin",422,false);
    xrtMutexLock(SdkPlugins.Lock);
    if(SdkPlugins.Busy||SdkPlugins.Recovery){xrtMutexUnlock(SdkPlugins.Lock);return MdoApplicationError(E,"sdk_plugin_busy","Plugin manager is changing or recovering","plugin",409,false);}
    bool Hello=!strcmp(Method,"plugin.frontend.hello"),Leave=!strcmp(Method,"plugin.frontend.leave"),Fail=!strcmp(Method,"plugin.frontend.fail"),Ok=true;
    if(!Hello&&(!MdoPackageId(Id)||!Revision||strlen(Revision)!=64)){xrtMutexUnlock(SdkPlugins.Lock);return MdoApplicationError(E,"sdk_arguments_invalid","A plugin identity and revision are required","plugin",422,false);}
    xvalue* Next=xrtValueArray();bool Existing=false;
    for(size_t I=0;I<xrtValueCount(SdkPlugins.Frontends);I++){
        const xvalue* Row=xrtValueArrayGet(SdkPlugins.Frontends,I);bool SameClient=!strcmp(Client,SdkField(Row,"client"));
        bool Same=SameClient&&Page&&!strcmp(Page,SdkField(Row,"page"))&&Id&&!strcmp(Id,SdkField(Row,"id"))&&Revision&&!strcmp(Revision,SdkField(Row,"revision"));
        if(Same)Existing=true;
        if((Hello&&SameClient&&strcmp(Page,SdkField(Row,"page")))||(Leave&&Same))continue;
        Ok=Ok&&xrtValueArrayAppend(Next,Row);
    }
    if(!Hello&&!Leave&&!Fail&&!Existing){
        const xvalue* Plugin=SdkPluginFind(SdkPlugins.Config,Id);
        if(SdkPlugins.Pending||!Plugin||!SdkPluginEnabled(Plugin)||strcmp(Revision,SdkField(Plugin,"revision")))Ok=MdoApplicationError(E,"sdk_plugin_not_ready","Plugin selection is not effective in this host; finish retirement or restart first","plugin",409,false);
        else if(xrtValueCount(Next)>=1024)Ok=MdoApplicationError(E,"sdk_plugin_window_limit","Too many retained plugin window instances","plugin",429,false);
        else {xvalue* Row=xrtValueObject();SdkPut(Row,"client",Client);SdkPut(Row,"page",Page);SdkPut(Row,"id",Id);SdkPut(Row,"revision",Revision);Ok=Ok&&xrtValueArrayAppend(Next,Row);xrtValueRelease(Row);}
    }
    if(Hello||Fail){
        xvalue* Failures=xrtValueArray();
        for(size_t I=0;I<xrtValueCount(SdkPlugins.Failures);I++){const xvalue* F=xrtValueArrayGet(SdkPlugins.Failures,I);
            if(!strcmp(Client,SdkField(F,"client"))&&(Hello||!strcmp(Id,SdkField(F,"id"))))continue;xrtValueArrayAppend(Failures,F);}
        if(Fail&&xrtValueCount(Failures)<1024){xvalue* F=xrtValueObject();SdkPut(F,"client",Client);SdkPut(F,"page",Page);SdkPut(F,"id",Id);SdkPut(F,"revision",Revision);SdkPut(F,"phase",SdkField(P,"phase"));xrtValueArrayAppendNew(Failures,F);}
        xrtValueRelease(SdkPlugins.Failures);SdkPlugins.Failures=Failures;
    }
    if(Ok){xrtValueRelease(SdkPlugins.Frontends);SdkPlugins.Frontends=Next;Next=NULL;Ok=SdkPluginReconcile();}
    if(Ok){*R=xrtValueObject();SdkPut(*R,"state",SdkPlugins.Pending?"pending":"ready");}
    xrtValueRelease(Next);xrtMutexUnlock(SdkPlugins.Lock);return Ok;
}
#include "../../include/mdo/diagnostics.h"
bool MdoApplicationPluginWarnings(xvalue* Items){
    if(!SdkPlugins.Lock)return true;bool Ok=true;xrtMutexLock(SdkPlugins.Lock);
    if(SdkPlugins.Recovery)Ok=MdoDiagnosticWarning(Items,"plugins","recovery","","config/plugins.json","Plugin recovery mode is active; plugin overlays and entries are disabled","Restore the plugin selection or start without MDO_PLUGINS_DISABLED after repairing it","extensions");
    for(size_t I=0;Ok&&I<32;I++)if(SdkPlugins.Plugins[I].Error[0])Ok=MdoDiagnosticWarning(Items,"plugins","invalid_revision",SdkField(SdkPlugins.Plugins[I].Manifest,"id"),"",SdkPlugins.Plugins[I].Error,"Disable or restore the affected plugin revision","extensions");
    for(size_t I=0;Ok&&I<xrtValueCount(SdkPlugins.Failures);I++){
        const xvalue* F=xrtValueArrayGet(SdkPlugins.Failures,I);bool Cleanup=SdkField(F,"phase")&&!strcmp(SdkField(F,"phase"),"cleanup");
        Ok=MdoDiagnosticWarning(Items,"plugins",Cleanup?"frontend_cleanup":"frontend_activation",SdkField(F,"id"),"",Cleanup?"A plugin window did not finish cleanup; its replacement is held":"A plugin frontend failed to start; it will not be automatically executed again","Refresh the affected window or disable/update the plugin; restart mdo if a window cannot retire","extensions");
    }xrtMutexUnlock(SdkPlugins.Lock);return Ok;
}

/* Used only at initial boot, after the rejected candidate has retired. Never
 * rewrite the user's selection, and never replay a plugin entry in recovery. */
bool MdoApplicationPluginsRecover(void){
    if(!SdkPlugins.Lock)return false;xrtMutexLock(SdkPlugins.Lock);
    bool Changed=!SdkPlugins.Recovery&&xrtValueCount(SdkPlugins.Effective)>0;
    xvalue* Empty=Changed?xrtValueArray():NULL,*Effective=Changed?xrtValueArray():NULL;
    if(Changed&&Empty&&Effective){xrtValueRelease(SdkPlugins.Map);SdkPlugins.Map=Empty;xrtValueRelease(SdkPlugins.Effective);SdkPlugins.Effective=Effective;SdkPlugins.Recovery=true;SdkPlugins.Pending=true;}
    else {Changed=false;xrtValueRelease(Empty);xrtValueRelease(Effective);}
    xrtMutexUnlock(SdkPlugins.Lock);return Changed;
}

void MdoApplicationPluginViewBegin(void){if(SdkPlugins.Lock){xrtMutexLock(SdkPlugins.Lock);SdkPlugins.Readers++;xrtMutexUnlock(SdkPlugins.Lock);}}
void MdoApplicationPluginViewEnd(void){if(SdkPlugins.Lock){xrtMutexLock(SdkPlugins.Lock);if(SdkPlugins.Readers)SdkPlugins.Readers--;SdkPluginReconcile();xrtMutexUnlock(SdkPlugins.Lock);}}
