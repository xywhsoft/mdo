#include <stdio.h>
#include <string.h>
#include "backup_internal.h"

#define MDO_ORIGIN_PATH "restore-origin.json"
#define MDO_ORIGIN_IMPORTS 16u

/* An origin is passive history. No archived identity/path is ever opened or
 * used to authorize a model, tool, publication or live input. */
static bool MdoOriginError(xwork_error* Error, const char* Message)
{
    const xerror* Cause = xrtGetError();
    return MdoBackupError(Error, Cause != NULL && xrtErrorKind(Cause) == XERR_MEMORY ?
        XWORK_ERROR_OUT_OF_MEMORY : XWORK_ERROR_IO, Message, MDO_ORIGIN_PATH);
}

static bool MdoOriginSet(xvalue* Root, const char* Key, xvalue* Value)
{
    bool Ok = Root != NULL && Value != NULL && xrtValueObjectSetTake(Root, xrtStrView(Key), &Value);
    xrtValueRelease(Value); return Ok;
}

bool MdoBackupWorkspaceAbsolute(xstrview Path)
{
    size_t i;
    bool Drive;
    if ( Path.Size == 0u || Path.Size >= MDO_SESSION_WORKSPACE_CAPACITY || !xrtUtf8Valid(Path, NULL) ) return false;
    for ( i = 0u; i < Path.Size; ++i ) if ( (unsigned char)Path.Data[i] < 32u || Path.Data[i] == 127 ) return false;
    Drive = Path.Size >= 3u && ((Path.Data[0] >= 'A' && Path.Data[0] <= 'Z') ||
        (Path.Data[0] >= 'a' && Path.Data[0] <= 'z')) && Path.Data[1] == ':' &&
        (Path.Data[2] == '/' || Path.Data[2] == '\\');
    if ( Path.Data[0] == '/' || Drive ) return true;
    if ( Path.Size < 5u || Path.Data[0] != '\\' || Path.Data[1] != '\\' ) return false;
    for ( i = 2u; i < Path.Size; ++i )
        if ( Path.Data[i] == '\\' || Path.Data[i] == '/' ) return i > 2u && i + 1u < Path.Size;
    return false;
}

static bool MdoOriginHash(xstrview Text, char Hash[65], const MdoSessionBackupLimits* Limits,
    const xcancel* Cancel, xwork_error* Error)
{
    static const char Hex[] = "0123456789abcdef";
    uint8 Digest[XRT_SHA256_SIZE];
    size_t i;
    if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
    if ( !xrtSha256(Text.Data, Text.Size, Digest) ) return MdoOriginError(Error, "cannot hash restore origin bytes");
    for ( i = 0u; i < sizeof(Digest); ++i ) { Hash[i * 2u] = Hex[Digest[i] >> 4u]; Hash[i * 2u + 1u] = Hex[Digest[i] & 15u]; }
    Hash[64] = '\0'; return MdoBackupCheck(Limits, Cancel, Error);
}

static bool MdoOriginMeta(const xvalue* Value, MdoSessionInfo* Info,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    xstrview Data, Claimed;
    char Hash[65];
    uint64 Bytes;
    if ( xrtValueType(Value) != XVALUE_OBJECT || xrtValueCount(Value) != 3u ||
         !MdoBackupView(Value, "data", &Data) || !MdoBackupView(Value, "sha256", &Claimed) ||
         !MdoBackupUInt(Value, "bytes", &Bytes) || Bytes != Data.Size || Data.Size == 0u || Data.Size > 64u * 1024u ||
         Claimed.Size != 64u || !MdoBackupDigits(Claimed.Data, Claimed.Size, true) ||
         memchr(Data.Data, 0, Data.Size) != NULL || !xrtUtf8Valid(Data, NULL) ) goto invalid;
    if ( !MdoOriginHash(Data, Hash, Limits, Cancel, Error) ) return false;
    if ( memcmp(Hash, Claimed.Data, 64u) != 0 || !MdoBackupMetaRead(Data, Info) ) goto invalid;
    return true;
invalid:
    return MdoOriginError(Error, "invalid origin metadata bytes, hash or live schema");
}

static bool MdoOriginTarget(const MdoSessionInfo* Source, const MdoSessionInfo* Target, int64 RestoredAt)
{
    return strlen(Target->Id) == 32u && MdoBackupDigits(Target->Id, 32u, true) &&
        strcmp(Target->Id, Source->Id) != 0 && MdoBackupWorkspaceAbsolute(xrtStrView(Target->WorkspaceRoot)) &&
        Target->Revision == 1u && Target->CreatedAt == RestoredAt && Target->UpdatedAt == RestoredAt &&
        !Target->Pinned && Target->Status == MDO_SESSION_ACTIVE && Target->PreviousStatus == MDO_SESSION_ACTIVE &&
        Target->ParentSessionId[0] == '\0' && Target->ForkedThroughSequence == 0u &&
        Target->ConfigRevision == 0u && Target->ModelGeneration == 0u && Target->ModuleGeneration == 0u && Target->SkillGeneration == 0u &&
        Target->Protocol == Source->Protocol && Target->MaxOutputTokens == Source->MaxOutputTokens &&
        strcmp(Target->Title, Source->Title) == 0 && strcmp(Target->AgentId, Source->AgentId) == 0 &&
        strcmp(Target->ModelId, Source->ModelId) == 0 && strcmp(Target->ReasoningEffort, Source->ReasoningEffort) == 0 &&
        strcmp(Target->PermissionProfile, Source->PermissionProfile) == 0;
}

