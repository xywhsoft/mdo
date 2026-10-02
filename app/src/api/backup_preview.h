#ifndef MDO_API_BACKUP_PREVIEW_H
#define MDO_API_BACKUP_PREVIEW_H

#include "internal.h"
#include "../../include/mdo/session_backup.h"

typedef struct MdoBackupPreviewDocument MdoBackupPreviewDocument;
typedef enum MdoBackupPreviewAccess {
    MDO_BACKUP_PREVIEW_ACCESS_OK,
    MDO_BACKUP_PREVIEW_ACCESS_MISSING,
    MDO_BACKUP_PREVIEW_ACCESS_NOT_READY,
    MDO_BACKUP_PREVIEW_ACCESS_UNAVAILABLE
} MdoBackupPreviewAccess;

/* One process-local offline inspection and one retained decoded document.
 * No request/connection, Home path or executable profile is retained. The
 * host must drain request admission before Unit, which cancels and joins the
 * worker before upload storage, other managers and TCC code are retired. */
bool MdoApiBackupPreviewsInit(void);
void MdoApiBackupPreviewsUnit(void);
bool MdoApiBackupPreviewStartRoute(MdoApiContext* Context);
bool MdoApiBackupPreviewsRoute(MdoApiContext* Context);
bool MdoApiBackupPreviewRoute(MdoApiContext* Context);

/* Acquire only a successful, unexpired, undiscarded semantic inspection.
 * The pin owns immutable decoded bytes independently of the upload/receipt;
 * delete/expiry hides the result immediately but keeps the slot occupied until
 * every pin is released. Data/Hash borrow from the pin, never a manager lock.
 * Reading a pin does not authorize restoration or run a model/tool/queue.
 *
 * Each acquisition requires one Release. Pins belong to their store generation:
 * a late release cannot access a replacement store after Unit/Init. They do NOT
 * retain TCC executable code. Drain all consumers before API/TCC retirement;
 * Unit additionally cancels and joins its own inspection worker. */
MdoBackupPreviewDocument* MdoApiBackupPreviewAcquire(cstr Id,
    MdoBackupPreviewAccess* Access);
const MdoSessionBackup* MdoApiBackupPreviewData(const MdoBackupPreviewDocument* Document);
bool MdoApiBackupPreviewHash(const MdoBackupPreviewDocument* Document, char Hash[65]);
void MdoApiBackupPreviewRelease(MdoBackupPreviewDocument* Document);

#endif
