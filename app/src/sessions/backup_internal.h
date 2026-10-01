#ifndef MDO_BACKUP_INTERNAL_H
#define MDO_BACKUP_INTERNAL_H

#include "../../include/mdo/session_backup.h"

/* Shared by capture/encode and the filesystem-free decoder. Keep one path
 * whitelist and one byte budget for both sides of the format boundary. */
#define MDO_BACKUP_JSON_VALUES 262144u
#define MDO_BACKUP_OPTIONAL_FILES 6u

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
