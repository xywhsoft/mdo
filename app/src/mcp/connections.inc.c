static bool McpPrivateError(char Error[512],cstr Message){if(Error)snprintf(Error,512,"%s",Message);return false;}
static xvalue* McpPrivateLoad(cstr Id,char** Raw,bool* Missing){
 *Raw=NULL;*Missing=false;if(!MdoMcpIdValid(Id))return NULL;char* Path=xrtFormat(MDO_MCP_CONNECTIONS "/%s.json",Id);
 *Raw=Path?MdoExtensionRead(Path,true,MDO_MCP_FILE_LIMIT,Missing):NULL;xrtFree(Path);return *Raw?xrtJsonParse(xrtStrView(*Raw)):NULL;
}
static void McpPrivateTextUnit(char* Text){if(Text){xrtSecureZero(Text,strlen(Text));xrtFree(Text);}}
static xvalue* McpPrivatePublic(const xvalue* Doc,cstr Raw){
 xvalue* Out=xrtValueClone(Doc),*Values=xrtValueClone(xrtValueObjectGet(Doc,XRT_STR_LITERAL("values"))),*Secrets=xrtValueObject();cstr Directory=MdoExtensionText(Doc,"extension_id",96);char Why[512];
 xvalue* Meta=MdoExtensionsRead("mcp",Directory,Why);char* Entry=Meta?MdoExtensionsEntryPath("mcp",Directory,Why):NULL,*Text=Entry?MdoExtensionRead(Entry,true,MDO_MCP_FILE_LIMIT,NULL):NULL;
 xvalue* Template=Text?xrtJsonParse(xrtStrView(Text)):NULL;const xvalue* Inputs=xrtValueObjectGet(Template,XRT_STR_LITERAL("inputs"));bool Ok=Out&&Values&&Secrets;
 for(size_t I=0;Ok&&I<xrtValueCount(Inputs);I++){const xvalue* Input=xrtValueArrayGet(Inputs,I);cstr Id=MdoExtensionText(Input,"id",64);if(Id&&MdoMcpTemplateFlag(Input,"secret",false)){
  cstr Ref=MdoExtensionText(Values,Id,4096);Ok=xrtValueObjectSetNew(Secrets,xrtStrView(Id),xrtValueBool(Ref&&Ref[0]));xrtValueObjectRemove(Values,xrtStrView(Id));}}
 /* A deleted or invalid recipe cannot reveal stale credential references. */
 if(!Template){for(size_t I=xrtValueCount(Values);I>0;I--){xstrview Key={0};const xvalue* Child=xrtValueObjectAt(Values,I-1,&Key);xstrview Ref={0};if(xrtValueGetString(Child,&Ref)&&MdoSecretReferenceSyntaxValid(Ref))xrtValueObjectRemove(Values,Key);}}
 char Revision[65];Ok=Ok&&Raw&&MdoExtensionHash(Raw,Revision)&&McpDirectoryPut(Out,"revision",Revision)&&xrtValueObjectSet(Out,XRT_STR_LITERAL("values"),Values)&&xrtValueObjectSet(Out,XRT_STR_LITERAL("secrets_set"),Secrets);
 if(Ok&&Meta)Ok=xrtValueObjectSet(Out,XRT_STR_LITERAL("extension"),Meta);
 xrtValueRelease(Meta);xrtFree(Entry);xrtFree(Text);xrtValueRelease(Template);xrtValueRelease(Values);xrtValueRelease(Secrets);if(!Ok){xrtValueRelease(Out);return NULL;}return Out;
}
xvalue* MdoMcpConnections(cstr Id,char Error[512]){
 if(Error)Error[0]=0;if(Id){char* Raw=NULL;bool Missing=false;xvalue* Doc=McpPrivateLoad(Id,&Raw,&Missing),*Result=Doc?McpPrivatePublic(Doc,Raw):NULL;xrtValueRelease(Doc);McpPrivateTextUnit(Raw);if(!Result)McpPrivateError(Error,"MCP connection is unavailable");return Result;}
 xvalue* Out=xrtValueArray();xdir D=Out?MdoHomeOpenDirectory(MDO_MCP_CONNECTIONS,XDIR_STAT):NULL;bool Ok=Out!=NULL;
 if(!D){const xerror* E=xrtGetError();if(E&&xrtErrorKind(E)==XERR_NOT_FOUND)xrtClearError();else Ok=false;}
 xdirentry Entry;xdirnext Next=XDIR_NEXT_END;size_t Count=0;
 while(Ok&&D&&(Next=xrtDirNext(D,&Entry))==XDIR_NEXT_ITEM){if(++Count>4096){Ok=false;break;}if(Entry.Info.Type!=XFILE_TYPE_FILE||!(Entry.Flags&XDIR_ENTRY_UTF8)||Entry.Name.Size<6||Entry.Name.Size>101||memcmp(Entry.Name.Data+Entry.Name.Size-5,".json",5))continue;
  char Name[97];memcpy(Name,Entry.Name.Data,Entry.Name.Size-5);Name[Entry.Name.Size-5]=0;char* Raw=NULL;bool Missing=false;xvalue* Doc=McpPrivateLoad(Name,&Raw,&Missing),*Row=Doc?McpPrivatePublic(Doc,Raw):NULL;
  if(!Row){Row=xrtValueObject();Ok=Row&&McpDirectoryPut(Row,"id",Name)&&McpDirectoryPut(Row,"error","Private connection or installed template is invalid");}
  Ok=Ok&&Row&&xrtValueArrayAppend(Out,Row);xrtValueRelease(Row);xrtValueRelease(Doc);McpPrivateTextUnit(Raw);
 }if(Next==XDIR_NEXT_ERROR)Ok=false;if(D&&!xrtDirClose(D))Ok=false;if(!Ok){xrtValueRelease(Out);McpPrivateError(Error,"Cannot list private MCP connections");return NULL;}return Out;
}
static bool McpPrivateReload(char Error[512]){
 if(!g_MdoMcp.Initialized)return true;return MdoMcpManagerReload()||McpPrivateError(Error,"MCP runtime could not apply the private connection");
}
bool MdoMcpConnectionManage(const xvalue* Input,xcancel* Cancel,xvalue** Result,char Error[512]){
 *Result=NULL;if(Error)Error[0]=0;cstr Id=MdoExtensionText(Input,"id",96),Action=MdoExtensionText(Input,"action",16),Expected=MdoExtensionText(Input,"expected_revision",64);
 if(!MdoMcpTemplateKeys(Input,"|id||action||expected_revision||extension_id||variant||approval_digest||values||secrets||secret_refs||clear_secrets||enabled||name||project_id|"))return McpPrivateError(Error,"Unsupported connection field; recipes are loaded from the installed MCP directory");
 bool Flag=false;const xvalue* EnabledField=xrtValueObjectGet(Input,XRT_STR_LITERAL("enabled"));if(EnabledField&&!xrtValueGetBool(EnabledField,&Flag))return McpPrivateError(Error,"Connection activation must be a boolean value");
 bool Preview=Action&&!strcmp(Action,"preview"),Remove=Action&&!strcmp(Action,"remove"),Save=Action&&!strcmp(Action,"save");
 if(!MdoMcpIdValid(Id)||(!Preview&&!Remove&&!Save)||(!Preview&&(!Expected||(Expected[0]&&strlen(Expected)!=64))))return McpPrivateError(Error,"Choose a connection action and its current private revision (empty for new)");
 if(!MdoExtensionsMutationBegin())return McpPrivateError(Error,"Another extension operation is still running");
 char* OldRaw=NULL,*NewRaw=NULL;bool Missing=false,Committed=false;xvalue* Old=McpPrivateLoad(Id,&OldRaw,&Missing),*Doc=NULL,*Values=NULL,*Meta=NULL,*Template=NULL,*Rendered=NULL;
 char* Path=xrtFormat(MDO_MCP_CONNECTIONS "/%s.json",Id),*Entry=NULL,*Text=NULL,*Native=NULL;char Revision[65]={0},Digest[65]={0},NewRefs[32][MDO_SECRET_VAULT_REFERENCE_CAPACITY];size_t RefCount=0;
 bool Ok=Path&&(!OldRaw||Old)&&(!Cancel||!xrtCancelRequested(Cancel));
 if(!Missing&&!OldRaw)Ok=false;if(Ok&&!Preview)Ok=OldRaw?(Expected[0]&&MdoExtensionHash(OldRaw,Revision)&&!strcmp(Revision,Expected)):!Expected[0];
 if(!Ok){McpPrivateError(Error,"Connection changed or cannot be read; refresh it before saving");goto done;}
 if(Remove){Ok=Old&&MdoHomeRemove(Path,false);if(Ok&&!McpPrivateReload(Error)){if(!MdoHomeAtomicWrite(Path,OldRaw,strlen(OldRaw),false)||!McpPrivateReload(NULL))(void)MdoHomeRequireRestart("MCP connection removal rollback needs recovery");Ok=false;}
  if(Ok){(void)MdoMcpAuthorizationForget(Id);*Result=xrtValueObject();Committed=true;}goto done;}
 cstr Directory=MdoExtensionText(Input,"extension_id",96),Variant=MdoExtensionText(Input,"variant",64),Approval=MdoExtensionText(Input,"approval_digest",64);
 Meta=MdoExtensionsRead("mcp",Directory,Error);Entry=Meta?MdoExtensionsEntryPath("mcp",Directory,Error):NULL;Text=Entry?MdoExtensionRead(Entry,true,MDO_MCP_FILE_LIMIT,NULL):NULL;Template=Text?xrtJsonParse(xrtStrView(Text)):NULL;
 cstr Alias=MdoExtensionText(Meta,"alias",64),Root=MdoExtensionText(Meta,"root",4096);Native=Root?MdoHomeExternalPath(Root):NULL;
 Ok=Meta&&Template&&Native&&Variant&&MdoMcpTemplateValidate(Template,Alias,Error,512)&&MdoExtensionsVerify("mcp",Directory,Cancel,Error)&&MdoExtensionsFingerprint("mcp",Directory,Digest,Cancel,Error);
 if(!Ok)goto done;if(!Preview&&(!Approval||strcmp(Approval,Digest))){Ok=McpPrivateError(Error,"Preview the current program/target and confirm its directory digest before saving");goto done;}
 const xvalue* Inputs=xrtValueObjectGet(Template,XRT_STR_LITERAL("inputs")),*Supplied=xrtValueObjectGet(Input,XRT_STR_LITERAL("values")),*Secrets=xrtValueObjectGet(Input,XRT_STR_LITERAL("secrets")),*References=xrtValueObjectGet(Input,XRT_STR_LITERAL("secret_refs"));
 if((Supplied&&xrtValueType(Supplied)!=XVALUE_OBJECT)||(Secrets&&xrtValueType(Secrets)!=XVALUE_OBJECT)||(References&&xrtValueType(References)!=XVALUE_OBJECT)){Ok=McpPrivateError(Error,"Connection parameters and secret fields must be objects");goto done;}
 const xvalue* Sets[]={Supplied,Secrets,References};for(size_t S=0;Ok&&S<3;S++)for(size_t I=0;I<xrtValueCount(Sets[S]);I++){xstrview Key={0};xrtValueObjectAt(Sets[S],I,&Key);char* Name=xrtStrDupN(Key.Data,Key.Size);const xvalue* Definition=MdoMcpTemplateInput(Template,Name);bool Secret=MdoMcpTemplateFlag(Definition,"secret",false);Ok=Definition&&(S?Secret:!Secret);xrtFree(Name);}
 if(!Ok){McpPrivateError(Error,"Unknown connection parameter or a secret supplied as an ordinary value");goto done;}
 bool Same=Old&&MdoExtensionText(Old,"extension_id",96)&&!strcmp(Directory,MdoExtensionText(Old,"extension_id",96))&&MdoExtensionText(Old,"variant",64)&&!strcmp(Variant,MdoExtensionText(Old,"variant",64))&&MdoExtensionText(Old,"approval_digest",64)&&!strcmp(Digest,MdoExtensionText(Old,"approval_digest",64));
 Values=xrtValueObject();Ok=Values!=NULL;
 for(size_t I=0;Ok&&I<xrtValueCount(Inputs);I++){
  const xvalue* Definition=xrtValueArrayGet(Inputs,I);cstr Key=MdoExtensionText(Definition,"id",64);bool Secret=MdoMcpTemplateFlag(Definition,"secret",false);cstr Value=MdoExtensionText(Supplied,Key,4096);
  if(Secret){cstr Raw=MdoExtensionText(Secrets,Key,4096),Ref=MdoExtensionText(References,Key,4096);bool Clear=false;const xvalue* Clears=xrtValueObjectGet(Input,XRT_STR_LITERAL("clear_secrets"));for(size_t J=0;J<xrtValueCount(Clears);J++){xstrview K={0};if(xrtValueGetString(xrtValueArrayGet(Clears,J),&K)&&xrtStrEqual(K,xrtStrView(Key)))Clear=true;}
   Value=Clear?"":Ref?Ref:Same?MdoExtensionText(xrtValueObjectGet(Old,XRT_STR_LITERAL("values")),Key,4096):NULL;
   if(Raw&&Raw[0]){if(Preview)Value="env:MDO_PREVIEW_SECRET";else if(RefCount<32&&xsCredentialProtectionAvailable()&&MdoSecretStore(Raw,NewRefs[RefCount]))Value=NewRefs[RefCount++];else{Ok=McpPrivateError(Error,"System credential protection is unavailable; no plaintext fallback is used");break;}}
   if(Value&&Value[0]&&!MdoSecretReferenceSyntaxValid(xrtStrView(Value))){Ok=McpPrivateError(Error,"Invalid credential reference");break;}
   if(!Value&&Preview&&MdoMcpTemplateFlag(Definition,"required",true))Value="env:MDO_PREVIEW_SECRET";
  }else if(!Value&&Same)Value=MdoExtensionText(xrtValueObjectGet(Old,XRT_STR_LITERAL("values")),Key,4096);
  if(!Value)Value=MdoExtensionText(Definition,"default",4096);if(!Value)Value="";Ok=McpDirectoryPut(Values,Key,Value);
 }
 Doc=xrtValueObject();Ok=Ok&&Doc&&McpDirectoryPut(Doc,"id",Id)&&McpDirectoryPut(Doc,"extension_id",Directory)&&McpDirectoryPut(Doc,"variant",Variant)&&McpDirectoryPut(Doc,"approval_digest",Digest)&&xrtValueObjectSet(Doc,XRT_STR_LITERAL("values"),Values);
 bool Enabled=MdoMcpTemplateFlag(Input,"enabled",Old?MdoMcpTemplateFlag(Old,"enabled",false):false);if(Ok)Ok=xrtValueObjectSetNew(Doc,XRT_STR_LITERAL("enabled"),xrtValueBool(Enabled));
 const cstr Optional[]={"name","project_id"};for(size_t I=0;Ok&&I<2;I++){cstr S=MdoExtensionText(Input,Optional[I],I?64:256);if(S)Ok=McpDirectoryPut(Doc,Optional[I],S);}
 if(!Ok)goto done;
 MdoMcpSource Source={0};Source.Id=xrtStrDup(Id);char Why[MDO_MCP_ERROR_LIMIT]={0};char* Config=Source.Id?McpDirectoryResolveDocument(&Source,Doc,Why):NULL;MdoMcpSourceUnit(&Source);Rendered=Config?xrtJsonParse(xrtStrView(Config)):NULL;McpPrivateTextUnit(Config);
 if(!Rendered){Ok=McpPrivateError(Error,Why[0]?Why:"Invalid private connection parameters");goto done;}
 if(Preview){*Result=xrtValueObject();Ok=*Result&&McpDirectoryPut(*Result,"approval_digest",Digest)&&xrtValueObjectSet(*Result,XRT_STR_LITERAL("config"),Rendered)&&xrtValueObjectSetNew(*Result,XRT_STR_LITERAL("target_changed"),xrtValueBool(Old&&!Same));goto done;}
 NewRaw=xrtJsonStringify(Doc,false,NULL);Ok=NewRaw&&strlen(NewRaw)<=MDO_MCP_FILE_LIMIT&&(!Cancel||!xrtCancelRequested(Cancel))&&MdoHomeAtomicWrite(Path,NewRaw,strlen(NewRaw),false);
 if(Ok&&!Same)(void)MdoMcpAuthorizationForget(Id);
 if(Ok&&!McpPrivateReload(Error)){bool Restored=OldRaw?MdoHomeAtomicWrite(Path,OldRaw,strlen(OldRaw),false):MdoHomeRemove(Path,false);if(!Restored||!McpPrivateReload(NULL))(void)MdoHomeRequireRestart("Private MCP connection rollback needs recovery");Ok=false;}
 if(Ok){Committed=true;*Result=McpPrivatePublic(Doc,NewRaw);Ok=*Result!=NULL;}
done:
 if(!Committed)for(size_t I=0;I<RefCount;I++)MdoSecretDiscard(NewRefs[I]);
 McpPrivateTextUnit(OldRaw);McpPrivateTextUnit(NewRaw);xrtFree(Path);xrtFree(Entry);xrtFree(Text);xrtFree(Native);xrtValueRelease(Old);xrtValueRelease(Doc);xrtValueRelease(Values);xrtValueRelease(Meta);xrtValueRelease(Template);xrtValueRelease(Rendered);MdoExtensionsMutationEnd();
 if(!Ok&&Error&&!Error[0])McpPrivateError(Error,"MCP connection could not be configured");return Ok;
}
