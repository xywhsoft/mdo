#ifndef MDO_API_BACKUP_PREVIEW_H
#define MDO_API_BACKUP_PREVIEW_H

#include "internal.h"

/* One process-local offline inspection and one retained decoded document.
 * No request/connection, Home path or executable profile is retained. The
 * host must drain request admission before Unit, which cancels and joins the
 * worker before upload storage, other managers and TCC code are retired. */
bool MdoApiBackupPreviewsInit(void);
void MdoApiBackupPreviewsUnit(void);
bool MdoApiBackupPreviewStartRoute(MdoApiContext* Context);
bool MdoApiBackupPreviewsRoute(MdoApiContext* Context);
bool MdoApiBackupPreviewRoute(MdoApiContext* Context);

#endif
