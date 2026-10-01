#ifndef MDO_SIDECAR_FEEDBACK_H
#define MDO_SIDECAR_FEEDBACK_H

#include <xsbase.h>
#define MDO_FEEDBACK_MAX_ITEMS 512u
#define MDO_FEEDBACK_MAX_BYTES (32u * 1024u)

typedef struct MdoFeedbackItem {
    uint64 EventId;
    bool Good;
} MdoFeedbackItem;


bool MdoFeedbackUInt(const xvalue* Object, cstr Name,
    uint64* Output);
bool MdoFeedbackValue(const xvalue* Object, bool* Good);
bool MdoFeedbackParse(xstrview Json, MdoFeedbackItem Items[MDO_FEEDBACK_MAX_ITEMS], size_t* Count);

#endif
