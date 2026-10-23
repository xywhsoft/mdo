#include "internal.h"
#include "../../include/mdo/home.h"

/* The durable model is shared by native extensions and the UI. Views never
 * own background jobs. Runtime browser handles are deliberately not durable. */
#define WB_PATH "data/workbench.json"
#define WB_BYTES (4u * 1024u * 1024u)
static struct { xmutex* Lock; xvalue* State; bool Ready; } Workbench;
static const cstr SdkWorkbenchMethods[]={
    "panel.types","panel.register","panel.unregister","panel.list","panel.open",
    "panel.get","panel.update","panel.attach","panel.close","panel.reveal","panel.layout.get","panel.layout.put",
    "resource.list","resource.get","resource.put","resource.remove",
    "browser.capabilities","browser.list","browser.create","browser.get","browser.command",
    "browser.close","browser.result"
};
static bool WbFail(mdo_error_v1* E,cstr Code,cstr Message,unsigned Status){
    return MdoApplicationError(E,Code,Message,"workbench",Status,false);
}
static bool WbString(const xvalue* P,cstr Key,size_t Max,bool Required){
    const xvalue* V=SdkGet(P,Key);cstr S=SdkText(V);
    return !V?!Required:S&&strlen(S)<=Max&&(!Required||S[0]);
}
static xvalue* WbArray(const xvalue* S,cstr Key){return (xvalue*)SdkGet(S,Key);}
static xvalue* WbFind(const xvalue* S,cstr Key,cstr Id){
    const xvalue* A=SdkGet(S,Key);
    for(size_t i=0;Id&&i<xrtValueCount(A);i++){
        xvalue* V=(xvalue*)xrtValueArrayGet(A,i);cstr Name=SdkField(V,"id");
        if(Name&&!strcmp(Name,Id))return V;
    }return NULL;
}
static void WbRemove(xvalue* S,cstr Key,cstr Id){
    xvalue* A=xrtValueArray();const xvalue* Old=SdkGet(S,Key);
    for(size_t i=0;i<xrtValueCount(Old);i++){
        const xvalue* V=xrtValueArrayGet(Old,i);
        if(strcmp(SdkField(V,"id"),Id))xrtValueArrayAppend(A,V);
    }xrtValueObjectSetNew(S,xrtStrView(Key),A);
}
static xvalue* WbContext(const mdo_sdk_context_v1* C){
    xvalue* V=xrtValueObject();SdkPut(V,"device",C->DeviceId);SdkPut(V,"project",C->ProjectId&&C->ProjectId[0]?C->ProjectId:"default");
    SdkPut(V,"session",C->SessionId);SdkPut(V,"run",C->RunId);return V;
}
static bool WbEqual(cstr A,cstr B){return !strcmp(A?A:"",B?B:"");}
static bool WbOwner(const mdo_sdk_context_v1* C,const xvalue* V){
    return WbEqual(C->ExtensionId,SdkField(V,"owner"));
}
static bool WbBound(const mdo_sdk_context_v1* C,const xvalue* V){
    const xvalue* B=SdkGet(V,"context");
    return WbOwner(C,V)&&WbEqual(C->DeviceId,SdkField(B,"device"))&&
        WbEqual(C->ProjectId&&C->ProjectId[0]?C->ProjectId:"default",SdkField(B,"project"))&&
        WbEqual(C->SessionId,SdkField(B,"session"));
}
static bool WbRevision(const xvalue* P,const xvalue* V,mdo_error_v1* E){
    uint64 Expected=0,Actual=0;
    if(!SdkUInt(SdkGet(P,"revision"),&Expected))return WbFail(E,"revision_required","Supply the current revision before changing this resource",428);
    (void)SdkUInt(SdkGet(V,"revision"),&Actual);
    return Expected==Actual||WbFail(E,"revision_conflict","The resource changed; read it again before editing",409);
}
static void WbBump(xvalue* V){uint64 N=0;(void)SdkUInt(SdkGet(V,"revision"),&N);SdkPutUInt(V,"revision",N+1);}
static bool WbSave(const xvalue* S){
    size_t Size=0;char* Text=xrtJsonStringify(S,false,&Size);
    bool Ok=Text&&Size<=WB_BYTES&&MdoHomeAtomicWrite(WB_PATH,Text,Size,false);xrtFree(Text);return Ok;
}
static xvalue* WbEmpty(void){
    xvalue* V=xrtValueObject();SdkPutUInt(V,"schema_version",1);SdkPutUInt(V,"revision",0);
    const cstr Names[]={"types","panels","resources","layouts","browsers"};
    for(size_t i=0;i<5;i++)xrtValueObjectSetNew(V,xrtStrView(Names[i]),xrtValueArray());return V;
}
bool MdoApplicationWorkbenchInit(void){
    Workbench.Lock=xrtMutexCreate();Workbench.State=WbEmpty();Workbench.Ready=Workbench.Lock&&Workbench.State;
    bool Exists=false;xfileinfo Info;
    if(!MdoHomeExternalStat(WB_PATH,&Exists,&Info)){Workbench.Ready=false;return Workbench.Lock!=NULL;}
    if(!Exists)return Workbench.Ready;
    xfile File=NULL;char* Text=NULL;xvalue* Parsed=NULL;
    if(Info.Type!=XFILE_TYPE_FILE||!(Info.Available&XFILE_INFO_SIZE)||Info.Size>WB_BYTES)goto invalid;
    File=MdoHomeOpenRead(WB_PATH);Text=xrtMalloc((size_t)Info.Size+1);
    if(!File||!Text||!xrtReadFull(File,Text,(size_t)Info.Size,NULL))goto invalid;
    Text[Info.Size]=0;Parsed=SdkParse(Text);uint64 Schema=0;
    if(!Parsed||!SdkUInt(SdkGet(Parsed,"schema_version"),&Schema)||Schema!=1)goto invalid;
    const cstr Names[]={"types","panels","resources","layouts","browsers"};
    for(size_t i=0;i<5;i++){
        const xvalue* A=SdkGet(Parsed,Names[i]);
        if(xrtValueType(A)!=XVALUE_ARRAY||xrtValueCount(A)>128)goto invalid;
        for(size_t j=0;j<xrtValueCount(A);j++)if(!SdkIdValid(SdkField(xrtValueArrayGet(A,j),"id")))goto invalid;
    }
    /* Browser navigation/input must be explicitly reattached after restart. */
    for(size_t i=0;i<xrtValueCount(SdkGet(Parsed,"browsers"));i++)
        SdkPut((xvalue*)xrtValueArrayGet(SdkGet(Parsed,"browsers"),i),"state","suspended");
    xrtValueRelease(Workbench.State);Workbench.State=Parsed;Parsed=NULL;goto done;
invalid:Workbench.Ready=false;
done:xrtValueRelease(Parsed);xrtFree(Text);if(File)xrtClose(File);return Workbench.Lock!=NULL;
}
void MdoApplicationWorkbenchUnit(void){
    xrtValueRelease(Workbench.State);if(Workbench.Lock)xrtMutexDestroy(Workbench.Lock);memset(&Workbench,0,sizeof(Workbench));
}
xvalue* MdoApplicationWorkbenchCatalog(void){
    xvalue* A=xrtValueArray();for(size_t i=0;i<sizeof(SdkWorkbenchMethods)/sizeof(*SdkWorkbenchMethods);i++){
        xvalue* V=xrtValueObject();SdkPut(V,"id",SdkWorkbenchMethods[i]);SdkPut(V,"domain","workbench");SdkPut(V,"transport","native");xrtValueArrayAppendNew(A,V);
    }return A;
}
static bool WbQueue(const mdo_sdk_context_v1* C,cstr Client,cstr Action,const xvalue* Params,xvalue** R,mdo_error_v1* E){
    xvalue* P=xrtValueObject();SdkPut(P,"client",Client);SdkPut(P,"action",Action);
    xrtValueObjectSet(P,XRT_STR_LITERAL("params"),Params);bool Found=false;
    bool Ok=MdoApplicationUiCall(C,"ui.invoke",P,R,E,&Found);xrtValueRelease(P);return Ok;
}
static bool WbUrl(cstr Url){return Url&&strlen(Url)<=8192&&(!strncmp(Url,"https://",8)||!strncmp(Url,"http://",7))&&!strchr(Url,'\r')&&!strchr(Url,'\n');}
bool MdoApplicationWorkbenchCall(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E,bool* Found){
    *Found=false;for(size_t i=0;i<sizeof(SdkWorkbenchMethods)/sizeof(*SdkWorkbenchMethods);i++)if(!strcmp(Method,SdkWorkbenchMethods[i]))*Found=true;
    if(!*Found)return true;
    if(!Workbench.Ready)return WbFail(E,"workbench_unavailable","Workbench data is unavailable or damaged; the saved file was preserved",503);
    if(!strcmp(Method,"browser.result"))return MdoApplicationUiCall(C,"ui.result",P,R,E,Found);
    if(!strcmp(Method,"browser.command")){
        cstr Id=SdkField(P,"id"),Command=SdkField(P,"command");
        const cstr Allowed[]={"navigate","back","forward","reload","stop","snapshot","screenshot","click","type","scroll","evaluate","takeover","release"};bool Valid=false;
        for(size_t i=0;Command&&i<sizeof(Allowed)/sizeof(*Allowed);i++)if(!strcmp(Command,Allowed[i]))Valid=true;
        if(!Valid)return WbFail(E,"browser_command_invalid","Unknown browser command",400);
        xrtMutexLock(Workbench.Lock);xvalue* V=WbFind(Workbench.State,"browsers",Id);xvalue* Copy=V?xrtValueClone(V):NULL;xrtMutexUnlock(Workbench.Lock);
        if(!Copy)return WbFail(E,"browser_missing","Browser session is unavailable",404);
        if(!WbBound(C,Copy)){xrtValueRelease(Copy);return WbFail(E,"context_mismatch","Browser belongs to another extension or context",409);}
        bool Ok=WbQueue(C,SdkField(Copy,"client"),"workbench.browser",P,R,E);xrtValueRelease(Copy);return Ok;
    }
    xrtMutexLock(Workbench.Lock);xvalue* Next=NULL;bool Ok=false,Changed=false;
    cstr Id=SdkField(P,"id"),Client=SdkField(P,"client");
    if(!strcmp(Method,"browser.capabilities")){
        /* Platform features are advertised by the actual displaying client. */
        xvalue* Args=xrtValueObject();SdkPut(Args,"command","capabilities");Ok=WbQueue(C,Client,"workbench.browser",Args,R,E);xrtValueRelease(Args);goto done;
    }
    if(!strcmp(Method,"panel.types")||!strcmp(Method,"panel.list")||!strcmp(Method,"resource.list")||!strcmp(Method,"browser.list")){
        cstr Kind=!strcmp(Method,"panel.types")?"types":!strcmp(Method,"panel.list")?"panels":!strcmp(Method,"resource.list")?"resources":"browsers";
        *R=xrtValueArray();const xvalue* A=SdkGet(Workbench.State,Kind);
        for(size_t i=0;i<xrtValueCount(A);i++){
            const xvalue* V=xrtValueArrayGet(A,i);
            if((!strcmp(Kind,"types")||Client||WbOwner(C,V))&&(!Client||WbEqual(Client,SdkField(V,"client"))))xrtValueArrayAppend(*R,V);
        }Ok=true;goto done;
    }
    if(!strcmp(Method,"panel.get")||!strcmp(Method,"resource.get")||!strcmp(Method,"browser.get")){
        cstr Kind=!strcmp(Method,"panel.get")?"panels":!strcmp(Method,"resource.get")?"resources":"browsers";xvalue* V=WbFind(Workbench.State,Kind,Id);
        if(!V){WbFail(E,"resource_missing","The requested workbench resource is unavailable",404);goto done;}
        if(!WbOwner(C,V)){WbFail(E,"owner_mismatch","The resource belongs to another extension",403);goto done;}
        *R=xrtValueClone(V);Ok=true;goto done;
    }
    if(!strcmp(Method,"panel.reveal")){
        xvalue* V=WbFind(Workbench.State,"panels",Id);
        if(!V||!WbBound(C,V)){WbFail(E,"context_mismatch","Panel is missing or belongs to another context",409);goto done;}
        Ok=WbQueue(C,SdkField(V,"client"),"workbench.reveal",V,R,E);goto done;
    }
    Next=xrtValueClone(Workbench.State);if(!Next)goto done;
    if(!strcmp(Method,"panel.register")){
        cstr Renderer=SdkField(P,"renderer");
        if(!SdkIdValid(Id)||!WbString(P,"title",256,true)||!Renderer||
            (strcmp(Renderer,"document")&&strcmp(Renderer,"plan")&&strcmp(Renderer,"diff")&&strcmp(Renderer,"web")&&strcmp(Renderer,"browser")&&strcmp(Renderer,"surface")&&strcmp(Renderer,"custom"))){WbFail(E,"panel_type_invalid","Panel needs an ID, title and supported renderer",400);goto done;}
        xvalue* Old=WbFind(Next,"types",Id);
        if(Old&&!WbOwner(C,Old)){WbFail(E,"owner_mismatch","Panel type is owned by another extension",409);goto done;}
        if(!Old&&xrtValueCount(SdkGet(Next,"types"))>=128){WbFail(E,"panel_limit","Panel type registry is full",429);goto done;}
        if(Old)WbRemove(Next,"types",Id);
        xvalue* V=xrtValueClone(P);SdkPut(V,"owner",C->ExtensionId);xrtValueArrayAppendNew(WbArray(Next,"types"),V);*R=xrtValueClone(V);
    }else if(!strcmp(Method,"panel.open")||!strcmp(Method,"browser.create")){
        bool Browser=!strcmp(Method,"browser.create");cstr Type=SdkField(P,"type"),Key=SdkField(P,"key");
        if(!SdkIdValid(Client)||!WbString(P,"key",256,false)||(!Browser&&(!Type||!WbFind(Next,"types",Type)))||(Browser&&SdkGet(P,"url")&&!WbUrl(SdkField(P,"url")))){WbFail(E,"panel_arguments_invalid","An explicit client and registered type (or HTTP URL) are required",400);goto done;}
        cstr Kind=Browser?"browsers":"panels";xvalue* V=NULL;
        const xvalue* A=SdkGet(Next,Kind);
        for(size_t i=0;Key&&Key[0]&&i<xrtValueCount(A);i++){
            xvalue* Item=(xvalue*)xrtValueArrayGet(A,i);
            if(WbBound(C,Item)&&WbEqual(Client,SdkField(Item,"client"))&&WbEqual(Key,SdkField(Item,"key"))&&(Browser||WbEqual(Type,SdkField(Item,"type")))){V=Item;break;}
        }
        if(V){*R=xrtValueClone(V);Ok=true;goto done;}
        if(xrtValueCount(A)>=64){WbFail(E,"panel_limit","Close unused instances before opening more (limit 64)",429);goto done;}
        char NewId[64];if(!SdkRandomId(Browser?"browser-":"panel-",NewId))goto done;
        V=xrtValueClone(P);SdkPut(V,"id",NewId);SdkPut(V,"owner",C->ExtensionId);SdkPut(V,"state",Browser?"suspended":"open");
        xrtValueObjectSetNew(V,XRT_STR_LITERAL("context"),WbContext(C));SdkPutUInt(V,"revision",1);
        xrtValueArrayAppendNew(WbArray(Next,Kind),V);*R=xrtValueClone(V);
    }else if(!strcmp(Method,"panel.layout.get")||!strcmp(Method,"panel.layout.put")){
        /* Layout belongs to the displaying window, never to a project. */
        if(!SdkIdValid(Id)){WbFail(E,"layout_invalid","A stable window profile ID is required",400);goto done;}
        xvalue* Old=WbFind(Next,"layouts",Id);
        if(!strcmp(Method,"panel.layout.get")){*R=Old?xrtValueClone(Old):SdkParse("{\"revision\":0,\"value\":{}}");Ok=true;goto done;}
        if(!WbRevision(P,Old,E))goto done;
        char* Json=SdkJson(SdkGet(P,"value"));bool Valid=Json&&strlen(Json)<=131072&&xrtValueType(SdkGet(P,"value"))==XVALUE_OBJECT;xrtFree(Json);
        if(!Valid||(!Old&&xrtValueCount(SdkGet(Next,"layouts"))>=32)){WbFail(E,"layout_invalid","Layout must be an object up to 128 KiB; at most 32 window profiles",400);goto done;}
        xvalue* V=xrtValueClone(P);SdkPut(V,"owner",C->ExtensionId);WbBump(V);if(Old)WbRemove(Next,"layouts",Id);xrtValueArrayAppendNew(WbArray(Next,"layouts"),V);*R=xrtValueClone(V);
    }else if(!strcmp(Method,"resource.put")){
        xvalue* Old=Id?WbFind(Next,"resources",Id):NULL;
        if(Old&&!WbBound(C,Old)){WbFail(E,"context_mismatch","Resource belongs to another context",409);goto done;}
        if(Old&&!WbRevision(P,Old,E))goto done;
        if(!Old&&(!WbRevision(P,NULL,E)||(Id&&!SdkIdValid(Id))))goto done;
        char* Data=SdkJson(SdkGet(P,"value"));bool Valid=Data&&strlen(Data)<=262144;xrtFree(Data);
        if(!Valid||!WbString(P,"title",256,true)||!WbString(P,"kind",32,true)){WbFail(E,"resource_invalid","A titled, typed resource value up to 256 KiB is required",400);goto done;}
        if(!Old&&xrtValueCount(SdkGet(Next,"resources"))>=128){WbFail(E,"resource_limit","Resource registry is full",429);goto done;}
        char NewId[64];if(!Id){if(!SdkRandomId("resource-",NewId))goto done;Id=NewId;}
        xvalue* V=xrtValueClone(P);SdkPut(V,"id",Id);SdkPut(V,"owner",C->ExtensionId);xrtValueObjectSetNew(V,XRT_STR_LITERAL("context"),WbContext(C));WbBump(V);
        if(Old)WbRemove(Next,"resources",Id);xrtValueArrayAppendNew(WbArray(Next,"resources"),V);*R=xrtValueClone(V);
    }else{
        cstr Kind=!strcmp(Method,"panel.unregister")?"types":!strcmp(Method,"resource.remove")?"resources":!strcmp(Method,"browser.close")?"browsers":"panels";
        xvalue* V=WbFind(Next,Kind,Id);
        if(!V){WbFail(E,"resource_missing","The requested instance is unavailable",404);goto done;}
        if(!WbOwner(C,V)){WbFail(E,"owner_mismatch","The instance belongs to another extension",403);goto done;}
        if(!strcmp(Method,"panel.update")||!strcmp(Method,"panel.attach")){
            if(!WbBound(C,V)){WbFail(E,"context_mismatch","Panel belongs to another context",409);goto done;}
            if(!WbRevision(P,V,E))goto done;
            if(!strcmp(Method,"panel.attach")){
                if(!SdkIdValid(Client)){WbFail(E,"client_invalid","A valid display client is required",400);goto done;}
                xvalue* Clients=NULL;bool UiFound=false;
                if(!MdoApplicationUiCall(C,"ui.clients",NULL,&Clients,E,&UiFound))goto done;
                bool Busy=false;
                for(size_t i=0;i<xrtValueCount(Clients);i++)if(WbEqual(SdkField(xrtValueArrayGet(Clients,i),"id"),SdkField(V,"client"))&&!WbEqual(Client,SdkField(V,"client")))Busy=true;
                xrtValueRelease(Clients);
                if(Busy){WbFail(E,"client_busy","This panel is still attached to another live window",409);goto done;}
                SdkPut(V,"client",Client);
            }
            const cstr Fields[]={"title","resource","params","pinned","view_state"};
            for(size_t i=0;i<5;i++)if(SdkGet(P,Fields[i]))xrtValueObjectSet(V,xrtStrView(Fields[i]),SdkGet(P,Fields[i]));WbBump(V);*R=xrtValueClone(V);
        }else{
            if(!strcmp(Kind,"resources")&&!WbRevision(P,V,E))goto done;
            *R=xrtValueClone(V);WbRemove(Next,Kind,Id);
            if(!strcmp(Kind,"types")){
                xvalue* A=xrtValueArray();const xvalue* Panels=SdkGet(Next,"panels");
                for(size_t i=0;i<xrtValueCount(Panels);i++)if(!WbEqual(SdkField(xrtValueArrayGet(Panels,i),"type"),Id))xrtValueArrayAppend(A,xrtValueArrayGet(Panels,i));
                xrtValueObjectSetNew(Next,XRT_STR_LITERAL("panels"),A);
            }
        }
    }
    WbBump(Next);
    if(!WbSave(Next)){WbFail(E,"workbench_save_failed","Workbench could not be saved; no in-memory change was published",503);goto done;}
    xrtValueRelease(Workbench.State);Workbench.State=Next;Next=NULL;Ok=true;Changed=true;
done:xrtValueRelease(Next);xrtMutexUnlock(Workbench.Lock);
    if(!Ok){xrtValueRelease(*R);*R=NULL;}
    if(Changed){xvalue* Facts=xrtValueObject();SdkPut(Facts,"method",Method);SdkPut(Facts,"id",SdkField(*R,"id"));MdoApplicationEvent("workbench.changed",C,Facts);xrtValueRelease(Facts);}
    return Ok;
}
