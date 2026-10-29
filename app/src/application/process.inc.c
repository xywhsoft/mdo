/* SDK processes share the model tools' loss-aware output store. A short
 * result stays inline; large output is readable without consuming a cursor. */
typedef struct MdoSdkProcessCapture {
    const mdo_sdk_context_v1* Context;
    xwork_process_output* Output;
    bool Stream;
} MdoSdkProcessCapture;

static bool SdkProcessOutput(xprocessstream Stream,xbytesview Bytes,void* User)
{
    MdoSdkProcessCapture* Capture=User;
    (void)xworkProcessOutputWrite(Capture->Output,Stream==XPROCESS_STDERR,Bytes.Data,Bytes.Size);
    if(Capture->Stream) {
        xvalue* Data=xrtValueObject();
        if(Data&&SdkByteValue(Data,Bytes.Data,Bytes.Size))
            MdoApplicationEvent(Stream==XPROCESS_STDOUT?"process.stdout":"process.stderr",Capture->Context,Data);
        xrtValueRelease(Data);
    }
    /* A storage error must not stop draining the child or trigger its replay.
     * The result reports the retained ranges and the exact storage failure. */
    return !SdkCancelled(Capture->Context);
}

static char* SdkProcessPath(cstr Base,cstr Path)
{
    char* Joined=xrtPathIsRooted(Path)?xrtStrDup(Path):xrtPathJoin(Base,Path);
    char* Absolute=Joined?xrtPathAbs(Joined):NULL;xrtFree(Joined);return Absolute;
}

static void SdkProcessSmallBytes(xvalue* Result,cstr Key,const void* Bytes,size_t Size)
{
    xvalue* Stream=(xvalue*)SdkGet(Result,Key);
    cstr Path=SdkField(Stream,"native_path");if(!Stream||(Path&&Path[0]))return;
    uint64 Saved=0;if(!SdkUInt(SdkGet(Stream,"saved_bytes"),&Saved)||Saved!=Size)return;
    char* Encoded=SdkBase64(Bytes,Size);if(Encoded)SdkPut(Stream,"base64",Encoded);xrtFree(Encoded);
}

