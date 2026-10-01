#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backup_internal.h"
#include "internal.h"

/* SAX keeps only the small envelope and one in-progress file. The JSON reader
 * owns its current decoded token; no borrowed token or base64 tree survives a
 * callback. Keys are checked here: JsonVisit does not enforce DOM duplicate-key
 * policy. Product JSON is subsequently parsed with duplicate rejection. */
static const char* const MdoDecodeKeys[] = {
    "export_schema", "format", "captured_at_us", "project_id", "session_id",
    "revision", "source_workspace", "scope", "history_retention", "validation",
    "restore_ready", "checksum", "queue_restore_policy", "file_count", "total_bytes",
    "ui_first_event_id", "ui_last_event_id", "ui_records", "absent_files", "files",
    "exported_at_us", "meta", "snapshot"
};

#define MDO_DECODE_V2_KEYS ((UINT32_C(1) << 20u) - 1u)
#define MDO_DECODE_V1_KEYS ((UINT32_C(1) << 0u) | (UINT32_C(1) << 20u) | \
    (UINT32_C(1) << 21u) | (UINT32_C(1) << 22u))

typedef enum MdoDecodeMode {
    MDO_DECODE_ROOT, MDO_DECODE_FILES, MDO_DECODE_FILE,
    MDO_DECODE_ABSENT, MDO_DECODE_LEGACY
} MdoDecodeMode;

typedef struct MdoDecode {
    const char* Input;
    size_t InputBytes;
    MdoSessionBackup* Backup;
    const xcancel* Cancel;
    xwork_error Error;
    xvalue* Manifest;
    uint32 Keys, Absent, FileKeys;
    MdoDecodeMode Mode;
    MdoBackupOwnedFile File;
    uint64 Declared;
    uint8 Hash[XRT_SHA256_SIZE];
    size_t LegacyStart;
    const char* LegacyPath;
    bool Began, Ended;
} MdoDecode;

static bool MdoDecodeEqual(xstrview Text, const char* Literal)
{
    return Text.Size == strlen(Literal) && memcmp(Text.Data, Literal, Text.Size) == 0;
}

static int MdoDecodeKey(xstrview Text, const char* const* Keys, size_t Count)
{
    size_t i;
    for ( i = 0u; i < Count; ++i ) if ( MdoDecodeEqual(Text, Keys[i]) ) return (int)i;
    return -1;
}

static bool MdoDecodeInvalid(MdoDecode* Decode, const char* Message, const char* Path)
{
    return MdoBackupError(&Decode->Error, XWORK_ERROR_IO, Message, Path);
}

static bool MdoDecodeMemory(MdoDecode* Decode)
{
    return MdoBackupError(&Decode->Error, XWORK_ERROR_OUT_OF_MEMORY,
        "cannot allocate decoded session backup", NULL);
}

static bool MdoDecodeSchemaFailure(MdoDecode* Decode, const char* Message, const char* Path)
{
    const xerror* Error = xrtGetError();
    if ( Error != NULL && xrtErrorKind(Error) == XERR_MEMORY ) return MdoDecodeMemory(Decode);
    return MdoDecodeInvalid(Decode, Message, Path);
}

static bool MdoDecodeUInt(const xjsonevent* Event, uint64* Value)
{
    if ( Event->Type == XJSON_EVENT_UINT ) { *Value = Event->Value.Unsigned; return true; }
    if ( Event->Type != XJSON_EVENT_INT || Event->Value.Integer < 0 ) return false;
    *Value = (uint64)Event->Value.Integer;
    return true;
}

static bool MdoDecodeText(xstrview Text, char* Output, size_t Capacity)
{
    if ( Text.Size >= Capacity || memchr(Text.Data, 0, Text.Size) != NULL ) return false;
    memcpy(Output, Text.Data, Text.Size); Output[Text.Size] = '\0';
    return true;
}

static bool MdoDecodeBudget(MdoDecode* Decode, size_t Bytes)
{
    const MdoSessionBackup* Backup = Decode->Backup;
    if ( Backup->Count >= Backup->Limits.Files || Bytes > Backup->Limits.FileBytes ||
         Bytes > Backup->Limits.TotalBytes - Backup->Bytes )
        return MdoBackupError(&Decode->Error, XWORK_ERROR_LIMIT,
            "decoded session backup exceeds file or total budget", NULL);
    return true;
}

