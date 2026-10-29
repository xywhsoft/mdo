/* Process execution owns byte capture; file tools own output consumption.
 * Included after the task-table helpers and argv/env parser in xwork_tools.c. */
typedef struct xwork_process_log_capture_stream {
    struct xwork_process_log_capture* pOwner;
    xprocessstream eStream;
    xthread* pThread;
} xwork_process_log_capture_stream;
typedef struct xwork_process_log_capture {
    xprocess* pProcess;
    xwork_process_output* Output;
    xwork_process_log_capture_stream tStdout,tStderr;
} xwork_process_log_capture;

static char* xwork__process_destination(xwork_agent* Agent,xvalue* Args,const char* Key)
{
    xvalue* V=xwork__json_get(Args,Key);if(!V)return NULL;
    xstrview Text={0};if(!xrtValueGetString(V,&Text)||!Text.Size||Text.Size>8192u||memchr(Text.Data,0,Text.Size)||
        !xrtUtf8Valid(Text,NULL))return NULL;
    const char* Cwd=xwork__json_text(Args,"cwd");if(!Cwd||!Cwd[0])Cwd=".";
    char* Joined=(xrtPathIsAbs(Text.Data)||Text.Data[0]=='@')?xrtStrDup(Text.Data):xrtPathJoin(Cwd,Text.Data);
    char* Result=Joined?xwork__strdup(Joined):NULL;if(Joined)xrtFree(Joined);
    (void)Agent;return Result;
}

static xwork_process_output* xwork__process_output_create(xwork_agent* Agent,xvalue* Args,
    bool Background,bool Merge,xwork_error* Error)
{
    xwork_process_output_options Opt;xworkProcessOutputOptionsInit(&Opt);
    xroot Root=NULL;char* Dest[2]={0};bool Valid=true;
    Opt.FilesFromStart=Background;Opt.MergeStderr=Merge;
    Opt.InlineBytes=Agent->iMaxInlineToolBytes;
    if(Opt.InlineBytes<256u)Opt.InlineBytes=256u;if(Opt.InlineBytes>XWORK_OUTPUT_INLINE)Opt.InlineBytes=XWORK_OUTPUT_INLINE;
    Opt.MaxBytes=xwork__json_u64(Args,"max_output_bytes",XWORK_OUTPUT_MAX,&Valid);
    if(!Valid||!Opt.MaxBytes||Opt.MaxBytes>XWORK_OUTPUT_STORE_MAX)goto invalid;
    for(size_t i=0;i<2u;i++) {
        const char* Key=i?"stderr_path":"stdout_path";if(!xwork__json_get(Args,Key))continue;
        char* Logical=xwork__process_destination(Agent,Args,Key);
        if(!Logical||!strncmp(Logical,"@results",8u)){free(Logical);goto invalid;}
        Dest[i]=xwork__resolve_path(Agent,Logical,Error);free(Logical);if(!Dest[i])goto done;
    }
    Opt.StdoutPath=Dest[0];Opt.StderrPath=Dest[1];
    if(Agent->bAllowArtifactWrites) {
        if(Agent->pArtifactStore)Root=xrtRootOpenIn(Agent->pArtifactStore->pDirectory,".");
        else {
            char* Absolute=xwork__resolve_path(Agent,Agent->sArtifactDirectory,Error);
            if(Absolute){Root=xwork__artifact_open_directory(Agent,Absolute,".",Error);free(Absolute);}
        }
    }
    Opt.Directory=Root;
    {
        xwork_process_output* Output=xworkProcessOutputCreate(&Opt,Error);
        if(Root)xrtRootClose(Root);free(Dest[0]);free(Dest[1]);return Output;
    }
invalid:xwork__set_error(Error,XWORK_ERROR_INVALID_ARGUMENT,"invalid output paths, merge mode, or max_output_bytes (1..1073741824)");
done:if(Root)xrtRootClose(Root);free(Dest[0]);free(Dest[1]);return NULL;
}

