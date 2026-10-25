/* A connection is one atomic runtime file, including its immutable recipe and
 * private bindings. Project files remain portable; secrets are vault references.
 * GET/list stays cold. Only explicit test/use can connect a saved definition. */
static const xvalue* McpConnectionBinding(const xvalue* Doc)
{return xrtValueObjectGet(Doc,XRT_STR_LITERAL("mdo_template"));}
static bool McpConnectionCredits(xvalue* Item,const xvalue* Doc)
{
    const xvalue* B=McpConnectionBinding(Doc);if(!B)return true;
    const xvalue* R=xrtValueObjectGet(B,XRT_STR_LITERAL("receipt")),*M=xrtValueObjectGet(R,XRT_STR_LITERAL("manifest"));
    cstr Source=MdoPackageText(R,"source",16);bool Managed=!Source||strcmp(Source,"local");
    bool Ok=MdoApiValueSetBool(Item,"store_managed",Managed)&&MdoApiValueSetBool(Item,"template_connection",true)&&xrtValueObjectSet(Item,XRT_STR_LITERAL("template_receipt"),R);
    const char* Keys[]={"submitter","author","copyright","license","version"};const size_t Limits[]={100,100,4096,80,32};
    for(size_t i=0;Ok&&i<5;i++){cstr S=MdoPackageText(i?M:R,Keys[i],Limits[i]);Ok=MdoApiValueSetString(Item,Keys[i],S?S:"");}return Ok;
}
static bool McpConnectionUses(const xvalue* V,cstr Id)
{
    if(xrtValueType(V)==XVALUE_OBJECT){cstr N=MdoPackageText(V,"input",64);if(N)return !strcmp(N,Id);xvalueiter It={0};xvaluekey K;xvalue* E;bool Found=false;if(xrtValueIterBegin(V,&It)){while(!Found&&(E=xrtValueIterNext(&It,&K)))Found=McpConnectionUses(E,Id);xrtValueIterEnd(&It);}return Found;}
    if(xrtValueType(V)==XVALUE_ARRAY){for(size_t i=0;i<xrtValueCount(V);i++)if(McpConnectionUses(xrtValueArrayGet(V,i),Id))return true;}
    xstrview S;return xrtValueGetString(V,&S)&&S.Size==strlen(Id)+6&&!memcmp(S.Data,"input:",6)&&!strcmp(S.Data+6,Id);
}
static xvalue* McpConnectionResolve(const xvalue* V,const xvalue* Values,unsigned Depth)
{
    if(Depth>20)return NULL;
    if(xrtValueType(V)==XVALUE_OBJECT){cstr Id=MdoPackageText(V,"input",64);if(Id)return xrtValueClone(xrtValueObjectGet(Values,xrtStrView(Id)));
        xvalue* O=xrtValueObject();xvalueiter It={0};xvaluekey K;xvalue* E;bool Ok=O!=NULL;
        if(xrtValueIterBegin(V,&It)){while(Ok&&(E=xrtValueIterNext(&It,&K))){xvalue* R=McpConnectionResolve(E,Values,Depth+1);Ok=R&&xrtValueObjectSet(O,K.String,R);xrtValueRelease(R);}xrtValueIterEnd(&It);}
        if(!Ok){xrtValueRelease(O);return NULL;}return O;}
    if(xrtValueType(V)==XVALUE_ARRAY){xvalue* A=xrtValueArray();bool Ok=A!=NULL;for(size_t i=0;Ok&&i<xrtValueCount(V);i++){xvalue* R=McpConnectionResolve(xrtValueArrayGet(V,i),Values,Depth+1);Ok=R&&xrtValueArrayAppend(A,R);xrtValueRelease(R);}if(!Ok){xrtValueRelease(A);return NULL;}return A;}
    xstrview S;if(xrtValueGetString(V,&S)&&S.Size>6&&!memcmp(S.Data,"input:",6))return xrtValueClone(xrtValueObjectGet(Values,xrtStrView(S.Data+6)));
    return xrtValueClone(V);
}
static bool McpConnectionValue(const xvalue* I,cstr S)
{
    if(!MdoMcpTemplateString(S)||strlen(S)>4096)return false;
    if(!S[0])return !MdoMcpTemplateFlag(I,"required",true);
    cstr Type=MdoPackageText(I,"type",16);
    if(!strcmp(Type,"select")){const xvalue* A=xrtValueObjectGet(I,XRT_STR_LITERAL("options"));for(size_t j=0;j<xrtValueCount(A);j++){xstrview V;if(xrtValueGetString(xrtValueArrayGet(A,j),&V)&&xrtStrEqual(V,xrtStrView(S)))return true;}return false;}
    if(!strcmp(Type,"boolean"))return !strcmp(S,"true")||!strcmp(S,"false");
    if(!strcmp(Type,"number")){cstr P=S;bool Negative=*P=='-';if(Negative)P++;if(!*P)return false;uint64 U=0;for(;*P;P++){if(*P<'0'||*P>'9'||U>((uint64)INT64_MAX-9)/10)return false;U=U*10+(*P-'0');}int64 N=Negative?-(int64)U:(int64)U,Bound=0;if(MdoPackageNumber(I,"min",&Bound)&&N<Bound)return false;if(MdoPackageNumber(I,"max",&Bound)&&N>Bound)return false;}
    return true;
}
static xvalue* McpConnectionTemplate(const xvalue* Body,const xvalue* Old,xvalue** Receipt)
{
    int64 Id=0;*Receipt=NULL;
    cstr ReceiptKey=MdoPackageText(Body,"receipt_key",80);
    if(!ReceiptKey&&!MdoPackageNumber(Body,"receipt_id",&Id)){const xvalue* B=McpConnectionBinding(Old);*Receipt=xrtValueClone(xrtValueObjectGet(B,XRT_STR_LITERAL("receipt")));return xrtValueClone(xrtValueObjectGet(B,XRT_STR_LITERAL("template")));}
    char Key[81];if(ReceiptKey)snprintf(Key,sizeof(Key),"%s",ReceiptKey);else snprintf(Key,sizeof(Key),"%lld",(long long)Id);bool Known=false;xvalue* Records=ExtensionStoreRead(&Known);const xvalue* R=xrtValueObjectGet(Records,xrtStrView(Key)),*Files=xrtValueObjectGet(R,XRT_STR_LITERAL("files"));
    xvalue* T=NULL;
    for(size_t i=0;Known&&i<xrtValueCount(Files);i++){const xvalue* F=xrtValueArrayGet(Files,i);cstr Path=MdoPackageText(F,"path",255);if(!Path||strncmp(Path,"data/ecosystem/mcp/",19))continue;
        bool Missing=false;char* Text=EcoLocalUnchanged(F)?MdoExtensionRead(Path,true,131072,&Missing):NULL;T=Text?xrtJsonParse(xrtStrView(Text)):NULL;xrtFree(Text);if(T){*Receipt=xrtValueClone(R);break;}}
    xrtValueRelease(Records);return T;
}
static bool McpConnectionSameTarget(const xvalue* A,const xvalue* B)
{
    const xvalue* OA=xrtValueObjectGet(A,XRT_STR_LITERAL("oauth")),*OB=xrtValueObjectGet(B,XRT_STR_LITERAL("oauth"));
    return xrtValueEqual(xrtValueObjectGet(A,XRT_STR_LITERAL("transport")),xrtValueObjectGet(B,XRT_STR_LITERAL("transport")))&&((!OA&&!OB)||xrtValueEqual(OA,OB));
}