static bool MdoDecodeBase64(MdoDecode* Decode, xstrview Text)
{
    size_t Bytes = 0u, Written = 0u;
    if ( !xrtBase64Decode(Text.Data, Text.Size, NULL, 0u, &Bytes, NULL) )
        return MdoDecodeInvalid(Decode, "invalid session backup base64", NULL);
    if ( !MdoDecodeBudget(Decode, Bytes) ||
         !MdoBackupCheck(&Decode->Backup->Limits, Decode->Cancel, &Decode->Error) ) return false;
    Decode->File.Data = (char*)xrtMalloc(Bytes + 1u);
    if ( Decode->File.Data == NULL ) return MdoDecodeMemory(Decode);
    if ( !xrtBase64Decode(Text.Data, Text.Size, Decode->File.Data, Bytes, &Written, NULL) ||
         Written != Bytes ) return MdoDecodeInvalid(Decode, "cannot decode session backup file", NULL);
    Decode->File.Data[Bytes] = '\0'; Decode->File.Bytes = Bytes;
    return true;
}

static bool MdoDecodeCanonicalPath(const char* Path)
{
    const char* Number = NULL;
    if ( strncmp(Path, "attachments/events/", 19u) == 0 ) Number = Path + 19u;
    else if ( strncmp(Path, "attachments/runs/", 17u) == 0 ) Number = Path + 17u;
    /* Event/run references have unpadded decimal names. Artifact directory and
     * file IDs deliberately use twenty padded digits in the production writer. */
    return MdoBackupPathLimit(Path, false) != 0u && (Number == NULL || Number[0] != '0');
}

static bool MdoDecodeFileDone(MdoDecode* Decode)
{
    uint8 Digest[XRT_SHA256_SIZE];
    size_t Limit;
    if ( Decode->FileKeys != 31u || !MdoDecodeCanonicalPath(Decode->File.Path) ||
         Decode->Declared != Decode->File.Bytes )
        return MdoDecodeInvalid(Decode, "invalid session backup file declaration", Decode->File.Path);
    Limit = MdoBackupPathLimit(Decode->File.Path, false);
    if ( Decode->File.Bytes > Limit )
        return MdoBackupError(&Decode->Error, XWORK_ERROR_LIMIT,
            "decoded session file exceeds its product limit", Decode->File.Path);
    if ( !MdoDecodeBudget(Decode, Decode->File.Bytes) ) return false;
    if ( !xrtSha256(Decode->File.Data, Decode->File.Bytes, Digest) ||
         memcmp(Digest, Decode->Hash, sizeof(Digest)) != 0 )
        return MdoDecodeInvalid(Decode, "session backup file checksum differs", Decode->File.Path);
    Decode->Backup->Files[Decode->Backup->Count++] = Decode->File;
    Decode->Backup->Bytes += Decode->File.Bytes;
    memset(&Decode->File, 0, sizeof(Decode->File));
    Decode->Mode = MDO_DECODE_FILES;
    return true;
}

static bool MdoDecodeFileValue(MdoDecode* Decode, const xjsonevent* Event)
{
    static const char* const Keys[] = { "path", "bytes", "sha256", "encoding", "data" };
    int Key = Event->HasName ? MdoDecodeKey(Event->Name, Keys, 5u) : -1;
    size_t HashBytes = 0u;
    if ( Key < 0 || (Decode->FileKeys & (1u << (unsigned)Key)) != 0u ) goto invalid;
    Decode->FileKeys |= 1u << (unsigned)Key;
    if ( Key == 1 ) {
        if ( !MdoDecodeUInt(Event, &Decode->Declared) ) goto invalid;
        if ( Decode->Declared > Decode->Backup->Limits.FileBytes ||
             Decode->Declared > Decode->Backup->Limits.TotalBytes - Decode->Backup->Bytes )
            return MdoBackupError(&Decode->Error, XWORK_ERROR_LIMIT,
                "declared session file exceeds byte budget", NULL);
        return true;
    }
    if ( Event->Type != XJSON_EVENT_STRING ) goto invalid;
    if ( Key == 0 ) {
        if ( !MdoDecodeText(Event->Value.String, Decode->File.Path, sizeof(Decode->File.Path)) ||
             !MdoDecodeCanonicalPath(Decode->File.Path) ) goto invalid;
    } else if ( Key == 2 ) {
        if ( Event->Value.String.Size != 64u ||
             !MdoBackupDigits(Event->Value.String.Data, 64u, true) ||
             !xrtHexDecode(Event->Value.String, Decode->Hash, sizeof(Decode->Hash), &HashBytes, 0u) ||
             HashBytes != sizeof(Decode->Hash) ) goto invalid;
    } else if ( Key == 3 ) {
        if ( !MdoDecodeEqual(Event->Value.String, "base64") ) goto invalid;
    } else return MdoDecodeBase64(Decode, Event->Value.String);
    return true;
invalid:
    return MdoDecodeInvalid(Decode, "invalid or duplicate session backup file field", NULL);
}

