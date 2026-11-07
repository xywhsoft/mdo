/* Host-owned policy at native safe boundaries. The runtime retains the exact
 * pending checkpoint/request/result and owns interruptible waiting. */
static xwork_result MdoAgentsRecoveryPolicy(xwork_agent_extension_context* Native)
{
    if(!Native->bRecoverySafe)return XWORK_RESULT_ERROR;
    MdoConfigSnapshot Config={0};Config.Size=sizeof(Config);
    (void)MdoConfigGetSnapshot(&Config);
    if(Native->bRecoveryProbe){
        Native->bRecoveryReady=Config.Revision!=Native->uRecoveryRevision;
        return XWORK_RESULT_OK;
    }
    cstr Stage=Native->sRecoveryStage?Native->sRecoveryStage:"context";
    uint32 Limit=MdoRecoveryRetryLimit();
    bool Resource=strstr(Stage,"resource")!=NULL;
    Native->bRecoveryHandled=true;
    Native->bRecoveryWaiting=Native->uRecoveryAttempt>Limit;
    Native->uRecoveryDelayMs=Resource?MdoRecoveryResourceDelay(Native->uRecoveryAttempt):
        Native->bRecoveryWaiting?MDO_RECOVERY_WAIT_MS:MdoRecoveryDelay(Native->uRecoveryAttempt);
    if(!strcmp(Stage,"loop_guard")&&!Native->bRecoveryWaiting)Native->uRecoveryDelayMs=MDO_RECOVERY_BASE_MS;
    if(!strcmp(Stage,"compaction_quality")||!strcmp(Stage,"verification")||
        ((!strcmp(Stage,"context")||!strcmp(Stage,"loop_guard"))&&Native->bRecoveryWaiting)){
        Native->bRecoveryWaiting=true;Native->bRecoveryNeedsChange=true;
        Native->uRecoveryRevision=Config.Revision;
        Native->uRecoveryDelayMs=MDO_RECOVERY_WAIT_MS;
    }
    return XWORK_RESULT_OK;
}
