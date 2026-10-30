static const xvalue* SdkPluginFind(const xvalue* Config,cstr Id){
    for(size_t I=0;Id&&I<xrtValueCount(Config);I++){const xvalue* R=xrtValueArrayGet(Config,I);if(SdkField(R,"id")&&!strcmp(Id,SdkField(R,"id")))return R;}return NULL;
}
static bool SdkPluginHash(const xvalue* V,char Out[65]){char* Text=SdkJson(V);bool Ok=Text&&MdoExtensionHash(Text,Out);xrtFree(Text);return Ok;}
static bool SdkPluginIssue(xvalue* Issues,cstr Code,cstr Id,cstr Other,cstr Detail){
    xvalue* R=xrtValueObject();bool Ok=R&&SdkPut(R,"code",Code)&&SdkPut(R,"id",Id)&&SdkPut(R,"other",Other)&&SdkPut(R,"detail",Detail)&&xrtValueArrayAppend(Issues,R);xrtValueRelease(R);return Ok;
}
static bool SdkPluginSetValidate(SdkPlugin* All,xvalue* Issues){
 for(size_t I=0;I<MDO_PLUGIN_COUNT;I++){
  const xvalue* M=All[I].Manifest;if(!M||!SdkPluginEnabled(M)||All[I].Error[0])continue;
  for(size_t J=0;J<I;J++){
   const xvalue* Other=All[J].Manifest;if(!Other||!SdkPluginEnabled(Other)||All[J].Error[0])continue;
   if(!strcmp(SdkField(M,"alias"),SdkField(Other,"alias")))SdkPluginIssue(Issues,"alias",SdkField(M,"id"),SdkField(Other,"id"),"Two enabled plugins have the same callable alias");
   const xvalue* A=SdkGet(SdkGet(M,"manifest"),"exclusive"),*B=SdkGet(SdkGet(Other,"manifest"),"exclusive");
   for(size_t X=0;X<xrtValueCount(A);X++)for(size_t Y=0;Y<xrtValueCount(B);Y++)if(xrtValueEqual(xrtValueArrayGet(A,X),xrtValueArrayGet(B,Y)))SdkPluginIssue(Issues,"exclusive",SdkField(M,"id"),SdkField(Other,"id"),SdkText(xrtValueArrayGet(A,X)));
  }
 }
 return !xrtValueCount(Issues);
}
static bool SdkPluginSetCheck(SdkPlugin* All,mdo_error_v1* E){
    xvalue* Issues=xrtValueArray();bool Ok=Issues&&SdkPluginSetValidate(All,Issues);
    if(!Ok){const xvalue* R=xrtValueArrayGet(Issues,0);char Message[256];snprintf(Message,sizeof(Message),"%s: %s / %s (%s)",SdkField(R,"code")?SdkField(R,"code"):"invalid",SdkField(R,"id")?SdkField(R,"id"):"",SdkField(R,"other")?SdkField(R,"other"):"",SdkField(R,"detail")?SdkField(R,"detail"):"");MdoApplicationError(E,"sdk_plugin_conflict",Message,"plugin",409,false);}
    xrtValueRelease(Issues);return Ok;
}
static bool SdkPluginTarget(const xvalue* Current,const xvalue* Changes,xvalue** Target,mdo_error_v1* E){
 *Target=NULL;if(xrtValueType(Changes)!=XVALUE_ARRAY||xrtValueCount(Changes)>32)return MdoApplicationError(E,"sdk_arguments_invalid","Plugin changes must be an array of at most 32 activation flags","plugin",422,false);
 const cstr Keys[]={"id","enabled"};
 for(size_t I=0;I<xrtValueCount(Changes);I++){const xvalue* F=xrtValueArrayGet(Changes,I);cstr Id=SdkField(F,"id");bool Enabled;
  if(!MdoPluginKeys(F,Keys,2)||!MdoPluginIdentity(Id)||!SdkPluginFind(Current,Id)||!xrtValueGetBool(SdkGet(F,"enabled"),&Enabled))return MdoApplicationError(E,"sdk_arguments_invalid","Choose an installed plugin and enabled flag; revision switching and partial removal are not supported","plugin",422,false);
  for(size_t J=0;J<I;J++)if(!strcmp(Id,SdkField(xrtValueArrayGet(Changes,J),"id")))return MdoApplicationError(E,"sdk_arguments_invalid","Duplicate plugin change","plugin",422,false);
 }
 xvalue* Result=xrtValueArray();bool Ok=Result!=NULL;
 for(size_t I=0;Ok&&I<xrtValueCount(Current);I++){const xvalue* Old=xrtValueArrayGet(Current,I);xvalue* Next=xrtValueClone(Old);const xvalue* F=SdkPluginFind(Changes,SdkField(Old,"id"));
  Ok=Next&&(!F||xrtValueObjectSet(Next,XRT_STR_LITERAL("enabled"),SdkGet(F,"enabled")))&&xrtValueArrayAppend(Result,Next);xrtValueRelease(Next);
 }
 if(!Ok){xrtValueRelease(Result);return false;}*Target=Result;return true;
}
static xvalue* SdkPluginPlan(const xvalue* Current,const xvalue* Changes,xvalue** Desired,mdo_error_v1* E){
    xvalue* Target=NULL;if(!SdkPluginTarget(Current,Changes,&Target,E))return NULL;
    SdkPlugin* All=xrtCalloc(32,sizeof(*All));xvalue* Issues=xrtValueArray(),*Result=xrtValueObject();bool Ok=All&&Issues&&Result;
    for(size_t I=0;Ok&&I<xrtValueCount(Target);I++)if(!SdkPluginMountDirectory(&All[I],xrtValueArrayGet(Target,I))&&SdkPluginEnabled(xrtValueArrayGet(Target,I)))SdkPluginIssue(Issues,"invalid_directory",SdkField(xrtValueArrayGet(Target,I),"id"),"","Installed plugin directory could not be verified");
    if(Ok)SdkPluginSetValidate(All,Issues);
    if(Ok){char Why[512];xvalue* Common=MdoExtensionsActivationIssues("plugins",Changes,Why);Ok=Common!=NULL;
        for(size_t I=0;Ok&&I<xrtValueCount(Common);I++){const xvalue* R=xrtValueArrayGet(Common,I);Ok=SdkPluginIssue(Issues,SdkField(R,"code"),SdkField(R,"id"),SdkField(R,"dependency_id"),SdkField(R,"message"));}xrtValueRelease(Common);}

    xvalue* Map=Ok&&!xrtValueCount(Issues)?SdkPluginMapBuild(All):NULL;
    if(Ok&&!xrtValueCount(Issues)&&!Map){cstr Why="Plugin targets overlap";for(size_t I=0;I<32;I++)if(All[I].Error[0])Why=All[I].Error;SdkPluginIssue(Issues,"files","","",Why);}
    char Before[65],After[65];Ok=Ok&&SdkPluginHash(Current,Before)&&SdkPluginHash(Target,After)&&SdkPut(Result,"base_revision",Before)&&SdkPut(Result,"plan_revision",After);
    if(Ok)Ok=xrtValueObjectSetNew(Result,XRT_STR_LITERAL("allowed"),xrtValueBool(!xrtValueCount(Issues)))&&xrtValueObjectSet(Result,XRT_STR_LITERAL("issues"),Issues)&&xrtValueObjectSet(Result,XRT_STR_LITERAL("desired"),Target);
    if(All){for(size_t I=0;I<32;I++)SdkPluginDrop(&All[I]);xrtFree(All);}xrtValueRelease(Map);xrtValueRelease(Issues);
    if(!Ok){xrtValueRelease(Target);xrtValueRelease(Result);return NULL;}if(Desired)*Desired=Target;else xrtValueRelease(Target);return Result;
}