static bool MdoDecodeScalar(MdoDecode* Decode, const xjsonevent* Event, int Key)
{
    xvalue* Value = NULL;
    if ( Event->Type == XJSON_EVENT_STRING ) {
        if ( Event->Value.String.Size >= MDO_SESSION_WORKSPACE_CAPACITY ||
             memchr(Event->Value.String.Data, 0, Event->Value.String.Size) != NULL ) goto invalid;
        Value = xrtValueString(Event->Value.String);
    } else if ( Event->Type == XJSON_EVENT_INT ) Value = xrtValueInt(Event->Value.Integer);
    else if ( Event->Type == XJSON_EVENT_UINT ) Value = xrtValueUInt(Event->Value.Unsigned);
    else if ( Event->Type == XJSON_EVENT_BOOL ) Value = xrtValueBool(Event->Value.Boolean);
    else goto invalid;
    if ( Value == NULL || !xrtValueObjectSetNew(Decode->Manifest, xrtStrView(MdoDecodeKeys[Key]), Value) )
        return MdoDecodeMemory(Decode);
    return true;
invalid:
    return MdoDecodeInvalid(Decode, "invalid session backup envelope field", MdoDecodeKeys[Key]);
}

static bool MdoDecodeLegacyDone(MdoDecode* Decode, size_t End)
{
    MdoBackupOwnedFile* File;
    size_t Bytes;
    if ( End >= Decode->InputBytes || End < Decode->LegacyStart )
        return MdoDecodeInvalid(Decode, "invalid legacy session backup range", NULL);
    Bytes = End - Decode->LegacyStart + 1u;
    if ( !MdoDecodeBudget(Decode, Bytes) ) return false;
    if ( Bytes > MdoBackupPathLimit(Decode->LegacyPath, false) )
        return MdoBackupError(&Decode->Error, XWORK_ERROR_LIMIT,
            "legacy session file exceeds its product limit", Decode->LegacyPath);
    File = &Decode->Backup->Files[Decode->Backup->Count];
    File->Data = (char*)xrtMalloc(Bytes + 1u);
    if ( File->Data == NULL ) return MdoDecodeMemory(Decode);
    memcpy(File->Data, Decode->Input + Decode->LegacyStart, Bytes); File->Data[Bytes] = '\0';
    snprintf(File->Path, sizeof(File->Path), "%s", Decode->LegacyPath); File->Bytes = Bytes;
    ++Decode->Backup->Count; Decode->Backup->Bytes += Bytes;
    Decode->Mode = MDO_DECODE_ROOT;
    return true;
}

