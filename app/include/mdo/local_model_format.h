#ifndef MDO_LOCAL_MODEL_FORMAT_H
#define MDO_LOCAL_MODEL_FORMAT_H
/* Inert deployment recipes. This module never downloads or executes a file. */
#define MD_LOCAL_MODEL_BYTES (256u*1024u)
#define MD_LOCAL_MODEL_PACKAGE_BYTES (2u*1024u*1024u)
#define MD_LOCAL_ASSET_BYTES (1099511627776ULL)
static bool LmFields(const xvalue* O,cstr Names){
 if(xrtValueType(O)!=XVALUE_OBJECT)return false;
 for(size_t I=0;I<xrtValueCount(O);I++){xstrview K={0};xrtValueObjectAt(O,I,&K);bool Found=false;cstr P=Names;
  while(*P){size_t N=strcspn(P," ");if(K.Size==N&&!memcmp(K.Data,P,N))Found=true;P+=N;if(*P)P++;}if(!Found)return false;}
 return true;
}
static bool LmNumber(const xvalue* O,cstr K,uint64 Min,uint64 Max){int64 N=0;return MdoExtensionNumber(O,K,&N)&&N>=0&&(uint64)N>=Min&&(uint64)N<=Max;}
static bool LmText(const xvalue* O,cstr K,size_t Max){cstr S=MdoExtensionText(O,K,Max);return S&&xrtStrTrim(xrtStrView(S)).Size;}
static bool LmHex(cstr S,size_t N){if(!S||strlen(S)!=N)return false;for(size_t I=0;I<N;I++)if(!strchr("0123456789abcdef",S[I]))return false;return true;}
static bool LmId(cstr S){return S&&strlen(S)<=64&&MdoExtensionIdentifier(S,64);}
static bool LmHost(cstr S){
 size_t N=S?strlen(S):0;if(N<3||N>253||!strchr(S,'.')||strstr(S,"..")||S[0]=='.'||S[N-1]=='.')return false;
 bool Letter=false;for(size_t I=0;I<N;I++){unsigned char C=S[I];if(C>='a'&&C<='z')Letter=true;else if(!((C>='0'&&C<='9')||C=='.'||C=='-'))return false;}
 return Letter&&strcmp(S,"localhost.localdomain")&&(N<6||strcmp(S+N-6,".local"));
}
static bool LmUrl(cstr S){
 if(!S||strlen(S)>2048||strncmp(S,"https://",8)||strpbrk(S,"?#@\\"))return false;
 cstr P=S+8;size_t N=strcspn(P,"/");char Host[254];if(N>=sizeof(Host))return false;memcpy(Host,P,N);Host[N]=0;
 if(!LmHost(Host))return false;
 for(;*P;P++)if((unsigned char)*P<=32||(unsigned char)*P>=127)return false;
 return true;
}
static bool LmLocale(cstr S){size_t N=S?strlen(S):0;if(N<2||N>35||S[0]=='-'||S[N-1]=='-'||strstr(S,"--"))return false;for(size_t I=0;I<N;I++)if(!((S[I]>='a'&&S[I]<='z')||(S[I]>='A'&&S[I]<='Z')||(S[I]>='0'&&S[I]<='9')||S[I]=='-'))return false;return true;}
static bool LmLabels(const xvalue* Labels,size_t Max){
 if(xrtValueType(Labels)!=XVALUE_OBJECT||xrtValueCount(Labels)<3||xrtValueCount(Labels)>32)return false;
 const cstr Required[]={"zh-CN","en-US","ru-RU"};for(size_t I=0;I<3;I++)if(!LmText(Labels,Required[I],Max))return false;
 for(size_t I=0;I<xrtValueCount(Labels);I++){xstrview K={0};xrtValueObjectAt(Labels,I,&K);char Key[36];if(K.Size>=sizeof(Key))return false;memcpy(Key,K.Data,K.Size);Key[K.Size]=0;if(!LmLocale(Key)||!LmText(Labels,Key,Max))return false;}return true;
}
static const xvalue* LmFind(const xvalue* Rows,cstr Id){for(size_t I=0;Id&&I<xrtValueCount(Rows);I++){const xvalue* R=xrtValueArrayGet(Rows,I);cstr S=MdoExtensionText(R,"id",64);if(S&&!strcmp(S,Id))return R;}return NULL;}
static bool LmRows(const xvalue* A,size_t Max){
 if(xrtValueType(A)!=XVALUE_ARRAY||!xrtValueCount(A)||xrtValueCount(A)>Max)return false;
 for(size_t I=0;I<xrtValueCount(A);I++){const xvalue* R=xrtValueArrayGet(A,I);cstr Id=MdoExtensionText(R,"id",64);if(!LmId(Id))return false;for(size_t J=0;J<I;J++){cstr Prior=MdoExtensionText(xrtValueArrayGet(A,J),"id",64);if(Prior&&!strcmp(Id,Prior))return false;}}return true;
}
static bool LmOneOf(cstr S,cstr Choices){if(!S)return false;for(cstr P=Choices;*P;){size_t N=strcspn(P," ");if(strlen(S)==N&&!memcmp(P,S,N))return true;P+=N;if(*P)P++;}return false;}
static bool LmStrings(const xvalue* A,size_t Min,size_t Max,cstr Choices){
 if(xrtValueType(A)!=XVALUE_ARRAY||xrtValueCount(A)<Min||xrtValueCount(A)>Max)return false;
 for(size_t I=0;I<xrtValueCount(A);I++){xstrview S={0};if(!xrtValueGetString(xrtValueArrayGet(A,I),&S)||!S.Size||S.Size>64||memchr(S.Data,0,S.Size))return false;
  if(Choices?!LmOneOf(S.Data,Choices):!LmId(S.Data))return false;
  for(size_t J=0;J<I;J++)if(xrtValueEqual(xrtValueArrayGet(A,J),xrtValueArrayGet(A,I)))return false;}return true;
}
static bool MdLocalModelValidate(const xvalue* D,const xvalue* Manifest,char Error[512]){
 cstr Why="Invalid local model recipe";
 if(!LmFields(D,"schema default_locale i18n license upstream tags weights runtimes profiles")||!MdoExtensionText(D,"schema",64)||strcmp(MdoExtensionText(D,"schema",64),"mdo.local-model.v1"))goto bad;
 const xvalue* Languages=xrtValueObjectGet(D,XRT_STR_LITERAL("i18n"));cstr Locale=MdoExtensionText(D,"default_locale",35);
 if(!LmLocale(Locale)||xrtValueType(Languages)!=XVALUE_OBJECT||xrtValueCount(Languages)<3||xrtValueCount(Languages)>32||!xrtValueObjectHas(Languages,xrtStrView(Locale))){Why="Declare default_locale and registered content translations";goto bad;}
 const cstr Required[]={"zh-CN","en-US","ru-RU"};for(size_t I=0;I<3;I++)if(!xrtValueObjectHas(Languages,xrtStrView(Required[I]))){Why="Provide Chinese, English and Russian model text";goto bad;}
 for(size_t I=0;I<xrtValueCount(Languages);I++){xstrview K={0};const xvalue* T=xrtValueObjectAt(Languages,I,&K);char Key[36];if(K.Size>=sizeof(Key))goto bad;memcpy(Key,K.Data,K.Size);Key[K.Size]=0;
  if(!LmLocale(Key)||!LmFields(T,"name description")||!LmText(T,"name",240)||!LmText(T,"description",8000)){Why="Translations contain only nonempty name and description";goto bad;}}
 const xvalue* License=xrtValueObjectGet(D,XRT_STR_LITERAL("license")),*Upstream=xrtValueObjectGet(D,XRT_STR_LITERAL("upstream"));
 cstr Revision=MdoExtensionText(Upstream,"revision",64);
 if(!LmFields(License,"id url")||!LmText(License,"id",128)||!LmUrl(MdoExtensionText(License,"url",2048))||!LmFields(Upstream,"url revision")||!LmUrl(MdoExtensionText(Upstream,"url",2048))||(!LmHex(Revision,40)&&!LmHex(Revision,64))){Why="Provide license, HTTPS upstream and a fixed commit revision";goto bad;}
 const xvalue* Tags=xrtValueObjectGet(D,XRT_STR_LITERAL("tags"));
 if(Tags){if(xrtValueType(Tags)!=XVALUE_ARRAY||xrtValueCount(Tags)>16)goto bad;for(size_t I=0;I<xrtValueCount(Tags);I++){const xvalue* T=xrtValueArrayGet(Tags,I);cstr Id=MdoExtensionText(T,"id",64);if(!LmFields(T,"id label")||!LmId(Id)||!LmLabels(xrtValueObjectGet(T,XRT_STR_LITERAL("label")),64))goto bad;for(size_t J=0;J<I;J++)if(!strcmp(Id,MdoExtensionText(xrtValueArrayGet(Tags,J),"id",64)))goto bad;}}
 const xvalue* Weights=xrtValueObjectGet(D,XRT_STR_LITERAL("weights")),*Runtimes=xrtValueObjectGet(D,XRT_STR_LITERAL("runtimes")),*Profiles=xrtValueObjectGet(D,XRT_STR_LITERAL("profiles"));
 if(!LmRows(Weights,32)||!LmRows(Runtimes,32)||!LmRows(Profiles,64)){Why="Declare unique weight, runtime and deployment profile IDs";goto bad;}
 for(size_t I=0;I<xrtValueCount(Weights);I++){
  const xvalue* W=xrtValueArrayGet(Weights,I),*Files=xrtValueObjectGet(W,XRT_STR_LITERAL("files"));
  if(!LmFields(W,"id format quantization files")||!LmOneOf(MdoExtensionText(W,"format",32),"gguf")||!LmText(W,"quantization",64)||!LmRows(Files,128)){Why="Declare GGUF quantization and explicit file identities";goto bad;}
  bool HasWeight=false;uint64 Total=0;
  for(size_t J=0;J<xrtValueCount(Files);J++){
   const xvalue* F=xrtValueArrayGet(Files,J);cstr Path=MdoExtensionText(F,"path",4096),Role=MdoExtensionText(F,"role",32);
   if(!LmFields(F,"id path role bytes sha256 urls redirect_hosts")||!MdoExtensionRelativePath(Path)||!LmOneOf(Role,"weights mmproj template tokenizer config")||!LmNumber(F,"bytes",1,MD_LOCAL_ASSET_BYTES)||!LmHex(MdoExtensionText(F,"sha256",64),64)){Why="Files require safe paths, roles, byte sizes and SHA-256";goto bad;}
   for(size_t K=0;K<J;K++){cstr Prior=MdoExtensionText(xrtValueArrayGet(Files,K),"path",4096);char* A=MdoExtensionPathKey(Prior),*B=MdoExtensionPathKey(Path);bool Same=A&&B&&!strcmp(A,B);xrtFree(A);xrtFree(B);if(Same){Why="Weight file paths must be unique on all platforms";goto bad;}}
   if(!strcmp(Role,"weights"))HasWeight=true;
   int64 Bytes=0;MdoExtensionNumber(F,"bytes",&Bytes);if((uint64)Bytes>MD_LOCAL_ASSET_BYTES-Total)goto bad;Total+=(uint64)Bytes;
   const xvalue* Urls=xrtValueObjectGet(F,XRT_STR_LITERAL("urls")),*Hosts=xrtValueObjectGet(F,XRT_STR_LITERAL("redirect_hosts"));
   if(xrtValueType(Urls)!=XVALUE_ARRAY||!xrtValueCount(Urls)||xrtValueCount(Urls)>8){Why="Each file requires public HTTPS sources";goto bad;}
   for(size_t K=0;K<xrtValueCount(Urls);K++){xstrview Url={0};if(!xrtValueGetString(xrtValueArrayGet(Urls,K),&Url)||memchr(Url.Data,0,Url.Size)||!LmUrl(Url.Data)){Why="Sources must be canonical HTTPS URLs without credentials, queries or fragments";goto bad;}
    if(!strncmp(Url.Data,"https://huggingface.co/",23)){char* Pin=xrtFormat("/resolve/%s/",Revision);bool Pinned=Pin&&strstr(Url.Data,Pin);xrtFree(Pin);if(!Pinned){Why="Hugging Face sources must resolve the declared commit revision";goto bad;}}}
   if(Hosts){if(xrtValueType(Hosts)!=XVALUE_ARRAY||xrtValueCount(Hosts)>8)goto bad;for(size_t K=0;K<xrtValueCount(Hosts);K++){xstrview Host={0};if(!xrtValueGetString(xrtValueArrayGet(Hosts,K),&Host)||memchr(Host.Data,0,Host.Size)||!LmHost(Host.Data)){Why="Redirect hosts must be exact DNS names";goto bad;}}}
  }
  if(!HasWeight){Why="Each weight variant requires model weight files";goto bad;}
 }
 for(size_t I=0;I<xrtValueCount(Runtimes);I++){
  const xvalue* R=xrtValueArrayGet(Runtimes,I);cstr Ref=MdoExtensionText(R,"ref",160),Kind=MdoExtensionRefKind(Ref),Platform=MdoExtensionText(R,"platform",32);
  if(!LmFields(R,"id ref version_id sha256 engine build platform backend min_os cpu_features min_driver")||!Kind||strcmp(Kind,"external-tools")||(!MdoExtensionText(R,"version_id",32)||strlen(MdoExtensionText(R,"version_id",32))!=32||!MdoExtensionDirectoryId(MdoExtensionText(R,"version_id",32)))||!LmHex(MdoExtensionText(R,"sha256",64),64)||!LmOneOf(MdoExtensionText(R,"engine",32),"llama.cpp strata")||!LmText(R,"build",128)||!MdoExtensionPlatform(Platform)||!strcmp(Platform,"any")||!MdoExtensionSupportsPlatform(Manifest,Platform)||!LmOneOf(MdoExtensionText(R,"backend",32),"cpu cuda vulkan metal rocm sycl")||!LmText(R,"min_os",128)){Why="Pin an external runtime version, hash, engine, platform and backend";goto bad;}
  if(xrtValueObjectHas(R,XRT_STR_LITERAL("cpu_features"))&&!LmStrings(xrtValueObjectGet(R,XRT_STR_LITERAL("cpu_features")),0,32,NULL))goto bad;
  if(xrtValueObjectHas(R,XRT_STR_LITERAL("min_driver"))&&!LmText(R,"min_driver",128))goto bad;
 }
 for(size_t I=0;I<xrtValueCount(Profiles);I++){
  const xvalue* P=xrtValueArrayGet(Profiles,I),*W=LmFind(Weights,MdoExtensionText(P,"weight",64)),*R=LmFind(Runtimes,MdoExtensionText(P,"runtime",64));
  const xvalue* Needs=xrtValueObjectGet(P,XRT_STR_LITERAL("requirements")),*Reasoning=xrtValueObjectGet(P,XRT_STR_LITERAL("reasoning"));int64 Ctx=0,Out=0,Gpu=0;
  if(!LmFields(P,"id label weight runtime context_tokens max_output_tokens output_tokens parallel gpu_layers capabilities reasoning requirements")||!LmLabels(xrtValueObjectGet(P,XRT_STR_LITERAL("label")),240)||!W||!R||!LmNumber(P,"context_tokens",512,UINT32_MAX)||!LmNumber(P,"max_output_tokens",1,UINT32_MAX)||!LmNumber(P,"output_tokens",1,UINT32_MAX)||!LmNumber(P,"parallel",1,64)||!MdoExtensionNumber(P,"gpu_layers",&Gpu)||Gpu< -1||Gpu>100000||!LmStrings(xrtValueObjectGet(P,XRT_STR_LITERAL("capabilities")),1,8,"text tools vision reasoning")){Why="Deployment profiles require known files/runtime and bounded inference settings";goto bad;}
  MdoExtensionNumber(P,"context_tokens",&Ctx);MdoExtensionNumber(P,"max_output_tokens",&Out);int64 Budget=0;MdoExtensionNumber(P,"output_tokens",&Budget);if(Out>=Ctx||Budget>Out){Why="Output budget must fit the effective deployment context and maximum output";goto bad;}
  cstr K=MdoExtensionText(Reasoning,"kind",32);if(!LmFields(Reasoning,"kind efforts default")||!LmOneOf(K,"none effort toggle budget adaptive provider-default")){Why="Declare reasoning capabilities without guessing effort levels";goto bad;}
  if(!strcmp(K,"effort")){const xvalue* Efforts=xrtValueObjectGet(Reasoning,XRT_STR_LITERAL("efforts"));if(!LmStrings(Efforts,1,7,"none minimal low medium high xhigh max"))goto bad;cstr Def=MdoExtensionText(Reasoning,"default",16);if(Def){bool Found=false;for(size_t J=0;J<xrtValueCount(Efforts);J++){xstrview E={0};xrtValueGetString(xrtValueArrayGet(Efforts,J),&E);if(!strcmp(Def,E.Data))Found=true;}if(!Found)goto bad;}}
  else if(xrtValueCount(Reasoning)!=1)goto bad;
  if(!LmFields(Needs,"ram_bytes vram_bytes disk_bytes margin_bytes basis source_url checked_at notes")||!LmNumber(Needs,"ram_bytes",1,MD_LOCAL_ASSET_BYTES)||!LmNumber(Needs,"vram_bytes",0,MD_LOCAL_ASSET_BYTES)||!LmNumber(Needs,"disk_bytes",1,MD_LOCAL_ASSET_BYTES)||!LmNumber(Needs,"margin_bytes",0,MD_LOCAL_ASSET_BYTES)||!LmOneOf(MdoExtensionText(Needs,"basis",32),"estimate measured")||!LmUrl(MdoExtensionText(Needs,"source_url",2048))){Why="Resource requirements need byte counts, estimate/measured basis and a source";goto bad;}
  cstr Date=MdoExtensionText(Needs,"checked_at",10);if(!Date||strlen(Date)!=10||Date[4]!='-'||Date[7]!='-')goto bad;for(size_t J=0;J<10;J++)if(J!=4&&J!=7&&(Date[J]<'0'||Date[J]>'9'))goto bad;
  int Year=(Date[0]-'0')*1000+(Date[1]-'0')*100+(Date[2]-'0')*10+Date[3]-'0',Month=(Date[5]-'0')*10+Date[6]-'0',Day=(Date[8]-'0')*10+Date[9]-'0';
  const int Days[]={31,28,31,30,31,30,31,31,30,31,30,31};if(Year<1970||Month<1||Month>12||Day<1||Day>Days[Month-1]+(Month==2&&Year%4==0&&(Year%100!=0||Year%400==0)))goto bad;
  if(xrtValueObjectHas(Needs,XRT_STR_LITERAL("notes"))&&!LmLabels(xrtValueObjectGet(Needs,XRT_STR_LITERAL("notes")),2000))goto bad;
  if(!strcmp(MdoExtensionText(Needs,"basis",32),"measured")&&!xrtValueObjectHas(Needs,XRT_STR_LITERAL("notes"))){Why="Measured requirements need multilingual notes describing the tested configuration";goto bad;}
  uint64 Total=0;const xvalue* Fs=xrtValueObjectGet(W,XRT_STR_LITERAL("files"));for(size_t J=0;J<xrtValueCount(Fs);J++){int64 N=0;MdoExtensionNumber(xrtValueArrayGet(Fs,J),"bytes",&N);Total+=(uint64)N;}int64 Disk=0;MdoExtensionNumber(Needs,"disk_bytes",&Disk);if((uint64)Disk<Total){Why="Required disk space cannot be smaller than the selected files";goto bad;}
 }
 return true;
bad:if(Error)snprintf(Error,512,"%s",Why);return false;
}
#endif
