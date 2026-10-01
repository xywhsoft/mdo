#include <string.h>
#include "binding.h"

bool MdoImageHexId(xstrview Value, char Output[33])
{
    size_t i;
    if ( Value.Size != 32u ) return false;
    for ( i = 0u; i < Value.Size; ++i ) {
        unsigned char Byte = (unsigned char)Value.Data[i];
        if ( !((Byte >= '0' && Byte <= '9') ||
               (Byte >= 'a' && Byte <= 'f')) ) return false;
    }
    memcpy(Output, Value.Data, 32u);
    Output[32] = '\0';
    return true;
}

bool MdoImageIdsRead(const xvalue* Array, char Ids[4][33],
    size_t* Count)
{
    size_t i;
    if ( Count == NULL || Ids == NULL ) return false;
    *Count = 0u;
    memset(Ids, 0, 4u * 33u);
    if ( xrtValueType(Array) != XVALUE_ARRAY ||
         xrtValueCount(Array) > 4u ) return false;
    for ( i = 0u; i < xrtValueCount(Array); ++i ) {
        const xvalue* Item = xrtValueArrayGet(Array, i);
        xstrview Text;
        size_t j;
        if ( xrtValueType(Item) != XVALUE_STRING ||
             !xrtValueGetString(Item, &Text) ||
             !MdoImageHexId(Text, Ids[i]) ) return false;
        for ( j = 0u; j < i; ++j )
            if ( strcmp(Ids[i], Ids[j]) == 0 ) return false;
    }
    *Count = xrtValueCount(Array);
    return true;
}

static bool MdoImageRunUnsigned(const xvalue* Value, uint64* Number)
{
    int64 Signed;
    if ( xrtValueType(Value) == XVALUE_UINT )
        return xrtValueGetUInt(Value, Number);
    if ( xrtValueType(Value) != XVALUE_INT ||
         !xrtValueGetInt(Value, &Signed) || Signed < 0 ) return false;
    *Number = (uint64)Signed;
    return true;
}

bool MdoImageBindingParse(xstrview Json, uint64 ExpectedRunId,
    uint64* RunId, char Ids[4][33], size_t* Count)
{
    xjsonreadconfig Config;
    xvalue* Root;
    uint64 Schema, ParsedRun;
    bool Ok;
    if ( RunId == NULL || Ids == NULL || Count == NULL ) return false;
    *RunId = 0u; *Count = 0u; memset(Ids, 0, 4u * 33u);
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_IMAGE_RUN_RECORD_MAX;
    Config.MaxDepth = 4u; Config.MaxValues = 12u; Config.MaxContainerItems = 4u;
    Root = xrtJsonRead(Json, &Config);
    Ok = xrtValueType(Root) == XVALUE_OBJECT && xrtValueCount(Root) == 3u &&
        MdoImageRunUnsigned(xrtValueObjectGet(Root, XRT_STR_LITERAL("schema_version")), &Schema) && Schema == 1u &&
        MdoImageRunUnsigned(xrtValueObjectGet(Root, XRT_STR_LITERAL("run_id")), &ParsedRun) && ParsedRun != 0u &&
        (ExpectedRunId == 0u || ParsedRun == ExpectedRunId) &&
        MdoImageIdsRead(xrtValueObjectGet(Root, XRT_STR_LITERAL("attachments")), Ids, Count);
    xrtValueRelease(Root);
    if ( Ok ) *RunId = ParsedRun;
    else { *Count = 0u; memset(Ids, 0, 4u * 33u); }
    return Ok;
}