static bool MdoOriginPaths(const xvalue* Rows, bool Ui, const MdoSessionBackupLimits* Limits,
    const xcancel* Cancel, xwork_error* Error)
{
    size_t i;
    uint64 Last = 0u;
    if ( xrtValueType(Rows) != XVALUE_ARRAY || xrtValueCount(Rows) > MDO_BACKUP_ORIGIN_MAX_REFS ||
         (!Ui && xrtValueCount(Rows) != 0u) ) goto invalid;
    for ( i = 0u; i < xrtValueCount(Rows); ++i ) {
        const xvalue* Row = xrtValueArrayGet(Rows, i);
        xstrview Source, Target;
        char Relative[MDO_SESSION_BACKUP_PATH_CAPACITY];
        uint64 Id;
        if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
        if ( xrtValueType(Row) != XVALUE_OBJECT || xrtValueCount(Row) != 3u ||
             !MdoBackupUInt(Row, "event_id", &Id) || Id <= Last ||
             !MdoBackupView(Row, "source_path", &Source) || Source.Size > 4096u ||
             !MdoBackupView(Row, "target_path", &Target) || !MdoBackupArtifactRelative(Source, Relative) ||
             Target.Size != strlen(Relative) || memcmp(Target.Data, Relative, Target.Size) != 0 ) goto invalid;
        Last = Id;
    }
    return true;
invalid:
    return MdoOriginError(Error, "invalid passive artifact origin mapping");
}

bool MdoBackupOriginValid(const xvalue* Root, const MdoSessionBackupLimits* Limits,
    const xcancel* Cancel, xwork_error* Error)
{
    const xvalue* Entries = xrtValueObjectGet(Root, XRT_STR_LITERAL("imports"));
    MdoSessionInfo Source, Target;
    char Seen[MDO_ORIGIN_IMPORTS * 2u][MDO_SESSION_ID_CAPACITY];
    uint64 Schema;
    size_t i, j, Count = 0u;
    if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
    if ( xrtValueType(Root) != XVALUE_OBJECT || xrtValueCount(Root) != 2u ||
         !MdoBackupUInt(Root, "schema_version", &Schema) || Schema != 1u ||
         xrtValueType(Entries) != XVALUE_ARRAY || xrtValueCount(Entries) == 0u || xrtValueCount(Entries) > MDO_ORIGIN_IMPORTS ) goto invalid;
    for ( i = 0u; i < xrtValueCount(Entries); ++i ) {
        const xvalue* Entry = xrtValueArrayGet(Entries, i);
        xstrview UiHash;
        uint64 At, Captured;
        bool Ui;
        if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
        if ( xrtValueType(Entry) != XVALUE_OBJECT || xrtValueCount(Entry) != 7u ||
             !MdoBackupUInt(Entry, "restored_at_us", &At) || At == 0u || At > INT64_MAX ||
             !MdoBackupUInt(Entry, "captured_at_us", &Captured) || Captured == 0u || Captured > INT64_MAX ||
             !xrtValueGetBool(xrtValueObjectGet(Entry, XRT_STR_LITERAL("ui_present")), &Ui) ||
             !MdoBackupView(Entry, "source_ui_sha256", &UiHash) ||
             (Ui ? (UiHash.Size != 64u || !MdoBackupDigits(UiHash.Data, UiHash.Size, true)) : UiHash.Size != 0u) ) goto invalid;
        if ( !MdoOriginMeta(xrtValueObjectGet(Entry, XRT_STR_LITERAL("source_meta")), &Source, Limits, Cancel, Error) ||
             !MdoOriginMeta(xrtValueObjectGet(Entry, XRT_STR_LITERAL("target_meta")), &Target, Limits, Cancel, Error) ) return false;
        if ( !MdoOriginTarget(&Source, &Target, (int64)At) ) goto invalid;
        for ( j = 0u; j < Count; ++j ) if ( strcmp(Seen[j], Target.Id) == 0 ) goto invalid;
        memcpy(Seen[Count++], Source.Id, sizeof(Source.Id)); memcpy(Seen[Count++], Target.Id, sizeof(Target.Id));
        if ( !MdoOriginPaths(xrtValueObjectGet(Entry, XRT_STR_LITERAL("artifact_paths")), Ui, Limits, Cancel, Error) ) return false;
    }
    return MdoBackupCheck(Limits, Cancel, Error);
invalid:
    return MdoOriginError(Error, "invalid restore origin schema or identity transition");
}

