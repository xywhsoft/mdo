/* Project-owned, bounded portable input history. Included by draft.c so it
 * shares the draft lock and shutdown ordering; project routes hold a lease. */
#include "../sessions/sidecars/queue.h"
#define MDO_PROMPT_HISTORY_MAX 30u
#define MDO_PROMPT_HISTORY_BYTES (192u * 1024u)

static xvalue* MdoPromptHistoryRead(const char* Path)
{
    xfileinfo Info; bool Exists;
    if ( !MdoHomeExternalStat(Path, &Exists, &Info) ) return NULL;
    if ( !Exists ) {
        xvalue* Root = xrtValueObject();
        if ( Root && MdoApiValueSetUInt(Root, "schema_version", 1u) &&
             xrtValueObjectSetNew(Root, XRT_STR_LITERAL("items"), xrtValueArray()) ) return Root;
        xrtValueRelease(Root); return NULL;
    }
    if ( Info.Type != XFILE_TYPE_FILE || !(Info.Available & XFILE_INFO_SIZE) ||
         !Info.Size || Info.Size > 2u * 1024u * 1024u ) return NULL;
    char* Bytes = (char*)xrtMalloc((size_t)Info.Size);
    if ( !Bytes ) return NULL;
    xfile File = MdoHomeOpenRead(Path);
    bool Ok = File && xrtReadFull(File, Bytes, (size_t)Info.Size, NULL);
    if ( File && !xrtClose(File) ) Ok = false;
    xjsonreadconfig Config; xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = 2u * 1024u * 1024u; Config.MaxDepth = 4u;
    Config.MaxValues = 128u; Config.MaxContainerItems = MDO_PROMPT_HISTORY_MAX;
    xvalue* Root = Ok ? xrtJsonRead(xrtStrViewN(Bytes, (size_t)Info.Size), &Config) : NULL;
    xrtFree(Bytes);
    uint64 Schema = 0u;
    const xvalue* Items = xrtValueObjectGet(Root, XRT_STR_LITERAL("items"));
    Ok = xrtValueType(Root) == XVALUE_OBJECT && xrtValueCount(Root) == 2u &&
        MdoDraftUInt(Root, "schema_version", &Schema) && Schema == 1u &&
        xrtValueType(Items) == XVALUE_ARRAY && xrtValueCount(Items) <= MDO_PROMPT_HISTORY_MAX;
    size_t Total = 0u;
    for ( size_t i = 0u; Ok && i < xrtValueCount(Items); ++i ) {
        const xvalue* Item = xrtValueArrayGet(Items, i);
        xstrview Id, Text; char Parsed[33];
        Ok = xrtValueType(Item) == XVALUE_OBJECT && xrtValueCount(Item) == 2u &&
            MdoQueueString(Item, "id", &Id) && MdoQueueId(Id, Parsed) &&
            MdoQueueString(Item, "text", &Text) && Text.Size && MdoQueueText(Text, false);
        for ( size_t j = 0u; Ok && j < i; ++j ) {
            xstrview Previous;
            MdoQueueString(xrtValueArrayGet(Items, j), "id", &Previous);
            if ( Previous.Size == Id.Size && !memcmp(Previous.Data, Id.Data, Id.Size) ) Ok = false;
        }
        if ( Ok ) Total += Text.Size;
    }
    if ( !Ok || Total > MDO_PROMPT_HISTORY_BYTES ) { xrtValueRelease(Root); return NULL; }
    return Root;
}

