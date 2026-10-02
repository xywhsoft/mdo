#include <stdio.h>
#include <string.h>

#include "backup_upload.h"
#include "../../include/mdo/session_backup.h"

typedef struct MdoBackupUploadStore MdoBackupUploadStore;
struct MdoBackupUploadDocument {
    MdoBackupUploadStore* Store;
    char Id[MDO_BACKUP_UPLOAD_ID_BYTES + 1u];
    uint8* Data;
    size_t Bytes, Received, Pins;
    char ExpectedHash[65], Hash[65];
    xsha256 Digest;
    xdeadline Deadline;
    bool Sealed, Removed;
};
struct MdoBackupUploadStore {
    xmutex* Lock;
    MdoBackupUploadDocument* Slot;
    size_t Users; /* API owner + document pins, protected by Lock */
    bool Stopping;
};
static MdoBackupUploadStore* g_MdoBackupUploads;

static bool MdoUploadHex(xstrview Text, size_t Bytes)
{
    size_t i;
    if ( Text.Size != Bytes ) return false;
    for ( i = 0u; i < Bytes; ++i )
        if ( !((Text.Data[i] >= '0' && Text.Data[i] <= '9') ||
               (Text.Data[i] >= 'a' && Text.Data[i] <= 'f')) ) return false;
    return true;
}

static void MdoUploadHash(const uint8 Digest[XRT_SHA256_SIZE], char Hash[65])
{
    static const char Hex[] = "0123456789abcdef";
    size_t i;
    for ( i = 0u; i < XRT_SHA256_SIZE; ++i ) {
        Hash[i * 2u] = Hex[Digest[i] >> 4u]; Hash[i * 2u + 1u] = Hex[Digest[i] & 15u];
    }
    Hash[64] = '\0';
}

/* Expiry is checked at every access, without an idle thread or script timer.
 * Memory is reclaimed at the next request/last pin/Unit, never exposed after
 * expiry. A removed pinned document still owns the single process quota. */
static void MdoUploadCollectLocked(MdoBackupUploadStore* Store)
{
    MdoBackupUploadDocument* Slot = Store->Slot;
    if ( Slot == NULL ) return;
    if ( Store->Stopping || xrtDeadlineExpired(Slot->Deadline) ) Slot->Removed = true;
    if ( Slot->Removed && Slot->Pins == 0u ) {
        Store->Slot = NULL;
        xrtFree(Slot->Data); xrtFree(Slot);
    }
}

static MdoBackupUploadDocument* MdoUploadFindLocked(MdoBackupUploadStore* Store, cstr Id)
{
    MdoUploadCollectLocked(Store);
    return Store->Slot != NULL && !Store->Slot->Removed &&
        strcmp(Store->Slot->Id, Id) == 0 ? Store->Slot : NULL;
}

static void MdoUploadStoreFree(MdoBackupUploadStore* Store)
{
    xrtMutexDestroy(Store->Lock); xrtFree(Store);
}

bool MdoApiBackupUploadsInit(void)
{
    MdoBackupUploadStore* Store;
    if ( g_MdoBackupUploads != NULL ) return true;
    Store = (MdoBackupUploadStore*)xrtCalloc(1u, sizeof(*Store));
    if ( Store == NULL ) return false;
    Store->Lock = xrtMutexCreate();
    if ( Store->Lock == NULL ) { xrtFree(Store); return false; }
    Store->Users = 1u; g_MdoBackupUploads = Store;
    return true;
}

void MdoApiBackupUploadsUnit(void)
{
    MdoBackupUploadStore* Store = g_MdoBackupUploads;
    bool Last;
    if ( Store == NULL ) return;
    g_MdoBackupUploads = NULL;
    xrtMutexLock(Store->Lock);
    Store->Stopping = true; MdoUploadCollectLocked(Store);
    Last = --Store->Users == 0u;
    xrtMutexUnlock(Store->Lock);
    if ( Last ) MdoUploadStoreFree(Store);
}