static bool xwork__process_run_output(xprocessstream Stream,xbytesview Bytes,void* User)
{return xworkProcessOutputWrite(User,Stream==XPROCESS_STDERR,Bytes.Data,Bytes.Size);}

static int32 xwork__process_log_capture_thread(void* User)
{
    xwork_process_log_capture_stream* Stream=User;unsigned char Chunk[16384];
    for(;;) {
        int64 Read=xrtProcessRead(Stream->pOwner->pProcess,Stream->eStream,Chunk,sizeof(Chunk));
        if(Read<0) {
            xwork_process_output* O=Stream->pOwner->Output;
            if(xrtMutexLock(O->Lock)){O->Streams[Stream->eStream==XPROCESS_STDERR?1u:0u].Lost=true;
                xwork__output_error(O,"output_read_failed");xrtMutexUnlock(O->Lock);}break;
        }
        if(!Read)break;
        if(!xworkProcessOutputWrite(Stream->pOwner->Output,Stream->eStream==XPROCESS_STDERR,Chunk,(size_t)Read))break;
    }
    return 0;
}

static void xwork__process_log_capture_destroy(xwork_process_log_capture* Capture)
{
    if(!Capture)return;
    if(Capture->tStdout.pThread){xrtThreadWait(Capture->tStdout.pThread);xrtThreadDestroy(Capture->tStdout.pThread);}
    if(Capture->tStderr.pThread){xrtThreadWait(Capture->tStderr.pThread);xrtThreadDestroy(Capture->tStderr.pThread);}
    free(Capture);
}

static xwork_process_log_capture* xwork__process_log_capture_create(xprocess* Process,xwork_process_output* Output,bool Stderr)
{
    xwork_process_log_capture* C=calloc(1,sizeof(*C));if(!C)return NULL;C->pProcess=Process;C->Output=Output;
    C->tStdout.pOwner=C;C->tStdout.eStream=XPROCESS_STDOUT;C->tStderr.pOwner=C;C->tStderr.eStream=XPROCESS_STDERR;
    C->tStdout.pThread=xrtThreadCreate(xwork__process_log_capture_thread,&C->tStdout,0u);
    if(C->tStdout.pThread&&Stderr)C->tStderr.pThread=xrtThreadCreate(xwork__process_log_capture_thread,&C->tStderr,0u);
    if(!C->tStdout.pThread||(Stderr&&!C->tStderr.pThread)) {
        (void)xrtProcessKillTree(Process);xwork__process_log_capture_destroy(C);return NULL;
    }
    return C;
}

static bool xwork__process_output_read(xwork_process_output* O,bool Err,uint64_t Offset,size_t Max,
    unsigned char** Bytes,size_t* Size,uint64_t* Next,bool* Dropped)
{
    *Bytes=NULL;*Size=0;*Next=Offset;*Dropped=false;if(!O||!xrtMutexLock(O->Lock))return false;
    xwork_output_stream* S=&O->Streams[Err?1u:0u];uint64_t Available=S->Path?S->FileBytes:S->Stored;
    if(Offset>Available)Offset=Available;*Next=Offset;*Dropped=S->Lost;
    size_t N=Available-Offset>Max?Max:(size_t)(Available-Offset);bool Ok=true;
    if(N) {
        *Bytes=malloc(N);if(!*Bytes){Ok=false;goto done;}
        if(S->Path) {
            xfileoptions Opt;xrtFileOptionsInit(&Opt);Opt.Flags=XFILE_READ|XFILE_NOFOLLOW;
            xfile F=S->Explicit?xrtFileOpen(S->Path,&Opt):xrtRootFileOpen(O->Run,Err?"stderr.log":"stdout.log",&Opt);
            size_t Got=0;Ok=F&&xrtReadAtFull(F,Offset,*Bytes,N,&Got)&&Got==N;if(F)xrtClose(F);
        } else memcpy(*Bytes,S->Preview.pData+(size_t)Offset,N);
        if(Ok){*Size=N;*Next=Offset+N;}
    }
done:if(!Ok){free(*Bytes);*Bytes=NULL;}xrtMutexUnlock(O->Lock);return Ok;
}