static xjsonvisitaction MdoDecodeVisit(const xjsonevent* Event, void* UserData)
{
    MdoDecode* Decode = (MdoDecode*)UserData;
    bool Ok = false;
    if ( !MdoBackupCheck(&Decode->Backup->Limits, Decode->Cancel, &Decode->Error) ) return XJSON_VISIT_FAIL;
    if ( !Decode->Began ) {
        if ( Event->Depth == 0u && Event->Type == XJSON_EVENT_OBJECT_BEGIN ) {
            Decode->Began = true; return XJSON_VISIT_NEXT;
        }
        goto invalid;
    }
    if ( Decode->Ended ) goto invalid;
    if ( Decode->Mode == MDO_DECODE_LEGACY ) {
        if ( Event->Depth > 1u ) return XJSON_VISIT_NEXT;
        if ( Event->Depth == 1u && Event->Type == XJSON_EVENT_OBJECT_END )
            Ok = MdoDecodeLegacyDone(Decode, Event->Location.Offset);
        else goto invalid;
    } else if ( Decode->Mode == MDO_DECODE_FILE ) {
        if ( Event->Depth == 3u ) Ok = MdoDecodeFileValue(Decode, Event);
        else if ( Event->Depth == 2u && Event->Type == XJSON_EVENT_OBJECT_END )
            Ok = MdoDecodeFileDone(Decode);
        else goto invalid;
    } else if ( Decode->Mode == MDO_DECODE_FILES ) {
        if ( Event->Depth == 1u && Event->Type == XJSON_EVENT_ARRAY_END ) {
            Decode->Mode = MDO_DECODE_ROOT; Ok = true;
        } else if ( Event->Depth == 2u && Event->Type == XJSON_EVENT_OBJECT_BEGIN ) {
            if ( !MdoDecodeBudget(Decode, 0u) ) return XJSON_VISIT_FAIL;
            Decode->Mode = MDO_DECODE_FILE; Decode->FileKeys = 0u; Decode->Declared = 0u; Ok = true;
        } else goto invalid;
    } else if ( Decode->Mode == MDO_DECODE_ABSENT ) {
        if ( Event->Depth == 1u && Event->Type == XJSON_EVENT_ARRAY_END ) {
            Decode->Mode = MDO_DECODE_ROOT; Ok = true;
        } else if ( Event->Depth == 2u && Event->Type == XJSON_EVENT_STRING ) {
            int Key = MdoDecodeKey(Event->Value.String, MdoBackupOptionalFiles, MDO_BACKUP_OPTIONAL_FILES);
            if ( Key < 0 || (Decode->Absent & (1u << (unsigned)Key)) != 0u ) goto invalid;
            Decode->Absent |= 1u << (unsigned)Key; Ok = true;
        } else goto invalid;
    } else if ( Event->Depth == 0u && Event->Type == XJSON_EVENT_OBJECT_END ) {
        Decode->Ended = true; Ok = true;
    } else if ( Event->Depth == 1u && Event->HasName ) {
        int Key = MdoDecodeKey(Event->Name, MdoDecodeKeys, sizeof(MdoDecodeKeys) / sizeof(MdoDecodeKeys[0]));
        if ( Key < 0 || (Decode->Keys & (1u << (unsigned)Key)) != 0u ) goto invalid;
        Decode->Keys |= 1u << (unsigned)Key;
        if ( Key == 18 || Key == 19 ) {
            if ( Event->Type != XJSON_EVENT_ARRAY_BEGIN ) goto invalid;
            Decode->Mode = Key == 18 ? MDO_DECODE_ABSENT : MDO_DECODE_FILES; Ok = true;
        } else if ( Key == 21 || Key == 22 ) {
            if ( Event->Type != XJSON_EVENT_OBJECT_BEGIN ) goto invalid;
            Decode->Mode = MDO_DECODE_LEGACY; Decode->LegacyStart = Event->Location.Offset;
            Decode->LegacyPath = Key == 21 ? "meta.json" : "snapshot.json"; Ok = true;
        } else Ok = MdoDecodeScalar(Decode, Event, Key);
    } else goto invalid;
    return Ok ? XJSON_VISIT_NEXT : XJSON_VISIT_FAIL;
invalid:
    (void)MdoDecodeInvalid(Decode, "invalid, unknown or duplicate session backup field", NULL);
    return XJSON_VISIT_FAIL;
}

static bool MdoDecodeLiteral(const xvalue* Root, const char* Key, const char* Literal)
{
    xstrview Text;
    return MdoBackupView(Root, Key, &Text) && MdoDecodeEqual(Text, Literal);
}

