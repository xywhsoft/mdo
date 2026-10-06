#ifndef MDO_DISTRIBUTION_H
#define MDO_DISTRIBUTION_H
#include <xsbase.h>
#ifndef MDO_DISTRIBUTION_ORIGIN
#define MDO_DISTRIBUTION_ORIGIN "https://ai.xywhsoft.com"
#endif
#ifndef MDO_DISTRIBUTION_HOST
#define MDO_DISTRIBUTION_HOST "ai.xywhsoft.com"
#define MDO_DISTRIBUTION_PORT 443
#define MDO_DISTRIBUTION_SECURE true
#endif
uint64 MdoBuildId(void);
cstr MdoEdition(void);
cstr MdoToolPlatform(void);
bool MdoTransferDownload(xnetengine* Engine,cstr Path,cstr Relative,uint64 Size,cstr Hash,xcancel* Cancel);
typedef void (*MdoTransferProgress)(uint64 Done,uint64 Total,void* Data);
bool MdoTransferDownloadProgress(xnetengine* Engine,cstr Path,cstr Relative,uint64 Size,cstr Hash,xcancel* Cancel,MdoTransferProgress Progress,void* Data);
bool MdoDistributionInit(XS_ServerInfo* Server);
void MdoDistributionUnit(void);
xvalue* MdoDistributionSnapshot(void);
bool MdoDistributionRequest(cstr Action,cstr Id);
bool MdoToolPrompt(char** Prompt);
#endif
