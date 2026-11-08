/* Session readers/event retirement can briefly own the sole runtime. Wait only
 * at this effect-free admission gate; never repeat an executor invocation. */
static MdoSession* BuToolSessionOpen(const mdo_sdk_context_v1* C,cstr Project,cstr SessionId,
    const MdoSessionRuntimeOptions* Options,xwork_error* Error){
    uint64 Until=MdoDeadlineAfterUs(500000);
    for(;;){
        if(SdkCancelled(C)){
            bool Timed=C->Deadline&&MdoDeadlineExpired(C->Deadline);xworkErrorInit(Error);
            Error->eCode=Timed?XWORK_ERROR_TIMEOUT:XWORK_ERROR_CANCELLED;
            snprintf(Error->sMessage,sizeof(Error->sMessage),"Tool session admission %s before execution",Timed?"timed out":"cancelled");return NULL;
        }
        MdoSession* Session=MdoSessionOpen(Project,SessionId,Options,Error);
        if(Session||Error->eCode!=XWORK_ERROR_CONTEXT||strcmp(Error->sMessage,"session already has an active runtime")||MdoDeadlineExpired(Until))return Session;
        xrtSleep(10);
    }
}