static xvalue* xwork__process_status_value(xwork_agent* Agent,xwork_process_entry* Entry)
{
    if(!xrtMutexLock(Entry->pStateLock))return NULL;
    xwork__task_refresh_locked(Entry);bool Complete=Entry->eState>=XWORK_TASK_SUCCEEDED;
    xvalue* V=Entry->Output?xworkProcessOutputSnapshot(Entry->Output,false):xrtValueObject();
    bool Ok=V&&xwork__output_uint(V,"task_id",Entry->uId)&&xwork__output_put(V,"state",xwork__task_state_name(Entry->eState))&&
        xwork__output_put(V,"kind",Entry->eKind==XWORK_TASK_PROCESS?"process":Entry->eKind==XWORK_TASK_AGENT?"agent":"scheduled")&&
        xwork__output_bool(V,"finished",Complete);
    if(Ok&&Entry->bExitStatusValid)Ok=xrtValueObjectSetNew(V,XRT_STR_LITERAL("exit_code"),xrtValueInt(Entry->iExitCode))&&
        xrtValueObjectSetNew(V,XRT_STR_LITERAL("signal"),xrtValueInt(Entry->iExitSignal))&&
        xrtValueObjectSetNew(V,XRT_STR_LITERAL("stop_reason"),xrtValueInt(Entry->iStopReason));
    if(Ok&&Complete&&Entry->eKind!=XWORK_TASK_PROCESS&&Entry->sResult) {
        size_t Len=strlen(Entry->sResult);
        if(Len>Agent->iMaxInlineToolBytes&&Agent->bAllowArtifactWrites&&!Entry->Output) {
            xvalue* Args=xrtValueObject();Entry->Output=xwork__process_output_create(Agent,Args,true,false,NULL);xrtValueRelease(Args);
            if(Entry->Output){xworkProcessOutputWrite(Entry->Output,false,Entry->sResult,Len);xworkProcessOutputFinish(Entry->Output);}
        }
        if(Entry->Output) {
            xvalue* Output=xworkProcessOutputSnapshot(Entry->Output,false);
            if(Output)Ok=xrtValueObjectSetNew(V,XRT_STR_LITERAL("result"),Output);else Ok=false;
        } else Ok=xwork__output_put(V,"result",Entry->sResult);
    }
    xrtMutexUnlock(Entry->pStateLock);if(!Ok){xrtValueRelease(V);V=NULL;}return V;
}

static int32 xwork__process_monitor(void* User)
{
    xwork_process_entry* Entry=User;xprocessstatus Exit={0};
    (void)xrtProcessWait(Entry->pProcess);(void)xrtProcessStatus(Entry->pProcess,&Exit);
    /* A finished leader must not leave descendants holding its output pipes. */
    (void)xrtProcessKillTree(Entry->pProcess);
    xwork__process_log_capture_destroy(Entry->LogCapture);Entry->LogCapture=NULL;
    xworkProcessOutputFinish(Entry->Output);
    xvalue* Summary=xworkProcessOutputSnapshot(Entry->Output,false);
    if(Summary){char* Json=xrtJsonStringify(Summary,false,NULL);Entry->OutputNotice=Json?xwork__strdup(Json):NULL;if(Json)xrtFree(Json);xrtValueRelease(Summary);}
    xrtMutexLock(Entry->pStateLock);
    xrtProcessDestroy(Entry->pProcess);Entry->pProcess=NULL;
    bool Held=Entry->bProcessOwnerHeld;Entry->bProcessOwnerHeld=false;
    xrtMutexUnlock(Entry->pStateLock);
    if(Held)Entry->OnProcessRelease(Entry->pProcessOwnerData,Entry->pProcessOwnerLease);
    xrtMutexLock(Entry->pStateLock);
    Entry->pProcessOwnerLease=NULL;Entry->bExitStatusValid=Exit.Kind!=XPROCESS_EXIT_NONE;
    Entry->iExitCode=Exit.Code;Entry->iExitSignal=Exit.Signal;Entry->iStopReason=Exit.Stop;
    Entry->bDone=true;Entry->bSuccess=Exit.Kind==XPROCESS_EXIT_CODE&&!Exit.Code;
    xwork_task_state State=Entry->bStopRequested?XWORK_TASK_CANCELLED:
        Exit.Kind==XPROCESS_EXIT_LOST||!Entry->bExitStatusValid?XWORK_TASK_LOST:
        Entry->bSuccess?XWORK_TASK_SUCCEEDED:XWORK_TASK_FAILED;
    xwork__task_set_state_locked(Entry,State);Entry->uExitedUs=xllmClockUs();
    xrtMutexUnlock(Entry->pStateLock);xwork__task_entry_release(Entry);return 0;
}