MdoBackupUploadDocument* MdoApiBackupUploadAcquire(cstr Id, MdoBackupUploadAccess* Access)
{
    MdoBackupUploadStore* Store = g_MdoBackupUploads;
    MdoBackupUploadDocument* Slot = NULL;
    MdoBackupUploadAccess Result = MDO_BACKUP_UPLOAD_ACCESS_UNAVAILABLE;
    if ( Id != NULL && Store != NULL ) {
        xrtMutexLock(Store->Lock);
        Slot = MdoUploadFindLocked(Store, Id);
        if ( Slot == NULL ) Result = MDO_BACKUP_UPLOAD_ACCESS_MISSING;
        else if ( !Slot->Sealed ) { Result = MDO_BACKUP_UPLOAD_ACCESS_INCOMPLETE; Slot = NULL; }
        else if ( Slot->Pins == SIZE_MAX || Store->Users == SIZE_MAX ) Slot = NULL;
        else { ++Slot->Pins; ++Store->Users; Result = MDO_BACKUP_UPLOAD_ACCESS_OK; }
        xrtMutexUnlock(Store->Lock);
    }
    if ( Access != NULL ) *Access = Result;
    return Slot;
}

const void* MdoApiBackupUploadData(const MdoBackupUploadDocument* Document)
{
    return Document != NULL ? Document->Data : NULL;
}

size_t MdoApiBackupUploadBytes(const MdoBackupUploadDocument* Document)
{
    return Document != NULL ? Document->Bytes : 0u;
}

bool MdoApiBackupUploadHash(const MdoBackupUploadDocument* Document, char Hash[65])
{
    if ( Document == NULL || Hash == NULL ) return false;
    memcpy(Hash, Document->Hash, 65u); return true;
}

void MdoApiBackupUploadRelease(MdoBackupUploadDocument* Document)
{
    MdoBackupUploadStore* Store;
    bool Last;
    if ( Document == NULL ) return;
    Store = Document->Store;
    xrtMutexLock(Store->Lock);
    --Document->Pins;
    MdoUploadCollectLocked(Store); /* Document may be freed here. */
    Last = --Store->Users == 0u;
    xrtMutexUnlock(Store->Lock);
    if ( Last ) MdoUploadStoreFree(Store);
}

static bool MdoUploadNoBody(const MdoApiContext* Context)
{
    const xhttp1head* Head = Context->Request->head;
    return (Head->Flags & XHTTP1_TRANSFER_ENCODING) == 0u &&
        ((Head->Flags & XHTTP1_CONTENT_LENGTH) == 0u || Head->ContentLength == 0u);
}

static bool MdoUploadId(const MdoApiContext* Context, char Id[33])
{
    if ( Context->ParamCount < 1u || !MdoUploadHex(Context->Params[0], 32u) ) return false;
    memcpy(Id, Context->Params[0].Data, 32u); Id[32] = '\0'; return true;
}

static bool MdoUploadOffset(xstrview Text, size_t* Offset)
{
    size_t i, Value = 0u;
    if ( Text.Size == 0u || Text.Size > 9u || (Text.Size > 1u && Text.Data[0] == '0') ) return false;
    for ( i = 0u; i < Text.Size; ++i ) {
        if ( Text.Data[i] < '0' || Text.Data[i] > '9' ) return false;
        Value = Value * 10u + (size_t)(Text.Data[i] - '0');
        if ( Value > MDO_SESSION_BACKUP_MAX_DOCUMENT_BYTES ) return false;
    }
    *Offset = Value; return true;
}

static bool MdoUploadUInt(const xvalue* Root, cstr Name, uint64* Number)
{
    int64 Signed;
    const xvalue* Value = xrtValueObjectGet(Root, xrtStrView(Name));
    if ( xrtValueGetUInt(Value, Number) ) return true;
    if ( !xrtValueGetInt(Value, &Signed) || Signed < 0 ) return false;
    *Number = (uint64)Signed; return true;
}

static bool MdoUploadError(MdoApiContext* Context, uint16 Status, cstr Code)
{
    cstr Message = strcmp(Code, "backup_upload_not_found") == 0 ?
        "The backup upload is missing, expired or cancelled; upload the file again" :
        strcmp(Code, "backup_upload_busy") == 0 ? "Another backup upload or reader owns the upload quota" :
        strcmp(Code, "backup_upload_conflict") == 0 ? "The upload ID, offset, content or state conflicts with accepted data" :
        strcmp(Code, "backup_upload_incomplete") == 0 ? "Receive the declared bytes before sealing the upload" :
        strcmp(Code, "backup_upload_checksum") == 0 ? "The uploaded bytes do not match the declared checksum; upload the original file again" :
        strcmp(Code, "backup_upload_limit") == 0 ? "A backup document may be at most 96 MiB and each chunk at most 256 KiB" :
        strcmp(Code, "backup_upload_unavailable") == 0 ? "Backup upload resources are unavailable; retry later" :
        "Use a valid backup upload ID, byte count, checksum, offset and body";
    return MdoApiReplyError(Context, Status, Code, Message, NULL);
}

