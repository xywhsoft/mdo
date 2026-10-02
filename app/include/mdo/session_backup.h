#ifndef MDO_SESSION_BACKUP_H
#define MDO_SESSION_BACKUP_H

#include "sessions.h"

#define MDO_SESSION_BACKUP_SCHEMA 2u
#define MDO_SESSION_BACKUP_MAX_FILES 1024u
#define MDO_SESSION_BACKUP_MAX_FILE_BYTES (32u * 1024u * 1024u)
#define MDO_SESSION_BACKUP_MAX_TOTAL_BYTES (64u * 1024u * 1024u)
#define MDO_SESSION_BACKUP_MAX_DOCUMENT_BYTES (96u * 1024u * 1024u)
#define MDO_SESSION_BACKUP_PATH_CAPACITY 256u

typedef struct MdoSessionBackup MdoSessionBackup;

/* A caller may lower, but never raise, these budgets. Deadline is monotonic;
 * zero selects five seconds for capture and thirty seconds for encoding.
 * A later deadline is clamped to those defaults. Checked between bounded
 * operations; it cannot interrupt a native file read or checkpoint.
 * Initialize with MdoSessionBackupLimitsInit before overriding fields. */
typedef struct MdoSessionBackupLimits {
    uint32 Size;
    size_t Files;
    size_t FileBytes;
    size_t TotalBytes;
    size_t DocumentBytes;
    xdeadline Deadline;
} MdoSessionBackupLimits;

typedef struct MdoSessionBackupFile {
    const char* Path;            /* portable, relative to the session directory */
    const void* Data;            /* owned by the immutable backup, binary safe */
    size_t Bytes;
} MdoSessionBackupFile;

/* Facts from an offline-decoded document. ExportSchema == 1 means ONLY meta
 * and a model snapshot; it must never be advertised as a full backup. */
typedef struct MdoSessionBackupPreview {
    uint32 Size;
    uint32 ExportSchema;
    MdoSessionInfo Info;
    int64 CapturedAt;
    size_t Files, Bytes;
    uint64 UiFirstEventId, UiLastEventId, UiRecords;
    /* Old retained sidecars without surviving UI evidence, and sidecars
     * covered by durable history removal markers. Neither is silently
     * repaired; restore must reconcile removed projections in staging. */
    size_t UnverifiedHistoryReferences, RemovedHistoryReferences;
} MdoSessionBackupPreview;

void MdoSessionBackupLimitsInit(MdoSessionBackupLimits* Limits);
/* Copies only through MdoSessionWithCapture. Callers sharing a process with
 * active API storage managers must additionally hold MdoApiSessionCaptureGuard
 * throughout this call (even from a non-HTTP thread). No network/encoding is
 * performed under that boundary. Returned ownership is independent of Home
 * and the session handle. Close/release the handle and API guard before encode.
 * This is a file capture, not authorization to restore or run a session. */
MdoSessionBackup* MdoSessionBackupCapture(MdoSession* Session,
    const MdoSessionBackupLimits* Limits, xwork_error* Error);
void MdoSessionBackupRelease(MdoSessionBackup* Backup);
size_t MdoSessionBackupFileCount(const MdoSessionBackup* Backup);
bool MdoSessionBackupFileGet(const MdoSessionBackup* Backup, size_t Index,
    MdoSessionBackupFile* File);
/* Checks model snapshot/journal fields and CRC, captured JSON syntax, image
 * pairs and attachment/artifact references,
 * then encodes exact bytes with per-file SHA-256. It does not reload Home or
 * validate every product schema/xllm replay (the offline restore validator is
 * a separate stage). The manifest explicitly declares restore_ready:false.
 * Returns xrtFree-owned JSON; failure always leaves Size zero. */
str MdoSessionBackupEncode(const MdoSessionBackup* Backup,
    const MdoSessionBackupLimits* Limits, size_t* Size, xwork_error* Error);