static bool MdoDecodeMeta(MdoDecode* Decode, bool Legacy)
{
    const MdoBackupOwnedFile* File = MdoBackupFind(Decode->Backup, "meta.json");
    xvalue* Root = NULL;
    const xvalue* Ids = Decode->Manifest;
    xstrview Project, Session, Workspace;
    char ProjectId[MDO_PROJECT_ID_CAPACITY], SessionId[MDO_SESSION_ID_CAPACITY];
    uint64 Revision;
    bool Ok = false;
    xrtClearError();
    if ( File == NULL || MdoBackupFind(Decode->Backup, "snapshot.json") == NULL ) goto done;
    if ( Legacy ) {
        xjsonreadconfig Config;
        xrtJsonReadConfigInit(&Config); Config.MaxInputBytes = 64u * 1024u;
        Config.MaxDepth = 8u; Config.MaxValues = 32u; Config.MaxContainerItems = 24u;
        Root = xrtJsonRead(xrtStrViewN(File->Data, File->Bytes), &Config); Ids = Root;
    }
    if ( !MdoBackupView(Ids, "project_id", &Project) ||
         !MdoBackupView(Ids, Legacy ? "id" : "session_id", &Session) ||
         !MdoDecodeText(Project, ProjectId, sizeof(ProjectId)) ||
         !MdoDecodeText(Session, SessionId, sizeof(SessionId)) ||
         !MdoSessionsInternalMetaParse(ProjectId, SessionId, xrtStrViewN(File->Data, File->Bytes),
            &Decode->Backup->Info) ) goto done;
    if ( !Legacy && (!MdoBackupUInt(Ids, "revision", &Revision) ||
            Revision != Decode->Backup->Info.Revision ||
            !MdoBackupView(Ids, "source_workspace", &Workspace) ||
            !MdoDecodeEqual(Workspace, Decode->Backup->Info.WorkspaceRoot)) ) goto done;
    Ok = true;
done:
    xrtValueRelease(Root);
    return Ok || MdoDecodeSchemaFailure(Decode, "session backup metadata identity differs or is missing", "meta.json");
}

static bool MdoDecodeManifest(MdoDecode* Decode)
{
    uint64 Schema, Captured, Files, Bytes, First = 0u, Last = 0u, Records = 0u;
    bool RestoreReady;
    size_t i;
    if ( !Decode->Ended || !MdoBackupUInt(Decode->Manifest, "export_schema", &Schema) ||
         (Schema != 1u && Schema != MDO_SESSION_BACKUP_SCHEMA) ) goto invalid;
    if ( Schema == 1u ) {
        if ( Decode->Keys != MDO_DECODE_V1_KEYS || Decode->Backup->Count != 2u ||
             !MdoBackupUInt(Decode->Manifest, "exported_at_us", &Captured) ||
             Captured == 0u || Captured > INT64_MAX || !MdoDecodeMeta(Decode, true) ) goto invalid;
    } else {
        if ( Decode->Keys != MDO_DECODE_V2_KEYS ||
             !MdoDecodeLiteral(Decode->Manifest, "format", "mdo-session-backup") ||
             !MdoDecodeLiteral(Decode->Manifest, "scope", "retained-session-files") ||
             !MdoDecodeLiteral(Decode->Manifest, "history_retention", "earlier-content-may-have-been-pruned") ||
             !MdoDecodeLiteral(Decode->Manifest, "validation", "json-syntax-and-resource-references") ||
             !MdoDecodeLiteral(Decode->Manifest, "checksum", "sha256") ||
             !MdoDecodeLiteral(Decode->Manifest, "queue_restore_policy", "require-user-confirmation") ||
             !xrtValueGetBool(xrtValueObjectGet(Decode->Manifest, XRT_STR_LITERAL("restore_ready")), &RestoreReady) ||
             RestoreReady ||
             !MdoBackupUInt(Decode->Manifest, "captured_at_us", &Captured) || Captured == 0u || Captured > INT64_MAX ||
             !MdoBackupUInt(Decode->Manifest, "file_count", &Files) || Files != Decode->Backup->Count ||
             !MdoBackupUInt(Decode->Manifest, "total_bytes", &Bytes) || Bytes != Decode->Backup->Bytes ||
             !MdoBackupUInt(Decode->Manifest, "ui_first_event_id", &First) ||
             !MdoBackupUInt(Decode->Manifest, "ui_last_event_id", &Last) ||
             !MdoBackupUInt(Decode->Manifest, "ui_records", &Records) || !MdoDecodeMeta(Decode, false) ) goto invalid;
        for ( i = 0u; i < MDO_BACKUP_OPTIONAL_FILES; ++i ) {
            bool Absent = MdoBackupFind(Decode->Backup, MdoBackupOptionalFiles[i]) == NULL;
            if ( Absent != ((Decode->Absent & (1u << i)) != 0u) ) goto invalid;
        }
    }
    if ( !MdoBackupValidate(Decode->Backup, &Decode->Backup->Limits, Decode->Cancel,
            &Decode->Backup->History, &Decode->Error) ) return false;
    /* Metadata/attachment schemas are already checked by the common reader.
     * Validate UI and todo with the exact live parsers, without creating a
     * bridge or projecting state. Other product schemas/replay remain a
     * distinct, required gate before restoration may be offered. */
    for ( i = 0u; i < Decode->Backup->Count; ++i ) {
        const MdoBackupOwnedFile* File = &Decode->Backup->Files[i];
        if ( !MdoBackupCheck(&Decode->Backup->Limits, Decode->Cancel, &Decode->Error) ) return false;
        if ( strcmp(File->Path, "todo.json") == 0 ) {
            xrtClearError();
            if ( !MdoSessionsInternalTodoValid(xrtStrViewN(File->Data, File->Bytes)) )
                return MdoDecodeSchemaFailure(Decode, "invalid session backup todo schema", File->Path);
        }
        if ( strcmp(File->Path, "ui-events.jsonl") == 0 ) {
            size_t Offset = 0u;
            while ( Offset < File->Bytes ) {
                const char* End = (const char*)memchr(File->Data + Offset, '\n', File->Bytes - Offset);
                size_t LineBytes;
                if ( End == NULL ) return MdoDecodeInvalid(Decode, "partial session backup UI record", File->Path);
                LineBytes = (size_t)(End - File->Data - Offset);
                xrtClearError();
                if ( !MdoSessionsInternalEventValid(Decode->Backup->Info.ProjectId, Decode->Backup->Info.Id,
                        xrtStrViewN(File->Data + Offset, LineBytes)) )
                    return MdoDecodeSchemaFailure(Decode, "invalid session backup UI schema or identity", File->Path);
                Offset += LineBytes + 1u;
                if ( !MdoBackupCheck(&Decode->Backup->Limits, Decode->Cancel, &Decode->Error) ) return false;
            }
        }
    }
    if ( Schema == MDO_SESSION_BACKUP_SCHEMA &&
         (First != Decode->Backup->History.First || Last != Decode->Backup->History.Last ||
          Records != Decode->Backup->History.Records) ) goto invalid;
    Decode->Backup->Schema = (uint32)Schema; Decode->Backup->CapturedAt = (int64)Captured;
    Decode->Backup->Decoded = true;
    return true;
invalid:
    if ( Decode->Error.eCode != XWORK_ERROR_NONE ) return false;
    return MdoDecodeInvalid(Decode, "invalid session backup format, totals or retention declaration", NULL);
}

