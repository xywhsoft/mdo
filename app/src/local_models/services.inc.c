#define LMS_COUNT 16u
#define LMS_LOG_BYTES 16384u
typedef struct LmInstance {
 char Id[33],State[24],Error[512],Alias[128],Key[96],Url[96],Log[LMS_LOG_BYTES];
 xthread *Thread,*Output; xprocess* Process;xcancel* Cancel;uint64 Started,LastUse;uint32 Calls;bool Stop,Force,AllowUnknown;
 MdoExtensionLease* RuntimeLease;MdoCliUse* CliUse;
} LmInstance;
static LmInstance g_Lms[LMS_COUNT];
typedef struct LmCall {LmInstance* Instance;xcancel* Cancel;xcancelwatch* Watch;} LmCall;
static LmInstance* LmsFind(cstr Id){for(size_t I=0;I<LMS_COUNT;I++)if(!strcmp(g_Lms[I].Id,Id))return &g_Lms[I];return NULL;}
static bool LmDeploymentBusy(cstr Id){xrtMutexLock(g_Lm.Lock);LmInstance* I=LmsFind(Id);bool Busy=I&&I->Thread&&strcmp(I->State,"stopped")&&strcmp(I->State,"failed"); xrtMutexUnlock(g_Lm.Lock);return Busy;}
bool MdoLocalModelOwned(cstr Id){return Id&&!strncmp(Id,"local-model.",12)&&LmXid(Id+12);}
static void LmsState(LmInstance* I,cstr State,cstr Error){xrtMutexLock(g_Lm.Lock);snprintf(I->State,sizeof(I->State),"%s",State);snprintf(I->Error,sizeof(I->Error),"%s",Error?Error:"");xrtMutexUnlock(g_Lm.Lock);MdoApiLiveChanged(NULL);}
static char* LmsSupervisor(char E[512]){
#if defined(_WIN32)
 cstr Name="windows/model-supervisor.exe",Suffix=".exe";
#elif defined(__APPLE__)
 cstr Name="macos/model-supervisor",Suffix="";
#else
 cstr Name="linux/model-supervisor",Suffix="";
#endif
 char* Resource=xrtFormat("/app/generated/native/%s",Name);size_t N=0;bytes Image=Resource?xrtVfsReadAllLimit(MdoHomeApplicationVfs(),Resource,1048576,&N):NULL;xrtFree(Resource);
 uint8 Digest[32];char Hash[65]={0};bool Ok=Image&&N&&xrtSha256(Image,N,Digest);if(Ok)for(size_t K=0;K<32;K++)snprintf(Hash+K*2,3,"%02x",Digest[K]);
 char* Path=Ok?xrtFormat("data/local-models/native/%s%s",Hash,Suffix):NULL;char Actual[65];xfile Existing=Path?MdoHomeOpenRead(Path):NULL;
 bool Same=Existing&&LmHash(Existing,N,Actual,NULL,NULL)&&!strcmp(Hash,Actual);xrtClose(Existing);
 Ok=Ok&&Path&&(Same||MdoHomeAtomicWrite(Path,Image,N,false));xrtFree(Image);
#if !defined(_WIN32)
 if(Ok)Ok=MdoHomeSetMode(Path,0700);
#endif
 char* Native=Ok?MdoHomeExternalPath(Path):NULL;xrtFree(Path);if(!Native)LmFail(E,"The bundled model supervisor is unavailable or could not be verified");return Native;
}
static char* LmsProgram(const xvalue* Runtime,char E[512]){
 cstr Ref=LmStr(Runtime,"ref");xvalue* M=MdoExtensionsResolve(Ref,E);const xvalue* Install=LmGet(M,"installation");
 bool Same=M&&!strcmp(LmStr(Install,"version_id"),LmStr(Runtime,"version_id"))&&!strcmp(LmStr(Install,"sha256"),LmStr(Runtime,"sha256"));
 char* Entry=Same?xrtFormat("%s/%s",LmStr(M,"root"),LmStr(M,"entry")):NULL;char* Text=Entry?MdoExtensionRead(Entry,true,262144,NULL):NULL;xvalue* Tool=Text?xrtJsonParse(xrtStrView(Text)):NULL;char* Program=NULL;
 const xvalue* Commands=LmGet(Tool,"commands");cstr Export=LmStr(Tool,"command");if(!Export[0])Export=LmStr(M,"alias");
 for(size_t K=0;K<xrtValueCount(Commands);K++){const xvalue* C=xrtValueArrayGet(Commands,K);if(!strcmp(LmStr(C,"entry"),LmStr(Tool,"program"))&&!xrtValueCount(LmGet(C,"args"))){Export=LmStr(C,"name");break;}}
 if(Same&&Tool)Program=MdoCliProgram(Ref,Export,E);else LmFail(E,"The fixed runtime build is missing or changed; reinstall the matching build");xrtValueRelease(M);xrtValueRelease(Tool);xrtFree(Text);xrtFree(Entry);return Program;
}
static bool LmsRootParents(xroot Root,cstr Path){char* P=xrtStrDup(Path);if(!P)return false;bool Ok=true;for(char* C=P;Ok&&*C;C++)if(*C=='/'){*C=0;xfileinfo Info;if(!xrtRootStat(Root,P,false,&Info))Ok=xrtRootDirCreate(Root,P,0700);else Ok=Info.Type==XFILE_TYPE_DIRECTORY;*C='/';}xrtFree(P);return Ok;}
static bool LmsFiles(const xvalue* Job,cstr Deployment,xroot Root,cstr Native,xcancel* Cancel,char** Model,char** Projector,char** Template,char E[512]){
 *Model=*Projector=*Template=NULL;const xvalue* Assets=LmGet(Job,"assets");size_t WeightCount=0;for(size_t K=0;K<xrtValueCount(Assets);K++)if(!LmStr(xrtValueArrayGet(Assets,K),"role")[0]||!strcmp(LmStr(xrtValueArrayGet(Assets,K),"role"),"weights"))WeightCount++;
 for(size_t K=0;K<xrtValueCount(Assets);K++){const xvalue* F=xrtValueArrayGet(Assets,K);cstr Hash=LmStr(F,"sha256"),Role=LmStr(F,"role");char* P=NULL;
  if(!strcmp(LmStr(Job,"kind"),"reference")){P=xrtStrDup(LmStr(Job,"source_path"));char* Parent=xrtPathParent(P);xroot R=Parent?xrtRootOpen(Parent):NULL;cstr Leaf=strrchr(P,'/');cstr Back=strrchr(P,'\\');if(Back&&(!Leaf||Back>Leaf))Leaf=Back;Leaf=Leaf?Leaf+1:P;xfile In=LmFile(R,Leaf,XFILE_READ);char Actual[65];bool Ok=In&&LmHash(In,LmUInt(F,"bytes"),Actual,Cancel,NULL)&&!strcmp(Actual,Hash);xrtClose(In);xrtRootClose(R);xrtFree(Parent);if(!Ok){xrtFree(P);return LmFail(E,"The referenced model file is missing or changed");}}
  else{if(!LmBlob(Root,Hash,LmUInt(F,"bytes"),Cancel))return LmFail(E,"Model resources failed their size or SHA-256 check");
   if(WeightCount>1&&(!Role[0]||!strcmp(Role,"weights"))){char* Source=xrtFormat("blobs/%s",Hash),*View=xrtFormat("views/%s/%s",Deployment,LmStr(F,"path"));xfileinfo Info;bool Exists=View&&xrtRootStat(Root,View,false,&Info);bool Ok=Source&&View&&LmsRootParents(Root,View)&&(!Exists||(Info.Type==XFILE_TYPE_FILE&&xrtRootRemove(Root,View)))&&xrtRootLinkHard(Root,Source,View);P=Ok?xrtFormat("%s/%s",Native,View):NULL;xrtFree(Source);xrtFree(View);if(!P)return LmFail(E,"Split GGUF files require a resource volume supporting hard links");}
   else P=xrtFormat("%s/blobs/%s",Native,Hash);
  }
  if(Role[0]&&strcmp(Role,"weights")&&strcmp(Role,"mmproj")&&strcmp(Role,"template")){xrtFree(P);return LmFail(E,"This adapter cannot consume the declared model resource role");}
  if(!P)return false;
  if((!Role[0]||!strcmp(Role,"weights"))&&!*Model)*Model=P;else if(!strcmp(Role,"mmproj")&&!*Projector)*Projector=P;else if(!strcmp(Role,"template")&&!*Template)*Template=P;else xrtFree(P);
 }return *Model!=NULL;
}
static bool LmsClearViews(const xvalue* Job,cstr Deployment,xroot Root){
 if(!Root)return false;
 bool Ok=true;const xvalue* Assets=LmGet(Job,"assets");for(size_t K=0;K<xrtValueCount(Assets);K++){cstr Path=LmStr(xrtValueArrayGet(Assets,K),"path");char* View=MdoExtensionRelativePath(Path)?xrtFormat("views/%s/%s",Deployment,Path):NULL;if(View){xfileinfo Info;if(xrtRootStat(Root,View,false,&Info))Ok=(Info.Type==XFILE_TYPE_FILE&&xrtRootRemove(Root,View))&&Ok;char* Parent=xrtPathParent(View);size_t Min=6+strlen(Deployment);while(Parent&&strlen(Parent)>=Min){if(!xrtRootRemove(Root,Parent))break;char* Next=xrtPathParent(Parent);xrtFree(Parent);Parent=Next;}xrtFree(Parent);}xrtFree(View);}return Ok;
}
static bool LmDeploymentClearViews(const xvalue* D,char E[512]){
 const xvalue* Rows[]={D,LmGet(D,"previous")};for(size_t K=0;K<2;K++){if(!Rows[K])continue;xvalue* Job=LmJob(LmStr(Rows[K],"installation")),*Store=Job?LmStore(LmStr(Job,"store")):NULL;char* Native=NULL;xroot Root=Store?LmRoot(Store,&Native,E):NULL;bool Ok=Root&&LmsClearViews(Job,LmStr(D,"id"),Root);xrtRootClose(Root);xrtFree(Native);xrtValueRelease(Store);xrtValueRelease(Job);if(!Ok)return LmFail(E,"Reconnect the model resource store and release its split-file views before changing this deployment");}return true;
}

