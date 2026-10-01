#ifndef MDO_API_BACKUP_UPLOAD_H
#define MDO_API_BACKUP_UPLOAD_H

#include "internal.h"

/* HTTP transport staging, deliberately independent of Home/session schemas.
 * A sealed upload only passed its transport checksum, not restore validation. */
#define MDO_BACKUP_UPLOAD_ID_BYTES 32u
#define MDO_BACKUP_UPLOAD_CHUNK_BYTES MDO_API_REQUEST_MAX_BYTES
#define MDO_BACKUP_UPLOAD_TTL_US 300000000u

typedef struct MdoBackupUploadDocument MdoBackupUploadDocument;
typedef enum MdoBackupUploadAccess {
    MDO_BACKUP_UPLOAD_ACCESS_OK,
    MDO_BACKUP_UPLOAD_ACCESS_MISSING,
    MDO_BACKUP_UPLOAD_ACCESS_INCOMPLETE,
    MDO_BACKUP_UPLOAD_ACCESS_UNAVAILABLE
} MdoBackupUploadAccess;

bool MdoApiBackupUploadsInit(void);
/* Stop admission and join reader workers before Unit/TCC unload. Pins also
 * retain their store generation: Unit/Init cannot make a late Release touch
 * the new store's lock/slot. This does not retain executable code for callers. */
void MdoApiBackupUploadsUnit(void);
bool MdoApiBackupUploadsRoute(MdoApiContext* Context);
bool MdoApiBackupUploadRoute(MdoApiContext* Context);
bool MdoApiBackupUploadChunkRoute(MdoApiContext* Context);
bool MdoApiBackupUploadSealRoute(MdoApiContext* Context);

/* Pins immutable bytes for the future offline validator, outside the manager
 * lock. Cancel/expiry removes visibility but keeps the quota occupied until
 * all pins are released. No borrowed request/stream or Home path is retained.
 * The pointer is valid only until the matching Release; never free it directly. */
MdoBackupUploadDocument* MdoApiBackupUploadAcquire(cstr Id,
    MdoBackupUploadAccess* Access);
const void* MdoApiBackupUploadData(const MdoBackupUploadDocument* Document);
size_t MdoApiBackupUploadBytes(const MdoBackupUploadDocument* Document);
void MdoApiBackupUploadRelease(MdoBackupUploadDocument* Document);

#endif