static xwork_result xwork__tool_spawn(void* User,const xwork_tool_context* Context,
    const char* Arguments,xwork_tool_output* Output,xwork_error* Error)
{
    xwork_agent* Agent=User;xvalue* Args=xwork__json_parse_object(Arguments);
    char** Argv=NULL,**EnvStorage=NULL;size_t Argc=0,EnvCount=0,EnvStorageCount=0;
    xprocessenv* Env=NULL;char* Cwd=NULL;xwork_process_entry* Entry=NULL;xwork_result Result=XWORK_RESULT_ERROR;
    bool Valid=true,Merge=false,Started=false;uint64_t Remind=0;
    if(!Args)return xwork__tool_fail(Output,"invalid arguments: expected a JSON object");
    if(!xwork__parse_string_array(Args,"argv",&Argv,&Argc,256u)||!Argv){Result=xwork__tool_fail(Output,"argv must contain 1-256 strings");goto done;}
    Merge=xwork__json_bool(Args,"merge_stderr",false,&Valid);if(!Valid)goto invalid;
    const char* Notify=xwork__json_text(Args,"notify");if(Notify&&strlen(Notify)>500u)goto invalid;
    Remind=xwork__json_u64(Args,"remind_after_ms",0u,&Valid);if(!Valid||Remind>3600000u)goto invalid;
    const char* Logical=xwork__json_text(Args,"cwd");if(!Logical||!Logical[0])Logical=".";
    Cwd=xwork__resolve_path(Agent,Logical,Error);if(!Cwd)goto fail;
    xrtClearError();if(!xrtDirExists(Cwd)){if(!xrtGetError())xrtSetErrorInfo(XERR_NOT_FOUND,"xwork.process",0,"working directory does not exist");Result=xwork__tool_process_fail(Output,"spawn/cwd",Argv[0],Cwd,xrtGetError());goto done;}
    if(!xwork__build_env(Args,&Env,&EnvCount,&EnvStorage,&EnvStorageCount))goto done;
    Entry=xwork__process_add(Agent,Context,XWORK_TASK_PROCESS);if(!Entry){Result=xwork__tool_fail(Output,"managed task limit reached");goto done;}
    Entry->Output=xwork__process_output_create(Agent,Args,true,Merge,Error);if(!Entry->Output)goto fail;
    if(Agent->OnProcessAcquire) {
        if(!Agent->OnProcessAcquire(Agent->pProcessOwnerData,&Entry->pProcessOwnerLease)){Result=xwork__tool_process_fail(Output,"spawn/resources",Argv[0],Cwd,xrtGetError());goto done;}
        Entry->OnProcessRelease=Agent->OnProcessRelease;Entry->pProcessOwnerData=Agent->pProcessOwnerData;Entry->bProcessOwnerHeld=true;
    }
    Entry->sCommand=xwork__argv_preview(Argv,Argc);Entry->sLabel=Entry->sCommand?xwork__strdup(Entry->sCommand):NULL;
    Entry->sNotify=Notify?xwork__strdup(Notify):NULL;Entry->uRemindAfterMs=Remind;
    if(!Entry->sCommand||!Entry->sLabel||(Notify&&!Entry->sNotify))goto done;
    /* Allocate the complete response before starting a process with effects. */
    xvalue* V=xwork__process_status_value(Agent,Entry);char* Json=NULL;
    if(V){xwork__output_put(V,"state","running");Json=xrtJsonStringify(V,false,NULL);}
    bool Prepared=Json&&xworkToolOutputSet(Output,true,Json);if(Json)xrtFree(Json);xrtValueRelease(V);if(!Prepared)goto done;
    xprocessconfig Config;xrtProcessConfigInit(&Config);xwork__apply_argv_config(&Config,Argv,Argc);
    Config.WorkDir=Cwd;Config.InheritEnv=true;Config.Env=Env;Config.EnvCount=EnvCount;Config.NewGroup=true;Config.HideWindow=true;
    Config.Stdin.Mode=XPROCESS_IO_PIPE;Config.Stdout.Mode=XPROCESS_IO_PIPE;Config.Stderr.Mode=Merge?XPROCESS_IO_MERGE:XPROCESS_IO_PIPE;
    xrtClearError();Entry->pProcess=xrtProcessSpawn(&Config);
    if(!Entry->pProcess){Result=xwork__tool_process_fail(Output,"spawn",Argv[0],Cwd,xrtGetError());goto done;}
    Entry->LogCapture=xwork__process_log_capture_create(Entry->pProcess,Entry->Output,!Merge);if(!Entry->LogCapture)goto done;
    xwork__atomic_store(&Entry->iReady,1);xwork__task_entry_ref(Entry);
    Entry->ProcessMonitor=xrtThreadCreate(xwork__process_monitor,Entry,0u);
    if(!Entry->ProcessMonitor){xwork__task_entry_release(Entry);goto done;}
    Started=true;Result=XWORK_RESULT_OK;goto done;
invalid:Result=xwork__tool_fail(Output,"invalid merge_stderr, notify or remind_after_ms");goto done;
fail:Result=xwork__tool_fail(Output,Error&&Error->sMessage[0]?Error->sMessage:"process output storage unavailable");
done:
    if(Entry&&!Started)xwork__process_remove_entry(Agent,Entry);
    xwork__task_entry_release(Entry);xwork__free_string_array(Argv,Argc);xwork__free_string_array(EnvStorage,EnvStorageCount);
    free(Env);free(Cwd);xrtValueRelease(Args);return Result;
}