/* Snapshot only small transport facts under Lock. Serialization/send happens
 * after unlock; a slow peer cannot hold either this store or session locks. */
static xvalue* MdoUploadValueLocked(const MdoBackupUploadDocument* Slot)
{
    xvalue* Value = xrtValueObject();
    uint64 Now = xrtClock();
    bool Ok = Value != NULL && MdoApiValueSetString(Value, "id", Slot->Id) &&
        MdoApiValueSetUInt(Value, "bytes", Slot->Bytes) &&
        MdoApiValueSetUInt(Value, "received_bytes", Slot->Received) &&
        MdoApiValueSetUInt(Value, "chunk_max_bytes", MDO_BACKUP_UPLOAD_CHUNK_BYTES) &&
        MdoApiValueSetUInt(Value, "expires_in_ms", Slot->Deadline > Now ? (Slot->Deadline - Now) / 1000u : 0u) &&
        MdoApiValueSetString(Value, "state", Slot->Sealed ? "sealed" : "receiving") &&
        MdoApiValueSetString(Value, "sha256", Slot->Hash) &&
        MdoApiValueSetString(Value, "validation", Slot->Sealed ? "transport-sha256" : "none") &&
        MdoApiValueSetBool(Value, "restore_ready", false);
    if ( !Ok ) { xrtValueRelease(Value); return NULL; }
    return Value;
}

static bool MdoUploadCreate(MdoApiContext* Context)
{
    MdoApiJsonBody Body;
    MdoApiBodyStatus BodyStatus = MdoApiJsonBodyRead(Context, &Body);
    MdoBackupUploadStore* Store = g_MdoBackupUploads;
    MdoBackupUploadDocument* Slot;
    uint64 Bytes;
    xstrview Id, Hash = {0};
    const xvalue* HashValue;
    char Name[33], Expected[65] = {0};
    uint16 Status = 201u;
    cstr Code = NULL;
    xvalue* Value = NULL;
    bool Valid;
    if ( BodyStatus != MDO_API_BODY_OK ) return MdoApiReplyBodyError(Context, BodyStatus);
    HashValue = xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("sha256"));
    Valid = xrtValueType(Body.Value) == XVALUE_OBJECT &&
        xrtValueCount(Body.Value) == (HashValue != NULL ? 3u : 2u) &&
        xrtValueGetString(xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("id")), &Id) &&
        MdoUploadHex(Id, 32u) && MdoUploadUInt(Body.Value, "bytes", &Bytes) && Bytes > 0u &&
        (HashValue == NULL || (xrtValueGetString(HashValue, &Hash) && MdoUploadHex(Hash, 64u)));
    if ( Valid ) {
        memcpy(Name, Id.Data, 32u); Name[32] = '\0';
        if ( Hash.Size != 0u ) memcpy(Expected, Hash.Data, Hash.Size);
    }
    MdoApiJsonBodyUnit(&Body);
    if ( !Valid ) return MdoUploadError(Context, 422u, "backup_upload_invalid");
    if ( Bytes > MDO_SESSION_BACKUP_MAX_DOCUMENT_BYTES ) return MdoUploadError(Context, 413u, "backup_upload_limit");
    if ( Store == NULL ) return MdoUploadError(Context, 503u, "backup_upload_unavailable");
    xrtMutexLock(Store->Lock); MdoUploadCollectLocked(Store);
    Slot = Store->Slot;
    if ( Store->Stopping ) Code = "backup_upload_unavailable";
    else if ( Slot != NULL ) {
        if ( Slot->Removed || strcmp(Slot->Id, Name) != 0 ) Code = "backup_upload_busy";
        else if ( Slot->Bytes != (size_t)Bytes || strcmp(Slot->ExpectedHash, Expected) != 0 ) {
            Code = "backup_upload_conflict"; Status = 409u;
        } else Status = 200u; /* Same creation intent, including a lost reply. */
    } else {
        Slot = (MdoBackupUploadDocument*)xrtCalloc(1u, sizeof(*Slot));
        /* One exact-capacity allocation: no growing/realloc copy of a 96 MiB
         * document. Bytes past Received are never readable by a consumer. */
        if ( Slot != NULL ) Slot->Data = (uint8*)xrtMalloc((size_t)Bytes);
        if ( Slot == NULL || Slot->Data == NULL ) {
            xrtFree(Slot); Slot = NULL; Code = "backup_upload_unavailable";
        } else {
            Slot->Store = Store; Slot->Bytes = (size_t)Bytes;
            memcpy(Slot->Id, Name, sizeof(Name)); memcpy(Slot->ExpectedHash, Expected, sizeof(Expected));
            Slot->Deadline = xrtDeadlineAfter(MDO_BACKUP_UPLOAD_TTL_US);
            xrtSha256Init(&Slot->Digest); Store->Slot = Slot;
        }
    }
    if ( Code == NULL ) Value = MdoUploadValueLocked(Slot);
    else if ( Status != 409u ) Status = 503u;
    xrtMutexUnlock(Store->Lock);
    return Code != NULL ? MdoUploadError(Context, Status, Code) :
        MdoApiReplySuccessTake(Context, Status, Value, NULL);
}

