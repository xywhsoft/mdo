#include "../../include/mdo/android_tools.h"
#include "../../include/mdo/extensions.h"
#include "../../include/mdo/extension_manifest.h"
#include "../../include/mdo/extension_files.h"
#include "../../include/mdo/distribution.h"

static struct {xvalue* Index;char* Native;char* Warning;} MdoAndroid;
static cstr AndroidText(const xvalue* V,cstr K,size_t N){return MdoExtensionText(V,K,N);}
static bool AndroidSet(xvalue* V,cstr K,cstr S){return S&&xrtValueObjectSetNew(V,xrtStrView(K),xrtValueString(xrtStrView(S)));}
static char* AndroidParent(cstr S){cstr Slash=S?strrchr(S,'/'):NULL;return Slash?xrtStrDupN(S,(size_t)(Slash-S)):NULL;}
static bool AndroidFail(char Error[512],cstr S){if(Error)snprintf(Error,512,"%s",S);return false;}
static bool AndroidHash(xfile File,char Hash[65]){
    xsha256 Sha;xrtSha256Init(&Sha);uint8 B[65536],Digest[32];bool Ok=File!=NULL;
    while(Ok){size_t N=0;Ok=xrtRead(File,B,sizeof(B),&N);if(!Ok||!N)break;Ok=xrtSha256Update(&Sha,B,N);}
    Ok=Ok&&xrtSha256Final(&Sha,Digest);if(Ok)for(size_t I=0;I<32;I++)snprintf(Hash+2*I,3,"%02x",Digest[I]);return Ok;
}
static bool AndroidWrite(const void* B,size_t N,void* U){return xrtWriteFull((xfile)U,B,N,NULL);}
typedef struct AndroidTextBuffer {char* Text;size_t At;} AndroidTextBuffer;
static bool AndroidRead(const void* B,size_t N,void* U){AndroidTextBuffer* T=U;memcpy(T->Text+T->At,B,N);T->At+=N;return true;}
static xvalue* AndroidEntryJson(MdoZip* Zip,cstr Path){
    int64 I=MdoZipFind(Zip,Path);if(I<0||Zip->Entries[I].Bytes>1048576)return NULL;
    AndroidTextBuffer T={0};T.Text=xrtMalloc((size_t)Zip->Entries[I].Bytes+1);if(!T.Text)return NULL;
    bool Ok=MdoZipEntryRead(Zip,(size_t)I,AndroidRead,&T,NULL,NULL);T.Text[T.At]=0;
    xjsonreadconfig C;xrtJsonReadConfigInit(&C);C.MaxInputBytes=1048576;C.MaxValues=60000;C.MaxDepth=16;C.MaxStringBytes=4096;
    xvalue* V=Ok?xrtJsonRead(xrtStrViewN(T.Text,T.At),&C):NULL;xrtFree(T.Text);return V;
}
bool MdoAndroidToolsActive(void){return MdoAndroid.Index!=NULL;}
cstr MdoAndroidToolsWarning(void){return MdoAndroid.Warning;}
void MdoAndroidToolsUnit(void){xrtValueRelease(MdoAndroid.Index);xrtFree(MdoAndroid.Native);xrtFree(MdoAndroid.Warning);memset(&MdoAndroid,0,sizeof(MdoAndroid));}
static bool AndroidIndexValid(const xvalue* V){
    const xvalue* Tools=xrtValueObjectGet(V,XRT_STR_LITERAL("extensions")),*Files=xrtValueObjectGet(V,XRT_STR_LITERAL("files"));
    if(xrtValueType(Tools)!=XVALUE_ARRAY||xrtValueCount(Tools)>64||xrtValueType(Files)!=XVALUE_ARRAY||xrtValueCount(Files)>4096)return false;
    for(size_t I=0;I<xrtValueCount(Tools);I++){
        const xvalue* T=xrtValueArrayGet(Tools,I);cstr Id=AndroidText(T,"id",96),Asset=AndroidText(T,"asset",4096),Sha=AndroidText(T,"sha256",64);int64 Bytes=0;
        char* Expected=Id?xrtFormat("assets/mdo-extensions/%s.zip",Id):NULL;
        bool Ok=MdoExtensionDirectoryId(Id)&&Asset&&Expected&&!strcmp(Asset,Expected)&&Sha&&strlen(Sha)==64&&MdoExtensionNumber(T,"bytes",&Bytes)&&Bytes>0&&Bytes<=MDO_EXTENSION_ARCHIVE_BYTES;xrtFree(Expected);if(!Ok)return false;
        for(size_t J=0;J<I;J++)if(!strcmp(Id,AndroidText(xrtValueArrayGet(Tools,J),"id",96)))return false;
    }
    for(size_t I=0;I<xrtValueCount(Files);I++){
        const xvalue* F=xrtValueArrayGet(Files,I);cstr Id=AndroidText(F,"tool",96),Path=AndroidText(F,"path",4096),Native=AndroidText(F,"native",255),Sha=AndroidText(F,"sha256",64);
        if(!MdoExtensionDirectoryId(Id)||!MdoExtensionRelativePath(Path)||!Native||strchr(Native,'/')||strchr(Native,'\\')||!MdoExtensionRelativePath(Native)||!Sha||strlen(Sha)!=64)return false;
        bool Found=false;for(size_t J=0;J<xrtValueCount(Tools);J++)if(!strcmp(Id,AndroidText(xrtValueArrayGet(Tools,J),"id",96)))Found=true;if(!Found)return false;
        for(size_t J=0;J<I;J++){const xvalue* Old=xrtValueArrayGet(Files,J);if(!strcmp(Id,AndroidText(Old,"tool",96))&&!strcmp(Path,AndroidText(Old,"path",4096)))return false;}
    }return true;
}
/* This function also serves the native fixture: no Android Java VM or second
 * application instance is required to exercise the actual installer. */