/* Filesystem-free, owning decode of v2 or legacy v1. Checks the envelope,
 * whitelist, budgets, base64/SHA-256, metadata identity, declared retention and
 * resource references using the same rules as encode. Duplicate JSON keys,
 * duplicate paths and unsupported format fields fail. Input/Cancel are only
 * borrowed synchronously and may be released after return. No Home/catalog,
 * Agent/model/tool or queue operation is performed. Use a bounded worker, not
 * a network callback. Deadline/cancel checks are cooperative between bounded
 * operations; a single JSON token/hash/codec operation cannot be interrupted.
 *
 * Metadata/UI/todo and draft/queue/receipts/feedback/image bindings reuse live
 * readers. Image metadata shares the export checker. Retained UI evidence is
 * checked against feedback/todo/bindings/queue receipts. Older or explicitly
 * removed references are reported separately. Model snapshot/journal schemas,
 * exact-byte CRC and checkpoint/tail record numbering are checked. Actual
 * model context replay and pending projection repair remain separate stages.
 * Success is NOT complete schema validation, image decoding or xllm/UI replay
 * and does not authorize restoration. Legacy v1 cannot be encoded as v2. */
MdoSessionBackup* MdoSessionBackupDecode(const void* Document, size_t Bytes,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error);
/* Only decoded objects supply preview facts. Initialize Preview.Size. A size
 * mismatch leaves the caller's object untouched; other failures clear fields
 * except Size. All returned metadata is copied. */
bool MdoSessionBackupPreviewGet(const MdoSessionBackup* Backup,
    MdoSessionBackupPreview* Preview);

/* Separate semantic gate for a successfully decoded backup. Replays its model
 * snapshot and optional journal through xllm-session's byte restore core.
 * Returns an independently owned, unbound session; the backup may be released
 * immediately. Destroy the result with xllmSessionDestroy(). No Home/catalog,
 * driver, tool, queue or persistence attachment is accessed. NULL Limits starts
 * a fresh bounded thirty-second operation; it does not reuse decode's deadline.
 * Failure never changes the decoded bytes. Use a bounded worker: cancellation
 * is cooperative between bounded parse/copy/replay operations. Success proves
 * model replay only, not UI consistency, image decoding or full restore readiness. */
xllm_session* MdoSessionBackupReplayModel(const MdoSessionBackup* Backup,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error);

typedef struct MdoSessionBackupModelHistory {
    uint32 Size;
    size_t MatchedUiRecords;
    size_t UnverifiedUiRecords;
    size_t UnprojectedModelMessages;
} MdoSessionBackupModelHistory;

/* Separate filesystem-free relation gate: replays the owned model, then
 * checks retained main-Agent user sequences, assistant turns/text and tool
 * turn/ID/name/arguments against its raw ledger. Tool display output differs
 * from model framing, so only call identity and durable result presence are
 * compared. Legacy/resume starts without sequences, ambiguous old assistant
 * turns, bounded text, old lost multimodal parts and tool completion before
 * result persistence are reported as unverified; absent UI projections are
 * reported separately.
 * No source bytes are repaired. A true result means no proven contradiction,
 * NOT complete restoration eligibility. Initialize History.Size. Size
 * mismatch leaves it untouched; other failures clear it except Size. A fresh
 * bounded operation uses the same cancellation/deadline as model replay. */
bool MdoSessionBackupCheckModelHistory(const MdoSessionBackup* Backup,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel,
    MdoSessionBackupModelHistory* History, xwork_error* Error);

typedef struct MdoSessionBackupImages {
    uint32 Size;
    size_t Attachments, InlineImages, UnverifiedImages;
    size_t RgbaBytes, PeakDecoderMemoryBytes;
} MdoSessionBackupImages;

/* Separate offline pixel gate. Replays an owned, unbound model to inspect ALL
 * retained IMAGE parts, including entries hidden by compaction/pruning. Decodes
 * static PNG/JPEG/WebP attachments and inline image bytes one at a time through
 * the optional native xs image extension. URL-only/empty references are counted
 * as unverified; no remote resource is fetched. Animated PNG/WebP are unsupported.
 * Per image: at most 8 MiB encoded, 16M pixels, 128 MiB decoder heap; aggregate:
 * at most 1024 image operations, 64 MiB encoded and 256 MiB RGBA. Caller's backup
 * file/total/count budgets only lower these limits. A fresh thirty-second bound
 * also covers model replay. Cancellation/deadline cannot preempt CPU-only codec
 * loops; run on a bounded worker. Decoded RGBA is discarded, backup bytes remain
 * unchanged. Success does not authorize full restoration. Initialize Images.Size;
 * mismatch leaves it untouched, other failures clear all facts except Size. */
bool MdoSessionBackupCheckImages(const MdoSessionBackup* Backup,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel,
    MdoSessionBackupImages* Images, xwork_error* Error);

#endif
