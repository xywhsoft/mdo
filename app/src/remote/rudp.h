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
typedef struct MdoRudpInfo {
    xnetaddr Local;
    unsigned char Public[32], Scope[16];
    size_t SendBytes, ReceiveBytes, SendRecords, ReceiveRecords;
    size_t NativeReservedBytes;
    uint32 NativeReason;
    uint16 CloseCode;
    bool Confirmed, Closed, PeerClosed;
} MdoRudpInfo;
typedef bool (*MdoRudpMessageProc)(bool Binary, xbytesview Message, void* Data);

/* Keys/pin/scope/binding must be nonzero and come from the authenticated
 * rendezvous owner, never an unauthenticated candidate or a STUN result.
 * This layer does not grant account authority. Binding is domain-separated
 * before Noise. The caller owns and must erase its input private key. */
MdoRudpTransport* MdoRudpBind(const MdoRudpConfig* Config, xcancel* Cancel);
bool MdoRudpGetInfo(MdoRudpTransport* Transport, MdoRudpInfo* Info);
/* Initiator only, one attempt per handle; false is terminal. Native callbacks
 * continue during this bounded wait. The responder observes Confirmed via Info. */
bool MdoRudpConnect(MdoRudpTransport* Transport, const xnetaddr* Remote, uint32 TimeoutMs);
bool MdoRudpConnected(MdoRudpTransport* Transport);
/* Admission copies exactly one complete frame. Success is local buffering,
 * not delivery/execution. No application message is replayed on another path.
 * A full queue waits for at most two seconds or caller cancellation. */
bool MdoRudpSend(MdoRudpTransport* Transport, bool Binary, xbytesview Message);
bool MdoRudpPoll(MdoRudpTransport* Transport, MdoRudpMessageProc Proc, void* Data);
void MdoRudpDestroy(MdoRudpTransport* Transport);
#endif
#endif
