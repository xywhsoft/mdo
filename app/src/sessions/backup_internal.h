#ifndef MDO_BACKUP_INTERNAL_H
#define MDO_BACKUP_INTERNAL_H

#include "../../include/mdo/session_backup.h"

/* Shared by capture/encode and the filesystem-free decoder. Keep one path
 * whitelist and one byte budget for both sides of the format boundary. */
#define MDO_BACKUP_JSON_VALUES 262144u
#define MDO_BACKUP_OPTIONAL_FILES 5u

typedef struct MdoBackupOwnedFile {
    char Path[MDO_SESSION_BACKUP_PATH_CAPACITY];
    char* Data;
    size_t Bytes;
} MdoBackupOwnedFile;

typedef struct MdoBackupHistory { uint64 First, Last, Records; } MdoBackupHistory;

typedef struct MdoBackupRelations {
    size_t UnverifiedHistoryReferences;
    size_t RemovedHistoryReferences;
} MdoBackupRelations;

struct MdoSessionBackup {
    MdoSessionInfo Info;
    MdoBackupOwnedFile* Files;
    size_t Count, Capacity, Bytes, Nodes;
    int64 CapturedAt;
    MdoSessionBackupLimits Limits;
    uint32 Schema;
    bool Decoded;
    MdoBackupHistory History;
    MdoBackupRelations Relations;
};

extern const char* const MdoBackupOptionalFiles[MDO_BACKUP_OPTIONAL_FILES];
bool MdoBackupError(xwork_error* Error, xwork_error_code Code,
    const char* Message, const char* Path);
bool MdoBackupLimits(const MdoSessionBackupLimits* Input,
    MdoSessionBackupLimits* Output, uint64 Timeout, xwork_error* Error);
bool MdoBackupCheck(const MdoSessionBackupLimits* Limits, const xcancel* Cancel,
    xwork_error* Error);
bool MdoBackupDigits(const char* Text, size_t Size, bool Hex);
size_t MdoBackupPathLimit(const char* Path, bool Directory);
int MdoBackupCompare(const void* Left, const void* Right);
const MdoBackupOwnedFile* MdoBackupFind(const MdoSessionBackup* Backup, const char* Path);
/* Independently owned file index; optionally copies bytes in cancellable
 * chunks. Callers first enforce budgets. With Data=false every data pointer
 * is NULL for a subsequent disk read. Partial allocations always release. */
MdoSessionBackup* MdoBackupClone(const MdoSessionBackup* Source, bool Data,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error);
bool MdoBackupReplaceJson(MdoSessionBackup* Copy, const char* Path, const xvalue* Root,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error);
/* Transfers a complete Bytes+1 allocation only on success; on failure the
 * caller still owns *Data. Resulting inventory/byte budgets are enforced. */
bool MdoBackupReplaceOwned(MdoSessionBackup* Copy, const char* Path, char** Data,
    size_t Bytes, const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error);
bool MdoBackupMetaRead(xstrview Text, MdoSessionInfo* Info);
/* Syntax only: normalize one unambiguous artifact manifest tail. */
bool MdoBackupArtifactRelative(xstrview Path, char Relative[MDO_SESSION_BACKUP_PATH_CAPACITY]);
#define MDO_BACKUP_ORIGIN_MAX_REFS (MDO_BACKUP_JSON_VALUES / 8u)
bool MdoBackupWorkspaceAbsolute(xstrview Path);
bool MdoBackupOriginValid(const xvalue* Root, const MdoSessionBackupLimits* Limits,
    const xcancel* Cancel, xwork_error* Error);
bool MdoBackupOriginAppend(const MdoSessionBackup* Source, MdoSessionBackup* Copy,
    xvalue** ArtifactPaths, MdoSessionBackupRestoreInfo* Facts,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error);
typedef enum MdoBackupAdmission { MDO_BACKUP_NOT_ACCEPTED, MDO_BACKUP_ACCEPTED,
    MDO_BACKUP_ADMISSION_UNCERTAIN } MdoBackupAdmission;
/* Validates the shared retained-history index, then classifies <=40 IDs. */
bool MdoBackupClassifyInputs(const MdoSessionBackup* Backup,
    const char Ids[MDO_SESSION_BACKUP_MAX_INPUTS][33], size_t Count,
    MdoBackupAdmission* Admission, const MdoSessionBackupLimits* Limits,
    const xcancel* Cancel, xwork_error* Error);
/* Passive input provenance is captured/exported as an ordinary owned file.
 * Validation checks exact source bytes/hash/codecs and mapping consistency;
 * historical dispositions never participate in live admission decisions. */
bool MdoBackupInputsArchive(const MdoSessionBackup* Source, MdoSessionBackup* Copy,
    MdoSessionBackupInputs* Facts, const MdoSessionBackupLimits* Limits,
    const xcancel* Cancel, xwork_error* Error);
bool MdoBackupInputsArchiveValid(const xvalue* Root,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error);
bool MdoBackupUInt(const xvalue* Value, const char* Key, uint64* Number);
bool MdoBackupView(const xvalue* Value, const char* Key, xstrview* Text);
xvalue* MdoBackupJson(const void* Data, size_t Bytes);
bool MdoBackupModelValidate(const MdoSessionBackup* Backup,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel, xwork_error* Error);
bool MdoBackupValidate(const MdoSessionBackup* Backup,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel,
    MdoBackupHistory* History, xwork_error* Error);
bool MdoBackupRelationsValidate(const MdoSessionBackup* Backup,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel,
    MdoBackupRelations* Relations, xwork_error* Error);

#endif
