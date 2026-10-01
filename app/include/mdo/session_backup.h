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
/* Checks captured JSON syntax, image pairs and attachment/artifact references,
 * then encodes exact bytes with per-file SHA-256. It does not reload Home or
 * validate every product schema/xllm replay (the offline restore validator is
 * a separate stage). The manifest explicitly declares restore_ready:false.
 * Returns xrtFree-owned JSON; failure always leaves Size zero. */
str MdoSessionBackupEncode(const MdoSessionBackup* Backup,
    const MdoSessionBackupLimits* Limits, size_t* Size, xwork_error* Error);

#endif
