#ifndef MDO_API_BACKUP_RESTORE_H
#define MDO_API_BACKUP_RESTORE_H

#include "internal.h"

/* One immutable review and one bounded worker. HTTP admission is drained
 * before Unit; Unit cancels/joins before preview, session, project, Home or TCC
 * retirement. Terminal storage receipts outlive this process-local manager. */
bool MdoApiBackupRestoresInit(void);
void MdoApiBackupRestoresUnit(void);
bool MdoApiBackupRestoreReviewRoute(MdoApiContext* Context);
bool MdoApiBackupRestoreApplyRoute(MdoApiContext* Context);
bool MdoApiBackupRestoreRoute(MdoApiContext* Context);
bool MdoApiBackupRestoresRoute(MdoApiContext* Context);

#endif
