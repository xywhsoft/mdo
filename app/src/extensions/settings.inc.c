#include "../../include/mdo/extension_settings.h"
#include "../../include/mdo/extension_files.h"
#include "../../include/mdo/extension_settings_form.h"
#include "../../include/mdo/secrets.h"

typedef struct MdoEsDocument {xvalue *Row,*Form,*Stored;char Path[120],Schema[65],Revision[65];} MdoEsDocument;
static void MdoEsDocumentUnit(MdoEsDocument* D){xrtValueRelease(D->Row);xrtValueRelease(D->Form);xrtValueRelease(D->Stored);memset(D,0,sizeof(*D));}
static bool MdoEsPut(xvalue* V,cstr K,const xvalue* X){return X&&xrtValueObjectSet(V,xrtStrView(K),X);}
static bool MdoEsLoad(cstr Kind,cstr Directory,MdoEsDocument* D,char Error[512]){
 memset(D,0,sizeof(*D));xvalue* State=MdoExtReadJson(MDO_EXT_STATE,MDO_EXT_STATE_LIMIT,true);
 D->Row=State&&MdoExtCanonical(Kind,Directory)?MdoExtRead(Kind,Directory,State,Error):NULL;xrtValueRelease(State);
 const xvalue* Spec=MdoEsGet(D->Row,"settings");cstr Form=MdoEsText(Spec,"form"),Ref=MdoEsText(D->Row,"id"),Root=MdoEsText(D->Row,"root");
 if(!Form||!Ref||!Root)return MdoExtError(Error,"extension_settings_disabled: This extension does not declare a settings form");
 char* P=xrtFormat("%s/%s",Root,Form);D->Form=P?MdoExtReadJson(P,MDO_ES_FORM_BYTES,false):NULL;xrtFree(P);
 if(!D->Form||!MdoExtensionSettingsFormValid(D->Form,Error,512))return MdoExtError(Error,"extension_form_invalid: The settings form is unavailable or invalid");
 char Hash[65];if(!MdoExtensionHash(Ref,Hash))return false;snprintf(D->Path,sizeof(D->Path),"config/extensions/%s.json",Hash);
 D->Stored=MdoExtReadJson(D->Path,MDO_ES_VALUE_BYTES,true);
 if(!D->Stored||xrtValueType(D->Stored)!=XVALUE_OBJECT)return MdoExtError(Error,"extension_settings_unreadable: The saved settings were preserved");
 if(xrtValueCount(D->Stored)){
  uint64 Version,Revision;const xvalue* Values=MdoEsGet(D->Stored,"values");
  if(!MdoEsText(D->Stored,"extension_id")||strcmp(Ref,MdoEsText(D->Stored,"extension_id"))||!MdoEsUInt(MdoEsGet(D->Stored,"config_version"),&Version)||!MdoEsUInt(MdoEsGet(D->Stored,"revision"),&Revision)||xrtValueType(Values)!=XVALUE_OBJECT)return MdoExtError(Error,"extension_settings_unreadable: Invalid private settings identity or version");
  uint64 Current=0;MdoEsUInt(MdoEsGet(D->Form,"config_version"),&Current);if(Version>Current)return MdoExtError(Error,"extension_settings_newer: Saved settings require a newer extension version");
 }
 char* Schema=MdoExtJson(D->Form,MDO_ES_FORM_BYTES),*Stored=MdoExtJson(D->Stored,MDO_ES_VALUE_BYTES);
 char* Both=Schema&&Stored?xrtFormat("%s\n%s",Schema,Stored):NULL;
 bool Ok=Schema&&Both&&MdoExtensionHash(Schema,D->Schema)&&MdoExtensionHash(Both,D->Revision);xrtFree(Schema);xrtFree(Stored);xrtFree(Both);return Ok;
}
static xvalue* MdoEsEffective(const xvalue* Form,const xvalue* Stored){
 xvalue* Values=xrtValueObject();const xvalue* Saved=MdoEsGet(Stored,"values"),*Groups=MdoEsGet(Form,"groups");bool Ok=Values!=NULL;
 for(size_t I=0;Ok&&I<xrtValueCount(Groups);I++){const xvalue* Fields=MdoEsGet(xrtValueArrayGet(Groups,I),"fields");for(size_t J=0;Ok&&J<xrtValueCount(Fields);J++){
  const xvalue* F=xrtValueArrayGet(Fields,J);cstr K=MdoEsText(F,"name");const xvalue* V=MdoEsGet(Saved,K);if(!V&&!MdoEsSecret(F))V=MdoEsGet(F,"default");if(V)Ok=MdoEsPut(Values,K,V);
 }}if(!Ok){xrtValueRelease(Values);return NULL;}return Values;
}
static bool MdoEsIssue(xvalue* Issues,cstr Field,cstr Code){xvalue* V=xrtValueObject();bool Ok=V&&MdoExtPut(V,"field",Field)&&MdoExtPut(V,"code",Code)&&xrtValueArrayAppend(Issues,V);xrtValueRelease(V);return Ok;}
static xvalue* MdoEsIssues(const xvalue* Form,const xvalue* Values){
 xvalue* Out=xrtValueArray();bool Ok=Out!=NULL;const xvalue* Groups=MdoEsGet(Form,"groups");
 for(size_t I=0;Ok&&I<xrtValueCount(Groups);I++){const xvalue* Fields=MdoEsGet(xrtValueArrayGet(Groups,I),"fields");for(size_t J=0;Ok&&J<xrtValueCount(Fields);J++){
  const xvalue* F=xrtValueArrayGet(Fields,J);if(!MdoEsVisible(F,Values))continue;cstr Name=MdoEsText(F,"name");
  const xvalue* V=MdoEsGet(Values,Name);cstr Why=NULL;
  if(MdoEsSecret(F)){cstr Ref=MdoEsText(V,"secret_ref");if(!Ref&&MdoEsBool(F,"required"))Why="required";else if(V&&(!Ref||!MdoSecretReferenceSyntaxValid(xrtStrView(Ref))))Why="credential";
  }else Why=MdoEsValueError(F,V);
  if(Why)Ok=MdoEsIssue(Out,Name,Why);
 }}if(!Ok){xrtValueRelease(Out);return NULL;}return Out;
}
static xvalue* MdoEsReadResult(const MdoEsDocument* D,const xvalue* Stored){
 xvalue *Values=MdoEsEffective(D->Form,Stored),*Public=xrtValueObject(),*Secrets=xrtValueObject(),*Issues=Values?MdoEsIssues(D->Form,Values):NULL,*Out=xrtValueObject();bool Ok=Values&&Public&&Secrets&&Issues&&Out;
 for(size_t I=0;Ok&&I<xrtValueCount(Values);I++){xstrview K;const xvalue* V=xrtValueObjectAt(Values,I,&K),*F=MdoEsFind(D->Form,K.Data);if(MdoEsSecret(F))Ok=xrtValueObjectSetNew(Secrets,K,xrtValueBool(MdoEsText(V,"secret_ref")!=NULL));else Ok=xrtValueObjectSet(Public,K,V);}
 uint64 Revision=0;MdoEsUInt(MdoEsGet(Stored,"revision"),&Revision);
 Ok=Ok&&MdoExtPut(Out,"extension_id",MdoEsText(D->Row,"id"))&&MdoExtPut(Out,"revision",D->Revision)&&MdoExtPut(Out,"schema_digest",D->Schema)&&
  MdoEsPut(Out,"form",D->Form)&&MdoEsPut(Out,"values",Public)&&MdoEsPut(Out,"secrets",Secrets)&&MdoEsPut(Out,"issues",Issues)&&
  xrtValueObjectSetNew(Out,XRT_STR_LITERAL("config_revision"),xrtValueUInt(Revision))&&xrtValueObjectSetNew(Out,XRT_STR_LITERAL("valid"),xrtValueBool(!xrtValueCount(Issues)));
 xrtValueRelease(Values);xrtValueRelease(Public);xrtValueRelease(Secrets);xrtValueRelease(Issues);if(!Ok){xrtValueRelease(Out);return NULL;}return Out;
}
static bool MdoEsTemplatePreview(const MdoEsDocument* D,const xvalue* Stored,xvalue* Result){
 xvalue *Values=MdoEsEffective(D->Form,Stored),*Prompt=xrtValueObject(),*Meta=xrtValueObject(),*Rows=xrtValueArray(),*Snapshot=xrtValueObject();
 const xvalue* Templates=MdoEsGet(MdoEsGet(D->Row,"settings"),"templates");cstr Ref=MdoEsText(D->Row,"id"),Root=MdoEsText(D->Row,"root");
 bool Ok=Values&&Prompt&&Meta&&Rows&&Snapshot;
 for(size_t I=0;Ok&&I<xrtValueCount(Values);I++){xstrview K={0};const xvalue* V=xrtValueObjectAt(Values,I,&K),*F=MdoEsFind(D->Form,K.Data);if(!MdoEsSecret(F)&&MdoEsBool(F,"expose_to_prompt"))Ok=xrtValueObjectSet(Prompt,K,V);}
 Ok=Ok&&MdoExtPut(Meta,"id",Ref)&&MdoEsPut(Meta,"prompt",Prompt)&&(!Templates||MdoEsPut(Meta,"templates",Templates))&&MdoEsPut(Snapshot,"$mdo",Meta);
 size_t Total=0;
 for(size_t I=0;Ok&&I<xrtValueCount(Templates);I++){
  xstrview File={0};xrtValueGetString(xrtValueArrayGet(Templates,I),&File);char Why[512]={0};char* Path=xrtFormat("%s/%s",Root,File.Data);
  char* Source=Path?MdoExtensionRead(Path,true,128u*1024u,NULL):NULL;
  char* Rendered=Source?MdoExtensionSettingsRender(Ref,File.Data,Source,Snapshot,Why):NULL;
  if(Rendered&&strlen(Rendered)>256u*1024u-Total){xrtFree(Rendered);Rendered=NULL;}
  xvalue* Row=xrtValueObject();Ok=Row&&MdoExtPut(Row,"file",File.Data)&&MdoExtPut(Row,Rendered?"text":"error",Rendered?Rendered:"extension_template_invalid")&&xrtValueArrayAppend(Rows,Row);
  if(Rendered)Total+=strlen(Rendered);xrtValueRelease(Row);xrtFree(Rendered);xrtFree(Source);xrtFree(Path);
 }
 Ok=Ok&&MdoEsPut(Result,"templates",Rows);xrtValueRelease(Values);xrtValueRelease(Prompt);xrtValueRelease(Meta);xrtValueRelease(Rows);xrtValueRelease(Snapshot);return Ok;
}
xvalue* MdoExtensionSettingsRead(cstr Kind,cstr Directory,char Error[512]){
 if(Error)Error[0]=0;if(!MdoExtLock)return NULL;xrtMutexLock(MdoExtLock);MdoEsDocument D;bool Ok=MdoEsLoad(Kind,Directory,&D,Error);xvalue* Out=Ok?MdoEsReadResult(&D,D.Stored):NULL;MdoEsDocumentUnit(&D);xrtMutexUnlock(MdoExtLock);return Out;
}
bool MdoExtensionSettingsSave(cstr Kind,cstr Directory,const xvalue* Input,bool Preview,xvalue** Result,char Error[512]){
 *Result=NULL;if(Error)Error[0]=0;if(!MdoExtLock||!MdoEsKeys(Input,"|revision||schema_digest||values||secrets||reset||clear_secrets|"))return MdoExtError(Error,"extension_settings_invalid: Invalid settings mutation");
 const xvalue* InputValues=MdoEsGet(Input,"values"),*Secrets=MdoEsGet(Input,"secrets");bool Flag;
 if((InputValues&&xrtValueType(InputValues)!=XVALUE_OBJECT)||(Secrets&&xrtValueType(Secrets)!=XVALUE_OBJECT)||(MdoEsGet(Input,"reset")&&!xrtValueGetBool(MdoEsGet(Input,"reset"),&Flag))||(MdoEsGet(Input,"clear_secrets")&&!xrtValueGetBool(MdoEsGet(Input,"clear_secrets"),&Flag)))return MdoExtError(Error,"extension_settings_invalid: Use object values and explicit boolean reset options");
 xrtMutexLock(MdoExtLock);MdoEsDocument D;bool Ok=MdoEsLoad(Kind,Directory,&D,Error);xvalue *Next=Ok?xrtValueClone(D.Stored):NULL,*Saved=Next&&MdoEsGet(Next,"values")?xrtValueObjectEdit(Next,XRT_STR_LITERAL("values")):NULL,*Created=xrtValueArray(),*Retired=xrtValueArray();
 cstr Revision=MdoEsText(Input,"revision"),Schema=MdoEsText(Input,"schema_digest");
 if(Ok&&(!Revision||strcmp(Revision,D.Revision)||!Schema||strcmp(Schema,D.Schema))){Ok=false;MdoExtError(Error,"extension_settings_conflict: The form or values changed; your edits were not saved");}
 if(Ok&&!Saved){Saved=xrtValueObject();Ok=Saved&&xrtValueObjectSetNew(Next,XRT_STR_LITERAL("values"),Saved);}
 Ok=Ok&&Next&&Saved&&Created&&Retired;
 for(size_t I=0;Ok&&I<xrtValueCount(InputValues);I++){xstrview K;const xvalue* V=xrtValueObjectAt(InputValues,I,&K),*F=MdoEsFind(D.Form,K.Data);if(!F||MdoEsSecret(F)){Ok=false;break;}Ok=xrtValueObjectSet(Saved,K,V);}
 if(Ok&&MdoEsBool(Input,"reset")){for(size_t I=xrtValueCount(Saved);I>0;I--){xstrview K;xrtValueObjectAt(Saved,I-1,&K);const xvalue* F=MdoEsFind(D.Form,K.Data);if(!MdoEsSecret(F))xrtValueObjectRemove(Saved,K);}}
 for(size_t I=0;Ok&&I<xrtValueCount(Secrets);I++){xstrview K;const xvalue* V=xrtValueObjectAt(Secrets,I,&K),*F=MdoEsFind(D.Form,K.Data);xstrview Text={0};if(!F||!MdoEsSecret(F)||(xrtValueType(V)!=XVALUE_NULL&&(!xrtValueGetString(V,&Text)||!Text.Size||Text.Size>4096||memchr(Text.Data,0,Text.Size)||!xrtUtf8Valid(Text,NULL)))){Ok=false;break;}}
 /* Allocate immutable credentials before publishing; failed transactions only
  * discard the new references, never the currently committed credential. */
 const xvalue* Groups=MdoEsGet(D.Form,"groups");
 for(size_t I=0;Ok&&I<xrtValueCount(Groups);I++){const xvalue* Fields=MdoEsGet(xrtValueArrayGet(Groups,I),"fields");for(size_t J=0;Ok&&J<xrtValueCount(Fields);J++){
  const xvalue* F=xrtValueArrayGet(Fields,J);if(!MdoEsSecret(F))continue;cstr K=MdoEsText(F,"name");const xvalue* V=MdoEsGet(Secrets,K);bool Clear=MdoEsBool(Input,"clear_secrets")||(V&&xrtValueType(V)==XVALUE_NULL);if(!Clear&&!V)continue;
  cstr Old=MdoEsText(MdoEsGet(Saved,K),"secret_ref");if(Old)Ok=xrtValueArrayAppendNew(Retired,xrtValueString(xrtStrView(Old)));
  if(Clear)xrtValueObjectRemove(Saved,xrtStrView(K));else{
   xstrview Text={0};char Ref[MDO_SECRET_VAULT_REFERENCE_CAPACITY];xrtValueGetString(V,&Text);
   if(Preview)strcpy(Ref,"env:MDO_EXTENSION_PREVIEW_ONLY");else Ok=Ok&&MdoSecretStore(Text.Data,Ref);
   if(Ok&&!Preview)Ok=xrtValueArrayAppendNew(Created,xrtValueString(xrtStrView(Ref)));
   xvalue* Value=xrtValueObject();Ok=Ok&&Value&&MdoExtPut(Value,"secret_ref",Ref)&&MdoEsPut(Saved,K,Value);xrtValueRelease(Value);
  }
 }}
 uint64 Number=0;MdoEsUInt(MdoEsGet(D.Stored,"revision"),&Number);
 Ok=Ok&&Number<UINT64_MAX&&MdoExtPut(Next,"extension_id",MdoEsText(D.Row,"id"))&&MdoEsPut(Next,"config_version",MdoEsGet(D.Form,"config_version"))&&xrtValueObjectSetNew(Next,XRT_STR_LITERAL("revision"),xrtValueUInt(Number+1));
 xvalue* Response=Ok?MdoEsReadResult(&D,Next):NULL;bool Valid=Response&&MdoEsBool(Response,"valid");
 bool Reset=MdoEsBool(Input,"reset");bool Committed=false;
 if(Ok&&Response&&(Preview||Valid||Reset)){
  if(Preview){if(MdoEsTemplatePreview(&D,Next,Response))*Result=Response;else{Ok=false;xrtValueRelease(Response);}}else{
   Ok=MdoExtEnsure("config/extensions")&&MdoExtSaveJson(D.Path,Next,MDO_ES_VALUE_BYTES);Committed=Ok;
   if(Ok){MdoEsDocument Fresh;if(MdoEsLoad(Kind,Directory,&Fresh,Error))*Result=MdoEsReadResult(&Fresh,Fresh.Stored);MdoEsDocumentUnit(&Fresh);}
   xrtValueRelease(Response);
  }
 }else{if(Response)*Result=Response;Ok=false;if(Error&&!Error[0])MdoExtError(Error,Response?"extension_settings_incomplete: Complete the highlighted settings":"extension_settings_invalid: Invalid field name, value or credential");}
 const xvalue* Discard=Committed?Retired:Created;for(size_t I=0;I<xrtValueCount(Discard);I++){xstrview Ref={0};if(xrtValueGetString(xrtValueArrayGet(Discard,I),&Ref)&&!strncmp(Ref.Data,"vault:",6))MdoSecretDiscard(Ref.Data);}
 xrtValueRelease(Created);xrtValueRelease(Retired);xrtValueRelease(Next);MdoEsDocumentUnit(&D);xrtMutexUnlock(MdoExtLock);
 return Ok&&*Result;
}
xvalue* MdoExtensionSettingsSnapshot(cstr Ref,bool Secrets,char Error[512]){
 if(Error)Error[0]=0;xvalue* Row=MdoExtensionsResolve(Ref,Error);cstr Kind=MdoEsText(Row,"kind"),Directory=MdoEsText(Row,"directory_id");
 if(!Row)return NULL;if(!MdoEsText(MdoEsGet(Row,"settings"),"form")){xrtValueRelease(Row);return xrtValueObject();}
 xrtMutexLock(MdoExtLock);MdoEsDocument D;bool Ok=MdoEsLoad(Kind,Directory,&D,Error);xvalue* Values=Ok?MdoEsEffective(D.Form,D.Stored):NULL,*Issues=Values?MdoEsIssues(D.Form,Values):NULL,*Prompt=xrtValueObject(),*Meta=xrtValueObject();Ok=Ok&&Values&&Issues&&!xrtValueCount(Issues)&&Prompt&&Meta;
 for(size_t I=0;Ok&&I<xrtValueCount(Values);I++){xstrview K;const xvalue* V=xrtValueObjectAt(Values,I,&K),*F=MdoEsFind(D.Form,K.Data);
  if(!MdoEsSecret(F)&&MdoEsBool(F,"expose_to_prompt"))Ok=xrtValueObjectSet(Prompt,K,V);
 }
 if(Ok){const xvalue* Groups=MdoEsGet(D.Form,"groups");for(size_t I=0;Ok&&I<xrtValueCount(Groups);I++){const xvalue* Fields=MdoEsGet(xrtValueArrayGet(Groups,I),"fields");for(size_t J=0;Ok&&J<xrtValueCount(Fields);J++){
  const xvalue* F=xrtValueArrayGet(Fields,J);if(!MdoEsSecret(F))continue;cstr K=MdoEsText(F,"name"),Reference=MdoEsText(MdoEsGet(Values,K),"secret_ref");
  if(Secrets&&Reference){char* Plain=NULL;Ok=MdoSecretResolve(xrtStrView(Reference),4096,&Plain)&&MdoExtPut(Values,K,Plain);MdoSecretRelease(&Plain);}else xrtValueObjectRemove(Values,xrtStrView(K));
 }}}
 const xvalue* Templates=MdoEsGet(MdoEsGet(D.Row,"settings"),"templates");
 const xvalue* Environment=MdoEsGet(MdoEsGet(D.Row,"settings"),"environment");
 Ok=Ok&&MdoExtPut(Meta,"id",Ref)&&MdoExtPut(Meta,"revision",D.Revision)&&MdoEsPut(Meta,"prompt",Prompt)&&(!Templates||MdoEsPut(Meta,"templates",Templates))&&MdoEsPut(Values,"$mdo",Meta);
 if(Ok&&Environment)Ok=MdoEsPut(Meta,"environment",Environment);
 if(!Ok){xrtValueRelease(Values);Values=NULL;if(Error&&!Error[0])MdoExtError(Error,"extension_settings_required: Open this extension's configuration to complete or repair its settings");}
 xrtValueRelease(Prompt);xrtValueRelease(Meta);xrtValueRelease(Issues);MdoEsDocumentUnit(&D);xrtMutexUnlock(MdoExtLock);xrtValueRelease(Row);return Values;
}
xvalue* MdoExtensionSettingsAt(cstr Root,bool Secrets,char Error[512]){
 if(Error)Error[0]=0;if(!Root||!MdoExtensionRelativePath(Root))return xrtValueObject();char* Path=xrtFormat("%s/extension.json",Root);xvalue* Meta=Path?MdoExtReadJson(Path,MDO_EXTENSION_MANIFEST_BYTES,false):NULL;xrtFree(Path);
 cstr Ref=MdoEsText(Meta,"id");xvalue* Result=NULL;
 if(!Meta)return xrtValueObject();
 if(!MdoExtensionRefValid(Ref)){MdoExtError(Error,"Invalid extension settings owner");goto done;}
 xvalue* Row=MdoExtensionsResolve(Ref,Error);cstr Installed=MdoEsText(Row,"root");if(Installed&&!strcmp(Root,Installed))Result=MdoExtensionSettingsSnapshot(Ref,Secrets,Error);xrtValueRelease(Row);
done:xrtValueRelease(Meta);return Result;
}
bool MdoExtensionSettingsForget(cstr Kind,cstr Directory,char Error[512]){
 xvalue* Before=MdoExtensionSettingsRead(Kind,Directory,Error),*Input=xrtValueObject(),*Out=NULL;bool Ok=Before&&Input&&MdoEsPut(Input,"revision",MdoEsGet(Before,"revision"))&&MdoEsPut(Input,"schema_digest",MdoEsGet(Before,"schema_digest"))&&xrtValueObjectSetNew(Input,XRT_STR_LITERAL("reset"),xrtValueBool(true))&&xrtValueObjectSetNew(Input,XRT_STR_LITERAL("clear_secrets"),xrtValueBool(true))&&MdoExtensionSettingsSave(Kind,Directory,Input,false,&Out,Error);xrtValueRelease(Before);xrtValueRelease(Input);xrtValueRelease(Out);return Ok;
}

