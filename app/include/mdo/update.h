#ifndef MDO_UPDATE_H
#define MDO_UPDATE_H
#include <xsbase.h>
typedef struct MdoUpdateStatus {
    bool Enabled, Busy, Ready, Installing;
    /* Publication policy survives transient operation failures. */
    bool Available, Required;
    char State[24], Platform[32], LocalHash[65], Hash[65], Notes[1025], Message[256];
    uint64 Bytes, BuildId;
    char DownloadPath[160], Edition[16];
    char LastInstallMessage[256];
} MdoUpdateStatus;
void MdoUpdateSetEngine(xnetengine* Engine);
bool MdoUpdateInit(void);
void MdoUpdateUnit(void);
bool MdoUpdateGetStatus(MdoUpdateStatus* Status);
bool MdoUpdateCheck(void);
bool MdoUpdateCheckEdition(cstr Edition);
bool MdoUpdateDownload(void);
bool MdoUpdateInstall(void);
void MdoUpdateCancel(void);
bool MdoUpdateInstalling(void);
bool MdoUpdateBlocked(void);
bool MdoUpdateExit(void);
/* Called on the update worker, after native confirmation and idle checks. */
bool MdoUpdateWindowsInstall(cstr Source, cstr Hash);
#endif