static int MdoDecodePortableCompare(const void* Left, const void* Right)
{
    const char* A = (*(const MdoBackupOwnedFile* const*)Left)->Path;
    const char* B = (*(const MdoBackupOwnedFile* const*)Right)->Path;
    for ( ;; ++A, ++B ) {
        unsigned char X = (unsigned char)*A, Y = (unsigned char)*B;
        if ( X >= 'A' && X <= 'Z' ) X += 'a' - 'A';
        if ( Y >= 'A' && Y <= 'Z' ) Y += 'a' - 'A';
        if ( X != Y ) return X < Y ? -1 : 1;
        if ( X == 0u ) return 0;
    }
}

static bool MdoDecodePaths(MdoDecode* Decode)
{
    /* The whitelist is ASCII. Distinct Linux artifact names can alias on
     * Windows; reject them before any future restore staging. Sort only this
     * small pointer index so the exact-path lookup order remains unchanged. */
    const MdoBackupOwnedFile* Names[MDO_SESSION_BACKUP_MAX_FILES];
    size_t i;
    for ( i = 0u; i < Decode->Backup->Count; ++i ) Names[i] = &Decode->Backup->Files[i];
    qsort(Names, Decode->Backup->Count, sizeof(*Names), MdoDecodePortableCompare);
    for ( i = 1u; i < Decode->Backup->Count; ++i ) {
        if ( MdoDecodePortableCompare(&Names[i - 1u], &Names[i]) == 0 )
            return MdoDecodeInvalid(Decode, "duplicate or portable alias session backup path", Names[i]->Path);
    }
    return true;
}

