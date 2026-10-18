#ifndef MDO_REMOTE_RENDEZVOUS_H
#define MDO_REMOTE_RENDEZVOUS_H
#include "net.h"
#if defined(XS_USE_XRUDP)
typedef struct MdoPeerOffer MdoPeerOffer;
typedef struct MdoPeerSession MdoPeerSession;
typedef struct MdoPeerPolicy {
    xnetaddr Local[2], Stun[2]; /* Optional trusted IPv4/IPv6 numeric overrides. */
    xnetresolver* Resolver; /* Borrowed through SessionDestroy. */
    char StunHost[256];
    uint16 StunPort;
} MdoPeerPolicy;
typedef struct MdoPeerInfo {
    char Peer[33], Runtime[33], Nonce[33], Owner[21];
    cstr Error;
    xnetfamily Family; /* Selected by the initiator, acknowledged by target. */
    bool ReadOnly, Ready, Failed;
} MdoPeerInfo;
/* Owner-thread only; signal callback borrows JSON and must not reenter this
 * session. Signals MUST use the existing authenticated peer's relay. Neither
 * an offer, address nor this helper independently grants account authority. */
typedef bool (*MdoPeerSignalProc)(const xvalue* Signal, void* Data);
void MdoPeerPolicyInit(MdoPeerPolicy* Policy, MdoRemoteNet* Net, cstr TrustedOrigin);
MdoPeerOffer* MdoPeerOfferCreate(cstr Peer, cstr Runtime, uint64 Owner, bool ReadOnly, const MdoPeerPolicy* Policy);
xvalue* MdoPeerOfferValue(const MdoPeerOffer* Offer);
void MdoPeerOfferDestroy(MdoPeerOffer* Offer);
/* Accept is called only for the relay-added peer identity owning this offer.
 * A valid init consumes the offer once; malformed init does not consume it. */
MdoPeerSession* MdoPeerAccept(MdoPeerOffer* Offer, const xvalue* Init, const MdoPeerPolicy* Policy,
    xcancel* Cancel, MdoPeerSignalProc Signal, void* Data);
MdoPeerSession* MdoPeerInitiate(const xvalue* Offer, uint64 ExpectedOwner, const MdoPeerPolicy* Policy,
    xcancel* Cancel, MdoPeerSignalProc Signal, void* Data);
bool MdoPeerInput(MdoPeerSession* Session, const xvalue* Signal);
bool MdoPeerPoll(MdoPeerSession* Session);
bool MdoPeerGetInfo(MdoPeerSession* Session, MdoPeerInfo* Info);
/* Transfer confirmed transport once. Destroy the returned socket before the
 * session, which retains its cancellation parent and immutable metadata. */
MdoRemoteSocket* MdoPeerTakeSocket(MdoPeerSession* Session);
void MdoPeerSessionDestroy(MdoPeerSession* Session);
#endif
#endif
