#ifndef MDO_EXTENSIONS_H
#define MDO_EXTENSIONS_H
#include <xsbase.h>

/* Physical installation identity is (kind,id); aliases are runtime names.
 * Returned objects/strings are owned. Reads never execute extension code. */
bool MdoExtensionsInit(void);
bool MdoExtensionsSeedBuiltins(char Error[512]);
void MdoExtensionsUnit(void);
xvalue* MdoExtensionsList(cstr Kind,char Error[512]);
xvalue* MdoExtensionsRead(cstr Kind,cstr Id,char Error[512]);
xvalue* MdoExtensionsFindAlias(cstr Kind,cstr Alias,char Error[512]);
xvalue* MdoExtensionsFiles(cstr Kind,cstr Id,xcancel* Cancel,char Error[512]);
char* MdoExtensionsEntryPath(cstr Kind,cstr Id,char Error[512]);
char* MdoExtensionsRoot(cstr Kind,cstr Id);
bool MdoExtensionsFingerprint(cstr Kind,cstr Id,char Hash[65],xcancel* Cancel,char Error[512]);
bool MdoExtensionsVerify(cstr Kind,cstr Id,xcancel* Cancel,char Error[512]);
typedef struct MdoExtensionLease MdoExtensionLease;
/* Pins an enabled, verified installation until its runtime finishes. Updates
 * and removal return busy rather than renaming a directory still in use. */
MdoExtensionLease* MdoExtensionsAcquire(cstr Kind,cstr Id,char Error[512]);
void MdoExtensionsRelease(MdoExtensionLease* Lease);
bool MdoExtensionsPack(cstr Kind,cstr Id,cstr Output,xcancel* Cancel,char Hash[65],uint64* Size,char Error[512]);
/* Installation is inert. Executable definitions need explicit approval before
 * their directory can become discoverable. Credits are host-verified, not ZIP
 * metadata. No archive-supplied installer or hook is executed. */
bool MdoExtensionsInstall(cstr Kind,cstr Id,cstr Archive,const xvalue* Credits,bool Trust,xcancel* Cancel,xvalue** Result,char Error[512]);
bool MdoExtensionsRemove(cstr Kind,cstr Id,xvalue** Result,char Error[512]);
bool MdoExtensionsRecover(char Error[512]);
bool MdoExtensionsSetEnabled(cstr Kind,cstr Id,bool Enabled,char Error[512]);
bool MdoExtensionsSetActivation(cstr Kind,const xvalue* Changes,char Error[512]);

#endif
