/* Included by draft.c: share its lock and shutdown admission ordering. */
#define MDO_SIDEBAR_PATH "data/sidebar-layout.json"
#define MDO_SIDEBAR_BYTES (192u * 1024u)

static bool MdoSidebarString(const xvalue* V, cstr Key, xstrview* Out)
{
    return MdoQueueString(V, Key, Out);
}

static bool MdoSidebarId(xstrview V)
{
    if ( !V.Size || V.Size > 128u ) return false;
    for ( size_t i = 0u; i < V.Size; ++i ) {
        unsigned char C = (unsigned char)V.Data[i];
        if ( !((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
            (C >= '0' && C <= '9') || C == '_' || C == '-' || (i && C == '.')) ) return false;
    }
    return true;
}

static bool MdoSidebarKey(xstrview V)
{
    if ( V.Size < 3u || V.Data[1] != ':' ) return false;
    xstrview Tail = xrtStrViewN(V.Data + 2u, V.Size - 2u);
    if ( V.Data[0] == 'p' ) return MdoSidebarId(Tail);
    if ( V.Data[0] != 's' ) return false;
    const char* Slash = (const char*)memchr(Tail.Data, '/', Tail.Size);
    return Slash && MdoSidebarId(xrtStrViewN(Tail.Data, (size_t)(Slash - Tail.Data))) &&
        MdoSidebarId(xrtStrViewN(Slash + 1u, Tail.Size - (size_t)(Slash - Tail.Data) - 1u));
}

static bool MdoSidebarEqual(xstrview A, xstrview B)
{ return A.Size == B.Size && (!A.Size || !memcmp(A.Data, B.Data, A.Size)); }

static bool MdoSidebarText(xstrview A, cstr B)
{ return MdoSidebarEqual(A, xrtStrView(B)); }

static bool MdoSidebarValid(const xvalue* Root)
{
    uint64 Schema, Revision; xstrview View, Sort;
    const xvalue* Groups = xrtValueObjectGet(Root, XRT_STR_LITERAL("groups"));
    const xvalue* Items = xrtValueObjectGet(Root, XRT_STR_LITERAL("placements"));
    const xvalue* Collapsed = xrtValueObjectGet(Root, XRT_STR_LITERAL("collapsed"));
    if ( xrtValueType(Root) != XVALUE_OBJECT || xrtValueCount(Root) != 7u ||
        !MdoDraftUInt(Root, "schema_version", &Schema) || Schema != 1u ||
        !MdoDraftUInt(Root, "revision", &Revision) || Revision > 9007199254740991ULL ||
        !MdoSidebarString(Root, "view", &View) ||
        !(MdoSidebarText(View, "workspace") || MdoSidebarText(View, "timeline")) ||
        !MdoSidebarString(Root, "project_sort", &Sort) ||
        !(MdoSidebarText(Sort, "name") || MdoSidebarText(Sort, "recent")) ||
        xrtValueType(Groups) != XVALUE_ARRAY || xrtValueCount(Groups) > 32u ||
        xrtValueType(Items) != XVALUE_ARRAY || xrtValueCount(Items) > 2048u ||
        xrtValueType(Collapsed) != XVALUE_ARRAY || xrtValueCount(Collapsed) > 256u ) return false;
    for ( size_t i = 0u; i < xrtValueCount(Groups); ++i ) {
        const xvalue* G = xrtValueArrayGet(Groups, i); xstrview Id, Name; char Parsed[33];
        if ( xrtValueType(G) != XVALUE_OBJECT || xrtValueCount(G) != 2u ||
            !MdoSidebarString(G, "id", &Id) || !MdoQueueId(Id, Parsed) ||
            !MdoSidebarString(G, "name", &Name) || !Name.Size || Name.Size > 256u ) return false;
        bool NonSpace = false;
        for ( size_t k = 0u; k < Name.Size; ++k ) {
            unsigned char C = (unsigned char)Name.Data[k];
            if ( C < 32u || C == 127u ) return false;
            if ( C != ' ' ) NonSpace = true;
        }
        if ( !NonSpace ) return false;
        for ( size_t j = 0u; j < i; ++j ) {
            xstrview Other; MdoSidebarString(xrtValueArrayGet(Groups, j), "id", &Other);
            if ( MdoSidebarEqual(Id, Other) ) return false;
        }
    }
    for ( size_t i = 0u; i < xrtValueCount(Items); ++i ) {
        const xvalue* Item = xrtValueArrayGet(Items, i); xstrview Key, Group;
        if ( xrtValueType(Item) != XVALUE_OBJECT || xrtValueCount(Item) != 2u ||
            !MdoSidebarString(Item, "key", &Key) || !MdoSidebarKey(Key) ||
            !MdoSidebarString(Item, "group", &Group) ) return false;
        bool Found = Group.Size == 0u;
        for ( size_t j = 0u; j < xrtValueCount(Groups); ++j ) {
            xstrview Id; MdoSidebarString(xrtValueArrayGet(Groups, j), "id", &Id);
            if ( MdoSidebarEqual(Group, Id) ) Found = true;
        }
        if ( !Found ) return false;
        for ( size_t j = 0u; j < i; ++j ) {
            xstrview Other; MdoSidebarString(xrtValueArrayGet(Items, j), "key", &Other);
            if ( MdoSidebarEqual(Key, Other) ) return false;
        }
    }
    for ( size_t i = 0u; i < xrtValueCount(Collapsed); ++i ) {
        const xvalue* V = xrtValueArrayGet(Collapsed, i);
        if ( xrtValueType(V) != XVALUE_STRING ) return false;
        xstrview S; if ( !xrtValueGetString(V, &S) ) return false;
        if ( !S.Size || S.Size > 180u || memchr(S.Data, 0, S.Size) ) return false;
        for ( size_t j = 0u; j < S.Size; ++j ) if ( (unsigned char)S.Data[j] < 32u ) return false;
        for ( size_t j = 0u; j < i; ++j ) {
            xstrview Other; xrtValueGetString(xrtValueArrayGet(Collapsed, j), &Other);
            if ( MdoSidebarEqual(S, Other) ) return false;
        }
    }
    return true;
}

static xvalue* MdoSidebarRead(void)
{
    bool Exists; xfileinfo Info;
    if ( !MdoHomeExternalStat(MDO_SIDEBAR_PATH, &Exists, &Info) ) return NULL;
    if ( !Exists ) return xrtJsonParse(xrtStrView("{\"schema_version\":1,\"revision\":0,\"view\":\"workspace\",\"project_sort\":\"name\",\"groups\":[],\"placements\":[],\"collapsed\":[]}"));
    if ( Info.Type != XFILE_TYPE_FILE || !(Info.Available & XFILE_INFO_SIZE) ||
        !Info.Size || Info.Size > MDO_SIDEBAR_BYTES ) return NULL;
    char* Bytes = (char*)xrtMalloc((size_t)Info.Size);
    if ( !Bytes ) return NULL;
    xfile File = MdoHomeOpenRead(MDO_SIDEBAR_PATH);
    bool Ok = File && xrtReadFull(File, Bytes, (size_t)Info.Size, NULL);
    if ( File && !xrtClose(File) ) Ok = false;
    xjsonreadconfig Config; xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_SIDEBAR_BYTES; Config.MaxDepth = 5u;
    Config.MaxValues = 16384u; Config.MaxContainerItems = 2048u;
    xvalue* Root = Ok ? xrtJsonRead(xrtStrViewN(Bytes, (size_t)Info.Size), &Config) : NULL;
    xrtFree(Bytes);
    if ( !MdoSidebarValid(Root) ) { xrtValueRelease(Root); return NULL; }
    return Root;
}

bool MdoApiSidebarLayoutRoute(MdoApiContext* Context)
{
    MdoApiJsonBody Body = {0}; uint64 Expected = 0u, Current = 0u;
    bool Write = Context->Request->head->MethodCode == XHTTP_METHOD_PUT;
    if ( Write ) {
        MdoApiBodyStatus Status = MdoApiJsonBodyRead(Context, &Body);
        if ( Status != MDO_API_BODY_OK ) return MdoApiReplyBodyError(Context, Status);
        if ( Body.Size > MDO_SIDEBAR_BYTES || !MdoSidebarValid(Body.Value) ) {
            MdoApiJsonBodyUnit(&Body);
            return MdoApiReplyError(Context, 422u, "sidebar_layout_invalid", "Invalid sidebar layout", NULL);
        }
        MdoDraftUInt(Body.Value, "revision", &Expected);
    }
    xrtMutexLock(g_MdoDraftLock);
    xvalue* Root = MdoSidebarRead(); bool Ok = Root != NULL, Conflict = false;
    if ( Ok && Write ) {
        MdoDraftUInt(Root, "revision", &Current); Conflict = Current != Expected || Current >= 9007199254740991ULL;
        if ( !Conflict ) {
            xvalue* Next = xrtValueClone(Body.Value);
            Ok = Next && MdoApiValueSetUInt(Next, "revision", Current + 1u);
            size_t Size = 0u; char* Json = Ok ? xrtJsonStringify(Next, false, &Size) : NULL;
            Ok = Json && Size <= MDO_SIDEBAR_BYTES && MdoHomeAtomicWrite(MDO_SIDEBAR_PATH, Json, Size, false);
            xrtFree(Json); xrtValueRelease(Root); Root = Next;
        }
    }
    xrtMutexUnlock(g_MdoDraftLock); MdoApiJsonBodyUnit(&Body);
    if ( !Ok || Conflict ) {
        xrtValueRelease(Root);
        return MdoApiReplyError(Context, Conflict ? 409u : 503u,
            Conflict ? "sidebar_layout_conflict" : "sidebar_layout_unavailable",
            Conflict ? "Sidebar changed in another window; reload before organizing" : "Sidebar layout could not be read or saved", NULL);
    }
    return MdoApiReplySuccessTake(Context, 200u, Root, NULL);
}
