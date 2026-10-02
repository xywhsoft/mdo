#include <stdio.h>
#include <string.h>

#include "backup_internal.h"
#include "backup_inputs.h"
#include "internal.h"

#define MDO_INPUT_ARCHIVE_PATH "restore-inputs.json"
#define MDO_INPUT_ARCHIVE_IMPORTS 16u

/* History is descriptive. Only current queue/receipts and retained start
 * evidence may decide live admission; this codec never authorizes a run. */
typedef struct MdoInputArchiveSource {
    MdoQueue Queue;
    MdoDraft Draft;
    MdoSessionInfo Info;
    char Ids[MDO_SESSION_BACKUP_MAX_INPUTS][33];
    size_t Count;
} MdoInputArchiveSource;

static bool MdoArchiveError(xwork_error* Error, const char* Message)
{
    const xerror* Cause = xrtGetError();
    return MdoBackupError(Error, Cause != NULL && xrtErrorKind(Cause) == XERR_MEMORY ?
        XWORK_ERROR_OUT_OF_MEMORY : XWORK_ERROR_IO, Message, MDO_INPUT_ARCHIVE_PATH);
}

static bool MdoArchiveSet(xvalue* Root, const char* Key, xvalue* Value)
{
    bool Ok = Root != NULL && Value != NULL && xrtValueObjectSetTake(Root, xrtStrView(Key), &Value);
    xrtValueRelease(Value); return Ok;
}

static bool MdoArchiveString(xvalue* Root, const char* Key, xstrview Text)
{
    return MdoArchiveSet(Root, Key, xrtValueString(Text));
}

static bool MdoArchiveHash(xstrview Text, char Hash[65], const MdoSessionBackupLimits* Limits,
    const xcancel* Cancel, xwork_error* Error)
{
    static const char Hex[] = "0123456789abcdef";
    uint8 Digest[XRT_SHA256_SIZE];
    size_t i;
    if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
    if ( !xrtSha256(Text.Data, Text.Size, Digest) ) return MdoArchiveError(Error, "cannot hash archived input bytes");
    for ( i = 0u; i < sizeof(Digest); ++i ) { Hash[i * 2u] = Hex[Digest[i] >> 4u]; Hash[i * 2u + 1u] = Hex[Digest[i] & 15u]; }
    Hash[64] = '\0'; return MdoBackupCheck(Limits, Cancel, Error);
}

static bool MdoArchiveSourceRead(const xvalue* Entry, MdoInputArchiveSource* Source,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    const xvalue* Files = xrtValueObjectGet(Entry, XRT_STR_LITERAL("source_files"));
    size_t i;
    unsigned Mask = 0u;
    if ( xrtValueType(Files) != XVALUE_ARRAY || xrtValueCount(Files) < 2u || xrtValueCount(Files) > 3u ) goto invalid;
    for ( i = 0u; i < xrtValueCount(Files); ++i ) {
        const xvalue* File = xrtValueArrayGet(Files, i);
        xstrview Path, Data, ClaimedHash;
        char Hash[65];
        uint64 Bytes;
        unsigned Bit;
        size_t Limit;
        if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
        if ( xrtValueType(File) != XVALUE_OBJECT || xrtValueCount(File) != 4u ||
             !MdoBackupView(File, "path", &Path) || !MdoBackupView(File, "data", &Data) ||
             !MdoBackupView(File, "sha256", &ClaimedHash) || ClaimedHash.Size != 64u ||
             !MdoBackupDigits(ClaimedHash.Data, ClaimedHash.Size, true) ||
             !MdoBackupUInt(File, "bytes", &Bytes) || Bytes != Data.Size || Data.Size == 0u ||
             memchr(Data.Data, 0, Data.Size) != NULL || !xrtUtf8Valid(Data, NULL) ) goto invalid;
        if ( Path.Size == 9u && memcmp(Path.Data, "meta.json", 9u) == 0 ) { Bit = 1u; Limit = 64u * 1024u; }
        else if ( Path.Size == 10u && memcmp(Path.Data, "queue.json", 10u) == 0 ) { Bit = 2u; Limit = MDO_QUEUE_FILE_MAX; }
        else if ( Path.Size == 10u && memcmp(Path.Data, "draft.json", 10u) == 0 ) { Bit = 4u; Limit = MDO_DRAFT_FILE_MAX; }
        else goto invalid;
        if ( (Mask & Bit) != 0u || Data.Size > Limit ) goto invalid;
        Mask |= Bit;
        if ( !MdoArchiveHash(Data, Hash, Limits, Cancel, Error) ) return false;
        if ( memcmp(Hash, ClaimedHash.Data, 64u) != 0 ) goto invalid;
        if ( (Bit == 1u && !MdoBackupMetaRead(Data, &Source->Info)) ||
             (Bit == 2u && !MdoQueueParse(Data, &Source->Queue)) ||
             (Bit == 4u && !MdoDraftParse(Data, MDO_DRAFT_SESSION, &Source->Draft)) ) goto invalid;
    }
    if ( (Mask & 1u) == 0u ) goto invalid;
    for ( i = 0u; i < Source->Queue.Count; ++i ) memcpy(Source->Ids[Source->Count++], Source->Queue.Items[i].Id, 33u);
    for ( i = 0u; i < Source->Draft.SubmissionCount; ++i ) {
        const MdoDraftSubmission* Item = Source->Draft.Submissions[i];
        size_t Match = MdoQueueFind(&Source->Queue, Item->Id);
        if ( Match != SIZE_MAX ) {
            if ( !MdoBackupInputPayloadEqual(&Source->Queue.Items[Match], Item) ) goto invalid;
        } else memcpy(Source->Ids[Source->Count++], Item->Id, 33u);
    }
    return true;
invalid:
    return MdoArchiveError(Error, "invalid archived source bytes, hash or live codec schema");
}

