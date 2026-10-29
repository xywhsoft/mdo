/* Exercise the actual SDK implementation with native files and native locks.
 * Function sections let this small probe link only the selected xwork/XRT
 * features, without any provider, service, copied runtime or account. */
#define XRT_IMPLEMENTATION
#include "xwork-xrt-roots.h"
#include "src/xllm_core.c"
#include "xwork.c"

static unsigned Checks;
#define CHECK(E) do {if(!(E)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#E);return 1;}Checks++;}while(0)
static bool Flag(const xvalue* V,const char* K) {bool B=false;return xrtValueGetBool(xrtValueObjectGet(V,xrtStrView(K)),&B)&&B;}
static uint64_t Number(const xvalue* V,const char* K) {uint64_t N=0;(void)xrtValueGetUInt(xrtValueObjectGet(V,xrtStrView(K)),&N);return N;}
static const char* Text(const xvalue* V,const char* K) {xstrview S={0};return xrtValueGetString(xrtValueObjectGet(V,xrtStrView(K)),&S)?S.Data:"";}
static const xvalue* Stream(const xvalue* V,bool Err) {return xrtValueObjectGet(V,Err?XRT_STR_LITERAL("stderr"):XRT_STR_LITERAL("stdout"));}
static bool SameFile(const char* Path,const void* Expected,size_t N) {
    xfileoptions Options;xrtFileOptionsInit(&Options);Options.Flags=XFILE_READ|XFILE_NOFOLLOW;
    xfile F=xrtFileOpen(Path,&Options);if(!F)return false;
    xfileinfo Info;bool Ok=xrtFileStat(F,&Info)&&Info.Size==N;
    unsigned char Data[4096];const unsigned char* Source=Expected;size_t Pos=0;
    while(Ok&&Pos<N){size_t Got=0,Take=N-Pos>sizeof(Data)?sizeof(Data):N-Pos;Ok=xrtReadAtFull(F,Pos,Data,Take,&Got)&&Got==Take&&!memcmp(Data,Source+Pos,Take);Pos+=Take;}
    xrtClose(F);return Ok;
}

