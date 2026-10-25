/* Type adapters prepare inert files only. Plugin mount publication is owned by
 * the same durable transaction as ordinary resources; no installer scripts. */
static bool EcoNativeJsonFile(xvalue* Files,cstr Path,const xvalue* Value)
{
    size_t Size=0;char* Text=xrtJsonStringify(Value,false,&Size);
    bool Ok=Text&&Size<=1048576u&&EcoLocalAppend(Files,Path,Text,Size);xrtFree(Text);return Ok;
}
static bool EcoNativeFiles(xvalue* Files,const xvalue* Resource,char Error[1024])
{
    cstr Kind=MdoPackageText(Resource,"kind",16),Id=MdoPackageText(Resource,"id",64),Content=MdoPackageText(Resource,"content",131072);
    xvalue* Descriptor=xrtJsonParse(xrtStrView(Content));const xvalue* Extras=xrtValueObjectGet(Resource,XRT_STR_LITERAL("files"));
    bool Plugin=!strcmp(Kind,"plugins"),Ok=Descriptor!=NULL,Core=false;char Revision[65],Identity[65],Root[192],Path[512];
    char* Json=xrtJsonStringify(Resource,false,NULL);Ok=Ok&&Json&&MdoExtensionHash(Json,Revision)&&MdoExtensionHash(Id,Identity);xrtFree(Json);
    if(!Ok){xrtValueRelease(Descriptor);return false;}
    if(Plugin)snprintf(Root,sizeof(Root),"plugins/%s/revisions/%s",Id,Revision);
    else snprintf(Root,sizeof(Root),"data/ecosystem/toolpacks/%.16s/%s",Identity,Revision);
    xvalue* Inventory=xrtValueArray();Ok=Ok&&Inventory!=NULL;
    for(size_t i=0;Ok&&i<xrtValueCount(Extras);i++){
        const xvalue* File=xrtValueArrayGet(Extras,i);cstr Name=MdoPackageText(File,"path",220),Encoded=MdoPackageText(File,"base64",90u*1024u*1024u);
        size_t Size=0;bytes Data=Encoded?xrtBase64DecodeNew(Encoded,strlen(Encoded),&Size,NULL):NULL;char Hash[65];
        int Written=snprintf(Path,sizeof(Path),Plugin?"%s/files/%s":"%s/%s",Root,Name);
        Ok=Data&&Written>0&&(size_t)Written<sizeof(Path)&&(Plugin||strlen(Path)<=220)&&MdoExtensionHashBytes(Data,Size,Hash)&&EcoLocalAppend(Files,Path,Data,Size);
        if(Ok){xvalue* Item=xrtValueObject();Ok=Item&&MdoApiValueSetString(Item,"path",Name)&&MdoApiValueSetString(Item,"sha256",Hash)&&MdoApiValueSetUInt(Item,"bytes",Size)&&xrtValueArrayAppend(Inventory,Item);xrtValueRelease(Item);}
        if(Plugin&&(!strncmp(Name,"src/",4)||!strncmp(Name,"include/",8)||!strncmp(Name,"generated/",10)||!strcmp(Name,"main.c")||!strcmp(Name,"sources.json")))Core=true;
        xrtFree(Data);
    }
    if(Ok&&Plugin){
        xvalue* Manifest=xrtValueObject();bool Modern=MdoPluginModern(Descriptor);cstr Frontend=MdoPackageText(Modern?MdoPluginGet(Descriptor,"entry"):Descriptor,"frontend",220);
        if(Modern){Core=false;const xvalue* Overrides=MdoPluginGet(Descriptor,"overrides");for(size_t I=0;I<xrtValueCount(Overrides);I++){cstr T=MdoPackageText(xrtValueArrayGet(Overrides,I),"target",220);if(!strncmp(T,"src/",4)||!strncmp(T,"include/",8)||!strncmp(T,"generated/",10)||!strcmp(T,"main.c")||!strcmp(T,"sources.json"))Core=true;}}
        Ok=Manifest&&MdoApiValueSetString(Manifest,"id",Id)&&MdoApiValueSetString(Manifest,"revision",Revision)&&
            MdoApiValueSetString(Manifest,"frontend",Frontend?Frontend:"")&&MdoApiValueSetBool(Manifest,"enabled",!Modern)&&
            MdoApiValueSetBool(Manifest,"source_reload_required",Core)&&xrtValueObjectSet(Manifest,XRT_STR_LITERAL("files"),Inventory);
        if(Ok&&Modern)Ok=xrtValueObjectSet(Manifest,XRT_STR_LITERAL("manifest"),Descriptor);
        snprintf(Path,sizeof(Path),"%s/manifest.json",Root);if(Ok)Ok=EcoNativeJsonFile(Files,Path,Manifest);xrtValueRelease(Manifest);
    }else if(Ok){
        Ok=MdoApiValueSetString(Descriptor,"id",Id)&&MdoApiValueSetString(Descriptor,"root",Root)&&
            MdoApiValueSetString(Descriptor,"revision",Revision)&&MdoApiValueSetBool(Descriptor,"enabled",true)&&
            xrtValueObjectSet(Descriptor,XRT_STR_LITERAL("files"),Inventory);
        snprintf(Path,sizeof(Path),"data/ecosystem/toolpacks/%s.json",Id);if(Ok)Ok=EcoNativeJsonFile(Files,Path,Descriptor);
    }
    xrtValueRelease(Inventory);xrtValueRelease(Descriptor);
    if(!Ok&&!Error[0])snprintf(Error,1024,"Cannot prepare %s %s; a file path or decoded file is too large",Kind,Id);
    return Ok;
}
static const xvalue* EcoNativeFind(const xvalue* Config,cstr Id)
{
    for(size_t i=0;Id&&i<xrtValueCount(Config);i++){const xvalue* Item=xrtValueArrayGet(Config,i);cstr Existing=MdoPackageText(Item,"id",64);if(Existing&&!strcmp(Id,Existing))return Item;}
    return NULL;
}
static bool EcoNativeOwns(const xvalue* Prior,cstr Id)
{
    const xvalue* Resources=xrtValueObjectGet(Prior,XRT_STR_LITERAL("resources"));
    for(size_t i=0;i<xrtValueCount(Resources);i++){const xvalue* R=xrtValueArrayGet(Resources,i);cstr Kind=MdoPackageText(R,"kind",16),Existing=MdoPackageText(R,"id",64);
        if(Kind&&Existing&&!strcmp(Kind,"plugins")&&!strcmp(Id,Existing))return true;}
    return false;
}
static bool EcoNativePrepare(xvalue* Files,const xvalue* Package,const xvalue* Prior,bool Uninstall,bool Local,
    xvalue** Before,xvalue** After,bool* Reserved,char Error[1024])
{
    const xvalue* Resources=Uninstall?NULL:xrtValueObjectGet(Package,XRT_STR_LITERAL("resources"));
    const xvalue* OldResources=xrtValueObjectGet(Prior,XRT_STR_LITERAL("resources"));bool Has=false;
    for(size_t i=0;i<xrtValueCount(Resources);i++){cstr Kind=MdoPackageText(xrtValueArrayGet(Resources,i),"kind",16);if(Kind&&!strcmp(Kind,"plugins"))Has=true;}
    for(size_t i=0;i<xrtValueCount(OldResources);i++){cstr Kind=MdoPackageText(xrtValueArrayGet(OldResources,i),"kind",16);if(Kind&&!strcmp(Kind,"plugins"))Has=true;}
    if(!Has)return true;
    if(!MdoApplicationPluginsStoreBegin(Before,Error,1024))return false;
    *Reserved=true;
    *After=xrtValueArray();bool Ok=*After!=NULL;
    for(size_t i=0;Ok&&i<xrtValueCount(*Before);i++){
        const xvalue* Item=xrtValueArrayGet(*Before,i);cstr Id=MdoPackageText(Item,"id",64);
        bool Owned=Id&&EcoNativeOwns(Prior,Id);
        if(Owned){const xvalue* Recorded=EcoNativeFind(xrtValueObjectGet(Prior,XRT_STR_LITERAL("plugin_revisions")),Id);
            cstr Expected=MdoPackageText(Recorded,"revision",64),Current=MdoPackageText(Item,"revision",64);
            if(!Expected||!Current||strcmp(Expected,Current)){if(!Uninstall){snprintf(Error,1024,"Plugin was changed locally: %s",Id);return false;}Owned=false;}}
        if(!Owned)Ok=xrtValueArrayAppend(*After,Item);
    }
    for(size_t i=0;Ok&&i<xrtValueCount(Resources);i++){
        const xvalue* Resource=xrtValueArrayGet(Resources,i);cstr Kind=MdoPackageText(Resource,"kind",16),Id=MdoPackageText(Resource,"id",64);if(strcmp(Kind,"plugins"))continue;
        if(EcoNativeFind(*Before,Id)&&!EcoNativeOwns(Prior,Id)){snprintf(Error,1024,"Plugin ID belongs to another installation: %s",Id);return false;}
        char* Raw=xrtJsonStringify(Resource,false,NULL);char Hash[65],Path[256];Ok=Raw&&MdoExtensionHash(Raw,Hash);xrtFree(Raw);if(!Ok)break;
        snprintf(Path,sizeof(Path),"plugins/%s/revisions/%s/manifest.json",Id,Hash);
        const xvalue* File=NULL;for(size_t j=0;j<xrtValueCount(Files);j++)if(!strcmp(Path,MdoPackageText(xrtValueArrayGet(Files,j),"path",599)))File=xrtValueArrayGet(Files,j);
        xstrview Encoded;size_t Size=0;bytes Decoded=File&&xrtValueGetString(xrtValueObjectGet(File,XRT_STR_LITERAL("new")),&Encoded)?xrtBase64DecodeNew(Encoded.Data,Encoded.Size,&Size,NULL):NULL;
        xvalue* Manifest=Decoded?xrtJsonParse(xrtStrViewN((cstr)Decoded,Size)):NULL;xrtFree(Decoded);
        const xvalue* Old=EcoNativeFind(*Before,Id);cstr Previous=MdoPackageText(Old,"revision",64);
        if(Manifest&&Previous&&strcmp(Previous,Hash))Ok=MdoApiValueSetString(Manifest,"previous",Previous);
        if(Manifest&&Old)xrtValueObjectSet(Manifest,XRT_STR_LITERAL("enabled"),xrtValueObjectGet(Old,XRT_STR_LITERAL("enabled")));
        if(Manifest)Ok=Ok&&MdoApiValueSetString(Manifest,"source",Local?"local":"store");
        if(Manifest)xrtValueObjectRemove(Manifest,XRT_STR_LITERAL("files"));
        Ok=Ok&&Manifest&&xrtValueArrayAppend(*After,Manifest);xrtValueRelease(Manifest);
    }
    /* Active mappings and compatibility are validated by the common plugin
     * publisher after staging; disabled plugins may share payload filenames. */
    if(Ok){
        bool Missing=false;char* Raw=MdoExtensionRead("config/plugins.json",true,16777216,&Missing);
        xvalue* Current=Raw?xrtJsonParse(xrtStrView(Raw)):Missing?xrtValueArray():NULL;
        Ok=Current&&xrtValueEqual(Current,*Before)&&EcoNativeJsonFile(Files,"config/plugins.json",*After);
        if(Ok){xvalue* Change=(xvalue*)xrtValueArrayGet(Files,xrtValueCount(Files)-1);
            char* Encoded=Raw?xrtBase64EncodeNew(Raw,strlen(Raw),NULL):NULL;
            Ok=xrtValueObjectSetNew(Change,XRT_STR_LITERAL("old"),Raw?(Encoded?xrtValueString(xrtStrView(Encoded)):NULL):xrtValueNull());xrtFree(Encoded);}
        xrtFree(Raw);xrtValueRelease(Current);
    }
    if(!Ok&&!Error[0])snprintf(Error,1024,"Plugin configuration changed or could not be prepared; reload before retrying");
    return Ok;
}
static bool EcoNativeNeedsConfiguration(const xvalue* Package)
{
    const xvalue* Rs=xrtValueObjectGet(Package,XRT_STR_LITERAL("resources"));for(size_t i=0;i<xrtValueCount(Rs);i++){cstr Kind=MdoPackageText(xrtValueArrayGet(Rs,i),"kind",16);if(Kind&&!strcmp(Kind,"mcp"))return true;}return false;
}
static bool EcoNativeModes(const xvalue* Files)
{
#if !defined(_WIN32) && !defined(_WIN64) && !defined(__ANDROID__)
    for(size_t i=0;i<xrtValueCount(Files);i++){const xvalue* F=xrtValueArrayGet(Files,i);cstr Path=MdoPackageText(F,"path",599);
        if(!strncmp(Path,"data/ecosystem/toolpacks/",24)&&xrtValueType(xrtValueObjectGet(F,XRT_STR_LITERAL("new")))!=XVALUE_NULL&&!MdoHomeSetMode(Path,0755u))return false;}
#else
    (void)Files;
#endif
    return true;
}