static xvalue* MdoOriginFile(const MdoBackupOwnedFile* File, const MdoSessionBackupLimits* Limits,
    const xcancel* Cancel, xwork_error* Error)
{
    xvalue* Row = xrtValueObject();
    xstrview Data = xrtStrViewN(File->Data, File->Bytes);
    char Hash[65];
    if ( Row != NULL && MdoOriginHash(Data, Hash, Limits, Cancel, Error) &&
         MdoOriginSet(Row, "data", xrtValueString(Data)) && MdoOriginSet(Row, "bytes", xrtValueUInt(File->Bytes)) &&
         MdoOriginSet(Row, "sha256", xrtValueString(xrtStrView(Hash))) ) return Row;
    xrtValueRelease(Row); return NULL;
}

bool MdoBackupOriginAppend(const MdoSessionBackup* Source, MdoSessionBackup* Copy,
    xvalue** ArtifactPaths, MdoSessionBackupRestoreInfo* Facts,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    const MdoBackupOwnedFile* Before = MdoBackupFind(Source, MDO_ORIGIN_PATH);
    const MdoBackupOwnedFile* Ui = MdoBackupFind(Source, "ui-events.jsonl");
    xvalue *Root = NULL, *Entry = NULL, *Entries;
    char Hash[65] = "";
    size_t i, j;
    bool Ok = false;
    Root = Before != NULL ? MdoBackupJson(Before->Data, Before->Bytes) : xrtValueObject();
    if ( Root == NULL ) goto done;
    if ( Before == NULL && (!MdoOriginSet(Root, "schema_version", xrtValueUInt(1u)) ||
                           !MdoOriginSet(Root, "imports", xrtValueArray())) ) goto done;
    Entries = (xvalue*)xrtValueObjectGet(Root, XRT_STR_LITERAL("imports"));
    if ( Before != NULL && !MdoBackupOriginValid(Root, Limits, Cancel, Error) ) goto done;
    if ( xrtValueCount(Entries) >= MDO_ORIGIN_IMPORTS ) {
        (void)MdoBackupError(Error, XWORK_ERROR_LIMIT, "restore origin import limit reached", MDO_ORIGIN_PATH); goto done;
    }
    for ( i = 0u; i < xrtValueCount(Entries); ++i ) {
        static const char* Names[] = { "source_meta", "target_meta" };
        for ( j = 0u; j < 2u; ++j ) {
            MdoSessionInfo Meta;
            if ( !MdoOriginMeta(xrtValueObjectGet(xrtValueArrayGet(Entries, i), xrtStrView(Names[j])), &Meta, Limits, Cancel, Error) ) goto done;
            if ( strcmp(Meta.Id, Copy->Info.Id) == 0 ) {
                (void)MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "target ID reuses restore history", NULL); goto done;
            }
        }
    }
    if ( Ui != NULL && !MdoOriginHash(xrtStrViewN(Ui->Data, Ui->Bytes), Hash, Limits, Cancel, Error) ) goto done;
    Entry = xrtValueObject();
    if ( Entry == NULL ||
         !MdoOriginSet(Entry, "captured_at_us", xrtValueInt(Source->CapturedAt)) ||
         !MdoOriginSet(Entry, "restored_at_us", xrtValueInt(Copy->Info.CreatedAt)) ||
         !MdoOriginSet(Entry, "source_meta", MdoOriginFile(MdoBackupFind(Source, "meta.json"), Limits, Cancel, Error)) ||
         !MdoOriginSet(Entry, "target_meta", MdoOriginFile(MdoBackupFind(Copy, "meta.json"), Limits, Cancel, Error)) ||
         !MdoOriginSet(Entry, "source_ui_sha256", xrtValueString(xrtStrView(Hash))) ||
         !MdoOriginSet(Entry, "ui_present", xrtValueBool(Ui != NULL)) ||
         !xrtValueObjectSetTake(Entry, XRT_STR_LITERAL("artifact_paths"), ArtifactPaths) ||
         !xrtValueArrayAppendTake(Entries, &Entry) ||
         !MdoBackupOriginValid(Root, Limits, Cancel, Error) ||
         !MdoBackupReplaceJson(Copy, MDO_ORIGIN_PATH, Root, Limits, Cancel, Error) ) goto done;
    Facts->ProvenanceEntries = xrtValueCount(Entries); Ok = true;
done:
    xrtValueRelease(Root); xrtValueRelease(Entry);
    if ( !Ok && Error->eCode == XWORK_ERROR_NONE ) return MdoOriginError(Error, "cannot preserve restore origin");
    return Ok;
}
