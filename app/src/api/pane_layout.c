#include <string.h>

#include "internal.h"
#include "../../include/mdo/home.h"

#define MDO_PANE_LAYOUT_PATH "data/pane-layout.json"
#define MDO_PANE_LAYOUT_MAX_BYTES 512u

typedef struct MdoPaneLayout {
    uint32 SidebarWidth;
    bool SidebarOpen;
} MdoPaneLayout;

static void MdoPaneLayoutDefaults(MdoPaneLayout* Layout)
{
    Layout->SidebarWidth = 272u;
    Layout->SidebarOpen = true;
}

static bool MdoPaneLayoutUInt(const xvalue* Object, cstr Key,
    uint32 Minimum, uint32 Maximum, uint32* Result)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    uint64 Unsigned = 0u;
    int64 Signed = 0;
    if ( xrtValueType(Value) == XVALUE_UINT ) {
        if ( !xrtValueGetUInt(Value, &Unsigned) ) return false;
    } else if ( xrtValueType(Value) == XVALUE_INT ) {
        if ( !xrtValueGetInt(Value, &Signed) || Signed < 0 ) return false;
        Unsigned = (uint64)Signed;
    } else return false;
    if ( Unsigned < Minimum || Unsigned > Maximum ) return false;
    *Result = (uint32)Unsigned;
    return true;
}

static bool MdoPaneLayoutBool(const xvalue* Object, cstr Key, bool* Result)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    return xrtValueType(Value) == XVALUE_BOOL &&
        xrtValueGetBool(Value, Result);
}

static bool MdoPaneLayoutFields(const xvalue* Object,
    MdoPaneLayout* Layout)
{
    return MdoPaneLayoutUInt(Object, "sidebar_width", 264u, 420u,
            &Layout->SidebarWidth) &&
        MdoPaneLayoutBool(Object, "sidebar_open", &Layout->SidebarOpen);
}

static bool MdoPaneLayoutRead(MdoPaneLayout* Layout)
{
    bool Exists = false;
    xfileinfo Info;
    xfile File = NULL;
    char Bytes[MDO_PANE_LAYOUT_MAX_BYTES + 1u];
    xjsonreadconfig Config;
    xvalue* Root = NULL;
    uint32 Version = 0u;
    bool Ok = false;

    MdoPaneLayoutDefaults(Layout);
    if ( !MdoHomeExternalStat(MDO_PANE_LAYOUT_PATH, &Exists, &Info) )
        return false;
    if ( !Exists ) return true;
    if ( Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Size > MDO_PANE_LAYOUT_MAX_BYTES ) return false;
    File = MdoHomeOpenRead(MDO_PANE_LAYOUT_PATH);
    if ( File == NULL ||
         (Info.Size != 0u &&
          !xrtReadFull(File, Bytes, (size_t)Info.Size, NULL)) ) goto done;
    Bytes[Info.Size] = '\0';
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_PANE_LAYOUT_MAX_BYTES;
    Config.MaxDepth = 2u;
    Config.MaxValues = 8u;
    Config.MaxContainerItems = 5u;
    Root = xrtJsonRead(xrtStrViewN(Bytes, (size_t)Info.Size), &Config);
    Ok = Root != NULL && xrtValueType(Root) == XVALUE_OBJECT &&
        MdoPaneLayoutUInt(Root, "schema_version", 1u, 2u, &Version) &&
        xrtValueCount(Root) == (Version == 1u ? 5u : 3u) &&
        MdoPaneLayoutFields(Root, Layout);
    if ( Ok && Version == 1u ) {
        // Read old geometry without rewriting it on startup. The next user
        // change saves only the surviving sidebar preference in schema 2.
        uint32 IgnoredWidth = 0u;
        bool IgnoredOpen = false;
        Ok = MdoPaneLayoutUInt(Root, "inspector_width", 300u, 520u,
                &IgnoredWidth) &&
            MdoPaneLayoutBool(Root, "inspector_open", &IgnoredOpen);
    }
done:
    xrtValueRelease(Root);
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    return Ok;
}

static bool MdoPaneLayoutValue(xvalue* Object,
    const MdoPaneLayout* Layout, bool IncludeSchema)
{
    return (!IncludeSchema ||
            MdoApiValueSetUInt(Object, "schema_version", 2u)) &&
        MdoApiValueSetUInt(Object, "sidebar_width", Layout->SidebarWidth) &&
        MdoApiValueSetBool(Object, "sidebar_open", Layout->SidebarOpen);
}

static bool MdoPaneLayoutWrite(const MdoPaneLayout* Layout)
{
    xvalue* Root = xrtValueObject();
    char* Json = NULL;
    size_t Size = 0u;
    bool Ok = Root != NULL && MdoPaneLayoutValue(Root, Layout, true);
    if ( Ok ) Json = xrtJsonStringify(Root, false, &Size);
    Ok = Json != NULL && Size <= MDO_PANE_LAYOUT_MAX_BYTES &&
        MdoHomeAtomicWrite(MDO_PANE_LAYOUT_PATH, Json, Size, false);
    xrtFree(Json);
    xrtValueRelease(Root);
    return Ok;
}

bool MdoApiPaneLayoutRoute(MdoApiContext* Context)
{
    MdoPaneLayout Layout;
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus;
    xvalue* Data;
    bool Ok;

    if ( Context->Request->head->MethodCode == XHTTP_METHOD_PUT ) {
        BodyStatus = MdoApiJsonBodyRead(Context, &Body);
        if ( BodyStatus != MDO_API_BODY_OK )
            return MdoApiReplyBodyError(Context, BodyStatus);
        Ok = Body.Size <= MDO_PANE_LAYOUT_MAX_BYTES &&
            xrtValueType(Body.Value) == XVALUE_OBJECT &&
            xrtValueCount(Body.Value) == 2u &&
            MdoPaneLayoutFields(Body.Value, &Layout);
        MdoApiJsonBodyUnit(&Body);
        if ( !Ok ) return MdoApiReplyError(Context, 422u,
            "pane_layout_invalid", "Pane layout fields are invalid", NULL);
        if ( !MdoPaneLayoutWrite(&Layout) )
            return MdoApiReplyError(Context, 503u, "pane_layout_unavailable",
                "Pane layout could not be saved", NULL);
    } else if ( !MdoPaneLayoutRead(&Layout) )
        return MdoApiReplyError(Context, 503u, "pane_layout_unavailable",
            "Pane layout could not be read", NULL);
    Data = xrtValueObject();
    Ok = Data != NULL && MdoPaneLayoutValue(Data, &Layout, false);
    if ( !Ok ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u, "pane_layout_response_unavailable",
            "Pane layout response could not be created", NULL);
    }
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}
