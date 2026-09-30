/* Copied app only. Hold an admitted request's final leave on a native thread,
 * after its handler replies. Sleeping in the listener callback would prevent
 * a second connection from reaching the gate, even with two engine workers.
 * Only admission flags transfer; no request, socket or stack pointer survives.
 * Product code never uses this fixture. */
static xthread* g_MdoWriteFixtureThread;
static xatomic32 g_MdoWriteFixtureStop;
static MdoApiContext g_MdoWriteFixtureContext;

static int32 MdoWriteFixtureHold(ptr Data)
{
    unsigned i;
    bool Exists;
    xfileinfo Info;
    const char* Kind = g_MdoWriteFixtureContext.WriteExclusive ? "exclusive" : "writer";
    (void)Data;
    (void)MdoHomeAtomicWrite("data/write-admission-probe-paused", Kind, strlen(Kind), false);
    for ( i = 0u; i < 800u && !xrtAtomic32Load(&g_MdoWriteFixtureStop, XMEMORY_ACQUIRE); ++i ) {
        if ( MdoHomeExternalStat("data/write-admission-probe-release", &Exists, &Info) && Exists ) break;
        xrtSleep(10u);
    }
    MdoApiWriteLeave(&g_MdoWriteFixtureContext);
    (void)MdoHomeRemove("data/write-admission-probe-release", false);
    (void)MdoHomeRemove("data/write-admission-probe-paused", false);
    return 0;
}

static void MdoWriteFixtureUnit(void)
{
    xrtAtomic32Store(&g_MdoWriteFixtureStop, 1u, XMEMORY_RELEASE);
    if ( g_MdoWriteFixtureThread != NULL ) {
        (void)xrtThreadWait(g_MdoWriteFixtureThread);
        xrtThreadDestroy(g_MdoWriteFixtureThread);
        g_MdoWriteFixtureThread = NULL;
    }
}

void MdoWriteFixtureDeferLeave(MdoApiContext* Context)
{
    const xhttpfield* Field = NULL;
    if ( xrtHttpFieldGetUnique(Context->Request->head->Fields,
            Context->Request->head->FieldCount, XRT_STR_LITERAL("X-Fixture-Admission-Hold"), &Field) != XHTTP_NEXT_ITEM ||
         Field == NULL || !((Context->WriteShared && MdoApiViewEqualText(Field->Value, "writer")) ||
            (Context->WriteExclusive && MdoApiViewEqualText(Field->Value, "exclusive"))) ) return;
    MdoWriteFixtureUnit();
    memset(&g_MdoWriteFixtureContext, 0, sizeof(g_MdoWriteFixtureContext));
    g_MdoWriteFixtureContext.WriteShared = Context->WriteShared;
    g_MdoWriteFixtureContext.WriteExclusive = Context->WriteExclusive;
    xrtAtomic32Init(&g_MdoWriteFixtureStop, 0u);
    g_MdoWriteFixtureThread = xrtThreadCreate(MdoWriteFixtureHold, NULL, 0u);
    if ( g_MdoWriteFixtureThread != NULL ) {
        Context->WriteShared = false;
        Context->WriteExclusive = false;
    }
}
