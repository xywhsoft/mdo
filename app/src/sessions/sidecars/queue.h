#ifndef MDO_SIDECAR_QUEUE_H
#define MDO_SIDECAR_QUEUE_H

#include "profile.h"
#include "../../../include/mdo/runs.h"
#define MDO_QUEUE_MAX_ITEMS 20u
#define MDO_QUEUE_MAX_TEXT (64u * 1024u - 1u)
#define MDO_QUEUE_MAX_TOTAL_TEXT (192u * 1024u)
#define MDO_QUEUE_FILE_MAX (256u * 1024u)
#define MDO_QUEUE_ID_SIZE 32u
#define MDO_QUEUE_RECEIPT_FILE_MAX 512u
#define MDO_QUEUE_DISCARD_MAX 256u

typedef enum MdoQueueState {
    MDO_QUEUE_STAGED,
    MDO_QUEUE_PENDING,
    MDO_QUEUE_SENDING
} MdoQueueState;

typedef struct MdoQueueItem {
    char Id[MDO_QUEUE_ID_SIZE + 1u];
    char* Text;
    size_t TextSize;
    char Attachments[4][33];
    size_t AttachmentCount;
    MdoQueueState State;
    bool Priority;
    MdoComposerProfile Profile;
    bool StartClaimed;
    char RunId[MDO_RUN_ID_CAPACITY];
} MdoQueueItem;

typedef struct MdoQueue {
    MdoQueueItem Items[MDO_QUEUE_MAX_ITEMS];
    size_t Count;
    size_t TextBytes;
    char DiscardImages[MDO_QUEUE_DISCARD_MAX][33];
    size_t DiscardCount;
} MdoQueue;

/* Parse owns Text allocations; Release before reuse. Receipt data is a
 * durable fact. Parsing never promotes a prepared receipt or starts a run. */
typedef struct MdoQueueReceipt {
    char Id[33];
    char RunId[MDO_RUN_ID_CAPACITY];
    uint64 AgentRunId;
    uint32 Schema;
} MdoQueueReceipt;

void MdoQueueRelease(MdoQueue* Queue);
bool MdoQueueCaptureId(xstrview View, char* Output,
    size_t Capacity);
bool MdoQueueId(xstrview View,
    char Output[MDO_QUEUE_ID_SIZE + 1u]);
size_t MdoQueueDiscardFind(const MdoQueue* Queue, const char* Id);
bool MdoQueueDiscardAdd(MdoQueue* Queue, const char* Id);
bool MdoQueueRunId(xstrview View, char Output[MDO_RUN_ID_CAPACITY]);
bool MdoQueueString(const xvalue* Object, cstr Key,
    xstrview* Text);
bool MdoQueueText(xstrview Text, bool AllowEmpty);
bool MdoQueueBool(const xvalue* Object, cstr Key, bool* Result);
bool MdoQueueParseState(xstrview State, bool AllowStaged,
    MdoQueueState* Result);
size_t MdoQueueFind(const MdoQueue* Queue, const char* Id);
bool MdoQueueInsert(MdoQueue* Queue, const char* Id,
    xstrview Text, const char Attachments[4][33],
    size_t AttachmentCount, bool First, bool Priority,
    MdoQueueState State, const MdoComposerProfile* Profile);
bool MdoQueueParse(xstrview Json, MdoQueue* Queue);
bool MdoQueueReceiptParse(xstrview Json, const char* ExpectedId, MdoQueueReceipt* Receipt);
bool MdoQueueReceiptApply(MdoQueueItem* Item, bool Exists, const char* RunId);

#endif