bool MdoApiPromptHistoryRoute(MdoApiContext* Context)
{
    char Project[MDO_PROJECT_ID_CAPACITY], Path[160];
    bool Found = false;
    if ( Context->ParamCount != 1u || !MdoDraftCaptureId(Context->Params[0], Project, sizeof(Project)) )
        return MdoApiReplyError(Context, 404u, "project_not_found", "Project not found", NULL);
    if ( !MdoDraftGlobalProjectAvailable(Project, &Found) )
        return MdoApiReplyError(Context, 503u, "draft_unavailable", "Input history is unavailable", NULL);
    if ( !Found ) return MdoApiReplyError(Context, 404u, "project_not_found", "Project not found", NULL);
    snprintf(Path, sizeof(Path), "data/prompt-history/%s.json", Project);
    bool Add = Context->Request->head->MethodCode == XHTTP_METHOD_POST;
    MdoApiJsonBody Body = {0}; xstrview Id = {0}, Text = {0}; char Parsed[33];
    if ( Add ) {
        MdoApiBodyStatus Status = MdoApiJsonBodyRead(Context, &Body);
        if ( Status != MDO_API_BODY_OK ) return MdoApiReplyBodyError(Context, Status);
        bool Valid = xrtValueType(Body.Value) == XVALUE_OBJECT && xrtValueCount(Body.Value) == 2u &&
            MdoQueueString(Body.Value, "id", &Id) && MdoQueueId(Id, Parsed) &&
            MdoQueueString(Body.Value, "text", &Text) && Text.Size && MdoQueueText(Text, false);
        if ( !Valid ) { MdoApiJsonBodyUnit(&Body); return MdoApiReplyError(Context, 422u,
            "draft_invalid", "Invalid input history entry", NULL); }
    }
    bool Ok = true, Conflict = false, Duplicate = false;
    xrtMutexLock(g_MdoDraftLock);
    xvalue* Root = MdoPromptHistoryRead(Path);
    const xvalue* Items = xrtValueObjectGet(Root, XRT_STR_LITERAL("items"));
    if ( Root && Add ) {
        size_t Total = Text.Size;
        for ( size_t i = 0u; i < xrtValueCount(Items); ++i ) {
            xstrview SavedId, SavedText;
            MdoQueueString(xrtValueArrayGet(Items, i), "id", &SavedId);
            MdoQueueString(xrtValueArrayGet(Items, i), "text", &SavedText);
            if ( SavedId.Size == Id.Size && !memcmp(SavedId.Data, Id.Data, Id.Size) ) {
                Duplicate = true; Conflict = SavedText.Size != Text.Size || memcmp(SavedText.Data, Text.Data, Text.Size);
            }
            Total += SavedText.Size;
        }
        if ( !Duplicate ) {
            size_t First = xrtValueCount(Items) >= MDO_PROMPT_HISTORY_MAX ? 1u : 0u;
            if ( First ) { xstrview Old; MdoQueueString(xrtValueArrayGet(Items, 0u), "text", &Old); Total -= Old.Size; }
            while ( Total > MDO_PROMPT_HISTORY_BYTES && First < xrtValueCount(Items) ) {
                xstrview Old; MdoQueueString(xrtValueArrayGet(Items, First++), "text", &Old); Total -= Old.Size;
            }
            xvalue* Next = xrtValueArray(); Ok = Next != NULL;
            for ( size_t i = First; Ok && i < xrtValueCount(Items); ++i ) {
                xvalue* Item = xrtValueClone(xrtValueArrayGet(Items, i));
                Ok = MdoApiValueAppendTake(Next, &Item); xrtValueRelease(Item);
            }
            xvalue* Item = xrtValueClone(Body.Value);
            Ok = Ok && MdoApiValueAppendTake(Next, &Item) && MdoApiValueSetTake(Root, "items", &Next);
            xrtValueRelease(Item); xrtValueRelease(Next);
            size_t Size = 0u; char* Json = Ok ? xrtJsonStringify(Root, false, &Size) : NULL;
            Ok = Json && MdoHomeAtomicWrite(Path, Json, Size, false); xrtFree(Json);
        }
    }
    xrtMutexUnlock(g_MdoDraftLock); MdoApiJsonBodyUnit(&Body);
    if ( !Root || !Ok || Conflict ) {
        xrtValueRelease(Root); return MdoApiReplyError(Context, Conflict ? 409u : 503u,
            Conflict ? "draft_conflict" : "draft_unavailable", "Input history could not be read or saved", NULL);
    }
    return MdoApiReplySuccessTake(Context, 200u, Root, NULL);
}
