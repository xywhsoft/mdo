#ifndef MDO_REMOTE_RECEIPTS_H
#define MDO_REMOTE_RECEIPTS_H

#include <xsbase.h>

#define MDO_REMOTE_RECEIPTS 64u
#define MDO_REMOTE_CLIENTS 128u
#define MDO_REMOTE_SEQUENCE_MAX 9007199254740991ull

typedef enum MdoRemoteReceiptPhase {
    MDO_REMOTE_RECEIPT_UPLOAD = 1,
    MDO_REMOTE_RECEIPT_RUNNING,
    MDO_REMOTE_RECEIPT_DONE,
    MDO_REMOTE_RECEIPT_UNCERTAIN,
    MDO_REMOTE_RECEIPT_NOT_STARTED
} MdoRemoteReceiptPhase;

typedef struct MdoRemoteReceipt {
    char Client[33], Id[33];
    uint8 Fingerprint[32];
    uint64 Sequence, Order;
    MdoRemoteReceiptPhase Phase;
    uint16 Status;
} MdoRemoteReceipt;

typedef struct MdoRemoteWatermark {
    char Client[33];
    uint64 Sequence;
} MdoRemoteWatermark;

typedef struct MdoRemoteReceiptStore {
    char Runtime[33];
    uint64 Order;
    MdoRemoteReceipt Records[MDO_REMOTE_RECEIPTS];
    MdoRemoteWatermark Clients[MDO_REMOTE_CLIENTS];
} MdoRemoteReceiptStore;

typedef enum MdoRemoteReceiptAdmission {
    MDO_REMOTE_RECEIPT_ADMITTED = 1,
    MDO_REMOTE_RECEIPT_DUPLICATE,
    MDO_REMOTE_RECEIPT_EXPIRED,
    MDO_REMOTE_RECEIPT_CONFLICT,
    MDO_REMOTE_RECEIPT_FULL,
    MDO_REMOTE_RECEIPT_RUNTIME_CHANGED,
    MDO_REMOTE_RECEIPT_READ_ONLY,
    MDO_REMOTE_RECEIPT_INVALID,
    MDO_REMOTE_RECEIPT_UNKNOWN
} MdoRemoteReceiptAdmission;

/* Bridge serializes every access using its lock. No payload, filename,
 * prompt, response or token is retained. Watermarks never expire during a
 * runtime: evicting an old terminal record cannot make its write executable.
 * Across runtime changes the controller must reconcile, never replay a write. */
bool MdoRemoteIdValid(cstr Id);
bool MdoRemoteReceiptsInit(MdoRemoteReceiptStore* Store);
MdoRemoteReceiptAdmission MdoRemoteReceiptClaim(MdoRemoteReceiptStore* Store,
    cstr Runtime, cstr Client, uint64 Sequence, cstr Id, const uint8 Fingerprint[32],
    bool ReadOnly, MdoRemoteReceipt** Record);
MdoRemoteReceiptAdmission MdoRemoteReceiptQuery(MdoRemoteReceiptStore* Store,
    cstr Runtime, cstr Client, uint64 Sequence, cstr Id, MdoRemoteReceipt** Record);
bool MdoRemoteReceiptRun(MdoRemoteReceipt* Record);
bool MdoRemoteReceiptFinish(MdoRemoteReceipt* Record, bool Complete, uint16 Status);
cstr MdoRemoteReceiptPhaseName(MdoRemoteReceiptPhase Phase);
cstr MdoRemoteReceiptAdmissionName(MdoRemoteReceiptAdmission Admission);

#endif
