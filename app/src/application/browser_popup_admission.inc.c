/* Called before the ordinary workbench lock. Native deferrals stay in the UI;
 * only an ephemeral request descriptor accompanies the owned browser record. */
static bool WbPopupId(cstr Id){
    if(!Id||strlen(Id)!=44||strncmp(Id,"popup-{",7)||Id[43]!='}')return false;
    for(size_t i=7;i<43;i++){
        if(i==15||i==20||i==25||i==30){if(Id[i]!='-')return false;}
        else if(!((Id[i]>='0'&&Id[i]<='9')||(Id[i]>='a'&&Id[i]<='f')||(Id[i]>='A'&&Id[i]<='F')))return false;
    }return true;
}
static bool WbPopupUrl(cstr Url){
    if(!Url||!strcmp(Url,"about:blank"))return true;
    if(!WbUrl(Url))return false;
    cstr Authority=Url+(!strncmp(Url,"https://",8)?8:7),End=Authority+strcspn(Authority,"/?#");
    if(End==Authority)return false;
    for(cstr P=Url;*P;P++)if((unsigned char)*P<=32||*P=='\\')return false;
    for(cstr P=Authority;P<End;P++)if(*P=='@')return false;
    return true;
}
static xvalue* WbPopupRecord(const xvalue* Row){
    xvalue* Out=xrtValueClone(Row);const xvalue* Runtime=SdkGet(Workbench.Runtime,SdkField(Row,"id"));
    if(Runtime)xrtValueObjectSet(Out,XRT_STR_LITERAL("runtime"),Runtime);return Out;
}
static bool WbPopupCall(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E){
    cstr Request=SdkField(P,"popup"),Client=SdkField(P,"client"),Opener=SdkField(P,"opener"),Id=SdkField(P,"id"),Url=SdkField(P,"url");
    if(!WbPopupId(Request)||!SdkIdValid(Client))return WbFail(E,"popup_arguments_invalid","A native popup identity and explicit display client are required",422);
    bool Settle=!strcmp(Method,"browser.popup.settle"),Ok=false,Changed=false;xvalue* Next=NULL;
    xrtMutexLock(Workbench.Lock);
    if(Settle){
        xvalue* Row=WbFind(Workbench.State,"browsers",Id);
        if(!Row||!WbBound(C,Row)||!WbEqual(Client,SdkField(Row,"client"))){WbFail(E,"context_mismatch","Popup settlement requires its original browser context and client",409);goto done;}
        const xvalue* Runtime=SdkGet(Workbench.Runtime,Id),*Pending=SdkGet(Runtime,"popup_request");
        if(Pending&&!WbEqual(Request,SdkField(Pending,"id"))){WbFail(E,"popup_identity_mismatch","Another native popup request is attached to this record",409);goto done;}
        if(Pending){
            xvalue* Updated=xrtValueClone(Runtime);xrtValueObjectRemove(Updated,XRT_STR_LITERAL("popup_request"));
            xrtValueObjectSetNew(Workbench.Runtime,xrtStrView(Id),Updated);
        }
        *R=SdkParse("{\"settled\":true}");Ok=true;goto done;
    }
    xvalue* Source=WbFind(Workbench.State,"browsers",Opener);
    if(!Source||!WbBound(C,Source)||!WbEqual(Client,SdkField(Source,"client"))){WbFail(E,"context_mismatch","Popup admission requires its original opener context and client",409);goto done;}
    if(!WbPopupUrl(Url)){WbFail(E,"popup_url_invalid","Use HTTP(S) or an empty popup without credentials or control characters",422);goto done;}
    char Key[80];snprintf(Key,sizeof(Key),"native-%s",Request);
    const xvalue* Rows=SdkGet(Workbench.State,"browsers");
    for(size_t i=0;i<xrtValueCount(Rows);i++){
        const xvalue* Row=xrtValueArrayGet(Rows,i);
        if(WbBound(C,Row)&&WbEqual(Client,SdkField(Row,"client"))&&WbEqual(Key,SdkField(Row,"key"))){
            if(!WbEqual(Opener,SdkField(Row,"opener"))){WbFail(E,"popup_identity_mismatch","A popup identity cannot change its opener",409);goto done;}
            *R=WbPopupRecord(Row);Ok=true;goto done;
        }
    }
    if(xrtValueCount(Rows)>=64){WbFail(E,"panel_limit","Close unused browser records before opening more (limit 64)",429);goto done;}
    char NewId[64];if(!SdkRandomId("browser-",NewId)){WbFail(E,"allocation_failed","Cannot allocate a browser identity",500);goto done;}
    Next=xrtValueClone(Workbench.State);xvalue* Row=xrtValueObject();
    SdkPut(Row,"id",NewId);SdkPut(Row,"client",Client);SdkPut(Row,"key",Key);SdkPut(Row,"opener",Opener);
    SdkPut(Row,"owner",C->ExtensionId);SdkPut(Row,"state","suspended");SdkPut(Row,"execution","display-client");SdkPut(Row,"title","新窗口");
    if(Url&&strcmp(Url,"about:blank"))SdkPut(Row,"url",Url);
    if(WbEqual("intercept",SdkField(Source,"download_policy")))SdkPut(Row,"download_policy","intercept");
    xrtValueObjectSetNew(Row,XRT_STR_LITERAL("context"),WbContext(C));SdkPutUInt(Row,"revision",1);
    xrtValueArrayAppendNew(WbArray(Next,"browsers"),Row);WbBump(Next);
    if(!WbSave(Next)){WbFail(E,"workbench_save_failed","Popup record could not be saved; no native adoption was requested",503);goto done;}
    xrtValueRelease(Workbench.State);Workbench.State=Next;Next=NULL;
    xvalue* Runtime=xrtValueObject(),*Pending=xrtValueObject();SdkPut(Pending,"id",Request);SdkPut(Pending,"opener",Opener);SdkPut(Pending,"client",Client);
    xrtValueObjectSetNew(Runtime,XRT_STR_LITERAL("popup_request"),Pending);xrtValueObjectSetNew(Workbench.Runtime,xrtStrView(NewId),Runtime);
    *R=WbPopupRecord(Row);Ok=true;Changed=true;
done:
    xrtValueRelease(Next);xrtMutexUnlock(Workbench.Lock);
    if(Changed){xvalue* Facts=xrtValueObject();SdkPut(Facts,"method",Method);SdkPut(Facts,"id",SdkField(*R,"id"));MdoApplicationEvent("workbench.changed",C,Facts);xrtValueRelease(Facts);}
    return Ok;
}
