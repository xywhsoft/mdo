#ifndef MDO_DOWNLOAD_NETWORK_H
#define MDO_DOWNLOAD_NETWORK_H
#include "model_http_sdk.h"
#include "models.h"
void MdoDownloadNetworkSetEngine(xnetengine* Engine);
/* Downloads inherit global transport settings, never a model's override.
 * Failed proxy/CA resolution never silently selects a direct connection. */
xhttpclient* MdoDownloadClientCreate(xnetengine* Engine,cstr Url,uint64 Timeout,xllm_error* Error);
bool MdoDownloadAriaOptions(xvalue* Options,cstr Url);
bool MdoDownloadFetch(const XS_FetchRequest* Request,XS_FetchResponse* Response);
void MdoDownloadFetchUnit(XS_FetchResponse* Response);
#endif
