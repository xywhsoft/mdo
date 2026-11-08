/* Recovery describes a lost live owner, never permission to replay its work. */
static const char* BuInterruptedMessage="This process no longer retains the operation's live owner or outcome. Inspect any effects before retrying.";
static bool BuInterruptedReceipt(xvalue* Value){
    xvalue* Error=xrtValueObject();
    bool Ok=Error&&SdkPut(Error,"code","sdk_operation_interrupted")&&SdkPut(Error,"message",BuInterruptedMessage)&&
        SdkPut(Error,"stage","operation_recovery")&&SdkPutUInt(Error,"status",409)&&
        xrtValueObjectSetNew(Error,XRT_STR_LITERAL("retryable"),xrtValueBool(false))&&
        xrtValueObjectSetNew(Error,XRT_STR_LITERAL("may_have_completed"),xrtValueBool(true));
    if(Ok)Ok=xrtValueObjectSet(Value,XRT_STR_LITERAL("error"),Error);
    xrtValueRelease(Error);
    return Ok&&SdkPut(Value,"state","interrupted")&&SdkPut(Value,"outcome","uncertain")&&
        xrtValueObjectSetNew(Value,XRT_STR_LITERAL("terminal"),xrtValueBool(true));
}
static bool BuToolTargetMatches(const xvalue* Target,cstr Project,cstr Session,cstr Tool){
    uint64 Version=0;
    cstr P=SdkField(Target,"project"),S=SdkField(Target,"session"),T=SdkField(Target,"tool");
    return xrtValueType(Target)==XVALUE_OBJECT&&xrtValueCount(Target)==4&&
        SdkUInt(SdkGet(Target,"version"),&Version)&&Version==1&&P&&S&&T&&
        !strcmp(P,Project)&&!strcmp(S,Session)&&!strcmp(T,Tool);
}
/* Called only after opening and validating the actual product session. The
 * caller's scope can differ from this target. Store IDs, never tool arguments. */
bool MdoApplicationJobToolTarget(const mdo_sdk_context_v1* C,cstr Project,cstr Session,cstr Tool){
    if(!C||!Project||!Session||!Tool)return false;
    const MdoApplicationInvocation* Invocation=C->Opaque;
    if(!Invocation||Invocation->Depth!=1||!SdkOperationId(C->InvocationId))return true;
    if(!SdkJobs.Lock||!xrtMutexLock(SdkJobs.Lock))return false;
    SdkJob* J=SdkJobFind(C->InvocationId);bool Ok=true;
    if(!J||strcmp(J->Method,"tool.call"))goto done;
    if(J->Terminal||J->Disposing||!BuOperationScope(C,J->Info)){Ok=false;goto done;}
    const xvalue* Existing=SdkGet(J->Info,"tool_target");
    if(Existing){Ok=BuToolTargetMatches(Existing,Project,Session,Tool);goto done;}
    xvalue* Target=xrtValueObject();xvalue* Next=xrtValueClone(J->Info);
    Ok=Target&&Next&&SdkPutUInt(Target,"version",1)&&SdkPut(Target,"project",Project)&&
        SdkPut(Target,"session",Session)&&SdkPut(Target,"tool",Tool)&&
        xrtValueObjectSet(Next,XRT_STR_LITERAL("tool_target"),Target);
    if(Ok){
        xvalue* Previous=J->Info;J->Info=Next;Ok=SdkJobSave(J);
        if(Ok){Next=NULL;xrtValueRelease(Previous);}else J->Info=Previous;
    }
    xrtValueRelease(Next);xrtValueRelease(Target);
done:xrtMutexUnlock(SdkJobs.Lock);return Ok;
}
/* Read-only presentation. Physical in-process ownership wins over persisted
 * final metadata. Only a qualified target in an interrupted receipt may add
 * this notice; no raw event, receipt, model message or tool result is changed. */
bool MdoApplicationToolOutcome(cstr Project,cstr Session,cstr Call,cstr Tool,xvalue** Outcome){
    if(!Outcome)return false;
    *Outcome=NULL;
    if(!Project||!Session||!Tool||!SdkOperationId(Call))return true;
    if(!SdkJobs.Lock||!xrtMutexLock(SdkJobs.Lock))return false;
    bool Ok=true;
    if(!SdkJobFind(Call)){
        bool Missing=false;xvalue* Receipt=SdkJobReceipt(Call,&Missing);
        uint64 Version=0;cstr State=SdkField(Receipt,"state"),Method=SdkField(Receipt,"method");
        if(Receipt&&State&&Method&&!strcmp(State,"interrupted")&&!strcmp(Method,"tool.call")&&
           SdkUInt(SdkGet(Receipt,"scope_version"),&Version)&&Version==1&&
           BuToolTargetMatches(SdkGet(Receipt,"tool_target"),Project,Session,Tool)){
            xvalue* Result=xrtValueObject();
            Ok=Result&&SdkPut(Result,"state","interrupted")&&SdkPut(Result,"code","sdk_operation_interrupted")&&
                SdkPut(Result,"message",BuInterruptedMessage)&&
                xrtValueObjectSetNew(Result,XRT_STR_LITERAL("may_have_completed"),xrtValueBool(true));
            if(Ok)*Outcome=Result;else xrtValueRelease(Result);
        }
        xrtValueRelease(Receipt);
    }
    xrtMutexUnlock(SdkJobs.Lock);return Ok;
}