static bool MdoArchiveMaps(const xvalue* Entry, const MdoInputArchiveSource* Source)
{
    const xvalue* Rows = xrtValueObjectGet(Entry, XRT_STR_LITERAL("inputs"));
    char ReviewIds[MDO_SESSION_BACKUP_MAX_INPUTS][33] = {{0}};
    size_t i, j;
    bool Direct;
    uint64 Discard;
    if ( xrtValueType(Rows) != XVALUE_ARRAY || xrtValueCount(Rows) != Source->Count ||
         !xrtValueGetBool(xrtValueObjectGet(Entry, XRT_STR_LITERAL("direct_run_admission_uncertain")), &Direct) ||
         Direct != Source->Draft.RunAdmissionUncertain ||
         !MdoBackupUInt(Entry, "cleared_discard_images", &Discard) || Discard != Source->Queue.DiscardCount ) return false;
    for ( i = 0u; i < Source->Count; ++i ) {
        const xvalue* Row = xrtValueArrayGet(Rows, i);
        xstrview Id, Review;
        uint64 Disposition;
        bool Uncertain;
        size_t Queue = MdoQueueFind(&Source->Queue, Source->Ids[i]);
        if ( xrtValueType(Row) != XVALUE_OBJECT || xrtValueCount(Row) != 4u ||
             !MdoBackupView(Row, "source_id", &Id) || Id.Size != 32u || memcmp(Id.Data, Source->Ids[i], 32u) != 0 ||
             !MdoBackupView(Row, "review_id", &Review) ||
             !MdoBackupUInt(Row, "disposition", &Disposition) ||
             !xrtValueGetBool(xrtValueObjectGet(Row, XRT_STR_LITERAL("uncertain")), &Uncertain) ) return false;
        if ( Disposition == MDO_SESSION_BACKUP_INPUT_ACCEPTED ) {
            if ( Review.Size != 0u || Uncertain ||
                 (Queue != SIZE_MAX && Source->Queue.Items[Queue].State != MDO_QUEUE_SENDING) ) return false;
        } else {
            if ( Disposition != (uint64)(Queue != SIZE_MAX ? MDO_SESSION_BACKUP_INPUT_QUEUE_REVIEW : MDO_SESSION_BACKUP_INPUT_DRAFT_REVIEW) ||
                 !MdoQueueId(Review, ReviewIds[i]) ) return false;
            for ( j = 0u; j < Source->Count; ++j ) if ( strcmp(ReviewIds[i], Source->Ids[j]) == 0 ) return false;
            for ( j = 0u; j < i; ++j ) if ( strcmp(ReviewIds[i], ReviewIds[j]) == 0 ) return false;
            if ( Queue != SIZE_MAX && Source->Queue.Items[Queue].State == MDO_QUEUE_SENDING && !Uncertain ) return false;
            for ( j = 0u; j < Source->Draft.SubmissionCount; ++j )
                if ( strcmp(Source->Ids[i], Source->Draft.Submissions[j]->Id) == 0 &&
                     Source->Draft.Submissions[j]->State == MDO_DRAFT_POSTING && !Uncertain ) return false;
        }
    }
    return true;
}

