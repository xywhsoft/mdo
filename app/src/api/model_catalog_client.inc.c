#define MDO_CLIENT_CATALOG_PATH "data/model-catalog.json"
#define MDO_CLIENT_CATALOG_LIMIT (512u*1024u)
/* A fixed public service request with no account token, provider key, provider
 * credential reference or user-defined URL. Cache contains public facts only. */
static bool MdoClientCatalogValid(const xvalue* Root)
{
    uint64 V=0; int64 S=0;
    const xvalue* Version=xrtValueObjectGet(Root,XRT_STR_LITERAL("schema_version"));
    const xvalue* P=xrtValueObjectGet(Root,XRT_STR_LITERAL("providers"));
    const xvalue* E=xrtValueObjectGet(Root,XRT_STR_LITERAL("entries"));
    return ((xrtValueGetUInt(Version,&V)&&V==1) || (xrtValueGetInt(Version,&S)&&S==1)) &&
        MdoApiModelText(Root,"version",64) && xrtValueType(P)==XVALUE_ARRAY && xrtValueCount(P)<=64 &&
        xrtValueType(E)==XVALUE_ARRAY && xrtValueCount(E)<=2048;
}
static xvalue* MdoClientCatalogCache(void)
{
    xfile F=MdoHomeOpenRead(MDO_CLIENT_CATALOG_PATH);xfileinfo Info={0};char* Bytes=NULL;xvalue* Root=NULL;
    if(F && xrtFileStat(F,&Info) && Info.Size && Info.Size<=MDO_CLIENT_CATALOG_LIMIT){
        Bytes=xrtMalloc((size_t)Info.Size);
        if(Bytes && xrtReadFull(F,Bytes,(size_t)Info.Size,NULL))Root=xrtJsonParse(xrtStrViewN(Bytes,(size_t)Info.Size));
    }
    if(F)xrtClose(F);xrtFree(Bytes);xrtClearError();
    if(!MdoClientCatalogValid(Root)){xrtValueRelease(Root);Root=NULL;}return Root;
}
static xvalue* MdoParameterRegistry(MdoApiContext* Context,bool Refresh,bool* Stale)
{
    xvalue* Cached=MdoClientCatalogCache();if(Stale)*Stale=false;
    int64 At=0; uint64 Unsigned=0;
    const xvalue* Stamp=xrtValueObjectGet(Cached,XRT_STR_LITERAL("cached_at_ms"));
    if(!xrtValueGetInt(Stamp,&At) && xrtValueGetUInt(Stamp,&Unsigned) && Unsigned<=INT64_MAX)At=(int64)Unsigned;
    int64 Now=MdoUtcUs()/1000;
    if(Cached && !Refresh && At>0 && At<=Now && Now-At<86400000)return Cached;
    const char* Url=MDO_ACCOUNT_SERVICE_ORIGIN "/api/v1/ai/client-catalog";
    xllm_error Error={0};xhttpclient* Client=MdoApiModelDiscoveryClient(Context,Url,true,NULL,5000000u,&Error);
    xhttprequest* Request=xrtHttpRequestCreate(XRT_STR_LITERAL("GET"),xrtStrView(Url));
    xhttpresult* Result=NULL;xvalue* Envelope=NULL;xvalue* Catalog=NULL;
    if(Client && Request){xhttpcalloptions O;xrtHttpCallOptionsInit(&O);O.Cancel=Context->SendCancel;
        O.ResponseBodyLimit=MDO_CLIENT_CATALOG_LIMIT;O.Redirect=XHTTP_REDIRECT_MANUAL;O.Retry.Mode=XHTTP_RETRY_DISABLED;
        Result=xrtHttpClientDoSync(Client,Request,&O);const xhttpresponse* Response=Result?xrtHttpResultResponse(Result):NULL;
        if(Response && xrtHttpResponseStatus(Response)==200){
            xbytesview Bytes=xrtHttpResponseBody(Response);Envelope=xrtJsonParse(xrtStrViewN((cstr)Bytes.Data,Bytes.Size));
            const xvalue* Data=xrtValueObjectGet(Envelope,XRT_STR_LITERAL("data"));
            uint64 Code=1;int64 Signed=1;const xvalue* C=xrtValueObjectGet(Envelope,XRT_STR_LITERAL("code"));
            if(((xrtValueGetUInt(C,&Code)&&Code==0)||(xrtValueGetInt(C,&Signed)&&Signed==0)) && MdoClientCatalogValid(Data))Catalog=xrtValueClone(Data);
        }
    }
    if(Catalog){MdoApiValueSetUInt(Catalog,"cached_at_ms",(uint64)Now);size_t Size=0;char* Json=xrtJsonStringify(Catalog,false,&Size);
        if(Json)MdoHomeAtomicWrite(MDO_CLIENT_CATALOG_PATH,Json,Size,false);xrtFree(Json);xrtValueRelease(Cached);Cached=NULL;
    }
    xrtValueRelease(Envelope);xrtHttpResultDestroy(Result);xrtHttpRequestDestroy(Request);xrtHttpClientDestroy(Client);xrtClearError();
    if(!Catalog && Cached){Catalog=Cached;Cached=NULL;if(Stale)*Stale=true;}
    xrtValueRelease(Cached);return Catalog;
}
static bool MdoApiModelProvidersRun(MdoApiContext* Context)
{
    bool Stale=false;xvalue* Catalog=MdoParameterRegistry(Context,true,&Stale);
    if(!Catalog)return MdoApiReplyError(Context,503,"model_catalog_unavailable","The supplier catalog is unavailable; retry or add a custom supplier manually",NULL);
    xvalue* Data=xrtValueObject();
    bool Ok=Data && xrtValueObjectSet(Data,XRT_STR_LITERAL("providers"),xrtValueObjectGet(Catalog,XRT_STR_LITERAL("providers"))) &&
        MdoApiValueSetString(Data,"version",MdoApiModelText(Catalog,"version",64)) && MdoApiValueSetBool(Data,"stale",Stale);
    xrtValueRelease(Catalog);if(Ok)return MdoApiReplySuccessTake(Context,200,Data,NULL);
    xrtValueRelease(Data);return MdoApiReplyError(Context,503,"model_catalog_unavailable","Supplier catalog is unavailable",NULL);
}