static bool SdkProcessCall(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E)
{
    cstr Program=SdkField(P,"program"),Directory=SdkField(P,"cwd"),Input=SdkField(P,"input");
    const xvalue* Args=SdkGet(P,"args");uint64 Timeout=0,Max=64u*1024u*1024u;
    bool Stream=false,Merge=false,Ok=false,HaveResult=false;
    char* Base=NULL,*Cwd=NULL,*OutPath=NULL,*ErrPath=NULL;cstr* Argv=NULL;
    xroot Root=NULL;xwork_process_output* Capture=NULL;MdoCliUse* Use=NULL;
    xprocessresult Result={0};xwork_error Error={0};
    if(!Program||!Program[0]||(Args&&xrtValueType(Args)!=XVALUE_ARRAY)||xrtValueCount(Args)>1024||
       (SdkGet(P,"cwd")&&(!Directory||!Directory[0]))||(SdkGet(P,"input")&&!Input)||
       (SdkGet(P,"timeout_ms")&&(!SdkUInt(SdkGet(P,"timeout_ms"),&Timeout)||Timeout>INT64_MAX))||
       (SdkGet(P,"max_output_bytes")&&(!SdkUInt(SdkGet(P,"max_output_bytes"),&Max)||!Max||Max>1024u*1024u*1024u))||
       (SdkGet(P,"stream")&&!xrtValueGetBool(SdkGet(P,"stream"),&Stream))||
       (SdkGet(P,"merge_stderr")&&!xrtValueGetBool(SdkGet(P,"merge_stderr"),&Merge)))
        return MdoApplicationError(E,"sdk_arguments_invalid","Supply a program, string argv and valid output/timeout options","process",400,false);
    Argv=xrtCalloc(xrtValueCount(Args)+1,sizeof(*Argv));if(!Argv)goto native_error;
    for(size_t i=0;i<xrtValueCount(Args);i++) {
        Argv[i]=SdkText(xrtValueArrayGet(Args,i));
        if(!Argv[i]){MdoApplicationError(E,"sdk_arguments_invalid","Process argv must contain strings without NUL; empty strings are supported","process",400,false);goto done;}
    }
    Base=C->Workspace&&C->Workspace[0]?xrtPathAbs(C->Workspace):MdoHomeDefaultWorkspacePath(false);
    Cwd=Base?SdkProcessPath(Base,Directory?Directory:"."):NULL;if(!Cwd)goto native_error;
    for(unsigned i=0;i<2;i++) {
        cstr Key=i?"stderr_path":"stdout_path";if(!SdkGet(P,Key))continue;
        cstr Path=SdkField(P,Key);
        if(!Path||!Path[0]||(i&&Merge)) {
            MdoApplicationError(E,"sdk_arguments_invalid","Output paths must be nonempty strings; merge_stderr excludes stderr_path","process",400,false);goto done;
        }
        char* Absolute=SdkProcessPath(Cwd,Path);if(!Absolute)goto native_error;
        if(i)ErrPath=Absolute;else OutPath=Absolute;
    }
    Root=MdoHomeOpenStorageDirectory("artifacts");if(!Root)goto native_error;
    xwork_process_output_options Storage;xworkProcessOutputOptionsInit(&Storage);
    Storage.Directory=Root;Storage.StdoutPath=OutPath;Storage.StderrPath=ErrPath;
    Storage.MaxBytes=Max;Storage.MergeStderr=Merge;
    Capture=xworkProcessOutputCreate(&Storage,&Error);
    if(!Capture){MdoApplicationError(E,"sdk_output_unavailable",Error.sMessage,"process",409,false);goto done;}
    xprocessconfig Config;xrtProcessConfigInit(&Config);Config.Program=Program;Config.Args=Argv;Config.ArgCount=xrtValueCount(Args);
    Config.WorkDir=Cwd;Config.HideWindow=true;Config.NewGroup=true;
    Config.Stdin.Mode=Input?XPROCESS_IO_PIPE:XPROCESS_IO_NULL;Config.Stdout.Mode=XPROCESS_IO_PIPE;Config.Stderr.Mode=Merge?XPROCESS_IO_MERGE:XPROCESS_IO_PIPE;
    xprocessrunoptions Options;xrtProcessRunOptionsInit(&Options);MdoApplicationInvocation* Invocation=C->Opaque;
    Options.Cancel=Invocation?Invocation->Cancel:NULL;Options.Timeout=C->Deadline?MdoDeadlineWaitMs(C->Deadline):XRT_WAIT_FOREVER;
    if(Timeout&&(Options.Timeout<0||Options.Timeout>(int64)Timeout))Options.Timeout=(int64)Timeout;
    Options.Input=(xbytesview){(const unsigned char*)Input,Input?strlen(Input):0};
    Options.StdoutLimit=4096;Options.StderrLimit=4096;Options.Overflow=XPROCESS_OVERFLOW_KEEP_FIRST;
    MdoSdkProcessCapture Callback={C,Capture,Stream};Options.Output=SdkProcessOutput;Options.UserData=&Callback;
    char Why[512]={0};
    if(!MdoCliUseBegin(&Use,Why)){MdoApplicationError(E,"sdk_commands_unavailable",Why,"process",503,false);goto done;}
    Ok=xrtProcessRun(&Config,&Options,&Result);HaveResult=true;
    if(!Ok){SdkNativeFailure(E,"process");if(E)E->MayHaveCompleted=Result.Status.Kind!=XPROCESS_EXIT_NONE||Result.InputWritten||Result.StdoutSize||Result.StderrSize;}
    xworkProcessOutputFinish(Capture);*R=xworkProcessOutputSnapshot(Capture,true);
    if(*R) {
        xrtValueObjectSetNew(*R,XRT_STR_LITERAL("exit_code"),xrtValueInt(Result.Status.Code));
        xrtValueObjectSetNew(*R,XRT_STR_LITERAL("signal"),xrtValueInt(Result.Status.Signal));
        xrtValueObjectSetNew(*R,XRT_STR_LITERAL("wait_status"),xrtValueInt(Result.Wait));
        bool Complete=false;xrtValueGetBool(SdkGet(*R,"output_complete"),&Complete);
        xrtValueObjectSetNew(*R,XRT_STR_LITERAL("truncated"),xrtValueBool(!Complete));
        if(!Merge) {
            SdkProcessSmallBytes(*R,"stdout",Result.Stdout,Result.StdoutSize);
            SdkProcessSmallBytes(*R,"stderr",Result.Stderr,Result.StderrSize);
        }
    }else if(Ok){Ok=SdkNativeFailure(E,"process_result");if(E)E->MayHaveCompleted=true;}
    if(Ok&&Result.Wait!=XWAIT_OK)Ok=MdoApplicationError(E,Result.Wait==XWAIT_TIMEOUT?"sdk_process_timeout":"sdk_cancelled",
        Result.Wait==XWAIT_TIMEOUT?"Process execution timed out; partial output is retained":"Process execution cancelled; partial output is retained",
        "process",Result.Wait==XWAIT_TIMEOUT?504:499,true);
    goto done;
native_error:
    SdkNativeFailure(E,"process");
done:
    MdoCliUseEnd(Use);if(HaveResult)xrtProcessResultUnit(&Result);
    if(Capture)xworkProcessOutputDestroy(Capture);
    if(Root)xrtRootClose(Root);
    xrtFree(Argv);xrtFree(Base);xrtFree(Cwd);xrtFree(OutPath);xrtFree(ErrPath);return Ok;
}
