/* Faults apply only to the copied HTTP probe app. */
#define MDO_INTENT_FIXTURE_PREFIX "/__fixture/project-purge-intent/"
static unsigned g_MdoIntentFault;

static void MdoIntentCheckpoint(cstr Name)
{
    printf("purge_intent_checkpoint=%s\n", Name); fflush(stdout); xrtSleep(8000u);
}

bool MdoIntentFixtureWrite(cstr Path, const void* Text, size_t Size, bool Backup)
{
    bool Ok;
    if ( g_MdoIntentFault == 1u ) return false;
    Ok = MdoHomeAtomicWrite(Path, Text, Size, Backup);
    if ( Ok && g_MdoIntentFault == 3u ) MdoIntentCheckpoint("saved");
    return Ok && g_MdoIntentFault != 2u;
}

bool MdoIntentFixtureRemove(cstr Path, bool Backup)
{
    bool Ok;
    if ( g_MdoIntentFault == 4u ) return false;
    Ok = MdoHomeRemove(Path, Backup);
    if ( Ok && g_MdoIntentFault == 6u ) MdoIntentCheckpoint("acknowledged");
    return Ok && g_MdoIntentFault != 5u;
}

static bool MdoIntentFixtureControl(XS_HttpReq* Http)
{
    MdoApiContext Context;
    MdoApiJsonBody Body;
    int64 Fault;
    bool Ok;
    xvalue* Data;
    if ( Http == NULL || Http->head == NULL ||
         !MdoApiViewEqualText(Http->head->Target, MDO_INTENT_FIXTURE_PREFIX "fault") ) return false;
    memset(&Context, 0, sizeof(Context)); Context.Request = Http;
    snprintf(Context.RequestId, sizeof(Context.RequestId), "fixture-purge-intent");
    if ( MdoApiJsonBodyRead(&Context, &Body) != MDO_API_BODY_OK ) {
        (void)MdoApiReplyError(&Context, 400u, "fixture_body", "Intent fixture needs JSON", NULL); return true;
    }
    Ok = xrtValueGetInt(xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("value")), &Fault) && Fault >= 0 && Fault <= 6;
    MdoApiJsonBodyUnit(&Body);
    if ( !Ok ) { (void)MdoApiReplyError(&Context, 400u, "fixture_fault", "Invalid intent fixture fault", NULL); return true; }
    g_MdoIntentFault = (unsigned)Fault;
    Data = xrtValueObject();
    (void)MdoApiReplySuccessTake(&Context, 200u, Data, NULL);
    return true;
}