static void LmsAppend(LmInstance* I,xfile File,const char* Text,size_t N){
 if(!xrtUtf8Valid(xrtStrViewN(Text,N),NULL))return;
 char Clean[8192];size_t At=0;for(size_t K=0;K<N&&At+12<sizeof(Clean);){size_t Key=strlen(I->Key);if(Key&&K+Key<=N&&!memcmp(Text+K,I->Key,Key)){memcpy(Clean+At,"[redacted]",10);At+=10;K+=Key;}else Clean[At++]=Text[K++];}Clean[At]=0;
 if(File){uint64 Size=0;if(xrtFileSize(File,&Size)&&Size+At>4u*1024u*1024u){xrtFileResize(File,0);xrtSeek(File,0,XSEEK_START,NULL);}xrtWriteFull(File,Clean,At,NULL);xrtFlush(File);}
 xrtMutexLock(g_Lm.Lock);size_t Old=strlen(I->Log);if(Old+At>=sizeof(I->Log)){size_t Drop=Old+At-sizeof(I->Log)+1;while(Drop<Old&&((unsigned char)I->Log[Drop]&0xc0)==0x80)Drop++;memmove(I->Log,I->Log+Drop,Old-Drop+1);Old-=Drop;}memcpy(I->Log+Old,Clean,At+1);xrtMutexUnlock(g_Lm.Lock);
}
static int32 LmsOutput(void* Data){
 LmInstance* I=Data;char* Path=xrtFormat("data/local-models/logs/%s.log",I->Id);xfile File=Path?MdoHomeOpenWrite(Path,XFILE_WRITE|XFILE_CREATE):NULL;xrtFree(Path);if(File)xrtSeek(File,0,XSEEK_END,NULL);
 char Input[1024],Line[8192];size_t N=0;bool Overflow=false;int64 Read;
 while((Read=xrtProcessRead(I->Process,XPROCESS_STDOUT,Input,sizeof(Input)))>0)for(int64 K=0;K<Read;K++){if(N+1<sizeof(Line))Line[N++]=Input[K];else Overflow=true;if(Input[K]=='\n'){if(!Overflow)LmsAppend(I,File,Line,N);N=0;Overflow=false;}}
 if(N&&!Overflow)LmsAppend(I,File,Line,N);
 xrtClose(File);return 0;
}
static xvalue* LmsGet(LmInstance* I,cstr Path,uint16* Status){
 char Url[256],Authorization[128];snprintf(Url,sizeof(Url),"%s%s",I->Url,Path);snprintf(Authorization,sizeof(Authorization),"Bearer %s",I->Key);XS_FetchHeader H={"Authorization",Authorization};XS_FetchRequest Q={0};XS_FetchResponse R={0};Q.Size=sizeof(Q);Q.Version=XS_FETCH_REQUEST_VERSION;Q.Url=Url;Q.Method="GET";Q.Headers=&H;Q.HeaderCount=1;Q.MaxBodyBytes=262144;Q.Timeout=1000000;Q.IdleTimeout=1000000;Q.Cancel=I->Cancel;R.Size=sizeof(R);
 bool Ok=xsFetch(&Q,&R)&&!R.Redirects;if(Status)*Status=Ok?R.Status:0;xvalue* V=Ok&&R.Status==200?xrtJsonParse(xrtStrViewN((cstr)R.Body,R.BodySize)):NULL;xsFetchResponseUnit(&R);xrtSecureZero(Authorization,sizeof(Authorization));return V;
}
static bool LmsReady(LmInstance* I,const xvalue* C,char E[512]){
 uint16 Status=0;xvalue* Health=LmsGet(I,"/health",&Status);bool Healthy=Health&&!strcmp(LmStr(Health,"status"),"ok");xrtValueRelease(Health);if(!Healthy)return false;
 xvalue* Models=LmsGet(I,"/v1/models",NULL),*Props=LmsGet(I,"/props",NULL);const xvalue* Rows=LmGet(Models,"data");bool Identity=false;for(size_t K=0;K<xrtValueCount(Rows);K++)if(!strcmp(LmStr(xrtValueArrayGet(Rows,K),"id"),I->Alias))Identity=true;
 bool Ok=Identity&&LmUInt(LmGet(Props,"default_generation_settings"),"n_ctx")==LmUInt(C,"context_tokens")&&LmUInt(Props,"total_slots")==LmUInt(C,"parallel");xrtValueRelease(Models);xrtValueRelease(Props);if(!Ok)LmFail(E,"The server identity, effective context or slot count differs from the deployment");return Ok;
}
static bool LmsPreflight(const xvalue* D,bool AllowUnknown,char E[512]){
 bool Allowed=false;xrtValueGetBool(LmGet(LmGet(D,"config"),"allow_uncertain_resources"),&Allowed);AllowUnknown=AllowUnknown||Allowed;
 const xvalue* R=LmGet(D,"runtime");if(strcmp(LmStr(R,"platform"),MdoExtensionRuntimePlatform())||strcmp(LmStr(R,"engine"),"llama.cpp")||!LmOneOf(LmStr(R,"backend"),"cpu cuda vulkan metal"))return LmFail(E,"This runtime adapter or platform is unavailable");
#if !defined(__APPLE__)
 if(!strcmp(LmStr(R,"backend"),"metal"))return LmFail(E,"The Metal backend requires macOS");
#endif
 xvalue* H=LmHardware();const xvalue* Needs=LmGet(D,"requirements"),*Features=LmGet(R,"cpu_features");bool Known=false; xrtValueGetBool(LmGet(H,"cpu_features_known"),&Known);bool Ok=true;
 for(size_t K=0;Ok&&Known&&K<xrtValueCount(Features);K++){xstrview F={0};xrtValueGetString(xrtValueArrayGet(Features,K),&F);bool Found=false;const xvalue* Current=LmGet(H,"cpu_features");for(size_t N=0;N<xrtValueCount(Current);N++)if(xrtValueEqual(xrtValueArrayGet(Current,N),xrtValueArrayGet(Features,K)))Found=true;if(!Found)Ok=LmFail(E,"The CPU or operating system lacks an instruction set required by this runtime build");}
 uint64 Available=LmUInt(H,"available_ram_bytes"),Required=LmUInt(Needs,"ram_bytes")+LmUInt(Needs,"margin_bytes");if(Ok&&Available&&Required>Available)Ok=LmFail(E,"Available system memory is below the deployment requirement; lower the configuration or free memory");
 if(Ok&&!AllowUnknown&&(!Available||(!Needs)||strcmp(LmStr(R,"backend"),"cpu")))Ok=LmFail(E,"Resource availability is uncertain. Review RAM/VRAM requirements and explicitly allow an attempted start");
 xrtValueRelease(H);return Ok;
}
static int32 LmsRun(void* Data){
 LmInstance* I=Data;char E[512]={0};xvalue* D=LmDeployment(I->Id),*Job=D?LmJob(LmStr(D,"installation")):NULL,*Store=Job?LmStore(LmStr(Job,"store")):NULL;char *Native=NULL,*Model=NULL,*Projector=NULL,*Template=NULL,*Program=NULL,*Supervisor=NULL; xroot Root=NULL;
 bool Ok=D&&Job&&!strcmp(LmStr(Job,"state"),"installed")&&LmsPreflight(D,I->AllowUnknown,E);if(Ok)Root=LmRoot(Store,&Native,E);Ok=Ok&&Root;
 /* Installed blobs are immutable and referenced by this deployment. Its
  * private split-file view cannot overlap the download producer. */
 if(Ok)Ok=LmsFiles(Job,I->Id,Root,Native,I->Cancel,&Model,&Projector,&Template,E);
 const xvalue* C=LmGet(D,"config"),*R=LmGet(D,"runtime");
 if(Ok){char* Dir=MdoExtensionsDirectoryId("external-tools",LmStr(R,"ref"),E);I->RuntimeLease=Dir?MdoExtensionsAcquire("external-tools",Dir,E):NULL;xrtFree(Dir);Ok=I->RuntimeLease&&MdoCliUseBegin(&I->CliUse,E);}
 if(Ok){Program=LmsProgram(R,E);Supervisor=LmsSupervisor(E);Ok=Program&&Supervisor;}
 xnetaddr Addr={0};xnetsocket Socket=Ok?xrtNetSocketOpen(XNET_FAMILY_IPV4,XNET_SOCKET_STREAM,0):NULL;if(Ok)Ok=Socket&&xrtNetAddrLoopback(&Addr,XNET_FAMILY_IPV4,0)&&xrtNetSocketBind(Socket,&Addr)&&xrtNetSocketLocal(Socket,&Addr);xrtNetSocketClose(Socket);
 uint8 Secret[32]={0};char* Nonce=Ok?xrtXidMakeString():NULL;if(Ok)Ok=Nonce&&xrtSecureRandom(Secret,sizeof(Secret));
 if(Ok){xrtMutexLock(g_Lm.Lock);for(size_t K=0;K<sizeof(Secret);K++)snprintf(I->Key+K*2,3,"%02x",Secret[K]);snprintf(I->Alias,sizeof(I->Alias),"mdo-local-%s-%s",I->Id,Nonce);snprintf(I->Url,sizeof(I->Url),"http://127.0.0.1:%u",(unsigned)Addr.Port);xrtMutexUnlock(g_Lm.Lock);}xrtSecureZero(Secret,sizeof(Secret));xrtFree(Nonce);
 char Port[12],Context[24],Parallel[12],Gpu[12],Threads[12],Output[12];snprintf(Port,sizeof(Port),"%u",(unsigned)Addr.Port);snprintf(Context,sizeof(Context),"%llu",(unsigned long long)(LmUInt(C,"context_tokens")*LmUInt(C,"parallel")));snprintf(Parallel,sizeof(Parallel),"%llu",(unsigned long long)LmUInt(C,"parallel"));int64 Layers=0;MdoExtensionNumber(C,"gpu_layers",&Layers);snprintf(Gpu,sizeof(Gpu),"%lld",(long long)Layers);snprintf(Threads,sizeof(Threads),"%llu",(unsigned long long)LmUInt(C,"threads"));snprintf(Output,sizeof(Output),"%llu",(unsigned long long)LmUInt(C,"max_output_tokens"));
 cstr Args[48];size_t N=0;Args[N++]=Program;Args[N++]="--model";Args[N++]=Model;Args[N++]="--host";Args[N++]="127.0.0.1";Args[N++]="--port";Args[N++]=Port;Args[N++]="--alias";Args[N++]=I->Alias;Args[N++]="--ctx-size";Args[N++]=Context;Args[N++]="--parallel";Args[N++]=Parallel;Args[N++]="--n-gpu-layers";Args[N++]=Gpu;Args[N++]="--n-predict";Args[N++]=Output;Args[N++]="--verbosity";Args[N++]="4";Args[N++]="--jinja";Args[N++]="--no-context-shift";
 if(LmUInt(C,"threads")){Args[N++]="--threads";Args[N++]=Threads;}if(Projector){Args[N++]="--mmproj";Args[N++]=Projector;}if(Template){Args[N++]="--chat-template-file";Args[N++]=Template;}
 if(!strcmp(LmStr(R,"backend"),"cpu")){Args[N++]="--device";Args[N++]="none";}
 else if(!strcmp(LmStr(R,"backend"),"vulkan")){Args[N++]="--device";Args[N++]="Vulkan0";}
 else if(!strcmp(LmStr(R,"backend"),"cuda")){Args[N++]="--device";Args[N++]="CUDA0";}
 unsigned Build=0;bool Modern=sscanf(LmStr(R,"build"),"b%u",&Build)==1&&Build>=12477;
 if(strcmp(LmStr(C,"reasoning_mode"),"default")){Args[N++]=Modern?"--reasoning":"--chat-template-kwargs";Args[N++]=Modern?LmStr(C,"reasoning_mode"):!strcmp(LmStr(C,"reasoning_mode"),"off")?"{\"enable_thinking\":false}":"{\"enable_thinking\":true}";}
 if(Ok&&!xrtCancelRequested(I->Cancel)){xprocessconfig P;xrtProcessConfigInit(&P);P.Program=Supervisor;P.Args=Args;P.ArgCount=N;P.WorkDir=MdoHomeApplicationRoot();P.HideWindow=true;P.NewGroup=true;P.Stdin.Mode=P.Stdout.Mode=XPROCESS_IO_PIPE;P.Stderr.Mode=XPROCESS_IO_MERGE;
  const xprocessenv Env[]={{"LLAMA_API_KEY",I->Key},{"LLAMA_API_KEY_FILE",NULL},{"LLAMA_ARG_CACHE_RAM","0"},{"LLAMA_ARG_REASONING","auto"},{"LLAMA_ARG_HOST",NULL},{"LLAMA_ARG_PORT",NULL},{"LLAMA_ARG_MODEL",NULL},{"LLAMA_ARG_MODELS_DIR",NULL},{"LLAMA_ARG_TOOLS",NULL},{"LLAMA_ARG_MCP_SERVERS_CONFIG",NULL},{"LLAMA_ARG_MCP_SERVERS_JSON",NULL},{"LLAMA_ARG_AGENT",NULL},{"LLAMA_ARG_CONNECT",NULL},{"LLAMA_ARG_HTTP_PROXY",NULL},{"LLAMA_ARG_HTTPS_PROXY",NULL}};P.Env=Env;P.EnvCount=sizeof(Env)/sizeof(*Env);
  I->Process=xrtProcessSpawn(&P);Ok=I->Process!=NULL;if(Ok){I->Output=xrtThreadCreate(LmsOutput,I,0);Ok=I->Output!=NULL;}}
 if(Ok){LmsState(I,"loading","");uint64 End=MdoDeadlineAfterUs(300000000);bool Ready=false;while(!xrtCancelRequested(I->Cancel)&&xrtProcessState(I->Process)==XPROCESS_RUNNING&&!MdoDeadlineExpired(End)){if(LmsReady(I,C,E)){Ready=true;break;}if(E[0])break;xrtSleep(200);}Ok=Ready;if(!Ready&&!E[0])LmFail(E,"Model loading failed, exited, or exceeded five minutes; inspect the runtime log");}
 if(Ok){xrtMutexLock(g_Lm.Lock);I->LastUse=MdoMonotonicUs();xrtMutexUnlock(g_Lm.Lock);LmsState(I,"ready","");MdoModelManagerReload();
  while(xrtProcessState(I->Process)==XPROCESS_RUNNING&&!xrtCancelRequested(I->Cancel)){xrtMutexLock(g_Lm.Lock);bool Stop=I->Stop;uint64 Idle=LmUInt(C,"idle_seconds");if(Idle&&!I->Calls&&MdoMonotonicUs()-I->LastUse>Idle*1000000u){I->Stop=true;Stop=true;}xrtMutexUnlock(g_Lm.Lock);if(Stop)break;xrtSleep(50);}
  if(xrtProcessState(I->Process)!=XPROCESS_RUNNING&&!I->Stop&&!xrtCancelRequested(I->Cancel)){Ok=false;LmFail(E,"The owned model runtime exited unexpectedly; inspect its log and restart it");}
 }
 if(I->Process){LmsState(I,"stopping",E);for(;;){xrtMutexLock(g_Lm.Lock);uint32 Calls=I->Calls;xrtMutexUnlock(g_Lm.Lock);if(!Calls)break;xrtSleep(25);}xrtProcessClose(I->Process,XPROCESS_STDIN);if(xrtProcessWaitFor(I->Process,2500)!=XWAIT_OK){xrtProcessKillTree(I->Process);xrtProcessWait(I->Process);}if(I->Output){xrtThreadWait(I->Output);xrtThreadDestroy(I->Output);I->Output=NULL;}xrtProcessDestroy(I->Process);I->Process=NULL;}
 MdoCliUseEnd(I->CliUse);I->CliUse=NULL;MdoExtensionsRelease(I->RuntimeLease);I->RuntimeLease=NULL;LmsClearViews(Job,I->Id,Root);xrtRootClose(Root);xrtFree(Native);xrtFree(Model);xrtFree(Projector);xrtFree(Template);xrtFree(Program);xrtFree(Supervisor);xrtValueRelease(D);xrtValueRelease(Job);xrtValueRelease(Store);
 xrtMutexLock(g_Lm.Lock);xrtSecureZero(I->Key,sizeof(I->Key));bool Requested=I->Stop||g_Lm.Stopping; xrtMutexUnlock(g_Lm.Lock);LmsState(I,Ok||Requested?"stopped":"failed",Ok||Requested?"":E);MdoModelManagerReload();MdoApiWriteReleaseBackground();return 0;
}
static void LmsReapLocked(LmInstance* I){if(I->Thread&&xrtThreadWaitFor(I->Thread,0)==XWAIT_OK){xrtThreadDestroy(I->Thread);I->Thread=NULL;xrtCancelDestroy(I->Cancel);I->Cancel=NULL;}}
static bool LmsStart(cstr Id,bool AllowUnknown,char E[512]){
 if(!g_Lm.Lock||!LmXid(Id)||!xrtMutexTryLock(g_Lm.Configurations))return LmFail(E,"The model registry is busy or unavailable");
 xvalue* D=LmDeployment(Id);if(!D){xrtMutexUnlock(g_Lm.Configurations);return LmFail(E,"Select a deployment configuration");}xrtValueRelease(D);
 xrtMutexLock(g_Lm.Lock);for(size_t K=0;K<LMS_COUNT;K++)LmsReapLocked(&g_Lms[K]);LmInstance* I=LmsFind(Id);if(I&&I->Thread){bool Same=!I->Stop;xrtMutexUnlock(g_Lm.Lock);xrtMutexUnlock(g_Lm.Configurations);return Same||LmFail(E,"Wait for the current deployment to stop");}
 if(!I)for(size_t K=0;K<LMS_COUNT;K++)if(!g_Lms[K].Thread){I=&g_Lms[K];break;}
 bool Ok=I&&!g_Lm.Stopping&&g_Lm.ServicesReady&&MdoApiWriteRetainBackground();if(Ok){memset(I,0,sizeof(*I));snprintf(I->Id,33,"%s",Id);snprintf(I->State,sizeof(I->State),"starting");I->AllowUnknown=AllowUnknown;I->Started=MdoUtcUs();I->Cancel=xrtCancelChild(g_Lm.Stop);I->Thread=I->Cancel?xrtThreadCreate(LmsRun,I,0):NULL;Ok=I->Thread!=NULL;if(!Ok){xrtCancelDestroy(I->Cancel);I->Cancel=NULL;MdoApiWriteReleaseBackground();}}
 xrtMutexUnlock(g_Lm.Lock);xrtMutexUnlock(g_Lm.Configurations);return Ok||LmFail(E,"No free model service slot or the service manager is stopping");
}
static bool LmsStop(cstr Id,bool Force,char E[512]){
 if(!LmXid(Id))return LmFail(E,"Select a deployment configuration");
 xrtMutexLock(g_Lm.Lock);LmInstance* I=LmsFind(Id);bool Ok=true;xcancel* Cancel=NULL;if(I&&I->Thread&&strcmp(I->State,"stopped")&&strcmp(I->State,"failed")){I->Stop=true;if(Force||strcmp(I->State,"ready"))Cancel=xrtCancelRef(I->Cancel);snprintf(I->State,sizeof(I->State),"stopping");}xrtMutexUnlock(g_Lm.Lock);if(Cancel){xrtCancelRequest(Cancel);xrtCancelDestroy(Cancel);}return Ok||LmFail(E,"The selected deployment has no running service");
}
static void LmsCancelCall(void* Data){xrtCancelRequest(Data);}
bool MdoLocalModelCallBegin(cstr Model,cstr Alias,xcancel* Parent,xcancel** Cancel,void** Handle,char E[512]){
 *Cancel=NULL;*Handle=NULL;if(!g_Lm.Lock||g_Lm.Stopping)return LmFail(E,"The local model service manager is stopping");LmCall* C=xrtCalloc(1,sizeof(*C));if(!C)return false;
 xrtMutexLock(g_Lm.Lock);LmInstance* I=MdoLocalModelOwned(Model)?LmsFind(Model+12):NULL;bool Ok=I&&!strcmp(I->State,"ready")&&!I->Stop&&!strcmp(I->Alias,Alias);if(Ok){C->Cancel=xrtCancelChild(Parent);C->Watch=C->Cancel?xrtCancelWatch(I->Cancel,LmsCancelCall,C->Cancel):NULL;Ok=C->Cancel&&C->Watch;if(Ok){C->Instance=I;I->Calls++;I->LastUse=MdoMonotonicUs();}}xrtMutexUnlock(g_Lm.Lock);
 if(Ok){*Cancel=C->Cancel;*Handle=C;return true;}xrtCancelUnwatch(C->Watch);xrtCancelDestroy(C->Cancel);xrtFree(C);return LmFail(E,"The local model is stopped, stopping, or was restarted. Start it and refresh the model selection");
}
void MdoLocalModelCallEnd(void* Handle){LmCall* C=Handle;if(!C)return;xrtCancelUnwatch(C->Watch);xrtMutexLock(g_Lm.Lock);if(C->Instance->Calls)C->Instance->Calls--;C->Instance->LastUse=MdoMonotonicUs();xrtMutexUnlock(g_Lm.Lock);xrtCancelDestroy(C->Cancel);xrtFree(C);}
bool MdoLocalModelResolve(cstr Model,MdoLocalModelEndpoint* Out,char E[512]){
 if(!g_Lm.Lock||g_Lm.Stopping||!Out||!MdoLocalModelOwned(Model))return false;
 memset(Out,0,sizeof(*Out));xrtMutexLock(g_Lm.Lock);LmInstance* I=LmsFind(Model+12);bool Ok=I&&!strcmp(I->State,"ready")&&!I->Stop;if(Ok){snprintf(Out->Endpoint,sizeof(Out->Endpoint),"%s/v1/chat/completions",I->Url);snprintf(Out->Alias,sizeof(Out->Alias),"%s",I->Alias);snprintf(Out->Key,sizeof(Out->Key),"%s",I->Key);}xrtMutexUnlock(g_Lm.Lock);
 if(!Ok){xvalue* D=LmDeployment(Model+12);bool Auto=false;xrtValueGetBool(LmGet(LmGet(D,"config"),"auto_start_on_use"),&Auto);xrtValueRelease(D);if(Auto){snprintf(Out->Endpoint,sizeof(Out->Endpoint),"http://127.0.0.1:1/v1/chat/completions");snprintf(Out->Alias,sizeof(Out->Alias),"pending");return true;}}
 return Ok||LmFail(E,"Start this local model from Settings > Local models before using it");
}
bool MdoLocalModelEnsure(cstr Model,xcancel* Cancel,uint64 Deadline,char E[512]){
 if(!g_Lm.Lock||g_Lm.Stopping||!MdoLocalModelOwned(Model))return false;
 xvalue* D=LmDeployment(Model+12);bool Auto=false;xrtValueGetBool(LmGet(LmGet(D,"config"),"auto_start_on_use"),&Auto);xrtValueRelease(D);uint64 End=MdoDeadlineAfterUs(300000000);if(Deadline&&Deadline<End)End=Deadline;
 bool Attempted=false;while(!xrtCancelRequested(Cancel)&&!xrtCancelRequested(g_Lm.Stop)&&!MdoDeadlineExpired(End)){
  xrtMutexLock(g_Lm.Lock);LmInstance* I=LmsFind(Model+12);bool Ready=I&&!I->Stop&&!strcmp(I->State,"ready"),Failed=I&&!strcmp(I->State,"failed"),Stopped=!I||I->Stop||!strcmp(I->State,"stopped");xrtMutexUnlock(g_Lm.Lock);if(Ready)return true;
  if(!Auto||(Attempted&&(Failed||Stopped)))break;
  if(!Attempted){if(!LmsStart(Model+12,false,E))return false;Attempted=true;}xrtSleep(25);
 }
 return LmFail(E,xrtCancelRequested(Cancel)?"Local model startup cancelled":"The local model is stopped, failed to load or exceeded its startup deadline");
}
static xvalue* LmsSnapshotLocked(const xvalue* D){
 xvalue* Out=xrtValueClone(D);LmInstance* I=LmsFind(LmStr(D,"id"));LmPut(Out,"service_state",I?I->State:"stopped");if(I){LmPut(Out,"service_error",I->Error);xrtValueObjectSetNew(Out,XRT_STR_LITERAL("active_requests"),xrtValueUInt(I->Calls));xrtValueObjectSetNew(Out,XRT_STR_LITERAL("started_at"),xrtValueUInt(I->Started));if(!strcmp(I->State,"ready"))LmPut(Out,"web_url",I->Url);}xrtValueObjectSetNew(Out,XRT_STR_LITERAL("can_rollback"),xrtValueBool(LmGet(D,"previous")!=NULL));xrtValueObjectRemove(Out,XRT_STR_LITERAL("previous"));return Out;
}
xvalue* MdoLocalModelCatalog(void){if(!g_Lm.Lock)return xrtValueArray();xrtMutexLock(g_Lm.Lock);xvalue* Rows=xrtValueArray();const xvalue* Items=LmGet(g_Lm.State,"deployments");for(size_t K=0;Rows&&K<xrtValueCount(Items);K++){const xvalue* D=xrtValueObjectAt(Items,K,NULL);const xvalue* Job=LmGet(LmGet(g_Lm.State,"installations"),LmStr(D,"installation"));if(!strcmp(LmStr(Job,"state"),"installed"))xrtValueArrayAppendNew(Rows,LmsSnapshotLocked(D));}xrtMutexUnlock(g_Lm.Lock);return Rows;}
bool MdoLocalModelsServicesInit(void){if(!g_Lm.Lock||g_Lm.ServicesReady)return g_Lm.Lock!=NULL;g_Lm.ServicesReady=true;xvalue* Rows=MdoLocalModelCatalog();for(size_t K=0;K<xrtValueCount(Rows);K++){const xvalue* D=xrtValueArrayGet(Rows,K);bool Start=false;xrtValueGetBool(LmGet(LmGet(D,"config"),"auto_start"),&Start);if(Start){char E[512]={0};LmsStart(LmStr(D,"id"),false,E);}}xrtValueRelease(Rows);return true;}
static void LmsUnit(void){for(size_t K=0;K<LMS_COUNT;K++){LmInstance* I=&g_Lms[K];if(I->Thread){xrtThreadWait(I->Thread);xrtThreadDestroy(I->Thread);}xrtCancelDestroy(I->Cancel);}memset(g_Lms,0,sizeof(g_Lms));}

void MdoLocalModelsServicesStop(void){if(!g_Lm.Lock)return;MdoLocalModelsStop();LmsUnit();g_Lm.ServicesReady=false;}
