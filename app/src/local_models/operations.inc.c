static bool LmPlan(const xvalue* Input,xcancel* Cancel,xvalue** Result,char E[512]){
 xvalue* Remote=NULL;if(!MdoLocalModelServiceRead(Input,Cancel,&Remote,E))return false;
 bool Ok=MdoLocalModelPlanValid(Remote,E)&&!strcmp(LmStr(LmGet(Remote,"recipe"),"version_id"),LmStr(Input,"version"))&&!strcmp(LmStr(LmGet(Remote,"profile"),"id"),LmStr(Input,"profile"));const xvalue* Runtime=LmGet(Remote,"runtime_plan");xvalue* Request=xrtValueObject(),*Native=NULL;
 Ok=Ok&&Request&&LmPut(Request,"action","install")&&LmPut(Request,"ref",LmStr(Runtime,"root"))&&MdoExtensionsPlan(Request,LmGet(Runtime,"items"),&Native,E);xrtValueRelease(Request);
 char* Id=Ok?xrtXidMakeString():NULL;xvalue* Plan=Ok?xrtValueObject():NULL;
 Ok=Ok&&Plan&&Id&&LmPut(Plan,"id",Id)&&xrtValueObjectSet(Plan,XRT_STR_LITERAL("model"),Remote)&&xrtValueObjectSet(Plan,XRT_STR_LITERAL("runtime_plan"),Native);
 char* Json=Ok?xrtJsonStringify(Plan,false,NULL):NULL;char Revision[65];Ok=Ok&&Json&&MdoExtensionHash(Json,Revision)&&LmPut(Plan,"revision",Revision);xrtFree(Json);
 char* Path=Id?xrtFormat("data/local-models/plans/%s.json",Id):NULL;Ok=Ok&&Path&&LmSaveJson(Path,Plan);
 if(Ok)*Result=Plan;else xrtValueRelease(Plan);xrtFree(Id);xrtFree(Path);xrtValueRelease(Native);xrtValueRelease(Remote);return Ok;
}
static bool LmDiscardPlan(const xvalue* Input,char E[512]){
 cstr Id=LmStr(Input,"plan_id");if(!LmXid(Id))return LmFail(E,"Select an inspected model plan");
 xrtMutexLock(g_Lm.Lock);const xvalue* Jobs=LmGet(g_Lm.State,"installations");bool Used=false;for(size_t I=0;I<xrtValueCount(Jobs);I++)if(!strcmp(LmStr(xrtValueObjectAt(Jobs,I,NULL),"install_plan_id"),Id))Used=true;xrtMutexUnlock(g_Lm.Lock);if(Used)return LmFail(E,"The plan belongs to an installed or pending model");
 char* Path=xrtFormat("data/local-models/plans/%s.json",Id);xvalue* Plan=Path?LmReadJson(Path):NULL;bool Ok=Plan&&!strcmp(LmStr(Plan,"revision"),LmStr(Input,"revision"));
 if(Ok){char Why[512]={0};const xvalue* Native=LmGet(Plan,"runtime_plan");MdoExtensionsPlanDiscard(LmStr(Native,"id"),LmStr(Native,"revision"),Why);Ok=MdoHomeRemove(Path,false);}
 xrtValueRelease(Plan);xrtFree(Path);return Ok||LmFail(E,"The model plan changed or could not be discarded");
}
bool MdoLocalModelsRead(const xvalue* Input,xcancel* Cancel,xvalue** Result,char E[512]){
 *Result=NULL;if(E)E[0]=0;cstr Action=LmStr(Input,"action");
 if(!strcmp(Action,"updates")){xvalue* J=LmJob(LmStr(Input,"id"));cstr Ref=LmStr(LmGet(LmGet(J,"plan"),"recipe"),"id");xvalue* Q=xrtValueObject();bool Ok=Ref[0]&&Q&&LmPut(Q,"action","updates")&&LmPut(Q,"refs",Ref)&&LmPut(Q,"kind","local-models")&&MdoExtensionServiceRead(Q,Cancel,Result,E);xrtValueRelease(Q);xrtValueRelease(J);return Ok;}
 if(!strcmp(Action,"catalog")||!strcmp(Action,"detail"))return MdoLocalModelServiceRead(Input,Cancel,Result,E);
 if(!strcmp(Action,"plan"))return !g_Lm.Fault&&LmPlan(Input,Cancel,Result,E);
 return LmFail(E,"Select a local model catalog, detail or install plan");
}
static bool LmQueue(const xvalue* Input,xvalue** Result,char E[512]){
 cstr Action=LmStr(Input,"action");bool Install=!strcmp(Action,"install");xvalue* Plan=NULL;
 if(Install){cstr Id=LmStr(Input,"plan_id");
  if(!LmXid(Id))return LmFail(E,"Inspect an install plan first");char* Path=xrtFormat("data/local-models/plans/%s.json",Id);Plan=Path?LmReadJson(Path):NULL;xrtFree(Path);
  bool Trust=false,Ready=false;if(!Plan||strcmp(LmStr(Plan,"revision"),LmStr(Input,"revision"))||!xrtValueGetBool(LmGet(Input,"trust_code"),&Trust)||!Trust||!xrtValueGetBool(LmGet(LmGet(Plan,"runtime_plan"),"ready"),&Ready)||!Ready||!MdoLocalModelPlanValid(LmGet(Plan,"model"),E)){xrtValueRelease(Plan);return LmFail(E,"Confirm the inspected runtime code and unchanged model plan");}
  xrtMutexLock(g_Lm.Lock);const xvalue* Existing=LmGet(g_Lm.State,"installations");for(size_t I=0;I<xrtValueCount(Existing);I++){const xvalue* J=xrtValueObjectAt(Existing,I,NULL);if(!strcmp(LmStr(J,"install_plan_id"),Id)){*Result=xrtValueClone(J);break;}}xrtMutexUnlock(g_Lm.Lock);if(*Result){xrtValueRelease(Plan);return true;}}
 xrtMutexLock(g_Lm.Lock);char Store[33];snprintf(Store,33,"%s",LmStr(g_Lm.State,"default_root"));size_t Count=xrtValueCount(LmGet(g_Lm.State,"installations"));xrtMutexUnlock(g_Lm.Lock);
 if(Count>=LM_LIMIT){xrtValueRelease(Plan);return LmFail(E,"Remove an unused model installation before adding another");}
 if(!Store[0]){xvalue* New=NULL;if(!LmNewStore(NULL,&New,E)){xrtValueRelease(Plan);return false;}snprintf(Store,33,"%s",LmStr(New,"id"));xrtValueRelease(New);}
 char* Id=xrtXidMakeString();xvalue* Job=xrtValueObject();bool Ok=Job&&Id&&LmPut(Job,"id",Id)&&LmPut(Job,"store",Store)&&LmPut(Job,"state","queued")&&LmPut(Job,"kind",Install?"download":!strcmp(LmStr(Input,"mode"),"reference")?"reference":"copy");
 if(Install){LmPut(Job,"install_plan_id",LmStr(Input,"plan_id"));const xvalue* Model=LmGet(Plan,"model"),*Recipe=LmGet(Model,"recipe");Ok=Ok&&LmPut(Job,"name",LmStr(Recipe,"name"))&&xrtValueObjectSet(Job,XRT_STR_LITERAL("plan"),Model)&&xrtValueObjectSet(Job,XRT_STR_LITERAL("runtime_plan"),(xvalue*)LmGet(Plan,"runtime_plan"))&&xrtValueObjectSet(Job,XRT_STR_LITERAL("assets"),(xvalue*)LmGet(Model,"assets"));}
 else {cstr Source=LmStr(Input,"path"),Name=LmStr(Input,"name");Ok=Ok&&Source[0]&&xrtPathIsAbs(Source)&&LmPut(Job,"source_path",Source)&&LmPut(Job,"name",Name[0]?Name:Source);}
 xrtMutexLock(g_Lm.Lock);xvalue* Before=xrtValueClone(g_Lm.State),*Jobs=xrtValueObjectEdit(g_Lm.State,XRT_STR_LITERAL("installations"));Ok=Ok&&Before&&Jobs&&xrtValueObjectSet(Jobs,xrtStrView(Id),Job);Ok=LmCommit(Before,Ok);xrtMutexUnlock(g_Lm.Lock);
 if(Ok)*Result=xrtValueClone(Job);else LmFail(E,"Cannot queue this model installation");xrtValueRelease(Job);xrtValueRelease(Plan);xrtFree(Id);return Ok;
}
static bool LmReferenced(cstr Exclude,cstr Store,cstr Hash){
 const xvalue* Jobs=LmGet(g_Lm.State,"installations");for(size_t I=0;I<xrtValueCount(Jobs);I++){xstrview Key={0};const xvalue* J=xrtValueObjectAt(Jobs,I,&Key);if(!strcmp(Key.Data,Exclude)||strcmp(LmStr(J,"store"),Store))continue;const xvalue* Assets=LmGet(J,"assets");for(size_t K=0;K<xrtValueCount(Assets);K++)if(!strcmp(LmStr(xrtValueArrayGet(Assets,K),"sha256"),Hash))return true;}return false;
}
static bool LmRemove(cstr Id,bool Files,xvalue** Result,char E[512]){
 if(!xrtMutexTryLock(g_Lm.Writer))return LmFail(E,"Finish or cancel the current model operation before uninstalling");
 xrtMutexLock(g_Lm.Lock);const xvalue* Ds=LmGet(g_Lm.State,"deployments");bool Used=false;for(size_t K=0;K<xrtValueCount(Ds);K++)if(!strcmp(LmStr(xrtValueObjectAt(Ds,K,NULL),"installation"),Id)||!strcmp(LmStr(LmGet(xrtValueObjectAt(Ds,K,NULL),"previous"),"installation"),Id))Used=true;xrtMutexUnlock(g_Lm.Lock);if(Used){xrtMutexUnlock(g_Lm.Writer);return LmFail(E,"Stop and remove this model's deployment configurations before uninstalling its weights");}
 xvalue* J=LmJob(Id);bool Ok=J&&LmJobState(Id,"removing","");
 xvalue* Store=J?LmStore(LmStr(J,"store")):NULL;char* Native=NULL;xroot Root=Files&&Store?LmRoot(Store,&Native,E):NULL;
 if(Ok&&Files&&strcmp(LmStr(J,"kind"),"reference")){
  Ok=Root!=NULL;const xvalue* Assets=LmGet(J,"assets");for(size_t I=0;Ok&&I<xrtValueCount(Assets);I++){cstr Hash=LmStr(xrtValueArrayGet(Assets,I),"sha256");if(!LmHex(Hash,64)){Ok=false;break;}xrtMutexLock(g_Lm.Lock);bool Shared=LmReferenced(Id,LmStr(J,"store"),Hash);xrtMutexUnlock(g_Lm.Lock);if(Shared)continue;
   char* P=xrtFormat("blobs/%s",Hash);xfileinfo Info;if(P&&xrtRootStat(Root,P,false,&Info))Ok=Info.Type==XFILE_TYPE_FILE&&xrtRootRemove(Root,P);else xrtClearError();xrtFree(P);
   const cstr Suffixes[]={".part",".part.resume.json"};for(size_t K=0;Ok&&K<2;K++){P=xrtFormat("partial/%s%s",Hash,Suffixes[K]);if(P&&xrtRootStat(Root,P,false,&Info))Ok=Info.Type==XFILE_TYPE_FILE&&xrtRootRemove(Root,P);else xrtClearError();xrtFree(P);}
  }
 }
 if(Ok){LmUnpin(Id);
  const xvalue* Recipe=LmGet(LmGet(J,"plan"),"recipe");bool Shared=false;xrtMutexLock(g_Lm.Lock);const xvalue* Jobs=LmGet(g_Lm.State,"installations");
  for(size_t I=0;Recipe&&I<xrtValueCount(Jobs);I++){xstrview Key={0};const xvalue* Other=xrtValueObjectAt(Jobs,I,&Key);if(strcmp(Key.Data,Id)&&!strcmp(LmStr(LmGet(LmGet(Other,"plan"),"recipe"),"id"),LmStr(Recipe,"id")))Shared=true;}xrtMutexUnlock(g_Lm.Lock);
  if(Recipe&&!Shared){char Why[512]={0};xvalue* Row=MdoExtensionsResolve(LmStr(Recipe,"id"),Why),*Out=NULL;if(Row)Ok=MdoExtensionsRemove("local-models",LmStr(Recipe,"xid"),&Out,E);xrtValueRelease(Row);xrtValueRelease(Out);}
 }
 if(Ok){xrtMutexLock(g_Lm.Lock);xvalue* Before=xrtValueClone(g_Lm.State),*Jobs=xrtValueObjectEdit(g_Lm.State,XRT_STR_LITERAL("installations"));Ok=Before&&Jobs&&xrtValueObjectRemove(Jobs,xrtStrView(Id));Ok=LmCommit(Before,Ok);xrtMutexUnlock(g_Lm.Lock);}
 if(!Ok&&J){char Why[512]={0};LmPins(Id,J,Why);}
 if(!Ok&&J)LmJobState(Id,"cleanup_pending",E[0]?E:"Cannot remove the model's owned resources; other resources were retained");
 if(Ok&&LmXid(LmStr(J,"install_plan_id"))){char* P=xrtFormat("data/local-models/plans/%s.json",LmStr(J,"install_plan_id"));if(P)MdoHomeRemove(P,false);xrtFree(P);}
 xrtRootClose(Root);xrtFree(Native);xrtValueRelease(Store);xrtValueRelease(J);xrtMutexUnlock(g_Lm.Writer);if(Ok)*Result=xrtValueObject();return Ok;
}
/* A cache sweep never follows links or removes referenced resources. */
static bool LmCache(const xvalue* Input,xvalue** Result,char E[512]){
 bool Delete=!strcmp(LmStr(Input,"action"),"prune_cache");if(!xrtMutexTryLock(g_Lm.Writer))return LmFail(E,"Finish the current model resource operation before clearing cache");
 char Id[33]={0};xrtMutexLock(g_Lm.Lock);snprintf(Id,33,"%s",LmStr(Input,"id")[0]?LmStr(Input,"id"):LmStr(g_Lm.State,"default_root"));xrtMutexUnlock(g_Lm.Lock);
 xvalue* Store=LmXid(Id)?LmStore(Id):NULL;char* Native=NULL;xroot Root=Store?LmRoot(Store,&Native,E):NULL;bool Ok=Root!=NULL;uint64 Count=0,Bytes=0;
 xvalue* Candidates=xrtValueArray();Ok=Ok&&Candidates;const cstr Dirs[]={"blobs","partial"};
 for(size_t K=0;Ok&&K<2;K++){xdir Dir=xrtRootDirOpen(Root,Dirs[K],XDIR_STAT);xdirentry Entry;size_t Seen=0;xdirnext Next=XDIR_NEXT_END;
  if(!Dir){Ok=false;break;}while((Next=xrtDirNext(Dir,&Entry))==XDIR_NEXT_ITEM){if(++Seen>50000){Ok=false;break;}cstr Name=Entry.Name.Data;size_t N=strlen(Name);char Hash[65]={0};if(N<64)continue;memcpy(Hash,Name,64);if(!LmHex(Hash,64)||!LmOneOf(Name+64," .part .part.resume.json"))continue;
   xrtMutexLock(g_Lm.Lock);bool Used=LmReferenced("",Id,Hash);xrtMutexUnlock(g_Lm.Lock);if(Used)continue;char* Path=xrtFormat("%s/%s",Dirs[K],Name);xfileinfo Info;if(Path&&xrtRootStat(Root,Path,false,&Info)&&Info.Type==XFILE_TYPE_FILE){Ok=xrtValueArrayAppendNew(Candidates,xrtValueString(xrtStrView(Path)));if(Ok){Count++;Bytes+=Info.Size;}}xrtFree(Path);if(!Ok)break;
  }if(Next==XDIR_NEXT_ERROR)Ok=false;xrtDirClose(Dir);
 }
 if(Ok&&Delete)for(size_t K=0;Ok&&K<xrtValueCount(Candidates);K++){xstrview Path={0};xrtValueGetString(xrtValueArrayGet(Candidates,K),&Path);Ok=xrtRootRemove(Root,Path.Data);}
 if(Ok){*Result=xrtValueObject();Ok=*Result&&xrtValueObjectSetNew(*Result,XRT_STR_LITERAL("files"),xrtValueUInt(Count))&&xrtValueObjectSetNew(*Result,XRT_STR_LITERAL("bytes"),xrtValueUInt(Bytes))&&LmPut(*Result,"store",Id);}
 xrtValueRelease(Candidates);xrtValueRelease(Store);xrtRootClose(Root);xrtFree(Native);xrtMutexUnlock(g_Lm.Writer);return Ok||LmFail(E,"Cannot inspect or clear the owned model cache");
}
bool MdoLocalModelsCommand(const xvalue* Input,xvalue** Result,char E[512]){
 *Result=NULL;if(E)E[0]=0;if(!g_Lm.Lock||g_Lm.Fault||g_Lm.Stopping)return LmFail(E,"The model registry is read-only or stopping; its original data was preserved");
 cstr Action=LmStr(Input,"action"),Id=LmStr(Input,"id");bool Ok=false;
 if(!strcmp(Action,"deployment_options")){
  xvalue* J=LmJob(LmStr(Input,"installation"));if(!J||strcmp(LmStr(J,"state"),"installed")){xrtValueRelease(J);return LmFail(E,"Select an installed model");}
  const xvalue* P=LmGet(LmGet(J,"plan"),"profile");*Result=xrtValueObject();xrtValueObjectSetNew(*Result,XRT_STR_LITERAL("config"),LmDefaultConfig(P));xrtValueObjectSetNew(*Result,XRT_STR_LITERAL("imported"),xrtValueBool(!P));
  if(P){xrtValueObjectSet(*Result,XRT_STR_LITERAL("capabilities"),LmGet(P,"capabilities"));xrtValueObjectSet(*Result,XRT_STR_LITERAL("requirements"),LmGet(P,"requirements"));}
  else{xrtValueObjectSetNew(*Result,XRT_STR_LITERAL("capabilities"),xrtJsonParse(XRT_STR_LITERAL("[\"text\"]")));xvalue* Rows=MdoExtensionsList("external-tools",E);xrtValueObjectSetNew(*Result,XRT_STR_LITERAL("runtimes"),Rows?Rows:xrtValueArray());}
  xrtValueRelease(J);return *Result!=NULL;
 }
 if(!strcmp(Action,"web_access")){
  if(!LmXid(Id))return LmFail(E,"Select a running deployment");xrtMutexLock(g_Lm.Lock);LmInstance* I=LmsFind(Id);Ok=I&&!strcmp(I->State,"ready")&&!I->Stop;
  if(Ok){*Result=xrtValueObject();Ok=*Result&&LmPut(*Result,"url",I->Url)&&LmPut(*Result,"key",I->Key);}xrtMutexUnlock(g_Lm.Lock);return Ok||LmFail(E,"Start the model before opening its runtime page");
 }
 if(!strcmp(Action,"cache")||!strcmp(Action,"prune_cache"))return LmCache(Input,Result,E);
 if(!strcmp(Action,"hardware")){*Result=LmHardware();return *Result!=NULL;}
 if(!strcmp(Action,"deployment_create")||!strcmp(Action,"deployment_update")||!strcmp(Action,"deployment_switch")){if(!xrtMutexTryLock(g_Lm.Writer))return LmFail(E,"Another model resource operation is running");Ok=LmDeploymentCommand(Input,Result,E);xrtMutexUnlock(g_Lm.Writer);if(Ok){MdoModelManagerReload();MdoApiLiveChanged(NULL);}return Ok;}
 if(!strcmp(Action,"start")||!strcmp(Action,"stop")){bool Flag=false;xrtValueGetBool(LmGet(Input,!strcmp(Action,"start")?"allow_unknown":"force"),&Flag);Ok=!strcmp(Action,"start")?LmsStart(Id,Flag,E):LmsStop(Id,Flag,E);if(Ok)*Result=xrtValueObject();return Ok;}
 if(!strcmp(Action,"logs")){xrtMutexLock(g_Lm.Lock);LmInstance* I=LmsFind(Id);*Result=xrtValueObject();if(I)LmPut(*Result,"text",I->Log);xrtMutexUnlock(g_Lm.Lock);
  if(!I&&LmXid(Id)){char* Path=xrtFormat("data/local-models/logs/%s.log",Id);xfile F=Path?MdoHomeOpenRead(Path):NULL;uint64 Size=0;char Text[LMS_LOG_BYTES]={0};if(F&&xrtFileSize(F,&Size)){uint64 Offset=Size>=sizeof(Text)?Size-sizeof(Text)+1:0;size_t N=(size_t)(Size-Offset);if(xrtReadAtFull(F,Offset,Text,N,NULL)){size_t Skip=0;while(Skip<N&&((unsigned char)Text[Skip]&0xc0)==0x80)Skip++;if(xrtUtf8Valid(xrtStrViewN(Text+Skip,N-Skip),NULL))LmPut(*Result,"text",Text+Skip);}}xrtClose(F);xrtFree(Path);}
  return *Result!=NULL;}
 if(!strcmp(Action,"deployment_delete")||!strcmp(Action,"deployment_rollback")){
  if(!xrtMutexTryLock(g_Lm.Writer))return LmFail(E,"Another model operation is running");
  xvalue* D=LmDeployment(Id);if(!D||LmDeploymentBusy(Id)||LmUInt(Input,"deployment_revision")!=LmUInt(D,"revision")){xrtValueRelease(D);xrtMutexUnlock(g_Lm.Writer);return LmFail(E,"Stop the deployment and reload its latest configuration first");}
  xvalue* Previous=!strcmp(Action,"deployment_rollback")?xrtValueClone(LmGet(D,"previous")):NULL;if(!strcmp(Action,"deployment_rollback")&&!LmDeploymentValid(Previous)){xrtValueRelease(D);xrtValueRelease(Previous);xrtMutexUnlock(g_Lm.Writer);return LmFail(E,"No previous deployment configuration is available");}
  if(Previous){xrtValueObjectSetNew(Previous,XRT_STR_LITERAL("revision"),xrtValueUInt(LmUInt(D,"revision")+1));xrtValueObjectRemove(D,XRT_STR_LITERAL("previous"));xrtValueObjectSet(Previous,XRT_STR_LITERAL("previous"),D);}
  xrtMutexLock(g_Lm.Lock);xvalue* Before=xrtValueClone(g_Lm.State),*Items=xrtValueObjectEdit(g_Lm.State,XRT_STR_LITERAL("deployments"));Ok=Before&&Items&&(Previous?xrtValueObjectSet(Items,xrtStrView(Id),Previous):xrtValueObjectRemove(Items,xrtStrView(Id)));Ok=LmCommit(Before,Ok);xrtMutexUnlock(g_Lm.Lock);
  if(Ok&&!Previous){xvalue* J=LmJob(LmStr(D,"installation")),*Store=J?LmStore(LmStr(J,"store")):NULL;char* Native=NULL;char Why[512]={0};xroot Root=Store?LmRoot(Store,&Native,Why):NULL;LmsClearViews(J,Id,Root);xrtRootClose(Root);xrtFree(Native);xrtValueRelease(Store);xrtValueRelease(J);char* Log=xrtFormat("data/local-models/logs/%s.log",Id);if(Log)MdoHomeRemove(Log,false);xrtFree(Log);}
  xrtValueRelease(D);xrtValueRelease(Previous);xrtMutexUnlock(g_Lm.Writer);if(Ok){*Result=xrtValueObject();MdoModelManagerReload();}return Ok;
 }
 if(!strcmp(Action,"discard_plan")){Ok=LmDiscardPlan(Input,E);if(Ok)*Result=xrtValueObject();return Ok;}
 if(!strcmp(Action,"storage"))return LmNewStore(LmStr(Input,"path"),Result,E);
 if(!strcmp(Action,"import")&&!LmOneOf(LmStr(Input,"mode"),"copy reference"))return LmFail(E,"Choose copy or reference import mode");
 if(!strcmp(Action,"install")||!strcmp(Action,"import"))return LmQueue(Input,Result,E);
 if(!LmXid(Id))return LmFail(E,"Select an installed model");
 if(!strcmp(Action,"retry")){xrtMutexLock(g_Lm.Lock);const xvalue* J=LmGet(LmGet(g_Lm.State,"installations"),Id);cstr State=LmStr(J,"state");bool Busy=!strcmp(g_Lm.Active,Id)||!J||(!strcmp(State,"installed")||!strcmp(State,"queued")||!strcmp(State,"installing"));xrtMutexUnlock(g_Lm.Lock);if(!Busy)Ok=LmJobState(Id,"queued","");}
 else if(!strcmp(Action,"cancel")){xrtMutexLock(g_Lm.Lock);if(!strcmp(g_Lm.Active,Id)&&g_Lm.ActiveCancel){xrtCancelRequest(g_Lm.ActiveCancel);Ok=true;}xrtMutexUnlock(g_Lm.Lock);if(!Ok){xvalue* J=LmJob(Id);bool Queued=!strcmp(LmStr(J,"state"),"queued");xrtValueRelease(J);if(Queued)Ok=LmJobState(Id,"interrupted","Cancelled; files are retained until uninstall");}}
 else if(!strcmp(Action,"uninstall")){bool Files=true;xrtValueGetBool(LmGet(Input,"delete_files"),&Files);return LmRemove(Id,Files,Result,E);}
 if(Ok)*Result=LmJob(Id);else if(!E[0])LmFail(E,"The model operation is already running or the requested action is unavailable");return Ok;
}
bool MdoLocalModelVerification(cstr Id,cstr Fingerprint,cstr Phase,bool Passed,xvalue** Result,char E[512]){
 *Result=NULL;if(!LmXid(Id)||!LmOneOf(Phase,"reply tools"))return false;xrtMutexLock(g_Lm.Lock);xvalue* Before=xrtValueClone(g_Lm.State),*D=LmEdit("deployments",Id);bool Ok=Before&&D&&!strcmp(LmStr(D,"fingerprint"),Fingerprint);xvalue* V=Ok?xrtValueObjectEdit(D,XRT_STR_LITERAL("verification")):NULL;
 if(Ok&&!V){xrtValueObjectSetNew(D,XRT_STR_LITERAL("verification"),xrtValueObject());V=xrtValueObjectEdit(D,XRT_STR_LITERAL("verification"));}Ok=Ok&&V&&LmPut(V,"fingerprint",Fingerprint)&&xrtValueObjectSetNew(V,xrtStrView(Phase),xrtValueBool(Passed))&&xrtValueObjectSetNew(V,XRT_STR_LITERAL("checked_at"),xrtValueUInt(MdoUtcUs()));Ok=LmCommit(Before,Ok);if(Ok)*Result=xrtValueClone(LmGet(LmGet(LmGet(g_Lm.State,"deployments"),Id),"verification"));xrtMutexUnlock(g_Lm.Lock);return Ok||LmFail(E,"The deployment changed while its model was being verified");
}