MdoSessionBackup* MdoSessionBackupDecode(const void* Document, size_t Bytes,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    MdoDecode Decode;
    MdoSessionBackupLimits Budget;
    xjsonreadconfig Config;
    bool Ok = false;
    xworkErrorInit(Error);
    memset(&Decode, 0, sizeof(Decode)); xworkErrorInit(&Decode.Error);
    if ( Document == NULL || Bytes == 0u ) {
        (void)MdoBackupError(Error, XWORK_ERROR_INVALID_ARGUMENT, "session backup document is required", NULL);
        return NULL;
    }
    if ( !MdoBackupLimits(Limits, &Budget, 30000000u, Error) ) return NULL;
    if ( Bytes > Budget.DocumentBytes ) {
        (void)MdoBackupError(Error, XWORK_ERROR_LIMIT, "session backup document exceeds byte budget", NULL);
        return NULL;
    }
    if ( !MdoBackupCheck(&Budget, Cancel, Error) ) return NULL;
    Decode.Backup = (MdoSessionBackup*)xrtCalloc(1u, sizeof(*Decode.Backup));
    Decode.Manifest = xrtValueObject(); Decode.Cancel = Cancel;
    Decode.Input = (const char*)Document; Decode.InputBytes = Bytes;
    if ( Decode.Backup == NULL || Decode.Manifest == NULL ) goto memory;
    Decode.Backup->Limits = Budget; Decode.Backup->Capacity = Budget.Files;
    Decode.Backup->Files = (MdoBackupOwnedFile*)xrtCalloc(Budget.Files, sizeof(*Decode.Backup->Files));
    if ( Decode.Backup->Files == NULL ) goto memory;
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = Budget.DocumentBytes; Config.MaxDepth = 32u;
    Config.MaxValues = MDO_BACKUP_JSON_VALUES; Config.MaxContainerItems = MDO_BACKUP_JSON_VALUES;
    Config.MaxStringBytes = ((Budget.FileBytes + 2u) / 3u) * 4u;
    if ( xrtJsonVisit(xrtStrViewN(Decode.Input, Bytes), &Config, MdoDecodeVisit, &Decode) != XJSON_VISIT_DONE ) {
        if ( Decode.Error.eCode == XWORK_ERROR_NONE ) {
            const xerror* ParseError = xrtGetError();
            xerrkind Kind = ParseError != NULL ? xrtErrorKind(ParseError) : XERR_NONE;
            (void)MdoBackupError(&Decode.Error, Kind == XERR_MEMORY ? XWORK_ERROR_OUT_OF_MEMORY :
                (Kind == XERR_RANGE ? XWORK_ERROR_LIMIT : XWORK_ERROR_IO),
                "cannot parse bounded session backup document", NULL);
        }
        goto done;
    }
    qsort(Decode.Backup->Files, Decode.Backup->Count, sizeof(*Decode.Backup->Files), MdoBackupCompare);
    if ( !MdoDecodePaths(&Decode) ) goto done;
    Ok = MdoDecodeManifest(&Decode) && MdoBackupCheck(&Budget, Cancel, &Decode.Error);
    goto done;
memory:
    (void)MdoDecodeMemory(&Decode);
done:
    xrtFree(Decode.File.Data); xrtValueRelease(Decode.Manifest);
    if ( Error != NULL ) *Error = Decode.Error;
    if ( !Ok ) { MdoSessionBackupRelease(Decode.Backup); return NULL; }
    return Decode.Backup;
}

bool MdoSessionBackupPreviewGet(const MdoSessionBackup* Backup, MdoSessionBackupPreview* Preview)
{
    bool Valid = Preview != NULL && Preview->Size == sizeof(*Preview);
    if ( Preview != NULL ) { memset(Preview, 0, sizeof(*Preview)); Preview->Size = sizeof(*Preview); }
    if ( !Valid || Backup == NULL || !Backup->Decoded ) return false;
    Preview->ExportSchema = Backup->Schema; Preview->Info = Backup->Info;
    Preview->CapturedAt = Backup->CapturedAt; Preview->Files = Backup->Count; Preview->Bytes = Backup->Bytes;
    Preview->UiFirstEventId = Backup->History.First; Preview->UiLastEventId = Backup->History.Last;
    Preview->UiRecords = Backup->History.Records;
    return true;
}
