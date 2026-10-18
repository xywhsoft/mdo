#ifndef MDO_REMOTE_RUDP_H
#define MDO_REMOTE_RUDP_H
#include <xsbase.h>

#if defined(XS_USE_XRUDP)
/* One authenticated, bidirectional message transport and one private Engine.
 * The owner calls every API below; only cancellation may originate elsewhere.
 * Destroy joins that Engine before releasing application code/data. Poll is
 * the only place that invokes application callbacks; they borrow the message
 * and must not destroy/reenter this transport. */
#define MDO_RUDP_MESSAGE_MAX 262123u
#define MDO_RUDP_QUEUE_BYTES (512u * 1024u)
#define MDO_RUDP_QUEUE_RECORDS 8u
#define MDO_RUDP_TRANSPORTS_MAX 8u
typedef struct MdoRudpTransport MdoRudpTransport;
typedef struct MdoRudpConfig {
    xnetaddr Local;
    unsigned char Private[32], PeerPublic[32], Scope[16], Binding[32];
    size_t MessageLimit;
    bool Listen;
} MdoRudpConfig;
/* Fixed-storage endpoint jobs. A mapping or warmup completion is only a hint,
 * never identity/reachability evidence. Only Confirmed admits messages. */
typedef struct MdoRudpProbe {
    xnetaddr Mapped;
    uint32 NativeReason, Sent;
    bool Pending, Complete;
} MdoRudpProbe;

typedef struct MdoRudpInfo {
    xnetaddr Local;
    unsigned char Public[32], Scope[16];
    size_t SendBytes, ReceiveBytes, SendRecords, ReceiveRecords;
    size_t NativeReservedBytes;
    uint32 NativeReason;
    uint32 DialAttempts, LastDialReason;
    uint16 CloseCode;
    bool Opened, Confirmed, Closed, PeerClosed;
    MdoRudpProbe Discovery, Warmup;
} MdoRudpInfo;
typedef bool (*MdoRudpMessageProc)(bool Binary, xbytesview Message, void* Data);

/* Keys/pin/scope/binding must be nonzero and come from the authenticated
 * rendezvous owner, never an unauthenticated candidate or a STUN result.
 * This layer does not grant account authority. Binding is domain-separated
 * before Noise. The caller owns and must erase its input private key. */
MdoRudpTransport* MdoRudpBind(const MdoRudpConfig* Config, xcancel* Cancel);
/* Does not wait for bind: observe Opened/Closed via GetInfo. */
MdoRudpTransport* MdoRudpStart(const MdoRudpConfig* Config, xcancel* Cancel);
bool MdoRudpGetInfo(MdoRudpTransport* Transport, MdoRudpInfo* Info);
/* Local policy chooses the STUN server, never a remote offer. At most one
 * mapping and warmup job each; restart only after completion. Inputs copied;
 * success means admission. Mapping timeout 1..60s; warmup 1..10s, 1..8 distinct
 * authenticated unicast candidates. Same socket as handshake/business. */
bool MdoRudpDiscover(MdoRudpTransport* Transport, const xnetaddr* Server, uint32 TimeoutMs);
bool MdoRudpWarmup(MdoRudpTransport* Transport, const xnetaddr* Candidates, size_t Count, uint32 TimeoutMs);
void MdoRudpCancelProbes(MdoRudpTransport* Transport);
/* Initiator only, one attempt per handle; false is terminal. Native callbacks
 * continue during this bounded wait. The responder observes Confirmed via Info. */
bool MdoRudpConnect(MdoRudpTransport* Transport, const xnetaddr* Remote, uint32 TimeoutMs);
/* Nonblocking initiation with the same one-attempt/deadline contract. A
 * private worker timer enforces deadlines even when the owner is idle. */
bool MdoRudpDial(MdoRudpTransport* Transport, const xnetaddr* Remote, uint32 TimeoutMs);
/* Try at most eight trusted candidates on this same bound socket, in order,
 * under one 1..15000ms deadline. Retry only before confirmation/application
 * admission; a confirmed stream is never recreated or replayed. */
bool MdoRudpDialCandidates(MdoRudpTransport* Transport, const xnetaddr* Candidates, size_t Count, uint32 TimeoutMs);
bool MdoRudpConnected(MdoRudpTransport* Transport);
/* Admission copies exactly one complete frame. Success is local buffering,
 * not delivery/execution. No application message is replayed on another path.
 * A full queue waits for at most two seconds or caller cancellation. */
bool MdoRudpSend(MdoRudpTransport* Transport, bool Binary, xbytesview Message);
bool MdoRudpPoll(MdoRudpTransport* Transport, MdoRudpMessageProc Proc, void* Data);
void MdoRudpDestroy(MdoRudpTransport* Transport);
#endif
#endif
