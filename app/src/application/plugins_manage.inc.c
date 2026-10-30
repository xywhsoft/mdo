static bool SdkPluginLookup(cstr Id,cstr Revision,SdkPlugin* Plugin,mdo_error_v1* E){
 char Error[512];xvalue* Summary=MdoExtensionsRead("plugins",Id,Error);
 if(!Summary)return MdoApplicationError(E,"sdk_plugin_missing","Plugin directory is not installed","plugin",404,false);
 bool Ok=SdkPluginMountDirectory(Plugin,Summary);xrtValueRelease(Summary);
 if(Ok&&Revision&&strcmp(Revision,SdkField(Plugin->Manifest,"revision")))Ok=false;
 return Ok||MdoApplicationError(E,"sdk_plugin_directory_changed","Plugin directory changed; read its current metadata","plugin",409,false);
}
static bool SdkPluginFileCall(const xvalue* P,xvalue** R,mdo_error_v1* E){
 cstr Id=SdkField(P,"id"),Path=SdkField(P,"path");if(!MdoPluginIdentity(Id)||!MdoPluginPath(Path))return MdoApplicationError(E,"sdk_arguments_invalid","Choose an installed plugin file","plugin",422,false);
 SdkPlugin V={0};if(!SdkPluginLookup(Id,SdkField(P,"revision"),&V,E)){SdkPluginDrop(&V);return false;}
 bool Ok=MdoPluginFile(SdkGet(V.Manifest,"files"),Path)!=NULL;char* Relative=Ok?xrtFormat("plugins/%s/%s",SdkField(V.Manifest,"directory_id"),Path):NULL;
 char* Text=Relative?MdoExtensionRead(Relative,true,1048576,NULL):NULL;Ok=Text&&xrtUtf8Valid(xrtStrView(Text),NULL);*R=Ok?xrtValueObject():NULL;
 if(Ok)Ok=*R&&SdkPut(*R,"text",Text)&&SdkPut(*R,"revision",SdkField(V.Manifest,"revision"));
 xrtFree(Relative);xrtFree(Text);SdkPluginDrop(&V);return Ok||MdoApplicationError(E,"sdk_plugin_file_not_text","Use the common binary file endpoint for non-text files or files over 1 MiB","plugin",422,false);
}
static xvalue* SdkPluginPresetsRead(void){
    bool Missing=false;char* Text=MdoExtensionRead("config/plugin-combinations.json",true,1048576,&Missing);xvalue* Doc=SdkParse(Text);xrtFree(Text);
    if(!Doc&&Missing){Doc=xrtValueObject();SdkPut(Doc,"format","mdo.plugin-combinations.v1");xrtValueObjectSetNew(Doc,XRT_STR_LITERAL("items"),xrtValueArray());}
    if(!Doc||!SdkField(Doc,"format")||strcmp(SdkField(Doc,"format"),"mdo.plugin-combinations.v1")||xrtValueType(SdkGet(Doc,"items"))!=XVALUE_ARRAY||xrtValueCount(SdkGet(Doc,"items"))>32){xrtValueRelease(Doc);return NULL;}
    const xvalue* Items=SdkGet(Doc,"items");bool Ok=true;
    for(size_t I=0;Ok&&I<xrtValueCount(Items);I++){
        const xvalue* R=xrtValueArrayGet(Items,I),*Pins=SdkGet(R,"plugins");cstr Id=SdkField(R,"id"),Name=SdkField(R,"name");
        Ok=MdoExtensionAlias(Id)&&Name&&strlen(Name)<=100&&xrtValueType(Pins)==XVALUE_ARRAY&&xrtValueCount(Pins)<=32;
        for(size_t J=0;Ok&&J<I;J++)if(!strcmp(Id,SdkField(xrtValueArrayGet(Items,J),"id")))Ok=false;
        for(size_t J=0;Ok&&J<xrtValueCount(Pins);J++){
            const xvalue* P=xrtValueArrayGet(Pins,J);cstr Pid=SdkField(P,"id");bool Enabled;
            Ok=MdoPluginIdentity(Pid)&&xrtValueGetBool(SdkGet(P,"enabled"),&Enabled)&&!SdkGet(P,"revision");
            for(size_t K=0;Ok&&K<J;K++)if(!strcmp(Pid,SdkField(xrtValueArrayGet(Pins,K),"id")))Ok=false;
        }
    }if(!Ok){xrtValueRelease(Doc);return NULL;}return Doc;
}
static bool SdkPluginPresetMatches(const xvalue* Plugins,const xvalue* Current){
    if(xrtValueCount(Plugins)!=xrtValueCount(Current))return false;
    for(size_t I=0;I<xrtValueCount(Plugins);I++){const xvalue* R=xrtValueArrayGet(Plugins,I),*C=SdkPluginFind(Current,SdkField(R,"id"));
        if(!C||SdkPluginEnabled(R)!=SdkPluginEnabled(C))return false;}return true;
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
    if(!MdoExtensionAlias(Id)){Ok=MdoApplicationError(E,"sdk_arguments_invalid","Choose a portable combination ID","plugin",422,false);goto done;}
    if(!strcmp(Method,"plugin.preset.plan")){
        if(!Selected){Ok=MdoApplicationError(E,"sdk_plugin_preset_missing","Plugin combination is unavailable","plugin",404,false);goto done;}
        xvalue* Changes=xrtValueArray(),*Issues=xrtValueArray();const xvalue* Pins=SdkGet(Selected,"plugins");
        for(size_t I=0;I<xrtValueCount(Pins);I++){
            const xvalue* Pin=xrtValueArrayGet(Pins,I);if(!SdkPluginFind(Current,SdkField(Pin,"id")))SdkPluginIssue(Issues,"missing_plugin",SdkField(Pin,"id"),"","Install the saved plugin before applying this combination");
        }
        for(size_t I=0;I<xrtValueCount(Current);I++){
            const xvalue* C=xrtValueArrayGet(Current,I),*Pin=SdkPluginFind(Pins,SdkField(C,"id"));xvalue* Change=xrtValueObject();SdkPut(Change,"id",SdkField(C,"id"));xrtValueObjectSetNew(Change,XRT_STR_LITERAL("enabled"),xrtValueBool(Pin&&SdkPluginEnabled(Pin)));xrtValueArrayAppendNew(Changes,Change);
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
        for(size_t I=0;I<xrtValueCount(Current);I++){const xvalue* C=xrtValueArrayGet(Current,I);xvalue* Pin=xrtValueObject();SdkPut(Pin,"id",SdkField(C,"id"));xrtValueObjectSetNew(Pin,XRT_STR_LITERAL("enabled"),xrtValueBool(SdkPluginEnabled(C)));xrtValueArrayAppendNew(Pins,Pin);}
        xrtValueObjectSetNew(Row,XRT_STR_LITERAL("plugins"),Pins);xrtValueArrayAppendNew(NewItems,Row);
    }
    Ok=xrtValueCount(NewItems)<=32&&xrtValueObjectSet(Doc,XRT_STR_LITERAL("items"),NewItems);xrtValueRelease(NewItems);
    char* Text=Ok?SdkJson(Doc):NULL;Ok=Text&&MdoHomeAtomicWrite("config/plugin-combinations.json",Text,strlen(Text),true);xrtFree(Text);
    if(Ok){*R=xrtValueClone(Doc);SdkPluginHash(Doc,Revision);SdkPut(*R,"revision",Revision);}
done:
    xrtValueRelease(Doc);xrtValueRelease(Current);MdoApplicationPluginsStoreEnd();return Ok;
}