static bool MdoAndroidToolsLoad(cstr Apk,cstr Native,char Error[512]){
    MdoAndroidToolsUnit();xfileoptions Options;xrtFileOptionsInit(&Options);Options.Flags=XFILE_READ;
    xfile Input=xrtFileOpen(Apk,&Options);MdoZip Z={0};bool Ok=Input&&MdoZipOpenContainer(&Z,Input,true);
    if(!Ok&&Error)snprintf(Error,512,"Cannot read installed APK ZIP: %.450s",Z.Error[0]?Z.Error:"Cannot open the installed package");
    xvalue* Index=Ok?AndroidEntryJson(&Z,"assets/mdo-runtime.json"):NULL;
    if(Ok){Ok=Index&&AndroidIndexValid(Index);if(!Ok)AndroidFail(Error,"APK external-tool index validation failed");}bool Missing=false;char* Text=Ok?MdoExtensionRead("data/extensions/android-builtins.json",true,131072,&Missing):NULL;
    xvalue* Receipts=Text?xrtJsonParse(xrtStrView(Text)):Missing?xrtValueObject():NULL;xrtFree(Text);if(Ok&&(!Receipts||xrtValueType(Receipts)!=XVALUE_OBJECT))AndroidFail(Error,"APK external-tool installation receipts cannot be read");Ok=Ok&&Receipts&&xrtValueType(Receipts)==XVALUE_OBJECT;
    const xvalue* Tools=xrtValueObjectGet(Index,XRT_STR_LITERAL("extensions"));
    for(size_t I=0;Ok&&I<xrtValueCount(Tools);I++){
        const xvalue* T=xrtValueArrayGet(Tools,I);cstr Id=AndroidText(T,"id",96),Sha=AndroidText(T,"sha256",64),Asset=AndroidText(T,"asset",4096);
        char* Root=MdoExtensionsRoot("external-tools",Id);bool Exists=false;xfileinfo Info={0};Ok=Root&&MdoHomeExternalStat(Root,&Exists,&Info);xrtFree(Root);
        xvalue* Before=Exists?MdoExtensionsRead("external-tools",Id,Error):NULL;const xvalue* Receipt=xrtValueObjectGet(Receipts,xrtStrView(Id));
        cstr Source=AndroidText(Before,"source",16),OldSha=AndroidText(xrtValueObjectGet(Before,XRT_STR_LITERAL("installation")),"sha256",64);
        bool First=!Exists&&!Receipt,Update=Exists&&Source&&!strcmp(Source,"builtin")&&(!OldSha||strcmp(Sha,OldSha));
        if(Ok&&(First||Update)){
            Ok=!Update||MdoExtensionsVerify("external-tools",Id,NULL,Error);int64 Entry=MdoZipFind(&Z,Asset),Bytes=0;MdoExtensionNumber(T,"bytes",&Bytes);
            char* Task=xrtXidMakeString(),*Relative=Task?xrtFormat("data/extension-tasks/%s/android.zip",Task):NULL;
            xfile Out=Ok&&Entry>=0&&Bytes>0&&Z.Entries[Entry].Bytes==(uint64)Bytes&&Relative?MdoHomeOpenWrite(Relative,XFILE_WRITE|XFILE_CREATE|XFILE_EXCLUSIVE):NULL;char Actual[65]={0};bool Created=Out!=NULL;
            Ok=Out&&MdoZipEntryRead(&Z,(size_t)Entry,AndroidWrite,Out,NULL,Actual)&&!strcmp(Actual,Sha);if(Out&&!xrtClose(Out))Ok=false;if(!Ok)AndroidFail(Error,"Cannot stage the APK external-tool ZIP or its SHA-256 does not match");
            xvalue* Credits=xrtValueObject(),*Result=NULL;Ok=Ok&&Credits&&AndroidSet(Credits,"source","builtin")&&AndroidSet(Credits,"sha256",Sha)&&MdoExtensionsInstall("external-tools",Id,Relative,Credits,true,NULL,&Result,Error);
            xrtValueRelease(Credits);xrtValueRelease(Result);if(Created)(void)MdoHomeRemove(Relative,false);char* Parent=Relative?xrtPathParent(Relative):NULL;if(Parent)(void)MdoHomeRemoveEmptyDirectory(Parent);xrtFree(Parent);xrtFree(Relative);xrtFree(Task);
        }
        if(Ok)Ok=AndroidSet(Receipts,Id,Sha);
        xrtValueRelease(Before);
    }
    Text=Ok?xrtJsonStringify(Receipts,false,NULL):NULL;Ok=Text&&MdoHomeAtomicWrite("data/extensions/android-builtins.json",Text,strlen(Text),false);xrtFree(Text);xrtValueRelease(Receipts);
    if(Ok){MdoAndroid.Index=Index;Index=NULL;MdoAndroid.Native=xrtStrDup(Native);Ok=MdoAndroid.Native!=NULL;}
    if(!Ok&&Error&&!Error[0])snprintf(Error,512,"%s",Z.Error[0]?Z.Error:!Index&&!MdoAndroid.Index?"Cannot read a valid APK external-tool index":"Cannot install APK tool directories or save their receipts");
    xrtValueRelease(Index);MdoZipClose(&Z);xrtClose(Input);if(!Ok){MdoAndroidToolsUnit();return false;}return true;
}
bool MdoAndroidToolsInit(char Error[512]){
#if defined(__ANDROID__)
    char* Apk=xrtEnvGet("MDO_APK_PATH"),*Native=xrtEnvGet("MDO_NATIVE_DIR");bool Ok=Apk&&Native&&MdoAndroidToolsLoad(Apk,Native,Error);xrtFree(Apk);xrtFree(Native);if(!Ok)MdoAndroid.Warning=xrtStrDup(Error[0]?Error:"The installed APK has no valid external-tool index");return Ok;
#else
    (void)Error;return true;
#endif
}
static const xvalue* AndroidFile(cstr Id,cstr Path);
char* MdoAndroidToolProgram(cstr Id,cstr Path){if(!MdoExtensionDirectoryId(Id)||!MdoExtensionRelativePath(Path)||!AndroidFile(Id,Path))return NULL;char* R=xrtFormat("data/external-tools/android/%s/%s",Id,Path),*Native=R?MdoHomeExternalPath(R):NULL;xrtFree(R);return Native;}
bool MdoAndroidToolOwnedLink(cstr Relative,cstr NativeTarget){
    char* Parent=AndroidParent(Relative);cstr Leaf=strrchr(Relative,'/');xroot R=Parent?MdoHomeOpenStorageDirectory(Parent):NULL;char* Target=R&&Leaf?xrtRootLinkRead(R,Leaf+1):NULL;
    bool Ok=Target&&NativeTarget&&!strcmp(Target,NativeTarget);xrtFree(Target);if(R)xrtRootClose(R);xrtFree(Parent);return Ok;
}
bool MdoAndroidToolLink(cstr Relative,cstr Target){
    if(!MdoExtensionRelativePath(Relative)||strncmp(Relative,"data/external-tools/",20)||!Target)return false;
    char* Parent=AndroidParent(Relative);xroot R=Parent?MdoHomeOpenStorageDirectory(Parent):NULL;xrtFree(Parent);if(!R)return false;xrtRootClose(R);
    if(MdoAndroidToolOwnedLink(Relative,Target))return true;
    bool Exists=false;
    xfileinfo Info;
    if(!MdoHomeExternalStat(Relative,&Exists,&Info)||(Exists&&Info.Type!=XFILE_TYPE_LINK))return false;
    char* Suffix=xrtXidMakeString(),*Pending=Suffix?xrtFormat("%s.%s.pending",Relative,Suffix):NULL,*P=Pending?MdoHomeExternalPath(Pending):NULL,*Dest=MdoHomeExternalPath(Relative);
    bool Ok=P&&Dest&&xrtLinkCreate(Target,P,false)&&xrtPathRename(P,Dest,true);xerror* Cause=!Ok?xrtErrorRef(xrtGetError()):NULL;if(Pending)(void)MdoHomeRemove(Pending,false);xrtFree(P);xrtFree(Pending);xrtFree(Dest);xrtFree(Suffix);if(Cause){xrtSetError(Cause);xrtErrorFree(Cause);}return Ok;
}
static const xvalue* AndroidFile(cstr Id,cstr Path){const xvalue* Files=xrtValueObjectGet(MdoAndroid.Index,XRT_STR_LITERAL("files"));for(size_t I=0;I<xrtValueCount(Files);I++){const xvalue* F=xrtValueArrayGet(Files,I);if(!strcmp(Id,AndroidText(F,"tool",96))&&!strcmp(Path,AndroidText(F,"path",4096)))return F;}return NULL;}
bool MdoAndroidToolPrepare(cstr Id,char Error[512]){
    xvalue* Meta=MdoExtensionsRead("external-tools",Id,Error);cstr Source=AndroidText(Meta,"source",16);bool Ok=Source&&!strcmp(Source,"builtin");xvalue* Files=Ok?MdoExtensionsFiles("external-tools",Id,NULL,Error):NULL;Ok=Ok&&Files;
    for(size_t I=0;Ok&&I<xrtValueCount(Files);I++){
        const xvalue* F=xrtValueArrayGet(Files,I);bool Directory=false;xrtValueGetBool(xrtValueObjectGet(F,XRT_STR_LITERAL("directory")),&Directory);cstr Path=AndroidText(F,"path",4096);
        if(Directory){char* P=xrtFormat("data/external-tools/android/%s/%s",Id,Path);xroot R=P?MdoHomeOpenStorageDirectory(P):NULL;Ok=R!=NULL;if(R)xrtRootClose(R);xrtFree(P);continue;}
        const xvalue* Native=AndroidFile(Id,Path);char* Relative=xrtFormat("external-tools/%s/%s",Id,Path),*Target=NULL;char Hash[65];
        if(Native){xfile SourceFile=MdoHomeOpenRead(Relative);Ok=AndroidHash(SourceFile,Hash)&&!strcmp(Hash,AndroidText(Native,"sha256",64));xrtClose(SourceFile);
            Target=xrtPathJoin(MdoAndroid.Native,AndroidText(Native,"native",255));xfileoptions O;xrtFileOptionsInit(&O);O.Flags=XFILE_READ;SourceFile=Target?xrtFileOpen(Target,&O):NULL;Ok=Ok&&AndroidHash(SourceFile,Hash)&&!strcmp(Hash,AndroidText(Native,"sha256",64));xrtClose(SourceFile);
        }else Target=MdoHomeExternalPath(Relative);
        char* Link=xrtFormat("data/external-tools/android/%s/%s",Id,Path);bool Verified=Ok;Ok=Ok&&Target&&Link&&MdoAndroidToolLink(Link,Target);if(!Ok&&Error)snprintf(Error,512,"%s: %.350s",Verified?"Cannot create derived Android binding":"APK native checksum mismatch",Path);xrtFree(Link);xrtFree(Target);xrtFree(Relative);
    }
    const xvalue* NativeFiles=xrtValueObjectGet(MdoAndroid.Index,XRT_STR_LITERAL("files"));
    for(size_t I=0;Ok&&I<xrtValueCount(NativeFiles);I++){const xvalue* F=xrtValueArrayGet(NativeFiles,I);bool Alias=false;xrtValueGetBool(xrtValueObjectGet(F,XRT_STR_LITERAL("alias")),&Alias);if(!Alias||strcmp(Id,AndroidText(F,"tool",96)))continue;
        char* Link=xrtFormat("data/external-tools/android/%s/%s",Id,AndroidText(F,"path",4096)),*Target=xrtPathJoin(MdoAndroid.Native,AndroidText(F,"native",255));Ok=Link&&Target&&MdoAndroidToolLink(Link,Target);xrtFree(Link);xrtFree(Target);
    }
    xrtValueRelease(Files);xrtValueRelease(Meta);if(!Ok&&Error&&!Error[0])AndroidFail(Error,"This Android tool must match a program installed by the full APK");return Ok;
}
char* MdoAndroidForwarder(void){return MdoAndroid.Native?xrtPathJoin(MdoAndroid.Native,"libmdo_cli_forwarder.so"):NULL;}
bool MdoAndroidToolsEnvironment(const xvalue* Items){
    if(!MdoAndroidToolsActive())return true;
    MdoHomeSnapshot H={0}
    ;
    H.Size=sizeof(H);
    if(!MdoHomeGetSnapshot(&H)||!H.Path||!xrtEnvSet("MDO_CLI_HOME",H.Path))return false;
    const cstr Vars[]={"PYTHONHOME","MDO_PYTHON_HOME","SSL_CERT_FILE","CURL_CA_BUNDLE","GIT_EXEC_PATH","GIT_TEMPLATE_DIR","GIT_SSL_CAINFO","MAGICK_HOME","MAGICK_CONFIGURE_PATH","POPPLER_DATADIR","FONTCONFIG_FILE","FONTCONFIG_PATH"};
    const cstr Tools[]={"python","python","curl","curl","git","git","curl","imagemagick","imagemagick","poppler","poppler","poppler"};
    const cstr Paths[]={".",".","cacert.pem","cacert.pem","libexec/git-core","share/git-core/templates","cacert.pem",".","etc/ImageMagick-7","share/poppler","fonts/fonts.conf","fonts"};
    bool Ok=true;for(size_t I=0;I<sizeof(Vars)/sizeof(*Vars);I++){
        bool Found=false;for(size_t J=0;J<xrtValueCount(Items);J++){const xvalue* R=xrtValueArrayGet(Items,J);bool Valid=false,Enabled=false;xrtValueGetBool(xrtValueObjectGet(R,XRT_STR_LITERAL("valid")),&Valid);xrtValueGetBool(xrtValueObjectGet(R,XRT_STR_LITERAL("enabled")),&Enabled);cstr Id=AndroidText(R,"id",96);if(Id&&!strcmp(Id,Tools[I])&&Valid&&Enabled)Found=true;}
        if(!Found){(void)xrtEnvRemove(Vars[I]);continue;}char* Path=!strcmp(Paths[I],".")?xrtFormat("data/external-tools/android/%s",Tools[I]):xrtFormat("data/external-tools/android/%s/%s",Tools[I],Paths[I]);char* Native=Path?MdoHomeExternalPath(Path):NULL;Ok=Ok&&Native&&xrtEnvSet(Vars[I],Native);xrtFree(Native);xrtFree(Path);
    }return Ok;
}
bool MdoAndroidToolClean(cstr Id){
    if(!MdoExtensionDirectoryId(Id))return false;
    xroot R=MdoHomeOpenStorageDirectory("data/external-tools/android");
    if(!R)return false;
    xfileinfo Info;
    if(!xrtRootStat(R,Id,false,&Info)){bool Missing=xrtErrorKind(xrtGetError())==XERR_NOT_FOUND;
    xrtRootClose(R);
    return Missing;
    }
    xvalue* Queue=xrtValueArray();bool Ok=Queue&&xrtValueArrayAppendNew(Queue,xrtValueString(xrtStrView(Id)));size_t Count=0;
    for(size_t I=0;Ok&&I<xrtValueCount(Queue);I++){xstrview Path;xrtValueGetString(xrtValueArrayGet(Queue,I),&Path);xdir D=xrtRootDirOpen(R,Path.Data,XDIR_STAT);xdirentry E;xdirnext Next=XDIR_NEXT_ERROR;Ok=D!=NULL;
        while(Ok&&(Next=xrtDirNext(D,&E))==XDIR_NEXT_ITEM){char* Child=xrtFormat("%s/%.*s",Path.Data,(int)E.Name.Size,E.Name.Data);Ok=++Count<=MDO_EXTENSION_DIRECTORY_ENTRIES+4096&&Child&&(E.Info.Type==XFILE_TYPE_DIRECTORY?xrtValueArrayAppendNew(Queue,xrtValueString(xrtStrView(Child))):xrtRootRemove(R,Child));xrtFree(Child);}if(Next==XDIR_NEXT_ERROR)Ok=false;if(D)xrtDirClose(D);
    }
    for(size_t I=xrtValueCount(Queue);Ok&&I;I--){xstrview P;xrtValueGetString(xrtValueArrayGet(Queue,I-1),&P);Ok=xrtRootRemove(R,P.Data);}xrtValueRelease(Queue);xrtRootClose(R);return Ok;
}
