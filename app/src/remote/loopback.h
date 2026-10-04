#ifndef MDO_REMOTE_LOOPBACK_H
#define MDO_REMOTE_LOOPBACK_H

#include <xsbase.h>

#define MDO_REMOTE_HTTP_BODY_MAX (33u * 1024u * 1024u)
/* Matches the existing session backup export cap; streamed, never retained. */
#define MDO_REMOTE_HTTP_RESPONSE_MAX (96u * 1024u * 1024u)
typedef struct MdoRemoteHttpRequest {
    cstr Method;
    cstr Target; /* Origin-form /api/v1 only; never a URL. */
    const XS_FetchHeader* Headers;
    size_t HeaderCount;
    xbytesview Body;
    bool ReadOnly;
} MdoRemoteHttpRequest;
typedef struct MdoRemoteHttpSink {
    /* Borrowed views for these synchronous callbacks only. Returning false
     * aborts the transport; it cannot undo an already admitted mutation. */
    bool (*Head)(const xhttp1head* Head, void* Data);
    bool (*Body)(xbytesview Bytes, void* Data);
    void* Data;
} MdoRemoteHttpSink;

bool MdoRemoteHttpRequestValid(const MdoRemoteHttpRequest* Request);
bool MdoRemoteHttpResponseField(xstrview Name);
/* Off-network-worker call, fixed numeric loopback and retained server lease.
 * Copies no whole response/attachment; decoded body streams in <=16 KiB
 * pieces. Header/body/time limits and cancellation apply independently of
 * HTTP status. False after sending a write means its outcome is uncertain. */
bool MdoRemoteLoopbackCall(XS_ServerInfo* Server, const MdoRemoteHttpRequest* Request,
    xcancel* Cancel, const MdoRemoteHttpSink* Sink);

#endif
