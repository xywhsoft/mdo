#ifndef MDO_DEVICE_TRANSFER_H
#define MDO_DEVICE_TRANSFER_H
#include <xsbase.h>
#include <stdio.h>
#include <string.h>
#include "home.h"

bool MdoDeviceTransfersInit(XS_ServerInfo* Server);
void MdoDeviceTransfersUnit(void);
bool MdoDeviceTransfersActive(void);
/* Pending identities are never presented as usable projects/sessions. These
 * queries also work before the worker starts, using persistent markers. */
static inline bool MdoDeviceTransferVisible(cstr Project,cstr Session) {
    char Path[256]; bool Exists=false;
    const char* Values[2]={Project,Session};
    for (size_t i=0;i<2u;i++) {
        cstr Value=Values[i]; if (!Value) { if (i==0u) return false; continue; }
        size_t Size=strlen(Value); if (!Size || Size>(i?32u:64u) || *Value=='.') return false;
        for (size_t j=0;j<Size;j++) {
            unsigned char Ch=(unsigned char)Value[j];
            if (!((Ch>='a' && Ch<='z') || (Ch>='A' && Ch<='Z') || (Ch>='0' && Ch<='9') || Ch=='-' || Ch=='_' || Ch=='.')) return false;
        }
    }
    snprintf(Path,sizeof(Path),"data/device-transfers/pending/%s/%s",Project,Session?Session:"project");
    return MdoHomeExternalStat(Path,&Exists,NULL) && !Exists;
}
static inline bool MdoDeviceTransferProjectVisible(cstr Project) { return MdoDeviceTransferVisible(Project,NULL); }
static inline bool MdoDeviceTransferSessionVisible(cstr Project,cstr Session) {
    return MdoDeviceTransferVisible(Project,NULL) && MdoDeviceTransferVisible(Project,Session);
}

#endif