int main(int Argc,char** Argv) {
    if(Argc!=2)return 2;
    xroot Root=xrtRootOpen(Argv[1]);CHECK(Root);
    xwork_process_output_options Opt;xworkProcessOutputOptionsInit(&Opt);Opt.Directory=Root;
    xwork_error Error;xwork_process_output* O=xworkProcessOutputCreate(&Opt,&Error);CHECK(O);
    CHECK(xworkProcessOutputWrite(O,false,"short stdout",12));CHECK(xworkProcessOutputWrite(O,true,"short stderr",12));
    xworkProcessOutputFinish(O);xvalue* V=xworkProcessOutputSnapshot(O,true);CHECK(V&&Flag(V,"output_complete"));
    CHECK(!strcmp(Text(Stream(V,false),"text"),"short stdout"));CHECK(!Text(Stream(V,false),"path")[0]);
    CHECK(!strcmp(Text(Stream(V,true),"text"),"short stderr"));xrtValueRelease(V);xworkProcessOutputDestroy(O);

    unsigned char Data[262144];for(size_t i=0;i<sizeof(Data);i++)Data[i]=(unsigned char)('a'+i%26u);
    O=xworkProcessOutputCreate(&Opt,&Error);CHECK(O);
    for(size_t i=0;i<sizeof(Data);i+=4096u)CHECK(xworkProcessOutputWrite(O,false,Data+i,4096u));
    CHECK(xworkProcessOutputWrite(O,true,"separate error",14u));xworkProcessOutputFinish(O);
    V=xworkProcessOutputSnapshot(O,true);CHECK(V&&Flag(V,"output_complete"));
    const xvalue* Out=Stream(V,false);CHECK(Number(Out,"bytes")==sizeof(Data)&&Number(Out,"saved_bytes")==sizeof(Data));
    CHECK(Flag(Out,"preview_omitted")&&Flag(Out,"complete"));CHECK(!strncmp(Text(Out,"path"),"@results/process/",17u));
    CHECK(SameFile(Text(Out,"native_path"),Data,sizeof(Data)));
    char* Path=xwork__strdup(Text(Out,"native_path"));CHECK(Path);
    xvalue* Again=xworkProcessOutputSnapshot(O,true);char* A=xrtJsonStringify(V,false,NULL);char* B=xrtJsonStringify(Again,false,NULL);
    CHECK(A&&B&&!strcmp(A,B));xrtFree(A);xrtFree(B);xrtValueRelease(Again);xrtValueRelease(V);xworkProcessOutputDestroy(O);
    CHECK(SameFile(Path,Data,sizeof(Data)));free(Path);

    Opt.FilesFromStart=true;O=xworkProcessOutputCreate(&Opt,&Error);CHECK(O);
    V=xworkProcessOutputSnapshot(O,false);CHECK(V&&!Flag(V,"output_complete"));
    Path=xwork__strdup(Text(Stream(V,false),"native_path"));CHECK(Path&&SameFile(Path,"",0));xrtValueRelease(V);
    CHECK(xworkProcessOutputWrite(O,false,"visible before finish",21u));CHECK(SameFile(Path,"visible before finish",21u));
    CHECK(!xworkProcessOutputCollect(Root,1u,1u));CHECK(SameFile(Path,"visible before finish",21u));
    xworkProcessOutputFinish(O);xworkProcessOutputDestroy(O);CHECK(SameFile(Path,"visible before finish",21u));free(Path);

    Opt.MaxBytes=257;O=xworkProcessOutputCreate(&Opt,&Error);CHECK(O);
    CHECK(xworkProcessOutputWrite(O,false,Data,4096u));xworkProcessOutputFinish(O);V=xworkProcessOutputSnapshot(O,true);
    CHECK(V&&!Flag(V,"output_complete")&&!Flag(Stream(V,false),"complete"));CHECK(!strcmp(Text(V,"output_error"),"output_limit"));
    CHECK(Number(Stream(V,false),"bytes")==4096u&&Number(Stream(V,false),"saved_bytes")==257u);
    CHECK(SameFile(Text(Stream(V,false),"native_path"),Data,257u));xrtValueRelease(V);xworkProcessOutputDestroy(O);

    Opt.MaxBytes=XWORK_OUTPUT_MAX;char* Manual=xrtPathJoin(Argv[1],"explicit-output.log");CHECK(Manual);
    Opt.StdoutPath=Manual;O=xworkProcessOutputCreate(&Opt,&Error);CHECK(O);
    unsigned char Binary[]={0,255,1,254,128};CHECK(xworkProcessOutputWrite(O,false,Binary,sizeof(Binary)));
    xworkProcessOutputFinish(O);V=xworkProcessOutputSnapshot(O,true);CHECK(V&&Flag(V,"output_complete"));
    CHECK(SameFile(Manual,Binary,sizeof(Binary)));CHECK(!strcmp(Text(Stream(V,false),"native_path"),Manual));
    A=xrtJsonStringify(V,false,NULL);CHECK(A&&xrtUtf8Valid(xrtStrView(A),NULL));xrtFree(A);xrtValueRelease(V);xworkProcessOutputDestroy(O);
    O=xworkProcessOutputCreate(&Opt,&Error);CHECK(!O&&SameFile(Manual,Binary,sizeof(Binary)));
    char* Partial=xrtPathJoin(Argv[1],"rollback-new-output.log");CHECK(Partial);
    Opt.StdoutPath=Partial;Opt.StderrPath=Manual;
    O=xworkProcessOutputCreate(&Opt,&Error);CHECK(!O&&SameFile(Manual,Binary,sizeof(Binary)));
    xfileinfo Missing;CHECK(!xrtPathStat(Partial,false,&Missing)&&xrtErrorKind(xrtGetError())==XERR_NOT_FOUND);xrtFree(Partial);
    CHECK(xworkProcessOutputCollect(Root,1u,1u));CHECK(SameFile(Manual,Binary,sizeof(Binary)));xrtFree(Manual);
    xrtRootClose(Root);printf("process output storage: PASS (%u checks)\n",Checks);return 0;
}