static xwork_result xwork__tool_poll(void* User,const xwork_tool_context* Context,
    const char* Arguments,xwork_tool_output* Output,xwork_error* Error)
{
    xwork_agent* Agent=User;xvalue* Args=xwork__json_parse_object(Arguments);bool Valid=false;
    uint64_t Id=Args?xwork__json_u64(Args,"task_id",0u,&Valid):0; (void)Context;
    if(!Valid||!Id){xrtValueRelease(Args);return xwork__tool_fail(Output,"positive task_id is required");}
    xwork_process_entry* Entry=xwork__process_find(Agent,Id,NULL);xrtValueRelease(Args);
    if(!Entry)return xwork__tool_fail(Output,"unknown task_id");
    xvalue* V=xwork__process_status_value(Agent,Entry);char* Json=V?xrtJsonStringify(V,false,NULL):NULL;
    bool Ok=Json&&xworkToolOutputSet(Output,true,Json);if(Json)xrtFree(Json);xrtValueRelease(V);xwork__task_entry_release(Entry);
    if(!Ok)xwork__set_error(Error,XWORK_ERROR_OUT_OF_MEMORY,"cannot build task status");return Ok?XWORK_RESULT_OK:XWORK_RESULT_ERROR;
}

static xwork_result xwork__tool_exec(void* User,const xwork_tool_context* Context,
    const char* Arguments,xwork_tool_output* Output,xwork_error* Error)
{
    xwork_agent* Agent=User;xvalue* Args=xwork__json_parse_object(Arguments);
    char** Argv=NULL,**EnvStorage=NULL;size_t Argc=0,EnvCount=0,EnvStorageCount=0;
    xprocessenv* Env=NULL;char* Cwd=NULL;xwork_process_output* Capture=NULL;xwork_result Result=XWORK_RESULT_ERROR;
    bool Valid=true,Merge=false,Expected=false;void* Lease=NULL;bool Held=false;xprocessresult Process={0};
    if(!Args)return xwork__tool_fail(Output,"invalid arguments: expected a JSON object");
    if(!xwork__parse_string_array(Args,"argv",&Argv,&Argc,256u)||!Argv){Result=xwork__tool_fail(Output,"argv must contain 1-256 strings");goto done;}
    uint64_t Timeout=xwork__json_u64(Args,"timeout_ms",Agent->uCommandTimeoutMs,&Valid);
    if(!Valid||!Timeout||Timeout>3600000u){Result=xwork__tool_fail(Output,"timeout_ms must be between 1 and 3600000");goto done;}
    Merge=xwork__json_bool(Args,"merge_stderr",false,&Valid);if(!Valid){Result=xwork__tool_fail(Output,"merge_stderr must be boolean");goto done;}
    xvalue* Codes=xwork__json_get(Args,"expected_exit_codes");
    if(Codes) {
        if(xrtValueType(Codes)!=XVALUE_ARRAY||!xrtValueCount(Codes)||xrtValueCount(Codes)>32u)goto invalid_codes;
        for(size_t i=0;i<xrtValueCount(Codes);i++){int64_t Code=0;if(!xrtValueGetInt(xrtValueArrayGet(Codes,i),&Code)||Code<INT32_MIN||Code>INT32_MAX)goto invalid_codes;}
    }
    const char* Logical=xwork__json_text(Args,"cwd");if(!Logical||!Logical[0])Logical=".";
    Cwd=xwork__resolve_path(Agent,Logical,Error);if(!Cwd)goto storage_error;
    xrtClearError();if(!xrtDirExists(Cwd)){if(!xrtGetError())xrtSetErrorInfo(XERR_NOT_FOUND,"xwork.process",0,"working directory does not exist");Result=xwork__tool_process_fail(Output,"exec/cwd",Argv[0],Cwd,xrtGetError());goto done;}
    if(!xwork__build_env(Args,&Env,&EnvCount,&EnvStorage,&EnvStorageCount))goto done;
    Capture=xwork__process_output_create(Agent,Args,false,Merge,Error);if(!Capture)goto storage_error;
    xprocessconfig Config;xrtProcessConfigInit(&Config);xwork__apply_argv_config(&Config,Argv,Argc);
    Config.WorkDir=Cwd;Config.InheritEnv=true;Config.Env=Env;Config.EnvCount=EnvCount;Config.NewGroup=true;Config.HideWindow=true;
    Config.Stdin.Mode=XPROCESS_IO_NULL;Config.Stdout.Mode=XPROCESS_IO_PIPE;Config.Stderr.Mode=Merge?XPROCESS_IO_MERGE:XPROCESS_IO_PIPE;
    xprocessrunoptions Options;xrtProcessRunOptionsInit(&Options);
    uint64_t Scope=Context&&Context->uDeadline?Context->uDeadline:xwork__active_deadline(Agent);
    uint64_t Deadline=xllmDeadlineAfterUs(Timeout*UINT64_C(1000));if(Scope!=XLLM_DEADLINE_NEVER&&Scope<Deadline)Deadline=Scope;
    Options.Timeout=xllmDeadlineWaitMs(Deadline);Options.Cancel=Context&&Context->pCancel?Context->pCancel:xwork__active_cancel(Agent);
    Options.StdoutLimit=Options.StderrLimit=0;Options.Overflow=XPROCESS_OVERFLOW_KEEP_FIRST;
    Options.Output=xwork__process_run_output;Options.UserData=Capture;
    if(Agent->OnProcessAcquire){if(!Agent->OnProcessAcquire(Agent->pProcessOwnerData,&Lease)){Result=xwork__tool_process_fail(Output,"exec/resources",Argv[0],Cwd,xrtGetError());goto done;}Held=true;}
    xrtClearError();bool Ran=xrtProcessRun(&Config,&Options,&Process);
    /* Format the native cause before closing output files can replace the
     * thread's error. Retaining partial logs must not hide the launch error. */
    if(!Ran)Result=xwork__tool_process_fail(Output,"exec/start-or-capture",Argv[0],Cwd,xrtGetError());
    xworkProcessOutputFinish(Capture);
    if(!Ran){
        if(xrtMutexLock(Capture->Lock)) {xwork__output_error(Capture,"process_capture_failed");xrtMutexUnlock(Capture->Lock);}
        xvalue* Partial=xworkProcessOutputSnapshot(Capture,true);char* Json=Partial?xrtJsonStringify(Partial,false,NULL):NULL;
        if(Json){xwork_buf Detail={0};if(xwork__buf_append_cstr(&Detail,Output->sContent?Output->sContent:"")&&
            xwork__buf_append_char(&Detail,'\n')&&xwork__buf_append_cstr(&Detail,Json))
                (void)xworkToolOutputSet(Output,false,Detail.pData);xwork__buf_unit(&Detail);xrtFree(Json);}
        xrtValueRelease(Partial);goto done;
    }
    Expected=Process.Wait==XWAIT_OK&&Process.Status.Kind==XPROCESS_EXIT_CODE;
    if(Expected&&Codes) {Expected=false;for(size_t i=0;i<xrtValueCount(Codes);i++){int64_t Code=0;xrtValueGetInt(xrtValueArrayGet(Codes,i),&Code);if(Code==Process.Status.Code)Expected=true;}}
    else if(Expected)Expected=Process.Status.Code==0;
    xvalue* V=xworkProcessOutputSnapshot(Capture,true);char* Json=NULL;
    bool Built=V&&xrtValueObjectSetNew(V,XRT_STR_LITERAL("exit_code"),xrtValueInt(Process.Status.Code))&&
        xrtValueObjectSetNew(V,XRT_STR_LITERAL("signal"),xrtValueInt(Process.Status.Signal))&&
        xwork__output_uint(V,"duration_ms",Process.Duration)&&xwork__output_bool(V,"exit_expected",Expected)&&
        xwork__output_put(V,"state",Process.Wait==XWAIT_CANCELLED?"cancelled":Process.Wait==XWAIT_TIMEOUT?"timed_out":Expected?"succeeded":"failed");
    if(Built)Json=xrtJsonStringify(V,false,NULL);
    bool Complete=false;if(V)xrtValueGetBool(xrtValueObjectGet(V,XRT_STR_LITERAL("output_complete")),&Complete);
    if(Json&&xworkToolOutputSet(Output,Expected&&Complete,Json))Result=XWORK_RESULT_OK;
    if(Json)xrtFree(Json);xrtValueRelease(V);
    /* Preserve partial output in the tool receipt even on scope cancellation. */
    if(Process.Wait==XWAIT_CANCELLED){xwork__set_error(Error,XWORK_ERROR_CANCELLED,"agent operation cancelled during execution");Result=XWORK_RESULT_CANCELLED;}
    else if(Process.Wait==XWAIT_TIMEOUT&&Scope!=XLLM_DEADLINE_NEVER&&xllmDeadlineExpired(Scope)){
        xwork__set_error(Error,XWORK_ERROR_TIMEOUT,"agent operation deadline exceeded during execution");Result=XWORK_RESULT_TIMEOUT;
    }
    goto done;
invalid_codes:Result=xwork__tool_fail(Output,"expected_exit_codes must contain 1-32 signed 32-bit integers");goto done;
storage_error:Result=xwork__tool_fail(Output,Error&&Error->sMessage[0]?Error->sMessage:"process output storage unavailable");
done:
    if(Held)Agent->OnProcessRelease(Agent->pProcessOwnerData,Lease);
    xrtProcessResultUnit(&Process);xworkProcessOutputDestroy(Capture);
    xwork__free_string_array(Argv,Argc);xwork__free_string_array(EnvStorage,EnvStorageCount);
    free(Env);free(Cwd);xrtValueRelease(Args);return Result;
}
