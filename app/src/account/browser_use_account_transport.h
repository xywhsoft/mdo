#ifndef MDO_ACCOUNT_TRANSPORT_H
#define MDO_ACCOUNT_TRANSPORT_H

/* Per-call public diagnostics contain only fixed labels and numeric codes.
 * Never retain an error object/message, URL, response body or credentials. */
typedef struct MdoAccountTransport {
    cstr Phase;
    uint32 Attempts, Failures, CauseCount;
    uint64 ElapsedMs;
    bool Retryable;
    struct { int32 Kind, Code, SystemCode; bool Fetch; cstr Domain; } Causes[8];
} MdoAccountTransport;

xvalue* MdoAccountServiceJsonDetailed(cstr Path, cstr Method, const xvalue* Body,
    const MdoAccountLease* Lease, uint16* Status, MdoAccountTransport* Transport);

#endif