char* MdoExtensionSettingsRender(cstr Ref,cstr File,cstr Text,const xvalue* Snapshot,char Error[512]){
 if(Error)Error[0]=0;if(!Text)return NULL;
 xvalue* Owned=Snapshot?NULL:MdoExtensionSettingsSnapshot(Ref,false,Error);const xvalue* Settings=Snapshot?Snapshot:Owned,*Meta=MdoEsGet(Settings,"$mdo"),*Templates=MdoEsGet(Meta,"templates");
 if(!Settings)return NULL;bool Selected=false;
 for(size_t I=0;File&&I<xrtValueCount(Templates);I++){xstrview P={0};if(xrtValueGetString(xrtValueArrayGet(Templates,I),&P)&&!strcmp(P.Data,File))Selected=true;}
 if(!Selected){xrtValueRelease(Owned);return xrtStrDup(Text);}
 if(!Ref||!MdoEsText(Meta,"id")||strcmp(Ref,MdoEsText(Meta,"id"))){xrtValueRelease(Owned);MdoExtError(Error,"Settings template identity does not match its extension");return NULL;}
 xtemplateconfig Config;xrtTemplateConfigInit(&Config);Config.Open=XRT_STR_LITERAL("{{");Config.Close=XRT_STR_LITERAL("}}");Config.MaxSourceBytes=128u*1024u;Config.MaxNodes=4096;Config.MaxBlockDepth=16;
 xtemplate* Template=xrtTemplateCompileConfig(xrtStrView(Text),&Config);xvalue* Root=xrtValueObject();bool Ok=Template&&Root&&MdoEsPut(Root,"settings",MdoEsGet(Meta,"prompt"));
 xtemplaterenderconfig Render;xrtTemplateRenderConfigInit(&Render);Render.Root=Root;Render.Current=Root;Render.Flags=XTEMPLATE_STRICT_UNDEFINED;Render.MaxOutputBytes=128u*1024u;Render.MaxSteps=65536;Render.MaxLoopIterations=1024;Render.MaxDepth=32;
 xstrbuf Buffer;xrtStrBufInit(&Buffer);if(Ok)Ok=xrtTemplateRenderTo(Template,&Render,&Buffer);char* Output=Ok?xrtStrBufTake(&Buffer):NULL;
 xrtStrBufFree(&Buffer);xrtTemplateRelease(Template);xrtValueRelease(Root);xrtValueRelease(Owned);
 if(!Output)MdoExtError(Error,"extension_template_invalid: An exposed setting is missing, or the template is invalid or exceeds its limits");return Output;
}
xvalue* MdoExtensionSettingsEnvironment(const xvalue* Snapshot,char Error[512]){
 if(Error)Error[0]=0;xvalue* Env=xrtValueObject();const xvalue* Bindings=MdoEsGet(MdoEsGet(Snapshot,"$mdo"),"environment");bool Ok=Env!=NULL;
 for(size_t I=0;Ok&&I<xrtValueCount(Bindings);I++){xstrview K={0},Field={0};const xvalue* Binding=xrtValueObjectAt(Bindings,I,&K);Ok=xrtValueGetString(Binding,&Field);const xvalue* V=Ok?MdoEsGet(Snapshot,Field.Data):NULL;xstrview Text={0};
  if(V&&xrtValueGetString(V,&Text))Ok=xrtValueObjectSet(Env,K,V);else if(V&&(xrtValueType(V)==XVALUE_INT||xrtValueType(V)==XVALUE_UINT||xrtValueType(V)==XVALUE_FLOAT||xrtValueType(V)==XVALUE_BOOL)){char* S=xrtJsonStringify(V,false,NULL);Ok=S&&MdoExtPut(Env,K.Data,S);xrtFree(S);}else Ok=false;
 }if(!Ok){xrtValueRelease(Env);MdoExtError(Error,"extension_settings_environment: A bound setting is missing or is not a scalar value");return NULL;}return Env;
}

char* MdoExtensionSettingsText(cstr Root,cstr File,cstr Text,char Error[512]){
 xvalue* S=MdoExtensionSettingsAt(Root,false,Error);cstr Ref=MdoEsText(MdoEsGet(S,"$mdo"),"id");
 char* Out=S?(Ref?MdoExtensionSettingsRender(Ref,File,Text,S,Error):xrtStrDup(Text)):NULL;xrtValueRelease(S);return Out;
}
