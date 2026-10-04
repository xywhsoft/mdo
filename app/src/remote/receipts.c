#include <string.h>
#include "receipts.h"

bool MdoRemoteIdValid(cstr Id)
{
    if (!Id || strlen(Id) != 32u) return false;
    for (size_t i = 0u; i < 32u; i++)
        if (!((Id[i] >= '0' && Id[i] <= '9') || (Id[i] >= 'a' && Id[i] <= 'f'))) return false;
    return true;
}
bool MdoRemoteReceiptsInit(MdoRemoteReceiptStore* Store)
{
    uint8 bytes[16]; static const char hex[] = "0123456789abcdef";
    if (!Store) return false;
    memset(Store,0,sizeof(*Store));
    if (!xrtSecureRandom(bytes,sizeof(bytes))) return false;
    for (size_t i = 0u; i < sizeof(bytes); i++) {
        Store->Runtime[i*2u] = hex[bytes[i] >> 4u];
        Store->Runtime[i*2u+1u] = hex[bytes[i] & 15u];
    }
    xrtSecureZero(bytes,sizeof(bytes)); return true;
}
static MdoRemoteReceiptAdmission MdoRemoteReceiptInput(const MdoRemoteReceiptStore* Store,
    cstr Runtime, cstr Client, uint64 Sequence, cstr Id)
{
    if (!Store || !MdoRemoteIdValid(Store->Runtime) || !MdoRemoteIdValid(Runtime) ||
        !MdoRemoteIdValid(Client) || !MdoRemoteIdValid(Id) || !Sequence ||
        Sequence > MDO_REMOTE_SEQUENCE_MAX) return MDO_REMOTE_RECEIPT_INVALID;
    if (strcmp(Store->Runtime,Runtime)) return MDO_REMOTE_RECEIPT_RUNTIME_CHANGED;
    return MDO_REMOTE_RECEIPT_ADMITTED;
}
static MdoRemoteWatermark* MdoRemoteReceiptClient(MdoRemoteReceiptStore* Store, cstr Client)
{
    for (size_t i = 0u; i < MDO_REMOTE_CLIENTS; i++)
        if (!strcmp(Store->Clients[i].Client,Client)) return &Store->Clients[i];
    return NULL;
}
MdoRemoteReceiptAdmission MdoRemoteReceiptQuery(MdoRemoteReceiptStore* Store,
    cstr Runtime, cstr Client, uint64 Sequence, cstr Id, MdoRemoteReceipt** Record)
{
    if (Record) *Record = NULL;
    MdoRemoteReceiptAdmission admission = MdoRemoteReceiptInput(Store,Runtime,Client,Sequence,Id);
    if (admission != MDO_REMOTE_RECEIPT_ADMITTED) return admission;
    for (size_t i = 0u; i < MDO_REMOTE_RECEIPTS; i++) {
        MdoRemoteReceipt* receipt = &Store->Records[i];
        if (!receipt->Phase || strcmp(receipt->Id,Id)) continue;
        if (strcmp(receipt->Client,Client) || receipt->Sequence != Sequence) return MDO_REMOTE_RECEIPT_CONFLICT;
        if (Record) *Record = receipt;
        return MDO_REMOTE_RECEIPT_DUPLICATE;
    }
    MdoRemoteWatermark* client = MdoRemoteReceiptClient(Store,Client);
    return client && Sequence <= client->Sequence ? MDO_REMOTE_RECEIPT_EXPIRED : MDO_REMOTE_RECEIPT_UNKNOWN;
}
MdoRemoteReceiptAdmission MdoRemoteReceiptClaim(MdoRemoteReceiptStore* Store,
    cstr Runtime, cstr Client, uint64 Sequence, cstr Id, const uint8 Fingerprint[32],
    bool ReadOnly, MdoRemoteReceipt** Record)
{
    if (Record) *Record = NULL;
    if (ReadOnly) return MDO_REMOTE_RECEIPT_READ_ONLY;
    if (!Fingerprint || !Record) return MDO_REMOTE_RECEIPT_INVALID;
    MdoRemoteReceipt* existing = NULL;
    MdoRemoteReceiptAdmission admission = MdoRemoteReceiptQuery(Store,Runtime,Client,Sequence,Id,&existing);
    if (admission == MDO_REMOTE_RECEIPT_DUPLICATE) {
        if (memcmp(existing->Fingerprint,Fingerprint,32u)) return MDO_REMOTE_RECEIPT_CONFLICT;
        *Record = existing; return admission;
    }
    if (admission != MDO_REMOTE_RECEIPT_UNKNOWN) return admission;
    MdoRemoteWatermark* client = MdoRemoteReceiptClient(Store,Client);
    if (!client) {
        for (size_t i = 0u; i < MDO_REMOTE_CLIENTS; i++)
            if (!Store->Clients[i].Client[0]) { client = &Store->Clients[i]; break; }
    }
    MdoRemoteReceipt* slot = NULL;
    for (size_t i = 0u; i < MDO_REMOTE_RECEIPTS; i++) {
        MdoRemoteReceipt* receipt = &Store->Records[i];
        if (!receipt->Phase) { slot = receipt; break; }
        /* Upload/running receipts own a live request and cannot be evicted. */
        if (receipt->Phase >= MDO_REMOTE_RECEIPT_DONE && (!slot || receipt->Order < slot->Order)) slot = receipt;
    }
    if (!client || !slot || Store->Order == UINT64_MAX) return MDO_REMOTE_RECEIPT_FULL;
    strcpy(client->Client,Client); client->Sequence = Sequence;
    memset(slot,0,sizeof(*slot)); strcpy(slot->Client,Client); strcpy(slot->Id,Id);
    memcpy(slot->Fingerprint,Fingerprint,32u); slot->Sequence = Sequence;
    slot->Order = ++Store->Order; slot->Phase = MDO_REMOTE_RECEIPT_UPLOAD;
    *Record = slot; return MDO_REMOTE_RECEIPT_ADMITTED;
}
bool MdoRemoteReceiptRun(MdoRemoteReceipt* Record)
{
    if (!Record || Record->Phase != MDO_REMOTE_RECEIPT_UPLOAD) return false;
    Record->Phase = MDO_REMOTE_RECEIPT_RUNNING; return true;
}
bool MdoRemoteReceiptFinish(MdoRemoteReceipt* Record, bool Complete, uint16 Status)
{
    if (!Record || (Record->Phase != MDO_REMOTE_RECEIPT_UPLOAD && Record->Phase != MDO_REMOTE_RECEIPT_RUNNING)) return false;
    if (Record->Phase == MDO_REMOTE_RECEIPT_UPLOAD) {
        if (Complete || Status) return false;
        Record->Phase = MDO_REMOTE_RECEIPT_NOT_STARTED;
    } else {
        if (Complete && (Status < 200u || Status > 599u)) return false;
        Record->Phase = Complete ? MDO_REMOTE_RECEIPT_DONE : MDO_REMOTE_RECEIPT_UNCERTAIN;
    }
    Record->Status = Status; return true;
}
cstr MdoRemoteReceiptPhaseName(MdoRemoteReceiptPhase Phase)
{
    switch (Phase) {
        case MDO_REMOTE_RECEIPT_UPLOAD: return "uploading";
        case MDO_REMOTE_RECEIPT_RUNNING: return "running";
        case MDO_REMOTE_RECEIPT_DONE: return "done";
        case MDO_REMOTE_RECEIPT_UNCERTAIN: return "uncertain";
        case MDO_REMOTE_RECEIPT_NOT_STARTED: return "not_started";
        default: return "unknown";
    }
}
cstr MdoRemoteReceiptAdmissionName(MdoRemoteReceiptAdmission Admission)
{
    switch (Admission) {
        case MDO_REMOTE_RECEIPT_ADMITTED: return "admitted";
        case MDO_REMOTE_RECEIPT_DUPLICATE: return "duplicate";
        case MDO_REMOTE_RECEIPT_EXPIRED: return "receipt_expired";
        case MDO_REMOTE_RECEIPT_CONFLICT: return "request_conflict";
        case MDO_REMOTE_RECEIPT_FULL: return "receipt_capacity";
        case MDO_REMOTE_RECEIPT_RUNTIME_CHANGED: return "runtime_changed";
        case MDO_REMOTE_RECEIPT_READ_ONLY: return "read_only";
        case MDO_REMOTE_RECEIPT_INVALID: return "invalid_receipt";
        default: return "unknown";
    }
}
