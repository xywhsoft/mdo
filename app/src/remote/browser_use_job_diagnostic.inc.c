static xvalue* MdoRemoteJobDiagnostic(const MdoAccountTransport* Info)
{
    xvalue* value = xrtValueObject(); xvalue* causes = xrtValueArray();
    bool ok = value && causes && MdoAccountSetString(value,"phase",Info->Phase) &&
        MdoAccountSetUInt(value,"attempts",Info->Attempts) &&
        MdoAccountSetUInt(value,"failures",Info->Failures) &&
        MdoAccountSetUInt(value,"elapsed_ms",Info->ElapsedMs) &&
        MdoAccountSetBool(value,"retryable",Info->Retryable);
    for (uint32 i = 0u; ok && i < Info->CauseCount && i < 8u; i++) {
        xvalue* cause = xrtValueObject();
        ok = cause && xrtValueObjectSetNew(cause,XRT_STR_LITERAL("kind"),xrtValueInt(Info->Causes[i].Kind)) &&
            xrtValueObjectSetNew(cause,XRT_STR_LITERAL("code"),xrtValueInt(Info->Causes[i].Code)) &&
            xrtValueObjectSetNew(cause,XRT_STR_LITERAL("system_code"),xrtValueInt(Info->Causes[i].SystemCode)) &&
            MdoAccountSetString(cause,"domain",Info->Causes[i].Domain);
        if (ok) ok = xrtValueArrayAppendNew(causes,cause); else xrtValueRelease(cause);
    }
    if (ok) ok = xrtValueObjectSetNew(value,XRT_STR_LITERAL("causes"),causes);
    else xrtValueRelease(causes);
    if (!ok) { xrtValueRelease(value); return NULL; }
    return value;
}
