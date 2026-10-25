static bool SdkPluginLookup(cstr Id,cstr Revision,SdkPlugin* Plugin,mdo_error_v1* E){
    xrtMutexLock(SdkPlugins.Lock);const xvalue* Row=SdkPluginFind(SdkPlugins.Config,Id);xvalue* Summary=Row?xrtValueClone(Row):NULL;xrtMutexUnlock(SdkPlugins.Lock);
    if(!Summary)return MdoApplicationError(E,"sdk_plugin_missing","Plugin is not installed","plugin",404,false);
    if(Revision&&strcmp(Revision,SdkField(Summary,"revision"))){
        xvalue* Changes=xrtValueArray(),*Change=xrtValueObject(),*Target=NULL;SdkPut(Change,"id",Id);SdkPut(Change,"revision",Revision);xrtValueArrayAppend(Changes,Change);
        xvalue* Current=xrtValueArray();xrtValueArrayAppend(Current,Summary);bool Ok=SdkPluginTarget(Current,Changes,&Target,E);xrtValueRelease(Current);xrtValueRelease(Changes);xrtValueRelease(Change);
        xrtValueRelease(Summary);Summary=Ok?xrtValueClone(xrtValueArrayGet(Target,0)):NULL;xrtValueRelease(Target);if(!Summary)return false;
    }
    bool Ok=SdkPluginMountRevision(Plugin,Summary);xrtValueRelease(Summary);
    return Ok||MdoApplicationError(E,"sdk_plugin_revision_invalid","Plugin revision files could not be verified","plugin",409,false);
}
static bool SdkPluginFileCall(const xvalue* P,xvalue** R,mdo_error_v1* E){
    cstr Id=SdkField(P,"id"),Path=SdkField(P,"path");SdkPlugin Plugin={0};if(!MdoPackageId(Id)||!MdoPluginPath(Path))return MdoApplicationError(E,"sdk_arguments_invalid","Choose an installed plugin inventory file","plugin",422,false);
    if(!SdkPluginLookup(Id,SdkField(P,"revision"),&Plugin,E))return false;
    const xvalue* F=MdoPluginFile(SdkGet(Plugin.Manifest,"files"),Path);uint64 Offset=0,Limit=65536,Size=0;SdkUInt(SdkGet(P,"offset"),&Offset);SdkUInt(SdkGet(P,"limit"),&Limit);SdkUInt(SdkGet(F,"bytes"),&Size);
    bool Ok=F&&Limit&&Limit<=65536&&Offset<=Size;char Native[600];snprintf(Native,sizeof(Native),"plugins/%s/revisions/%s/files/%s",Id,SdkField(Plugin.Manifest,"revision"),Path);
    xfile File=Ok?MdoHomeOpenRead(Native):NULL;size_t Count=Size-Offset<Limit?(size_t)(Size-Offset):(size_t)Limit;bytes Data=File?xrtMalloc(Count+1):NULL;
    Ok=Ok&&File&&Data&&xrtReadAtFull(File,Offset,Data,Count,NULL);if(File)Ok=xrtClose(File)&&Ok;
    if(Ok){char* Encoded=xrtBase64EncodeNew(Data,Count,NULL);*R=xrtValueObject();Ok=Encoded&&*R&&SdkPut(*R,"base64",Encoded)&&SdkPutUInt(*R,"next",Offset+Count)&&SdkPutUInt(*R,"total",Size);xrtFree(Encoded);}
    xrtFree(Data);SdkPluginDrop(&Plugin);return Ok||MdoApplicationError(E,"sdk_plugin_file_missing","The inventory file or requested byte range is unavailable","plugin",404,false);
}
static xvalue* SdkPluginPresetsRead(void){
    bool Missing=false;char* Text=MdoExtensionRead("config/plugin-combinations.json",true,1048576,&Missing);xvalue* Doc=SdkParse(Text);xrtFree(Text);
    if(!Doc&&Missing){Doc=xrtValueObject();SdkPut(Doc,"format","mdo.plugin-combinations.v1");xrtValueObjectSetNew(Doc,XRT_STR_LITERAL("items"),xrtValueArray());}
    if(!Doc||!SdkField(Doc,"format")||strcmp(SdkField(Doc,"format"),"mdo.plugin-combinations.v1")||xrtValueType(SdkGet(Doc,"items"))!=XVALUE_ARRAY||xrtValueCount(SdkGet(Doc,"items"))>32){xrtValueRelease(Doc);return NULL;}
    const xvalue* Items=SdkGet(Doc,"items");bool Ok=true;
    for(size_t I=0;Ok&&I<xrtValueCount(Items);I++){
        const xvalue* R=xrtValueArrayGet(Items,I),*Pins=SdkGet(R,"plugins");cstr Id=SdkField(R,"id"),Name=SdkField(R,"name");
        Ok=MdoPackageId(Id)&&Name&&strlen(Name)<=100&&xrtValueType(Pins)==XVALUE_ARRAY&&xrtValueCount(Pins)<=32;
        for(size_t J=0;Ok&&J<I;J++)if(!strcmp(Id,SdkField(xrtValueArrayGet(Items,J),"id")))Ok=false;
        for(size_t J=0;Ok&&J<xrtValueCount(Pins);J++){
            const xvalue* P=xrtValueArrayGet(Pins,J);cstr Pid=SdkField(P,"id"),Rev=SdkField(P,"revision");bool Enabled;
            Ok=MdoPackageId(Pid)&&Rev&&strlen(Rev)==64&&xrtValueGetBool(SdkGet(P,"enabled"),&Enabled);
            for(size_t K=0;Ok&&K<64;K++)if(!strchr("0123456789abcdef",Rev[K]))Ok=false;
            for(size_t K=0;Ok&&K<J;K++)if(!strcmp(Pid,SdkField(xrtValueArrayGet(Pins,K),"id")))Ok=false;
        }
    }if(!Ok){xrtValueRelease(Doc);return NULL;}return Doc;
}
static bool SdkPluginPresetMatches(const xvalue* Plugins,const xvalue* Current){
    if(xrtValueCount(Plugins)!=xrtValueCount(Current))return false;
    for(size_t I=0;I<xrtValueCount(Plugins);I++){const xvalue* R=xrtValueArrayGet(Plugins,I),*C=SdkPluginFind(Current,SdkField(R,"id"));
        if(!C||!SdkField(R,"revision")||strcmp(SdkField(R,"revision"),SdkField(C,"revision"))||SdkPluginEnabled(R)!=SdkPluginEnabled(C))return false;}return true;
}
static bool SdkPluginPresetCall(cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E){
    xvalue* Current=NULL;if(!MdoApplicationPluginsStoreBegin(&Current,E->Message,sizeof(E->Message)))return MdoApplicationError(E,"sdk_plugin_busy","Plugin manager is busy or recovering","plugin",409,false);
    xvalue* Doc=SdkPluginPresetsRead();bool Ok=Doc!=NULL;char Revision[65]={0};if(Ok)Ok=SdkPluginHash(Doc,Revision);
    if(!Ok){MdoApplicationError(E,"sdk_plugin_presets_invalid","Plugin combinations could not be read; the original file is retained","plugin",409,false);goto done;}
    const xvalue* Items=SdkGet(Doc,"items");cstr Id=SdkField(P,"id");const xvalue* Selected=SdkPluginFind(Items,Id);
    if(!strcmp(Method,"plugin.preset.list")){
        *R=xrtValueClone(Doc);SdkPut(*R,"revision",Revision);xvalue* Out=(xvalue*)SdkGet(*R,"items");
        for(size_t I=0;I<xrtValueCount(Out);I++){xvalue* Row=(xvalue*)xrtValueArrayGet(Out,I);xrtValueObjectSetNew(Row,XRT_STR_LITERAL("matches"),xrtValueBool(SdkPluginPresetMatches(SdkGet(Row,"plugins"),Current)));}goto done;
    }
    if(!MdoPackageId(Id)){Ok=MdoApplicationError(E,"sdk_arguments_invalid","Choose a portable combination ID","plugin",422,false);goto done;}
    if(!strcmp(Method,"plugin.preset.plan")){
        if(!Selected){Ok=MdoApplicationError(E,"sdk_plugin_preset_missing","Plugin combination is unavailable","plugin",404,false);goto done;}
        xvalue* Changes=xrtValueArray(),*Issues=xrtValueArray();const xvalue* Pins=SdkGet(Selected,"plugins");
        for(size_t I=0;I<xrtValueCount(Pins);I++){
            const xvalue* Pin=xrtValueArrayGet(Pins,I);if(!SdkPluginFind(Current,SdkField(Pin,"id")))SdkPluginIssue(Issues,"missing_plugin",SdkField(Pin,"id"),"","Install the saved plugin revision before applying this combination");
        }
        for(size_t I=0;I<xrtValueCount(Current);I++){
            const xvalue* C=xrtValueArrayGet(Current,I),*Pin=SdkPluginFind(Pins,SdkField(C,"id"));xvalue* Change=xrtValueObject();SdkPut(Change,"id",SdkField(C,"id"));xrtValueObjectSetNew(Change,XRT_STR_LITERAL("enabled"),xrtValueBool(Pin&&SdkPluginEnabled(Pin)));if(Pin)SdkPut(Change,"revision",SdkField(Pin,"revision"));xrtValueArrayAppendNew(Changes,Change);
        }
        if(xrtValueCount(Issues)){*R=xrtValueObject();xrtValueObjectSetNew(*R,XRT_STR_LITERAL("allowed"),xrtValueBool(false));xrtValueObjectSet(*R,XRT_STR_LITERAL("issues"),Issues);}
        else *R=SdkPluginPlan(Current,Changes,NULL,E);
        Ok=*R!=NULL;if(Ok)xrtValueObjectSet(*R,XRT_STR_LITERAL("changes"),Changes);xrtValueRelease(Changes);xrtValueRelease(Issues);goto done;
    }
    cstr Expected=SdkField(P,"expected_revision");if(!Expected||strcmp(Revision,Expected)){Ok=MdoApplicationError(E,"sdk_plugin_presets_changed","Combination list changed; read it again before saving","plugin",412,false);goto done;}
    bool Remove=!strcmp(Method,"plugin.preset.remove");cstr Name=SdkField(P,"name");
    if((Remove&&!Selected)||(!Remove&&(!Name||!*Name||strlen(Name)>100))){Ok=MdoApplicationError(E,"sdk_arguments_invalid","Choose an existing combination or provide its name","plugin",422,false);goto done;}
    xvalue* NewItems=xrtValueArray();for(size_t I=0;I<xrtValueCount(Items);I++){const xvalue* Row=xrtValueArrayGet(Items,I);if(strcmp(Id,SdkField(Row,"id")))xrtValueArrayAppend(NewItems,Row);}
    if(!Remove){
        xvalue* Row=xrtValueObject(),*Pins=xrtValueArray();SdkPut(Row,"id",Id);SdkPut(Row,"name",Name);
        for(size_t I=0;I<xrtValueCount(Current);I++){const xvalue* C=xrtValueArrayGet(Current,I);xvalue* Pin=xrtValueObject();SdkPut(Pin,"id",SdkField(C,"id"));SdkPut(Pin,"revision",SdkField(C,"revision"));xrtValueObjectSetNew(Pin,XRT_STR_LITERAL("enabled"),xrtValueBool(SdkPluginEnabled(C)));xrtValueArrayAppendNew(Pins,Pin);}
        xrtValueObjectSetNew(Row,XRT_STR_LITERAL("plugins"),Pins);xrtValueArrayAppendNew(NewItems,Row);
    }
    Ok=xrtValueCount(NewItems)<=32&&xrtValueObjectSet(Doc,XRT_STR_LITERAL("items"),NewItems);xrtValueRelease(NewItems);
    char* Text=Ok?SdkJson(Doc):NULL;Ok=Text&&MdoHomeAtomicWrite("config/plugin-combinations.json",Text,strlen(Text),true);xrtFree(Text);
    if(Ok){*R=xrtValueClone(Doc);SdkPluginHash(Doc,Revision);SdkPut(*R,"revision",Revision);}
done:
    xrtValueRelease(Doc);xrtValueRelease(Current);MdoApplicationPluginsStoreEnd();return Ok;
}
