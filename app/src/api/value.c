#include <string.h>

#include "internal.h"

bool MdoApiValueSetString(xvalue* Object, cstr Key, cstr Value)
{
    return Object != NULL && Key != NULL &&
        xrtValueObjectSetNew(Object, xrtStrView(Key),
            xrtValueString(xrtStrView(Value != NULL ? Value : "")));
}

bool MdoApiValueSetUInt(xvalue* Object, cstr Key, uint64 Value)
{
    return Object != NULL && Key != NULL &&
        xrtValueObjectSetNew(Object, xrtStrView(Key), xrtValueUInt(Value));
}

bool MdoApiValueSetInt(xvalue* Object, cstr Key, int64 Value)
{
    return Object != NULL && Key != NULL &&
        xrtValueObjectSetNew(Object, xrtStrView(Key), xrtValueInt(Value));
}

bool MdoApiValueSetBool(xvalue* Object, cstr Key, bool Value)
{
    return Object != NULL && Key != NULL &&
        xrtValueObjectSetNew(Object, xrtStrView(Key), xrtValueBool(Value));
}

bool MdoApiValueSetTake(xvalue* Object, cstr Key, xvalue** pChild)
{
    xvalue* Child;

    if ( Object == NULL || Key == NULL || pChild == NULL || *pChild == NULL )
        return false;
    Child = *pChild;
    *pChild = NULL;
    return xrtValueObjectSetNew(Object, xrtStrView(Key), Child);
}

bool MdoApiValueAppendTake(xvalue* Array, xvalue** pItem)
{
    xvalue* Item;

    if ( Array == NULL || pItem == NULL || *pItem == NULL ) return false;
    Item = *pItem;
    *pItem = NULL;
    return xrtValueArrayAppendNew(Array, Item);
}

bool MdoApiValueAppendString(xvalue* Array, cstr Value)
{
    return Array != NULL && xrtValueArrayAppendNew(Array,
        xrtValueString(xrtStrView(Value != NULL ? Value : "")));
}

bool MdoApiValueSetStrings(xvalue* Object, cstr Key,
    const char* const* Values, size_t Count)
{
    xvalue* Array = xrtValueArray();
    size_t Index;

    if ( Array == NULL || (Values == NULL && Count != 0u) ) {
        xrtValueRelease(Array);
        return false;
    }
    for ( Index = 0u; Index < Count; Index++ ) {
        if ( !MdoApiValueAppendString(Array, Values[Index]) ) {
            xrtValueRelease(Array);
            return false;
        }
    }
    return MdoApiValueSetTake(Object, Key, &Array);
}