bool MdoBackupInputsArchiveValid(const xvalue* Root, const MdoSessionBackupLimits* Limits,
    const xcancel* Cancel, xwork_error* Error)
{
    const xvalue* Entries = xrtValueObjectGet(Root, XRT_STR_LITERAL("imports"));
    MdoInputArchiveSource* Source = NULL;
    uint64 Schema;
    size_t i;
    bool Ok = false;
    if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
    if ( xrtValueType(Root) != XVALUE_OBJECT || xrtValueCount(Root) != 2u ||
         !MdoBackupUInt(Root, "schema_version", &Schema) || Schema != 1u ||
         xrtValueType(Entries) != XVALUE_ARRAY || xrtValueCount(Entries) == 0u ||
         xrtValueCount(Entries) > MDO_INPUT_ARCHIVE_IMPORTS ) goto invalid;
    Source = (MdoInputArchiveSource*)xrtCalloc(1u, sizeof(*Source));
    if ( Source == NULL ) goto memory;
    for ( i = 0u; i < xrtValueCount(Entries); ++i ) {
        const xvalue* Entry = xrtValueArrayGet(Entries, i);
        uint64 Captured;
        MdoQueueRelease(&Source->Queue); MdoDraftUnit(&Source->Draft); memset(Source, 0, sizeof(*Source));
        if ( !MdoBackupCheck(Limits, Cancel, Error) ) goto done;
        if ( xrtValueType(Entry) != XVALUE_OBJECT || xrtValueCount(Entry) != 5u ||
             !MdoBackupUInt(Entry, "captured_at_us", &Captured) || Captured == 0u || Captured > INT64_MAX ) goto invalid;
        if ( !MdoArchiveSourceRead(Entry, Source, Limits, Cancel, Error) ) goto done;
        if ( !MdoArchiveMaps(Entry, Source) ) goto invalid;
    }
    Ok = MdoBackupCheck(Limits, Cancel, Error); goto done;
invalid:
    (void)MdoArchiveError(Error, "invalid input provenance schema or mapping consistency"); goto done;
memory:
    (void)MdoBackupError(Error, XWORK_ERROR_OUT_OF_MEMORY, "cannot own input provenance reader", MDO_INPUT_ARCHIVE_PATH);
done:
    if ( Source != NULL ) { MdoQueueRelease(&Source->Queue); MdoDraftUnit(&Source->Draft); xrtFree(Source); }
    return Ok;
}

static xvalue* MdoArchiveFile(const MdoBackupOwnedFile* File, const MdoSessionBackupLimits* Limits,
    const xcancel* Cancel, xwork_error* Error)
{
    xvalue* Row = xrtValueObject();
    char Hash[65];
    xstrview Data = xrtStrViewN(File->Data, File->Bytes);
    if ( Row != NULL && MdoArchiveHash(Data, Hash, Limits, Cancel, Error) &&
         MdoArchiveString(Row, "path", xrtStrView(File->Path)) && MdoArchiveSet(Row, "bytes", xrtValueUInt(File->Bytes)) &&
         MdoArchiveString(Row, "sha256", xrtStrView(Hash)) && MdoArchiveString(Row, "data", Data) ) return Row;
    xrtValueRelease(Row); return NULL;
}

static bool MdoArchiveInheritUncertainty(const xvalue* Entries, MdoSessionBackupInputs* Facts,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    size_t i, j, k;
    for ( i = 0u; i < xrtValueCount(Entries); ++i ) {
        const xvalue* Rows = xrtValueObjectGet(xrtValueArrayGet(Entries, i), XRT_STR_LITERAL("inputs"));
        if ( !MdoBackupCheck(Limits, Cancel, Error) ) return false;
        for ( j = 0u; j < xrtValueCount(Rows); ++j ) {
            const xvalue* Row = xrtValueArrayGet(Rows, j);
            xstrview Review;
            bool Uncertain;
            if ( !MdoBackupView(Row, "review_id", &Review) || Review.Size != 32u ||
                 !xrtValueGetBool(xrtValueObjectGet(Row, XRT_STR_LITERAL("uncertain")), &Uncertain) || !Uncertain ) continue;
            for ( k = 0u; k < Facts->Count; ++k )
                if ( Facts->Items[k].Disposition != MDO_SESSION_BACKUP_INPUT_ACCEPTED &&
                     memcmp(Review.Data, Facts->Items[k].SourceId, 32u) == 0 ) Facts->Items[k].AdmissionUncertain = true;
        }
    }
    return true;
}

