#ifndef MDO_REMOTE_H
#define MDO_REMOTE_H

#include <xsbase.h>

typedef enum MdoRemoteAction {
    MDO_REMOTE_ENABLE = 1,
    MDO_REMOTE_DISABLE,
    MDO_REMOTE_LIST,
    MDO_REMOTE_REVOKE,
    MDO_REMOTE_REMOVE,
    MDO_REMOTE_CONTROL,
    MDO_REMOTE_VIEW
} MdoRemoteAction;

bool MdoRemoteInit(XS_ServerInfo* Server);
/* Joins every application worker before API/account teardown or TCC unload. */
void MdoRemoteUnit(void);
xvalue* MdoRemoteSnapshot(void); /* Public metadata only; no credentials. */
/* Local origin/write admission is the API's responsibility. Returns promptly;
 * the snapshot's job ID/state report completion. One service job at a time.
 * Disable supersedes and cancels a pending job immediately. */
bool MdoRemoteRequest(MdoRemoteAction Action, cstr Argument);
/* Single consume by the outer, local connector. Never available through the
 * target dispatcher; ownership transfers and caller must clear the secrets. */
xvalue* MdoRemoteTicketTake(cstr JobId);

#endif