bool MdoApiBackupUploadsRoute(MdoApiContext* Context)
{
    MdoBackupUploadStore* Store = g_MdoBackupUploads;
    xvalue* Upload = NULL;
    xvalue* Value;
    bool Present;
    if ( Context->Request->head->MethodCode == XHTTP_METHOD_POST ) return MdoUploadCreate(Context);
    if ( !MdoUploadNoBody(Context) ) return MdoUploadError(Context, 400u, "backup_upload_invalid");
    if ( Store == NULL ) return MdoUploadError(Context, 503u, "backup_upload_unavailable");
    xrtMutexLock(Store->Lock); MdoUploadCollectLocked(Store);
    Present = Store->Slot != NULL && !Store->Slot->Removed;
    if ( Present ) Upload = MdoUploadValueLocked(Store->Slot);
    xrtMutexUnlock(Store->Lock);
    if ( !Present ) Upload = xrtValueNull();
    /* Discover a lost creation reply or a previous page's ID, so users can
     * explicitly cancel it rather than waiting for the fixed expiry. */
    Value = xrtValueObject();
    if ( Upload != NULL && Value != NULL &&
         MdoApiValueSetTake(Value, "upload", &Upload) )
        return MdoApiReplySuccessTake(Context, 200u, Value, NULL);
    xrtValueRelease(Upload); xrtValueRelease(Value);
    return MdoUploadError(Context, 503u, "backup_upload_unavailable");
}

bool MdoApiBackupUploadRoute(MdoApiContext* Context)
{
    MdoBackupUploadStore* Store = g_MdoBackupUploads;
    MdoBackupUploadDocument* Slot;
    char Id[33];
    xvalue* Value = NULL;
    bool Found;
    if ( !MdoUploadId(Context, Id) || !MdoUploadNoBody(Context) )
        return MdoUploadError(Context, 400u, "backup_upload_invalid");
    if ( Store == NULL ) return MdoUploadError(Context, 503u, "backup_upload_unavailable");
    xrtMutexLock(Store->Lock); Slot = MdoUploadFindLocked(Store, Id);
    Found = Slot != NULL;
    if ( Slot != NULL ) {
        Value = MdoUploadValueLocked(Slot);
        if ( Context->Request->head->MethodCode == XHTTP_METHOD_DELETE ) {
            Slot->Removed = true; MdoUploadCollectLocked(Store);
        }
    }
    xrtMutexUnlock(Store->Lock);
    return !Found ? MdoUploadError(Context, 404u, "backup_upload_not_found") :
        MdoApiReplySuccessTake(Context, 200u, Value, NULL);
}