bool MdoBackupInputsArchive(const MdoSessionBackup* Source, MdoSessionBackup* Copy,
    MdoSessionBackupInputs* Facts, const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error)
{
    static const char* Names[] = { "meta.json", "queue.json", "draft.json" };
    const MdoBackupOwnedFile* Before = MdoBackupFind(Source, MDO_INPUT_ARCHIVE_PATH);
    xvalue *Root = NULL, *Entry = NULL, *Files = NULL, *Rows = NULL;
    xvalue* Entries;
    size_t i;
    bool Ok = false;
    Root = Before != NULL ? MdoBackupJson(Before->Data, Before->Bytes) : xrtValueObject();
    if ( Root == NULL ) goto done;
    if ( Before == NULL && (!MdoArchiveSet(Root, "schema_version", xrtValueUInt(1u)) ||
                           !MdoArchiveSet(Root, "imports", xrtValueArray())) ) goto done;
    Entries = (xvalue*)xrtValueObjectGet(Root, XRT_STR_LITERAL("imports"));
    if ( Before != NULL && !MdoBackupInputsArchiveValid(Root, Limits, Cancel, Error) ) goto done;
    Facts->ProvenanceEntries = xrtValueCount(Entries);
    if ( MdoBackupFind(Source, "queue.json") == NULL && MdoBackupFind(Source, "draft.json") == NULL ) { Ok = true; goto done; }
    if ( xrtValueCount(Entries) >= MDO_INPUT_ARCHIVE_IMPORTS ) {
        (void)MdoBackupError(Error, XWORK_ERROR_LIMIT, "input provenance import limit reached", MDO_INPUT_ARCHIVE_PATH); goto done;
    }
    if ( !MdoArchiveInheritUncertainty(Entries, Facts, Limits, Cancel, Error) ) goto done;
    Entry = xrtValueObject(); Files = xrtValueArray(); Rows = xrtValueArray();
    if ( Entry == NULL || Files == NULL || Rows == NULL ) goto done;
    for ( i = 0u; i < sizeof(Names) / sizeof(Names[0]); ++i ) {
        const MdoBackupOwnedFile* File = MdoBackupFind(Source, Names[i]);
        xvalue* Row;
        if ( File == NULL ) continue;
        Row = MdoArchiveFile(File, Limits, Cancel, Error);
        if ( Row == NULL ) goto done;
        Ok = xrtValueArrayAppendTake(Files, &Row); xrtValueRelease(Row);
        if ( !Ok ) goto done;
        Ok = false;
    }
    for ( i = 0u; i < Facts->Count; ++i ) {
        const MdoSessionBackupInputReview* Fact = &Facts->Items[i];
        xvalue* Row = xrtValueObject();
        Ok = MdoBackupCheck(Limits, Cancel, Error) && Row != NULL &&
            MdoArchiveString(Row, "source_id", xrtStrView(Fact->SourceId)) &&
            MdoArchiveString(Row, "review_id", xrtStrView(Fact->ReviewId)) &&
            MdoArchiveSet(Row, "disposition", xrtValueUInt(Fact->Disposition)) &&
            MdoArchiveSet(Row, "uncertain", xrtValueBool(Fact->AdmissionUncertain)) && xrtValueArrayAppendTake(Rows, &Row);
        xrtValueRelease(Row);
        if ( !Ok ) goto done;
        Ok = false;
    }
    if ( !MdoArchiveSet(Entry, "captured_at_us", xrtValueInt(Source->CapturedAt)) ||
         !MdoArchiveSet(Entry, "direct_run_admission_uncertain", xrtValueBool(Facts->DirectRunAdmissionUncertain)) ||
         !MdoArchiveSet(Entry, "cleared_discard_images", xrtValueUInt(Facts->ClearedDiscardImages)) ||
         !xrtValueObjectSetTake(Entry, XRT_STR_LITERAL("source_files"), &Files) ||
         !xrtValueObjectSetTake(Entry, XRT_STR_LITERAL("inputs"), &Rows) || !xrtValueArrayAppendTake(Entries, &Entry) ) goto done;
    if ( !MdoBackupInputsArchiveValid(Root, Limits, Cancel, Error) ||
         !MdoBackupReplaceJson(Copy, MDO_INPUT_ARCHIVE_PATH, Root, Limits, Cancel, Error) ) goto done;
    Facts->ProvenanceEntries = xrtValueCount(Entries); Ok = true;
done:
    xrtValueRelease(Root); xrtValueRelease(Entry); xrtValueRelease(Files); xrtValueRelease(Rows);
    if ( !Ok && Error->eCode == XWORK_ERROR_NONE ) return MdoArchiveError(Error, "cannot preserve source inputs as provenance");
    return Ok;
}
