#ifndef MDO_ARIA2_H
#define MDO_ARIA2_H
#include "downloads.h"
#include "distribution.h"
typedef enum MdoAriaResult {
    MDO_ARIA_FAILED=-1,MDO_ARIA_UNAVAILABLE=0,MDO_ARIA_COMPLETE=1,MDO_ARIA_FALLBACK=2
} MdoAriaResult;
/* Trusted native producers supply a fixed URL and optional in-memory bearer
 * header. No RPC endpoint, executable path or URL is accepted from the UI. */
MdoAriaResult MdoAriaDownload(MdoNativeDownload* Task,cstr Url,uint64 Bytes,
    cstr Hash,cstr Authorization,xcancel* Cancel,char** Relative,MdoTransferReport* Report);
bool MdoAriaCleanup(cstr Id);
#endif