/* Conservative reference collection: an unreadable/truncated scope retains
 * credentials. Never remove a vault item still referenced by another config. */
static bool McpConnectionReferenceExists(cstr Reference,xroot Root,cstr Directory)
{
    xdir D=Root?xrtRootDirOpen(Root,Directory,XDIR_STAT):MdoHomeOpenDirectory(Directory,XDIR_STAT);
    if(!D){const xerror* E=xrtGetError();bool Missing=E&&xrtErrorKind(E)==XERR_NOT_FOUND;xrtClearError();return !Missing;}
    bool Found=false;xdirentry E;xdirnext Next=XDIR_NEXT_END;size_t Count=0;
    while(!Found&&(Next=xrtDirNext(D,&E))==XDIR_NEXT_ITEM){if(E.Name.Size<6||E.Name.Size>69||memcmp(E.Name.Data+E.Name.Size-5,".json",5))continue;
        if(++Count>256){Found=true;break;}char Path[192];snprintf(Path,sizeof(Path),"%s/%.*s",Directory,(int)E.Name.Size,E.Name.Data);bool Missing=false;
        char* Text=Root?SkillRead(Root,Path,&Missing):MdoExtensionRead(Path,true,MDO_EXTENSION_TEXT_LIMIT,&Missing);Found=!Text||strstr(Text,Reference)!=NULL;xrtFree(Text);}
    if(Next==XDIR_NEXT_ERROR)Found=true;
    xrtDirClose(D);return Found;
}
static bool McpConnectionScopeReference(cstr Reference,cstr Workspace)
{
    xroot Root=xrtRootOpen(Workspace);if(!Root){const xerror* E=xrtGetError();bool Missing=E&&xrtErrorKind(E)==XERR_NOT_FOUND;xrtClearError();return !Missing;}
    bool Found=McpConnectionReferenceExists(Reference,Root,".mdo/mcp");xrtRootClose(Root);return Found;
}
static bool McpConnectionReferenced(cstr Reference)
{
    if(McpConnectionReferenceExists(Reference,NULL,"mcp"))return true;
    MdoProjectInfo Projects[MDO_PROJECT_LIST_LIMIT];size_t Count=0,Invalid=0;bool Truncated=false;xwork_error E;
    if(!MdoProjectList(Projects,MDO_PROJECT_LIST_LIMIT,&Count,&Invalid,&Truncated,&E)||Truncated||Invalid)return true;
    char* Default=MdoHomeDefaultWorkspacePath(false);bool Found=Default&&McpConnectionScopeReference(Reference,Default);xrtFree(Default);
    for(size_t i=0;!Found&&i<Count;i++)Found=McpConnectionScopeReference(Reference,Projects[i].WorkspaceRoot);
    return Found;
}
static void McpConnectionCleanTemplate(const xvalue* Old)
{
    const xvalue* B=McpConnectionBinding(Old),*Files=xrtValueObjectGet(xrtValueObjectGet(B,XRT_STR_LITERAL("receipt")),XRT_STR_LITERAL("files"));bool Known=false;xvalue* Records=ExtensionStoreRead(&Known);
    for(size_t i=0;Known&&i<xrtValueCount(Files);i++){const xvalue* F=xrtValueArrayGet(Files,i);cstr P=MdoPackageText(F,"path",255);if(P&&MdoPackagePath(P)&&!strncmp(P,"data/ecosystem/mcp/",19)&&!EcoMcpFileRetained(Records,"",P)&&EcoLocalUnchanged(F))(void)MdoHomeRemove(P,false);}
    xrtValueRelease(Records);
}
static void McpConnectionReleaseCredentials(const xvalue* Old)
{
    const xvalue* B=McpConnectionBinding(Old),*T=xrtValueObjectGet(B,XRT_STR_LITERAL("template")),*Inputs=xrtValueObjectGet(T,XRT_STR_LITERAL("inputs")),*Values=xrtValueObjectGet(B,XRT_STR_LITERAL("values"));
    for(size_t i=0;i<xrtValueCount(Inputs);i++){
        const xvalue* I=xrtValueArrayGet(Inputs,i);if(!MdoMcpTemplateFlag(I,"secret",false))continue;cstr Ref=MdoPackageText(Values,MdoPackageText(I,"id",64),256);
        if(Ref&&*Ref&&!McpConnectionReferenced(Ref))(void)MdoSecretDiscard(Ref);
    }
}
bool MdoApiMcpTemplatesRoute(MdoApiContext* C)
{
    xrtMutexLock(g_MdoExtensionLock);bool Known=false;xvalue* Records=ExtensionStoreRead(&Known),*Items=xrtValueArray();
    xvalueiter It={0};xvaluekey K;xvalue* R;bool Ok=Known&&Items;
    if(xrtValueIterBegin(Records,&It)){while(Ok&&(R=xrtValueIterNext(&It,&K))){
        const xvalue* Files=xrtValueObjectGet(R,XRT_STR_LITERAL("files"));
        for(size_t i=0;Ok&&i<xrtValueCount(Files);i++){const xvalue* F=xrtValueArrayGet(Files,i);cstr P=MdoPackageText(F,"path",255);if(!P||strncmp(P,"data/ecosystem/mcp/",19))continue;
            bool Missing=false;char* Text=EcoLocalUnchanged(F)?MdoExtensionRead(P,true,131072,&Missing):NULL;xvalue* T=Text?xrtJsonParse(xrtStrView(Text)):NULL;xrtFree(Text);xvalue* Item=xrtValueObject();
            Ok=Item&&MdoApiValueSetBool(Item,"valid",T!=NULL)&&xrtValueObjectSet(Item,XRT_STR_LITERAL("receipt"),R)&&(!T||xrtValueObjectSet(Item,XRT_STR_LITERAL("template"),T))&&xrtValueArrayAppend(Items,Item);xrtValueRelease(Item);xrtValueRelease(T);}}
        xrtValueIterEnd(&It);}
    xvalue* Data=xrtValueObject();Ok=Ok&&Data&&xrtValueObjectSet(Data,XRT_STR_LITERAL("items"),Items);xrtValueRelease(Items);xrtValueRelease(Records);xrtMutexUnlock(g_MdoExtensionLock);
    if(!Ok){xrtValueRelease(Data);return MdoApiReplyError(C,503,"mcp_templates_unavailable","Template receipts cannot be verified",NULL);}return MdoApiReplySuccessTake(C,200,Data,NULL);
}
bool MdoApiMcpConnectionsRoute(MdoApiContext* C)
{
    MdoApiJsonBody Body={0};MdoApiBodyStatus Read=MdoApiJsonBodyRead(C,&Body);if(Read!=MDO_API_BODY_OK)return MdoApiReplyBodyError(C,Read);
    cstr Id=MdoPackageText(Body.Value,"id",64),Action=MdoPackageText(Body.Value,"action",16);char Error[1024]={0},Path[160],RuntimeId[129];bool Result=false;
    if(!MdoExtensionIdValid(Id)||!Action){ExtensionBodyUnit(&Body);return MdoApiReplyError(C,422,"mcp_connection_invalid","Choose an instance ID and action",NULL);}
    char* Workspace=NULL;if(!MdoApiSkillScopeMode(C,&Workspace,true)){ExtensionBodyUnit(&Body);return true;}
    MdoProjectLease* Lease=NULL;xroot Root=NULL;xfile Lock=NULL;MdoProjectInfo Project={0};Project.Size=sizeof(Project);char ProjectId[65]={0};
    if(Workspace){/* Resolve only a registered project, never a request path. */
        size_t N=0;(void)xrtPercentDecode(xrtStrViewN(C->Target.Query.Data+8,C->Target.Query.Size-8),ProjectId,64,&N);ProjectId[N]=0;
        xwork_error E;Lease=MdoProjectLeaseAcquire(ProjectId,MDO_PROJECT_LEASE_SHARED,&E);
        if(!Lease){xrtFree(Workspace);ExtensionBodyUnit(&Body);return MdoApiReplyError(C,409,"mcp_project_busy","Project is unavailable or busy",NULL);}
    }
    xrtMutexLock(g_MdoExtensionLock);
    if(Workspace){Root=xrtRootOpen(Workspace);Lock=Root?McpProjectWriterLock(Root):NULL;snprintf(Path,sizeof(Path),".mdo/mcp/%s.json",Id);char H[65];MdoExtensionHash(Workspace,H);snprintf(RuntimeId,sizeof(RuntimeId),"mdo-project-%.16s.%s",H,Id);}
    else {ExtensionPath("mcp",Id,Path);snprintf(RuntimeId,sizeof(RuntimeId),"%s",Id);}
    bool Missing=false;char* Old=(Workspace&&Lock)?SkillRead(Root,Path,&Missing):!Workspace?MdoExtensionRead(Path,false,MDO_EXTENSION_TEXT_LIMIT,&Missing):NULL;
    xvalue* OldDoc=Old?xrtJsonParse(xrtStrView(Old)):NULL,*Receipt=NULL,*T=NULL,*Values=xrtValueObject(),*Doc=NULL,*Binding=NULL;
    char* Text=NULL;char References[32][MDO_SECRET_VAULT_REFERENCE_CAPACITY];size_t RefCount=0;bool Saved=false,Preview=!strcmp(Action,"preview");
    if((Workspace&&!Lock)||(!Old&&!Missing)){snprintf(Error,sizeof(Error),"Existing configuration cannot be read safely");goto done;}
    if(!ExtensionMatch(C,Old)){Result=true;goto done;}
    if(!strcmp(Action,"delete")){
        if(!Old){snprintf(Error,sizeof(Error),"Connection no longer exists");goto done;}
        Saved=Workspace?xrtRootRemove(Root,Path):MdoHomeRemove(Path,false);
        if(Saved){bool Ok=Workspace?MdoMcpConfigureAgent(NULL,Workspace,NULL):MdoMcpManagerReload();if(!Ok){if(Workspace)SkillWrite(Root,Path,Old,strlen(Old));else MdoHomeAtomicWrite(Path,Old,strlen(Old),false);snprintf(Error,sizeof(Error),"Runtime reload failed; connection restored");Saved=false;}else{(void)MdoMcpAuthorizationForget(RuntimeId);Result=MdoApiReplySuccessTake(C,200,xrtValueObject(),NULL);}}
        goto done;
    }
    if(strcmp(Action,"save")&&strcmp(Action,"preview")){snprintf(Error,sizeof(Error),"Unsupported connection action");goto done;}
    if(Old&&!McpConnectionBinding(OldDoc)){snprintf(Error,sizeof(Error),"A custom server already owns this ID; choose another instance ID");goto done;}
    T=McpConnectionTemplate(Body.Value,OldDoc,&Receipt);cstr Tid=MdoPackageText(T,"id",64);
    if(!T||!Receipt||!MdoMcpTemplateValidate(T,Tid,Error,sizeof(Error))){if(!Error[0])snprintf(Error,sizeof(Error),"Install a verified template version first");goto done;}
    cstr Variant=MdoPackageText(Body.Value,"variant",64);if(!Variant)Variant=MdoPackageText(McpConnectionBinding(OldDoc),"variant",64);
    const xvalue* Variants=xrtValueObjectGet(T,XRT_STR_LITERAL("variants")),*Selected=NULL;
    for(size_t i=0;Variant&&i<xrtValueCount(Variants);i++){const xvalue* V=xrtValueArrayGet(Variants,i);if(!strcmp(Variant,MdoPackageText(V,"id",64)))Selected=V;}
    bool Supported=false;const xvalue* Ps=xrtValueObjectGet(Selected,XRT_STR_LITERAL("platforms"));for(size_t i=0;i<xrtValueCount(Ps);i++){xstrview P;if(xrtValueGetString(xrtValueArrayGet(Ps,i),&P)&&xrtStrEqual(P,xrtStrView(MdoToolPlatform())))Supported=true;}
    if(!Selected||!Supported){snprintf(Error,sizeof(Error),"This connection variant does not support this device");goto done;}
    const xvalue* OldBinding=McpConnectionBinding(OldDoc),*OldValues=xrtValueObjectGet(OldBinding,XRT_STR_LITERAL("values")),*Supplied=xrtValueObjectGet(Body.Value,XRT_STR_LITERAL("values")),*Inputs=xrtValueObjectGet(T,XRT_STR_LITERAL("inputs"));
    bool SameTemplate=xrtValueEqual(T,xrtValueObjectGet(OldBinding,XRT_STR_LITERAL("template")))&&Variant&&MdoPackageText(OldBinding,"variant",64)&&!strcmp(Variant,MdoPackageText(OldBinding,"variant",64));
    for(size_t i=0;i<xrtValueCount(Inputs);i++){
        const xvalue* I=xrtValueArrayGet(Inputs,i);cstr Key=MdoPackageText(I,"id",64),S=MdoPackageText(Supplied,Key,4096);if(!McpConnectionUses(xrtValueObjectGet(Selected,XRT_STR_LITERAL("config")),Key))continue;bool Secret=MdoMcpTemplateFlag(I,"secret",false);
        bool Clear=false;const xvalue* Clears=xrtValueObjectGet(Body.Value,XRT_STR_LITERAL("clear_secrets"));for(size_t j=0;j<xrtValueCount(Clears);j++){xstrview V;if(xrtValueGetString(xrtValueArrayGet(Clears,j),&V)&&xrtStrEqual(V,xrtStrView(Key)))Clear=true;}
        if(Secret&&!Clear&&(!S||!*S)&&SameTemplate){cstr Ref=MdoPackageText(OldValues,Key,256);if(Ref&&strncmp(Ref,"input:",6)&&MdoApiValueSetString(Values,Key,Ref))continue;}
        if(!S&&!Secret)S=MdoPackageText(OldValues,Key,4096);
        if(!S&&!Secret)S=MdoPackageText(I,"default",4096);
        if(!S)S="";
        if(!McpConnectionValue(I,S)){snprintf(Error,sizeof(Error),"Parameter '%s' is missing or invalid",Key);goto done;}
        if(Secret&&!S[0]&&!MdoMcpTemplateFlag(I,"required",true)){if(!MdoApiValueSetString(Values,Key,""))goto done;continue;}
        if(Secret){if(Preview){char Ref[80];snprintf(Ref,sizeof(Ref),"env:MDO_PREVIEW_%zu",i);if(!MdoApiValueSetString(Values,Key,Ref))goto done;}
            else if(!xsCredentialProtectionAvailable()||RefCount>=32||!MdoSecretStore(S,References[RefCount])){snprintf(Error,sizeof(Error),"System credential protection is unavailable; no plaintext fallback is used");goto done;}
            else {if(!MdoApiValueSetString(Values,Key,References[RefCount++]))goto done;}}
        else if(!MdoApiValueSetString(Values,Key,S))goto done;
    }
    Doc=McpConnectionResolve(xrtValueObjectGet(Selected,XRT_STR_LITERAL("config")),Values,0);
    cstr Name=MdoPackageText(Body.Value,"name",256);bool Enable=MdoMcpTemplateFlag(Body.Value,"enabled",OldDoc?MdoMcpTemplateFlag(OldDoc,"enabled",false):false);
    if(!Doc||!MdoApiValueSetString(Doc,"id",Id)||!MdoApiValueSetBool(Doc,"enabled",Enable)||(Name&&(!*Name||!MdoApiValueSetString(Doc,"name",Name))))goto done;
    const xvalue* Tools=xrtValueObjectGet(Body.Value,XRT_STR_LITERAL("tools"));if(!Tools)Tools=xrtValueObjectGet(OldDoc,XRT_STR_LITERAL("tools"));if(Tools&&!xrtValueObjectSet(Doc,XRT_STR_LITERAL("tools"),Tools))goto done;
    xvalue* MutableTransport=(xvalue*)xrtValueObjectGet(Doc,XRT_STR_LITERAL("transport"));
    for(size_t k=0;k<2;k++){xvalue* A=(xvalue*)xrtValueObjectGet(MutableTransport,xrtStrView(k?"headers":"environment"));for(size_t j=0;j<xrtValueCount(A);){cstr Ref=MdoPackageText(xrtValueArrayGet(A,j),"secret_ref",256);if(Ref&&!Ref[0])xrtValueArrayRemove(A,j,1);else j++;}}
    xvalue* OptionalOAuth=(xvalue*)xrtValueObjectGet(Doc,XRT_STR_LITERAL("oauth"));cstr OptionalSecret=MdoPackageText(OptionalOAuth,"client_secret_ref",256);if(OptionalSecret&&!OptionalSecret[0])xrtValueObjectRemove(OptionalOAuth,XRT_STR_LITERAL("client_secret_ref"));
    const xvalue* Transport=xrtValueObjectGet(Doc,XRT_STR_LITERAL("transport"));cstr Type=MdoPackageText(Transport,"type",32);
    bool Changed=OldDoc&&!McpConnectionSameTarget(OldDoc,Doc);
    if(Changed){/* A changed target must never receive a retained credential. */
        for(size_t i=0;i<xrtValueCount(Inputs);i++){const xvalue* I=xrtValueArrayGet(Inputs,i);cstr Key=MdoPackageText(I,"id",64),S=MdoPackageText(Supplied,Key,4096);if(McpConnectionUses(xrtValueObjectGet(Selected,XRT_STR_LITERAL("config")),Key)&&MdoMcpTemplateFlag(I,"secret",false)&&(MdoMcpTemplateFlag(I,"required",true)||MdoPackageText(Values,Key,256)[0])&&(!S||!*S)){snprintf(Error,sizeof(Error),"Target changed; re-enter credentials before saving");goto done;}}}
    if(!Preview&&!strcmp(Type,"stdio")&&!MdoMcpTemplateFlag(Body.Value,"confirm_launch",false)){snprintf(Error,sizeof(Error),"Inspect the complete launch command and confirm local code trust");goto done;}
#if defined(__ANDROID__)
    if(!strcmp(Type,"stdio")){cstr Program=MdoPackageText(Transport,"program",4096);if(Program&&strstr(Program,"data/ecosystem")){snprintf(Error,sizeof(Error),"Android native tools must be preinstalled by the full APK");goto done;}}
#endif
    Binding=xrtValueObject();bool Ok=Binding&&xrtValueObjectSet(Binding,XRT_STR_LITERAL("template"),T)&&xrtValueObjectSet(Binding,XRT_STR_LITERAL("receipt"),Receipt)&&MdoApiValueSetString(Binding,"variant",Variant)&&xrtValueObjectSet(Binding,XRT_STR_LITERAL("values"),Values)&&xrtValueObjectSet(Doc,XRT_STR_LITERAL("mdo_template"),Binding);
    Text=Ok?xrtJsonStringify(Doc,true,NULL):NULL;
    char* Validation=NULL;if(Preview&&Doc){xvalue* Check=xrtValueClone(Doc);xvalue* Tr=(xvalue*)xrtValueObjectGet(Check,XRT_STR_LITERAL("transport")),*O=(xvalue*)xrtValueObjectGet(Check,XRT_STR_LITERAL("oauth"));if(xrtValueObjectHas(Tr,XRT_STR_LITERAL("headers")))xrtValueObjectSetNew(Tr,XRT_STR_LITERAL("headers"),xrtValueArray());if(xrtValueObjectHas(Tr,XRT_STR_LITERAL("environment")))xrtValueObjectSetNew(Tr,XRT_STR_LITERAL("environment"),xrtValueArray());if(xrtValueType(O)==XVALUE_OBJECT)xrtValueObjectRemove(O,XRT_STR_LITERAL("client_secret_ref"));Validation=xrtJsonStringify(Check,false,NULL);xrtValueRelease(Check);}
    bool Valid=Text&&MdoMcpValidateText(Id,Validation?Validation:Text,Error,sizeof(Error));xrtFree(Validation);if(!Valid)goto done;
    if(Preview){xrtValueObjectRemove(Doc,XRT_STR_LITERAL("mdo_template"));for(size_t k=0;k<2;k++){xvalue* A=(xvalue*)xrtValueObjectGet(Transport,xrtStrView(k?"headers":"environment"));for(size_t i=0;i<xrtValueCount(A);i++)MdoApiValueSetString((xvalue*)xrtValueArrayGet(A,i),"secret_ref","[private credential]");}
        xvalue* O=(xvalue*)xrtValueObjectGet(Doc,XRT_STR_LITERAL("oauth"));if(xrtValueType(O)==XVALUE_OBJECT&&xrtValueObjectHas(O,XRT_STR_LITERAL("client_secret_ref")))MdoApiValueSetString(O,"client_secret_ref","[private credential]");
        xvalue* Out=xrtValueObject();MdoApiValueSetBool(Out,"target_changed",Changed);xrtValueObjectSet(Out,XRT_STR_LITERAL("config"),Doc);Result=MdoApiReplySuccessTake(C,200,Out,NULL);goto done;}
    if(Changed)(void)MdoMcpAuthorizationForget(RuntimeId); /* Revoke before publishing a new target. */
    Saved=Workspace?SkillWrite(Root,Path,Text,strlen(Text)):MdoHomeAtomicWrite(Path,Text,strlen(Text),false);
    if(Saved){Ok=Workspace?MdoMcpConfigureAgent(NULL,Workspace,NULL):MdoMcpManagerReload();if(!Ok){bool Restored=Old?(Workspace?SkillWrite(Root,Path,Old,strlen(Old)):MdoHomeAtomicWrite(Path,Old,strlen(Old),false)):(Workspace?xrtRootRemove(Root,Path):MdoHomeRemove(Path,false));if(!Restored)(void)MdoHomeRequireRestart("MCP connection rollback failed");Saved=false;snprintf(Error,sizeof(Error),"Runtime registration failed; previous connection restored");}
        else {if(Changed)(void)MdoMcpAuthorizationForget(RuntimeId);xvalue* Item=Workspace?McpProjectItem(Root,Workspace,Id):ExtensionItem("mcp",Id,true);Result=Item?MdoApiReplySuccessTake(C,200,Item,NULL):MdoApiReplyError(C,503,"mcp_result_unavailable","Saved; reread connection status before retrying",NULL);}}
done:
    if(Saved&&!Preview){McpConnectionReleaseCredentials(OldDoc);McpConnectionCleanTemplate(OldDoc);}
    if(!Saved)for(size_t i=0;i<RefCount;i++)MdoSecretDiscard(References[i]);
    if(!Result)Result=MdoApiReplyError(C,409,"mcp_connection_invalid",Error[0]?Error:"Connection could not be configured",NULL);
    if(Text){xrtSecureZero(Text,strlen(Text));xrtFree(Text);}xrtFree(Old);xrtValueRelease(OldDoc);xrtValueRelease(Receipt);xrtValueRelease(T);xrtValueRelease(Values);xrtValueRelease(Doc);xrtValueRelease(Binding);
    if(Lock)xrtClose(Lock);
    if(Root)xrtRootClose(Root);
    xrtMutexUnlock(g_MdoExtensionLock);if(Lease)MdoProjectLeaseRelease(Lease);xrtFree(Workspace);ExtensionBodyUnit(&Body);return Result;
}
