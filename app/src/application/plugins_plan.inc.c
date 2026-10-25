static const xvalue* SdkPluginFind(const xvalue* Config,cstr Id){
    for(size_t I=0;Id&&I<xrtValueCount(Config);I++){const xvalue* R=xrtValueArrayGet(Config,I);if(!strcmp(Id,SdkField(R,"id")))return R;}return NULL;
}
static bool SdkPluginHash(const xvalue* V,char Out[65]){char* Text=SdkJson(V);bool Ok=Text&&MdoExtensionHash(Text,Out);xrtFree(Text);return Ok;}
static bool SdkPluginIssue(xvalue* Issues,cstr Code,cstr Id,cstr Other,cstr Detail){
    xvalue* R=xrtValueObject();bool Ok=R&&SdkPut(R,"code",Code)&&SdkPut(R,"id",Id)&&SdkPut(R,"other",Other)&&SdkPut(R,"detail",Detail)&&xrtValueArrayAppend(Issues,R);xrtValueRelease(R);return Ok;
}
static bool SdkPluginSetValidate(SdkPlugin* All,xvalue* Issues){
    bool Edge[32][32]={{false}};size_t Count=0,Sorted=0;bool Active[32]={false},Done[32]={false};
    for(size_t I=0;I<32;I++)if(All[I].Manifest&&SdkPluginEnabled(All[I].Manifest)&&!All[I].Error[0]){Active[I]=true;Count++;}
    for(size_t I=0;I<32;I++)if(Active[I]){
        const xvalue* M=All[I].Manifest,*D=SdkGet(M,"manifest");cstr Id=SdkField(M,"id");
        for(size_t J=0;J<I;J++)if(Active[J]){
            const xvalue* Other=All[J].Manifest,*OD=SdkGet(Other,"manifest");cstr OId=SdkField(Other,"id");
            const xvalue* A=SdkGet(D,"exclusive"),*B=SdkGet(OD,"exclusive");
            for(size_t X=0;X<xrtValueCount(A);X++)for(size_t Y=0;Y<xrtValueCount(B);Y++)if(xrtValueEqual(xrtValueArrayGet(A,X),xrtValueArrayGet(B,Y)))SdkPluginIssue(Issues,"exclusive",Id,OId,SdkText(xrtValueArrayGet(A,X)));
        }
        const cstr Relations[]={"requires","conflicts"};
        for(size_t K=0;K<2;K++){
            const xvalue* Rows=SdkGet(D,Relations[K]);
            for(size_t J=0;J<xrtValueCount(Rows);J++){
                const xvalue* R=xrtValueArrayGet(Rows,J);cstr Other=SdkField(R,"id");size_t Found=32;
                for(size_t X=0;X<32;X++)if(Active[X]&&!strcmp(Other,SdkField(All[X].Manifest,"id")))Found=X;
                bool Match=Found<32&&MdoPluginVersionMatches(R,SdkField(SdkGet(All[Found].Manifest,"manifest"),"version"));
                if(!K){if(!Match)SdkPluginIssue(Issues,"requires",Id,Other,"Required plugin is missing, disabled, or outside the allowed version range");else Edge[I][Found]=true;}
                else if(Match)SdkPluginIssue(Issues,"conflicts",Id,Other,SdkField(R,"reason")?SdkField(R,"reason"):"Declared incompatible plugins");
            }
        }
    }
    for(size_t Pass=0;Pass<32;Pass++)for(size_t I=0;I<32;I++)if(Active[I]&&!Done[I]){
        bool Ready=true;for(size_t J=0;J<32;J++)if(Edge[I][J]&&!Done[J])Ready=false;
        if(Ready){Done[I]=true;Sorted++;}
    }
    if(Sorted!=Count)SdkPluginIssue(Issues,"dependency_cycle","","","Enabled plugin dependencies contain a cycle");
    return !xrtValueCount(Issues);
}
static bool SdkPluginSetCheck(SdkPlugin* All,mdo_error_v1* E){
    xvalue* Issues=xrtValueArray();bool Ok=Issues&&SdkPluginSetValidate(All,Issues);
    if(!Ok){const xvalue* R=xrtValueArrayGet(Issues,0);char Message[256];snprintf(Message,sizeof(Message),"%s: %s / %s (%s)",SdkField(R,"code")?SdkField(R,"code"):"invalid",SdkField(R,"id")?SdkField(R,"id"):"",SdkField(R,"other")?SdkField(R,"other"):"",SdkField(R,"detail")?SdkField(R,"detail"):"");MdoApplicationError(E,"sdk_plugin_conflict",Message,"plugin",409,false);}
    xrtValueRelease(Issues);return Ok;
}
static bool SdkPluginTarget(const xvalue* Current,const xvalue* Changes,xvalue** Target,mdo_error_v1* E){
    *Target=NULL;if(xrtValueType(Changes)!=XVALUE_ARRAY||xrtValueCount(Changes)>32)return MdoApplicationError(E,"sdk_arguments_invalid","Plugin changes must be an array of at most 32 entries","plugin",422,false);
    for(size_t I=0;I<xrtValueCount(Changes);I++){
        const xvalue* R=xrtValueArrayGet(Changes,I);cstr Id=SdkField(R,"id");const cstr Keys[]={"id","enabled","revision","remove"};
        if(!MdoPackageId(Id)||!MdoPluginKeys(R,Keys,4)||!SdkPluginFind(Current,Id))return MdoApplicationError(E,"sdk_plugin_missing","Plugin change refers to an uninstalled plugin or invalid fields","plugin",404,false);
        for(size_t J=0;J<I;J++)if(!strcmp(Id,SdkField(xrtValueArrayGet(Changes,J),"id")))return MdoApplicationError(E,"sdk_arguments_invalid","Duplicate plugin change","plugin",422,false);
    }
    xvalue* Result=xrtValueArray();bool Ok=Result!=NULL;
    for(size_t I=0;Ok&&I<xrtValueCount(Current);I++){
        const xvalue* Old=xrtValueArrayGet(Current,I);cstr Id=SdkField(Old,"id");const xvalue* Change=SdkPluginFind(Changes,Id);bool Remove=false;
        if(SdkGet(Change,"remove")&&!xrtValueGetBool(SdkGet(Change,"remove"),&Remove)){Ok=false;break;}
        if(Remove){if(SdkGet(Change,"revision")||SdkGet(Change,"enabled")){Ok=false;break;}continue;}
        xvalue* Next=xrtValueClone(Old);cstr Revision=SdkField(Change,"revision");
        if(SdkGet(Change,"revision")){
            bool Valid=Revision&&strlen(Revision)==64;for(size_t J=0;Valid&&J<64;J++)if(!strchr("0123456789abcdef",Revision[J]))Valid=false;
            char Path[256];snprintf(Path,sizeof(Path),"plugins/%s/revisions/%s/manifest.json",Id,Valid?Revision:"invalid");bool Missing=false;
            char* Text=Valid?MdoExtensionRead(Path,true,1048576,&Missing):NULL;xvalue* Saved=SdkParse(Text);xrtFree(Text);
            if(!Saved||strcmp(Id,SdkField(Saved,"id")?SdkField(Saved,"id"):"")){xrtValueRelease(Saved);xrtValueRelease(Next);Ok=false;break;}
            xrtValueObjectRemove(Saved,XRT_STR_LITERAL("files"));
            xrtValueObjectSet(Saved,XRT_STR_LITERAL("enabled"),SdkGet(Old,"enabled"));
            if(strcmp(Revision,SdkField(Old,"revision")))SdkPut(Saved,"previous",SdkField(Old,"revision"));
            xrtValueRelease(Next);Next=Saved;
        }
        bool Enabled=false;if(SdkGet(Change,"enabled")){if(!xrtValueGetBool(SdkGet(Change,"enabled"),&Enabled)){xrtValueRelease(Next);Ok=false;break;}xrtValueObjectSetNew(Next,XRT_STR_LITERAL("enabled"),xrtValueBool(Enabled));}
        Ok=Next&&xrtValueArrayAppend(Result,Next);xrtValueRelease(Next);
    }
    if(!Ok){xrtValueRelease(Result);return MdoApplicationError(E,"sdk_arguments_invalid","Invalid plugin change or unavailable revision","plugin",422,false);}*Target=Result;return true;
}
static xvalue* SdkPluginPlan(const xvalue* Current,const xvalue* Changes,xvalue** Desired,mdo_error_v1* E){
    xvalue* Target=NULL;if(!SdkPluginTarget(Current,Changes,&Target,E))return NULL;
    SdkPlugin* All=xrtCalloc(32,sizeof(*All));xvalue* Issues=xrtValueArray(),*Result=xrtValueObject();bool Ok=All&&Issues&&Result;
    for(size_t I=0;Ok&&I<xrtValueCount(Target);I++)if(!SdkPluginMountRevision(&All[I],xrtValueArrayGet(Target,I)))SdkPluginIssue(Issues,"invalid_revision",SdkField(xrtValueArrayGet(Target,I),"id"),"","Installed plugin revision could not be verified");
    if(Ok)SdkPluginSetValidate(All,Issues);
    xvalue* Map=Ok&&!xrtValueCount(Issues)?SdkPluginMapBuild(All):NULL;
    if(Ok&&!xrtValueCount(Issues)&&!Map){cstr Why="Plugin targets overlap";for(size_t I=0;I<32;I++)if(All[I].Error[0])Why=All[I].Error;SdkPluginIssue(Issues,"files","","",Why);}
    char Before[65],After[65];Ok=Ok&&SdkPluginHash(Current,Before)&&SdkPluginHash(Target,After)&&SdkPut(Result,"base_revision",Before)&&SdkPut(Result,"plan_revision",After);
    if(Ok)Ok=xrtValueObjectSetNew(Result,XRT_STR_LITERAL("allowed"),xrtValueBool(!xrtValueCount(Issues)))&&xrtValueObjectSet(Result,XRT_STR_LITERAL("issues"),Issues)&&xrtValueObjectSet(Result,XRT_STR_LITERAL("desired"),Target);
    if(All){for(size_t I=0;I<32;I++)SdkPluginDrop(&All[I]);xrtFree(All);}xrtValueRelease(Map);xrtValueRelease(Issues);
    if(!Ok){xrtValueRelease(Target);xrtValueRelease(Result);return NULL;}if(Desired)*Desired=Target;else xrtValueRelease(Target);return Result;
}
