#include <string.h>
#include "feedback.h"

bool MdoFeedbackUInt(const xvalue* Object, cstr Name,
    uint64* Output)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Name));
    int64 Signed;
    if ( xrtValueType(Value) == XVALUE_UINT )
        return xrtValueGetUInt(Value, Output);
    if ( xrtValueType(Value) != XVALUE_INT ||
         !xrtValueGetInt(Value, &Signed) || Signed < 0 ) return false;
    *Output = (uint64)Signed;
    return true;
}

bool MdoFeedbackValue(const xvalue* Object, bool* Good)
{
    const xvalue* Value = xrtValueObjectGet(Object, XRT_STR_LITERAL("value"));
    xstrview Text;
    if ( xrtValueType(Value) != XVALUE_STRING ||
         !xrtValueGetString(Value, &Text) ) return false;
    if ( Text.Size == 4u && memcmp(Text.Data, "good", 4u) == 0 ) {
        *Good = true;
        return true;
    }
    if ( Text.Size == 3u && memcmp(Text.Data, "bad", 3u) == 0 ) {
        *Good = false;
        return true;
    }
    return false;
}

bool MdoFeedbackParse(xstrview Json, MdoFeedbackItem Items[MDO_FEEDBACK_MAX_ITEMS], size_t* Count)
{
    xvalue* Root;
    const xvalue* Array;
    xjsonreadconfig Config;
    uint64 Schema;
    size_t i;
    bool Ok = false;
    if ( Items == NULL || Count == NULL ) return false;
    *Count = 0u; memset(Items, 0, MDO_FEEDBACK_MAX_ITEMS * sizeof(*Items));
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_FEEDBACK_MAX_BYTES;
    Config.MaxDepth = 4u;
    Config.MaxValues = MDO_FEEDBACK_MAX_ITEMS * 3u + 3u;
    Config.MaxContainerItems = MDO_FEEDBACK_MAX_ITEMS;
    Root = xrtJsonRead(Json, &Config);
    Array = Root != NULL ? xrtValueObjectGet(Root,
        XRT_STR_LITERAL("items")) : NULL;
    if ( xrtValueType(Root) != XVALUE_OBJECT ||
         xrtValueCount(Root) != 2u ||
         !MdoFeedbackUInt(Root, "schema_version", &Schema) || Schema != 1u ||
         xrtValueType(Array) != XVALUE_ARRAY ||
         xrtValueCount(Array) > MDO_FEEDBACK_MAX_ITEMS ) goto done;
    for ( i = 0u; i < xrtValueCount(Array); ++i ) {
        const xvalue* Item = xrtValueArrayGet(Array, i);
        uint64 EventId;
        size_t j;
        bool Good;
        if ( xrtValueType(Item) != XVALUE_OBJECT ||
             xrtValueCount(Item) != 2u ||
             !MdoFeedbackUInt(Item, "event_id", &EventId) ||
             EventId == 0u || !MdoFeedbackValue(Item, &Good) ) goto done;
        for ( j = 0u; j < i; ++j )
            if ( Items[j].EventId == EventId ) goto done;
        Items[i].EventId = EventId;
        Items[i].Good = Good;
    }
    *Count = xrtValueCount(Array);
    Ok = true;
done:
    xrtValueRelease(Root);
    if ( !Ok ) memset(Items, 0, MDO_FEEDBACK_MAX_ITEMS * sizeof(*Items));
    return Ok;
}
