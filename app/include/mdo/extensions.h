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
/* Explicit import copies an entire anchored local directory through a real ZIP.
 * It never registers an absolute executable path or follows links. */
bool MdoExtensionsImportDirectory(cstr Kind,cstr Id,cstr NativeRoot,cstr Expected,bool Trust,xcancel* Cancel,xvalue** Result,char Error[512]);
/* Explicit authoring adapter: copy a third-party directory and add/replace its
 * root manifest in the generated ZIP. The source directory is never edited. */
bool MdoExtensionsImportDirectoryManifest(cstr Kind,cstr Id,cstr NativeRoot,const xvalue* Manifest,cstr Expected,bool Trust,xcancel* Cancel,xvalue** Result,char Error[512]);
bool MdoExtensionsImportDirectoryEntry(cstr Kind,cstr Id,cstr NativeRoot,const xvalue* Manifest,cstr EntryText,cstr Expected,bool Trust,xcancel* Cancel,xvalue** Result,char Error[512]);
bool MdoExtensionsCreate(cstr Kind,cstr Id,const xvalue* Manifest,const void* Entry,size_t Bytes,bool Trust,xvalue** Result,char Error[512]);
/* Atomically save manifest + bounded text edits while retaining all other
 * directory files. This is an editor operation, never a publication format. */
bool MdoExtensionsSave(cstr Kind,cstr Id,const xvalue* Manifest,const xvalue* Edits,cstr Expected,bool Trust,xcancel* Cancel,xvalue** Result,char Error[512]);
/* Manual directories only. Expected is the last whole-directory fingerprint. */
bool MdoExtensionsEdit(cstr Kind,cstr Id,cstr Action,cstr Path,cstr Target,const void* Data,size_t Bytes,cstr Expected,xvalue** Result,char Error[512]);
/* Serialize multi-manager operations without blocking the UI network worker. */
bool MdoExtensionsMutationBegin(void);
void MdoExtensionsMutationEnd(void);
bool MdoExtensionsInUse(cstr Kind,cstr Id);
/* Native plugins can retain their directory until host restart. The deferred
 * delete is content-bound and is finished during startup recovery. */
bool MdoExtensionsScheduleRemove(cstr Kind,cstr Id,cstr Expected,xvalue** Result,char Error[512]);
bool MdoExtensionsCancelRemove(cstr Kind,cstr Id,char Error[512]);
/* Product/SDK management coordinates catalogue retirement around storage.
 * Credits is host-verified store metadata, never a public request field. */
bool MdoExtensionsManage(const xvalue* Input,const xvalue* Credits,xcancel* Cancel,xvalue** Result,char Error[512]);
bool MdoExtensionsWarnings(xvalue* Items);

#endif
