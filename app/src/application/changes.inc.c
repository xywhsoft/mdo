/* Git is executed with typed argv and literal paths. Read services never use
 * the user's index as scratch space or enable external diff/textconv hooks. */
#include "../../include/mdo/extension_files.h"
#define GIT_METADATA_LIMIT (4u*1024u*1024u)
#define GIT_FILE_LIMIT 20000u
static bool SdkGitFail(mdo_error_v1* E,cstr Code,cstr Message,unsigned Status){return MdoApplicationError(E,Code,Message,"git",Status,false);}
static uint64 SdkGitNumber(cstr Text){uint64 N=0;while(*Text>='0'&&*Text<='9'){unsigned Digit=(unsigned)(*Text++-'0');if(N>(UINT64_MAX-Digit)/10)return UINT64_MAX;N=N*10+Digit;}return N;}
static void SdkGitChomp(char* Text){size_t N=Text?strlen(Text):0;if(N&&Text[N-1]=='\n')Text[--N]=0;if(N&&Text[N-1]=='\r')Text[--N]=0;}
static bool SdkGitRun(const mdo_sdk_context_v1* C,cstr Root,const cstr* Args,size_t Count,xbytesview Input,size_t Limit,xprocessoutputproc Output,void* User,xprocessresult* R,mdo_error_v1* E){
    const cstr Prefix[]={"--literal-pathspecs","-c","core.quotepath=false","-c","core.fsmonitor=false","-c","color.ui=false","-c","core.pager=cat"};
    cstr Argv[64];size_t At=0;if(Count+sizeof(Prefix)/sizeof(*Prefix)>64)return SdkGitFail(E,"git_arguments_invalid","Too many Git arguments",400);
    for(size_t i=0;i<sizeof(Prefix)/sizeof(*Prefix);i++)Argv[At++]=Prefix[i];for(size_t i=0;i<Count;i++)Argv[At++]=Args[i];
    const xprocessenv Env[]={{"GIT_OPTIONAL_LOCKS","0"},{"GIT_TERMINAL_PROMPT","0"},{"LC_ALL","C"},{"GIT_DIR",NULL},{"GIT_WORK_TREE",NULL},{"GIT_INDEX_FILE",NULL},{"GIT_COMMON_DIR",NULL},{"GIT_OBJECT_DIRECTORY",NULL},{"GIT_ALTERNATE_OBJECT_DIRECTORIES",NULL},{"GIT_CONFIG_COUNT",NULL},{"GIT_CONFIG_PARAMETERS",NULL}};
    xprocessconfig Config;xrtProcessConfigInit(&Config);Config.Program="git";Config.Args=Argv;Config.ArgCount=At;Config.WorkDir=Root;Config.HideWindow=true;Config.Env=Env;Config.EnvCount=sizeof(Env)/sizeof(*Env);
    Config.Stdin.Mode=Input.Data?XPROCESS_IO_PIPE:XPROCESS_IO_NULL;Config.Stdout.Mode=XPROCESS_IO_PIPE;Config.Stderr.Mode=XPROCESS_IO_PIPE;
    xprocessrunoptions Options;xrtProcessRunOptionsInit(&Options);Options.Input=Input;Options.StdoutLimit=Limit;Options.StderrLimit=8192;Options.Overflow=XPROCESS_OVERFLOW_KEEP_FIRST;Options.Output=Output;Options.UserData=User;
    Options.Timeout=C->Deadline?MdoDeadlineWaitMs(C->Deadline):15000;MdoApplicationInvocation* I=C->Opaque;Options.Cancel=I?I->Cancel:NULL;
    if(Options.Timeout<0||Options.Timeout>15000)Options.Timeout=15000;
    if(!xrtProcessRun(&Config,&Options,R))return SdkNativeFailure(E,"git");
    if(R->Wait!=XWAIT_OK){bool Timed=R->Wait==XWAIT_TIMEOUT||(C->Deadline&&MdoDeadlineExpired(C->Deadline));return SdkGitFail(E,Timed?"git_timeout":"sdk_cancelled",Timed?"Git inspection timed out; no repository state was changed":"Git inspection cancelled",Timed?504:499);}
    if(!Output&&R->StdoutTruncated)return SdkGitFail(E,"git_result_too_large","Git metadata exceeds 4 MiB; narrow the repository or comparison",413);
    return true;
}
static bool SdkGitSuccess(const xprocessresult* R,mdo_error_v1* E,cstr Code){
    if(R->Status.Code==0)return true;
    char Message[1200];size_t N=R->StderrSize<1000?R->StderrSize:1000;
    snprintf(Message,sizeof(Message),"Git inspection failed (exit %lld): %.*s",(long long)R->Status.Code,(int)N,R->Stderr?(cstr)R->Stderr:"");
    return SdkGitFail(E,Code,Message,422);
}
static char* SdkGitText(const mdo_sdk_context_v1* C,cstr Root,const cstr* Args,size_t Count,bool Optional,mdo_error_v1* E){
    xprocessresult R={0};char* Text=NULL;
    if(SdkGitRun(C,Root,Args,Count,(xbytesview){0},65536,NULL,NULL,&R,E)){
        if(R.Status.Code==0){Text=xrtStrDupN((cstr)R.Stdout,R.StdoutSize);SdkGitChomp(Text);}
        else if(!Optional)SdkGitSuccess(&R,E,"git_revision_invalid");
    }xrtProcessResultUnit(&R);return Text;
}
static char* SdkGitRoot(const mdo_sdk_context_v1* C,const xvalue* P,mdo_error_v1* E){
    cstr Provided=SdkField(P,"repository");xvalue* Params=xrtValueObject();SdkPut(Params,"path",Provided&&Provided[0]?Provided:".");
    char* Directory=SdkNativePath(C,Params,false,E);xrtValueRelease(Params);if(!Directory)return NULL;
    const cstr Args[]={"rev-parse","--show-toplevel"};char* Root=SdkGitText(C,Directory,Args,2,false,E);xrtFree(Directory);return Root;
}
static bool SdkGitOid(cstr Text){size_t N=Text?strlen(Text):0;if(N!=40&&N!=64)return false;for(size_t i=0;i<N;i++)if(!((Text[i]>='0'&&Text[i]<='9')||(Text[i]>='a'&&Text[i]<='f')))return false;return true;}
static char* SdkGitResolve(const mdo_sdk_context_v1* C,cstr Root,cstr Name,mdo_error_v1* E){
    if(!Name||!Name[0]||strlen(Name)>512)return SdkGitFail(E,"git_revision_invalid","Select a branch or commit",400),NULL;
    char* Revision=xrtFormat("%s^{commit}",Name);if(!Revision)return NULL;
    const cstr Args[]={"rev-parse","--verify","--end-of-options",Revision};char* Result=SdkGitText(C,Root,Args,4,false,E);xrtFree(Revision);
    if(Result&&!SdkGitOid(Result)){xrtFree(Result);Result=NULL;SdkGitFail(E,"git_revision_invalid","Git returned an invalid commit identity",422);}return Result;
}
static bool SdkGitPath(cstr Path){
    if(!Path||!Path[0]||strlen(Path)>8192||Path[0]=='/'||Path[0]=='\\'||(strlen(Path)>1&&Path[1]==':'))return false;
    for(size_t i=0;Path[i];){size_t Begin=i;while(Path[i]&&Path[i]!='/')i++;size_t N=i-Begin;if(!N||(N==1&&Path[Begin]=='.')||(N==2&&Path[Begin]=='.'&&Path[Begin+1]=='.'))return false;if(Path[i])i++;}
    return xrtUtf8Valid(xrtStrView(Path),NULL);
}
static cstr SdkGitToken(const unsigned char* Bytes,size_t Size,size_t* At){
    if(*At>=Size)return NULL;const unsigned char* End=memchr(Bytes+*At,0,Size-*At);if(!End)return NULL;
    cstr Token=(cstr)(Bytes+*At);*At=(size_t)(End-Bytes)+1;return Token;
}
static xvalue* SdkGitFind(xvalue* Files,cstr Path){
    for(size_t i=0;i<xrtValueCount(Files);i++){xvalue* V=(xvalue*)xrtValueArrayGet(Files,i);if(!strcmp(SdkField(V,"path"),Path))return V;}return NULL;
}
static xvalue* SdkGitFile(xvalue* Files,cstr Path,cstr Status,cstr Old,mdo_error_v1* E){
    if(!SdkGitPath(Path)||(Old&&!SdkGitPath(Old))){SdkGitFail(E,"git_path_invalid","Git returned an invalid or non-UTF-8 path",422);return NULL;}
    if(xrtValueCount(Files)>=GIT_FILE_LIMIT){SdkGitFail(E,"git_result_too_large","The comparison exceeds 20,000 files; choose a narrower repository",413);return NULL;}
    xvalue* V=xrtValueObject();SdkPut(V,"path",Path);SdkPut(V,"status",Status);if(Old)SdkPut(V,"old_path",Old);
    SdkPutUInt(V,"additions",0);SdkPutUInt(V,"deletions",0);xrtValueObjectSetNew(V,XRT_STR_LITERAL("stats_available"),xrtValueBool(false));xrtValueArrayAppendNew(Files,V);return V;
}
static bool SdkGitNames(xvalue* Files,const xprocessresult* R,bool Porcelain,mdo_error_v1* E){
    size_t At=0;while(At<R->StdoutSize){
        cstr First=SdkGitToken(R->Stdout,R->StdoutSize,&At),Path=NULL,Old=NULL;char Status[3]={0};
        if(!First)return SdkGitFail(E,"git_output_invalid","Git returned incomplete NUL-delimited metadata",502);
        if(Porcelain){if(strlen(First)<4||First[2]!=' ')return SdkGitFail(E,"git_output_invalid","Malformed Git status record",502);Status[0]=First[0];Status[1]=First[1];Path=First+3;if(strchr(Status,'R')||strchr(Status,'C'))Old=SdkGitToken(R->Stdout,R->StdoutSize,&At);}
        else {Status[0]=First[0];Path=SdkGitToken(R->Stdout,R->StdoutSize,&At);if(First[0]=='R'||First[0]=='C'){Old=Path;Path=SdkGitToken(R->Stdout,R->StdoutSize,&At);}}
        if(!Path||((strchr(Status,'R')||strchr(Status,'C'))&&!Old)||!SdkGitFile(Files,Path,Status,Old,E))return false;
    }return true;
}
static bool SdkGitNumbers(xvalue* Files,const xprocessresult* R,mdo_error_v1* E){
    xvalue* Index=xrtValueObject();for(size_t i=0;i<xrtValueCount(Files);i++){const xvalue* File=xrtValueArrayGet(Files,i);xrtValueObjectSet(Index,xrtStrView(SdkField(File,"path")),File);}
    bool Ok=false;
    size_t At=0;while(At<R->StdoutSize){
        cstr Token=SdkGitToken(R->Stdout,R->StdoutSize,&At);if(!Token){SdkGitFail(E,"git_output_invalid","Incomplete Git statistics",502);goto done;}
        cstr A=strchr(Token,'\t'),B=A?strchr(A+1,'\t'):NULL;if(!A||!B){SdkGitFail(E,"git_output_invalid","Malformed Git statistics",502);goto done;}
        cstr Path=B+1;if(!Path[0]){(void)SdkGitToken(R->Stdout,R->StdoutSize,&At);Path=SdkGitToken(R->Stdout,R->StdoutSize,&At);}
        if(!Path){SdkGitFail(E,"git_output_invalid","Missing renamed file statistics",502);goto done;}
        xvalue* V=(xvalue*)SdkGet(Index,Path);if(!V)continue;
        xrtValueObjectSetNew(V,XRT_STR_LITERAL("stats_available"),xrtValueBool(true));
        if(Token[0]=='-'||A[1]=='-')xrtValueObjectSetNew(V,XRT_STR_LITERAL("binary"),xrtValueBool(true));
        else {SdkPutUInt(V,"additions",SdkGitNumber(Token));SdkPutUInt(V,"deletions",SdkGitNumber(A+1));}
    }Ok=true;
done:xrtValueRelease(Index);return Ok;
}
static void SdkGitNewStats(const mdo_sdk_context_v1* C,cstr Path,const xfileinfo* Info,xvalue* V,uint64* Budget){
    if(Info->Type!=XFILE_TYPE_FILE)return;
    unsigned char Bytes[8192];xfileoptions Options;xrtFileOptionsInit(&Options);Options.Flags=XFILE_READ;
    xfile File=xrtFileOpen(Path,&Options);if(!File)return;
    uint64 Lines=0,Read=0;bool Binary=false,Complete=false;unsigned char Last='\n';
    uint64 Limit=Info->Size<=1024u*1024u&&Info->Size<=*Budget?Info->Size:8192u;if(Limit>Info->Size)Limit=Info->Size;
    while(Read<Limit&&!SdkCancelled(C)){
        size_t N=0,Want=(size_t)(Limit-Read);if(Want>sizeof(Bytes))Want=sizeof(Bytes);
        if(!xrtRead(File,Bytes,Want,&N)||!N)break;
        if(memchr(Bytes,0,N)){Read+=N;Binary=true;break;}
        for(size_t i=0;i<N;i++)if(Bytes[i]=='\n')Lines++;
        Read+=N;Last=Bytes[N-1];
    }
    if(Read>=Info->Size)Complete=true;
    if(*Budget>Read)*Budget-=Read;else *Budget=0;
    xfileinfo After={0};bool Stable=xrtFileStat(File,&After)&&After.Identity==Info->Identity&&After.Size==Info->Size&&After.Modified==Info->Modified;xrtClose(File);
    if(!Stable)return;
    if(Binary)xrtValueObjectSetNew(V,XRT_STR_LITERAL("binary"),xrtValueBool(true));
    if(Binary||Complete){xrtValueObjectSetNew(V,XRT_STR_LITERAL("stats_available"),xrtValueBool(true));SdkPutUInt(V,"additions",Binary?0:Lines+(Read&&Last!='\n'));SdkPutUInt(V,"deletions",0);}
}
typedef struct SdkGitRange {char *Root,*Head,*Base,*Branch;const char* Scope;bool Unborn;} SdkGitRange;
static void SdkGitRangeUnit(SdkGitRange* G){xrtFree(G->Root);xrtFree(G->Head);xrtFree(G->Base);xrtFree(G->Branch);memset(G,0,sizeof(*G));}
static bool SdkGitRangeInit(const mdo_sdk_context_v1* C,const xvalue* P,SdkGitRange* G,mdo_error_v1* E){
    G->Scope=SdkField(P,"scope");if(!G->Scope)G->Scope="workspace";
    if(strcmp(G->Scope,"workspace")&&strcmp(G->Scope,"staged")&&strcmp(G->Scope,"unstaged")&&strcmp(G->Scope,"branch")&&strcmp(G->Scope,"session"))return SdkGitFail(E,"git_scope_invalid","Unknown comparison scope",400);
    if(!strcmp(G->Scope,"session"))return SdkGitFail(E,"git_baseline_missing","This session has no captured baseline; select workspace or branch changes",409);
    G->Root=SdkGitRoot(C,P,E);if(!G->Root)return false;
    const cstr Head[]={"rev-parse","--verify","HEAD"},Branch[]={"symbolic-ref","--quiet","--short","HEAD"};
    G->Head=SdkGitText(C,G->Root,Head,3,true,E);if(E&&E->Code[0])return false;
    G->Unborn=!G->Head;G->Branch=SdkGitText(C,G->Root,Branch,4,true,E);if(E&&E->Code[0])return false;
    if(!strcmp(G->Scope,"branch")){G->Base=SdkGitResolve(C,G->Root,SdkField(P,"base"),E);if(!G->Base)return false;if(G->Unborn)return SdkGitFail(E,"git_head_missing","Branch comparison needs an initial commit",409);}
    else if(G->Head)G->Base=xrtStrDup(G->Head);return true;
}
static size_t SdkGitDiffArgs(const SdkGitRange* G,const char* Format,cstr* Args){
    size_t N=0;Args[N++]="diff";Args[N++]="--no-ext-diff";Args[N++]="--no-textconv";Args[N++]="--no-color";Args[N++]="--find-renames";Args[N++]=Format;
    if(!strcmp(G->Scope,"staged"))Args[N++]="--cached";
    if(!strcmp(G->Scope,"branch")){Args[N++]=G->Base;Args[N++]=G->Head;}
    else if(strcmp(G->Scope,"unstaged")&&G->Base)Args[N++]=G->Base;
    Args[N++]="--";return N;
}
static bool SdkGitList(const mdo_sdk_context_v1* C,const xvalue* P,SdkGitRange* G,xvalue** R,mdo_error_v1* E){
    xvalue* Files=xrtValueArray();xprocessresult Result={0};bool Ok=false;cstr Args[32];size_t Count;
    if(!SdkGitRangeInit(C,P,G,E))goto done;
    if(!strcmp(G->Scope,"workspace")){
        const cstr Status[]={"status","--porcelain=v1","-z","--untracked-files=all","--ignore-submodules=none"};
        if(!SdkGitRun(C,G->Root,Status,5,(xbytesview){0},GIT_METADATA_LIMIT,NULL,NULL,&Result,E)||!SdkGitSuccess(&Result,E,"git_status_failed")||!SdkGitNames(Files,&Result,true,E))goto done;
    }else{
        Count=SdkGitDiffArgs(G,"--name-status",Args);Args[Count-1]="-z";Args[Count++]="--";
        if(!SdkGitRun(C,G->Root,Args,Count,(xbytesview){0},GIT_METADATA_LIMIT,NULL,NULL,&Result,E)||!SdkGitSuccess(&Result,E,"git_status_failed")||!SdkGitNames(Files,&Result,false,E))goto done;
    }
    xrtProcessResultUnit(&Result);Count=SdkGitDiffArgs(G,"--numstat",Args);Args[Count-1]="-z";Args[Count++]="--";
    /* An unborn workspace contains only new entries; --cached works without HEAD. */
    if(G->Unborn&&!strcmp(G->Scope,"workspace")){Args[Count-1]="--cached";Args[Count++]="--";}
    if(!SdkGitRun(C,G->Root,Args,Count,(xbytesview){0},GIT_METADATA_LIMIT,NULL,NULL,&Result,E)||!SdkGitSuccess(&Result,E,"git_status_failed")||!SdkGitNumbers(Files,&Result,E))goto done;
    uint64 Additions=0,Deletions=0,Unknown=0,StatsBudget=8u*1024u*1024u;
    for(size_t i=0;i<xrtValueCount(Files);i++){
        if(SdkCancelled(C)){bool Timed=C->Deadline&&MdoDeadlineExpired(C->Deadline);SdkGitFail(E,Timed?"git_timeout":"sdk_cancelled",Timed?"Git inspection timed out":"Git inspection cancelled",Timed?504:499);goto done;}
        xvalue* V=(xvalue*)xrtValueArrayGet(Files,i);cstr Path=SdkField(V,"path");char* Full=xrtPathJoin(G->Root,Path);xfileinfo Info={0};
        if(strcmp(G->Scope,"branch")&&Full&&xrtPathStat(Full,false,&Info)){
            xrtValueObjectSetNew(V,XRT_STR_LITERAL("identity"),SdkFileInfo(&Info));
            if(Info.Type==XFILE_TYPE_DIRECTORY)xrtValueObjectSetNew(V,XRT_STR_LITERAL("directory"),xrtValueBool(true));
            if(!strcmp(SdkField(V,"status"),"??")||(G->Unborn&&!strcmp(G->Scope,"workspace")))SdkGitNewStats(C,Full,&Info,V,&StatsBudget);
        }xrtFree(Full);
        bool Available=false;xrtValueGetBool(SdkGet(V,"stats_available"),&Available);if(!Available)Unknown++;
        uint64 A=0,D=0;SdkUInt(SdkGet(V,"additions"),&A);SdkUInt(SdkGet(V,"deletions"),&D);Additions+=A;Deletions+=D;
    }
    *R=xrtValueObject();SdkPut(*R,"repository",G->Root);SdkPut(*R,"head",G->Head);SdkPut(*R,"base",G->Base);SdkPut(*R,"branch",G->Branch);SdkPut(*R,"scope",G->Scope);
    xrtValueObjectSetNew(*R,XRT_STR_LITERAL("unborn"),xrtValueBool(G->Unborn));xrtValueObjectSet(*R,XRT_STR_LITERAL("files"),Files);SdkPutUInt(*R,"additions",Additions);SdkPutUInt(*R,"deletions",Deletions);SdkPutUInt(*R,"unmeasured_files",Unknown);
    char* Json=SdkJson(*R);char Revision[65];if(!Json||!MdoExtensionHash(Json,Revision)){xrtFree(Json);goto done;}xrtFree(Json);SdkPut(*R,"revision",Revision);Ok=true;
done:xrtProcessResultUnit(&Result);xrtValueRelease(Files);if(!Ok){xrtValueRelease(*R);*R=NULL;}return Ok;
}
#include "changes_diff.inc.c"
static bool SdkChangesCall(const mdo_sdk_context_v1* C,cstr Method,const xvalue* P,xvalue** R,mdo_error_v1* E){
    mdo_sdk_context_v1 Bound=*C;uint64 End=MdoDeadlineAfterUs(15000000);if(!Bound.Deadline||Bound.Deadline>End)Bound.Deadline=End;C=&Bound;
    if(!strcmp(Method,"changes.diff"))return SdkGitDiff(C,P,R,E);
    SdkGitRange Range={0};bool Ok=false;
    if(!strcmp(Method,"changes.list"))Ok=SdkGitList(C,P,&Range,R,E);
    else if(!strcmp(Method,"changes.branches")){
        Range.Root=SdkGitRoot(C,P,E);if(!Range.Root)goto done;
        const cstr Args[]={"for-each-ref","--format=%(refname)%00%(objectname)%00","refs/heads/","refs/remotes/"};xprocessresult Result={0};
        if(SdkGitRun(C,Range.Root,Args,4,(xbytesview){0},GIT_METADATA_LIMIT,NULL,NULL,&Result,E)&&SdkGitSuccess(&Result,E,"git_refs_failed")){
            *R=xrtValueArray();size_t At=0;Ok=true;
            while(At<Result.StdoutSize){cstr Ref=SdkGitToken(Result.Stdout,Result.StdoutSize,&At),Oid=SdkGitToken(Result.Stdout,Result.StdoutSize,&At);
                if(!Ref||!SdkGitOid(Oid)){Ok=SdkGitFail(E,"git_output_invalid","Malformed Git reference list",502);break;}
                while(*Ref=='\n'||*Ref=='\r')Ref++;
                xvalue* V=xrtValueObject();SdkPut(V,"ref",Ref);SdkPut(V,"oid",Oid);SdkPut(V,"name",!strncmp(Ref,"refs/heads/",11)?Ref+11:!strncmp(Ref,"refs/remotes/",13)?Ref+13:Ref);xrtValueArrayAppendNew(*R,V);
                while(At<Result.StdoutSize&&(Result.Stdout[At]=='\n'||Result.Stdout[At]=='\r'))At++;
            }
        }xrtProcessResultUnit(&Result);
    }
done:SdkGitRangeUnit(&Range);return Ok;
}
