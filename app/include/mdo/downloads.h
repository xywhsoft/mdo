#ifndef MDO_DOWNLOADS_H
#define MDO_DOWNLOADS_H
#include <xsbase.h>
typedef struct MdoNativeDownload MdoNativeDownload;
bool MdoDownloadsInit(xnetengine* Engine);
void MdoDownloadsStop(void);
void MdoDownloadsUnit(void);
/* Only trusted native producers create tasks. Context contains identifiers and
 * Home-relative paths, never account tokens or arbitrary executable commands. */
MdoNativeDownload* MdoDownloadsBegin(cstr Source,cstr Identity,cstr Name,
    cstr Hash,uint64 Bytes,const xvalue* Context,xcancel* Cancel,char Error[512]);
xcancel* MdoDownloadsCancelToken(MdoNativeDownload* Task,xcancel* Fallback);
xvalue* MdoDownloadsFind(cstr Source,cstr Identity,cstr Hash);
bool MdoDownloadsGate(MdoNativeDownload* Task,xcancel* Cancel);
bool MdoDownloadsProgress(MdoNativeDownload* Task,uint64 Done,cstr Phase);
bool MdoDownloadsEnd(MdoNativeDownload* Task,bool Ok,cstr Code,cstr Error,xcancel* Cancel);
bool MdoDownloadsCancelled(MdoNativeDownload* Task);
xvalue* MdoDownloadsList(void);
bool MdoDownloadsAction(cstr Id,cstr Action,char Error[512]);
#endif