bool MdoApiBackupUploadChunkRoute(MdoApiContext* Context)
{
    MdoBackupUploadStore* Store = g_MdoBackupUploads;
    MdoBackupUploadDocument* Slot;
    MdoApiBodyStatus BodyStatus;
    const xhttpfield* Type = NULL;
    char Id[33];
    size_t Offset, Bytes = 0u;
    char* Data = NULL;
    xvalue* Value = NULL;
    cstr Code = NULL;
    uint16 Status = 409u;
    if ( !MdoUploadId(Context, Id) || Context->ParamCount != 2u ||
         !MdoUploadOffset(Context->Params[1], &Offset) )
        return MdoUploadError(Context, 400u, "backup_upload_invalid");
    if ( xrtHttpFieldGetUnique(Context->Request->head->Fields, Context->Request->head->FieldCount,
            XRT_STR_LITERAL("Content-Type"), &Type) != XHTTP_NEXT_ITEM || Type == NULL ||
         !xrtStrCaseEqual(xrtStrTrim(Type->Value), XRT_STR_LITERAL("application/octet-stream")) )
        return MdoUploadError(Context, 415u, "backup_upload_invalid");
    BodyStatus = MdoApiBinaryBodyRead(Context, MDO_BACKUP_UPLOAD_CHUNK_BYTES, &Data, &Bytes);
    if ( BodyStatus != MDO_API_BODY_OK ) return MdoUploadError(Context,
        BodyStatus == MDO_API_BODY_TOO_LARGE ? 413u : 400u,
        BodyStatus == MDO_API_BODY_TOO_LARGE ? "backup_upload_limit" : "backup_upload_invalid");
    if ( Store == NULL ) { xrtFree(Data); return MdoUploadError(Context, 503u, "backup_upload_unavailable"); }
    xrtMutexLock(Store->Lock); Slot = MdoUploadFindLocked(Store, Id);
    if ( Slot == NULL ) { Status = 404u; Code = "backup_upload_not_found"; }
    else if ( Slot->Sealed || Offset > Slot->Received ||
              Offset > Slot->Bytes || Bytes > Slot->Bytes - Offset ) Code = "backup_upload_conflict";
    else if ( Offset < Slot->Received ) {
        if ( Bytes > Slot->Received - Offset || memcmp(Slot->Data + Offset, Data, Bytes) != 0 )
            Code = "backup_upload_conflict";
        /* A byte-identical, fully accepted retry cannot advance the hash. */
    } else {
        xsha256 Digest = Slot->Digest;
        if ( !xrtSha256Update(&Digest, Data, Bytes) ) { Status = 503u; Code = "backup_upload_unavailable"; }
        else {
            memcpy(Slot->Data + Offset, Data, Bytes); Slot->Digest = Digest; Slot->Received += Bytes;
        }
    }
    if ( Code == NULL ) Value = MdoUploadValueLocked(Slot);
    xrtMutexUnlock(Store->Lock); xrtFree(Data);
    return Code != NULL ? MdoUploadError(Context, Status, Code) :
        MdoApiReplySuccessTake(Context, 200u, Value, NULL);
}

bool MdoApiBackupUploadSealRoute(MdoApiContext* Context)
{
    MdoBackupUploadStore* Store = g_MdoBackupUploads;
    MdoBackupUploadDocument* Slot;
    char Id[33];
    uint8 Digest[XRT_SHA256_SIZE];
    xvalue* Value = NULL;
    uint16 Status = 409u;
    cstr Code = NULL;
    if ( !MdoUploadId(Context, Id) || !MdoUploadNoBody(Context) )
        return MdoUploadError(Context, 400u, "backup_upload_invalid");
    if ( Store == NULL ) return MdoUploadError(Context, 503u, "backup_upload_unavailable");
    xrtMutexLock(Store->Lock); Slot = MdoUploadFindLocked(Store, Id);
    if ( Slot == NULL ) { Status = 404u; Code = "backup_upload_not_found"; }
    else if ( Slot->Received != Slot->Bytes ) Code = "backup_upload_incomplete";
    else if ( !Slot->Sealed ) {
        if ( !xrtSha256Final(&Slot->Digest, Digest) ) { Status = 503u; Code = "backup_upload_unavailable"; }
        else {
            MdoUploadHash(Digest, Slot->Hash);
            if ( Slot->ExpectedHash[0] != '\0' && strcmp(Slot->ExpectedHash, Slot->Hash) != 0 ) {
                Code = "backup_upload_checksum"; Status = 422u;
                Slot->Removed = true; MdoUploadCollectLocked(Store);
            } else Slot->Sealed = true;
        }
    }
    if ( Code == NULL ) Value = MdoUploadValueLocked(Slot);
    xrtMutexUnlock(Store->Lock);
    return Code != NULL ? MdoUploadError(Context, Status, Code) :
        MdoApiReplySuccessTake(Context, 200u, Value, NULL);
}
