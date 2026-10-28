/* Android writable data cannot contain executable programs. This OS-installed
 * launcher execs the verified APK binding, preserving PID/signals/stdio/cwd. */
#define XRT_MODULE_JSON_READ
#define XRT_MODULE_FILE_WHOLE
#define XRT_MODULE_PATH_SYSTEM
#define XRT_MODULE_ENVIRONMENT
#define XRT_IMPLEMENTATION
#include "xrt.h"
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

static cstr Text(const xvalue* V,cstr Key,size_t Limit){xstrview S;return xrtValueGetString(xrtValueObjectGet(V,xrtStrView(Key)),&S)&&S.Size<=Limit&&!memchr(S.Data,0,S.Size)?S.Data:NULL;}
static bool Name(cstr S){size_t N=S?strlen(S):0;if(!N||N>96||S[0]=='.'||S[N-1]=='.'||strstr(S,".."))return false;for(size_t I=0;I<N;I++)if(!((S[I]>='a'&&S[I]<='z')||(S[I]>='A'&&S[I]<='Z')||(S[I]>='0'&&S[I]<='9')||strchr("._+-",S[I])))return false;return true;}
static bool Relative(cstr S){if(!S||!S[0]||S[0]=='/'||strchr(S,'\\')||strchr(S,':')||strstr(S,"//"))return false;for(cstr P=S;*P;){cstr E=strchr(P,'/');size_t N=E?(size_t)(E-P):strlen(P);if(!N||(N==1&&*P=='.')||(N==2&&!memcmp(P,"..",2)))return false;for(size_t I=0;I<N;I++)if((unsigned char)P[I]<32||(unsigned char)P[I]==127)return false;P=E?E+1:P+N;}return true;}
static bool Inside(cstr Root,cstr Path){size_t N=Root?strlen(Root):0;return N&&Path&&!strncmp(Root,Path,N)&&Path[N]=='/';}
int main(int argc,char** argv){
    cstr Failure="Command is unavailable or its index is invalid";char *Home=xrtEnvGet("MDO_CLI_HOME"),*Native=xrtEnvGet("MDO_NATIVE_DIR"),*Command=argc>0?xrtPathName(argv[0]):NULL;
    char* Config=Home?xrtPathJoin(Home,"data/external-tools/state.json"):NULL;size_t Bytes=0;bytes Raw=Config?xrtFileReadAllLimit(Config,1048576,&Bytes):NULL;xvalue* Index=NULL;char **Args=NULL,*Target=NULL,*Real=NULL,*NativeReal=NULL;
    if(argc<1||argc>1024||!Home||!Native||!Name(Command)||!Raw)goto fail;
    xjsonreadconfig C;xrtJsonReadConfigInit(&C);C.MaxInputBytes=1048576;C.MaxDepth=24;C.MaxValues=50000;C.MaxStringBytes=65536;Index=xrtJsonRead(xrtStrViewN((cstr)Raw,Bytes),&C);
    const xvalue* Row=xrtValueObjectGet(xrtValueObjectGet(Index,XRT_STR_LITERAL("commands")),xrtStrView(Command));cstr Id=Text(Row,"tool_id",96),Path=Text(Row,"target",4096);if(!Name(Id)||!Relative(Path))goto fail;
    char Prefix[128];snprintf(Prefix,sizeof(Prefix),"external-tools/%s/",Id);size_t N=strlen(Prefix);if(strncmp(Path,Prefix,N)||!Relative(Path+N))goto fail;
    size_t Capacity=strlen(Home)+strlen(Id)+strlen(Path+N)+40;Target=xrtMalloc(Capacity);if(Target)snprintf(Target,Capacity,"%s/data/external-tools/android/%s/%s",Home,Id,Path+N);Real=Target?xrtPathReal(Target):NULL;NativeReal=xrtPathReal(Native);if(!Inside(NativeReal,Real))goto fail;
    const xvalue* Before=xrtValueObjectGet(Row,XRT_STR_LITERAL("args"));size_t Count=xrtValueCount(Before);if(xrtValueType(Before)!=XVALUE_ARRAY||Count>64||Count+(size_t)argc>1088)goto fail;
    Args=xrtCalloc(Count+(size_t)argc+1,sizeof(char*));if(!Args)goto fail;Args[0]=Target;
    for(size_t I=0;I<Count;I++){xstrview V;if(!xrtValueGetString(xrtValueArrayGet(Before,I),&V)||V.Size>32768||memchr(V.Data,0,V.Size))goto fail;Args[I+1]=(char*)V.Data;}
    for(int I=1;I<argc;I++)Args[Count+(size_t)I]=argv[I];
    char* Depth=xrtEnvGet("MDO_CLI_FORWARD_DEPTH");unsigned D=0;if(Depth){if(strlen(Depth)>1||Depth[0]<'0'||Depth[0]>'3'){xrtFree(Depth);Failure="Recursive command forwarding refused";goto fail;}D=(unsigned)(Depth[0]-'0');}xrtFree(Depth);
    char Next[2]={(char)('1'+D),0};if(!xrtEnvSet("MDO_CLI_FORWARD_DEPTH",Next))goto fail;
    execv(Target,Args);Failure="Cannot execute APK program";
fail:
    fprintf(stderr,"mdo: %s%s%s\n",Failure,Command?": ":"",Command?Command:"");xrtFree(Args);xrtFree(Target);xrtFree(Real);xrtFree(NativeReal);xrtValueRelease(Index);xrtFree(Raw);xrtFree(Config);xrtFree(Home);xrtFree(Native);xrtFree(Command);return errno==ENOENT?127:126;
}
