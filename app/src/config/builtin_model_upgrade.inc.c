/* Existing portable Homes may contain full arrays copied from the old product
 * baseline. Upgrade only that retired built-in on load; custom entries and
 * unknown keys survive. Loading never writes the configuration or its backup.
 * New imports still pass the normal immutable built-in validation. */
static bool MdoConfigUpgradeBuiltinArray(xvalue* Patch, cstr Key, cstr OldId,
    cstr NewId, const xvalue* Defaults, bool Models)
{
    const xvalue* Before = xrtValueObjectGet(Patch, MdoConfigKey(Key));
    const xvalue* Replacement;
    xvalue* After;
    size_t i, OldCount = 0u;
    bool HasNew = false, Ok = true;
    if ( Before == NULL ) return true;
    if ( xrtValueType(Before) != XVALUE_ARRAY ) return false;
    for ( i = 0u; i < xrtValueCount(Before); ++i ) {
        const xvalue* Item = xrtValueArrayGet(Before, i);
        xstrview Id;
        if ( !MdoConfigString(xrtValueObjectGet(Item, MdoConfigKey("id")), &Id) ) return false;
        if ( MdoConfigViewEqual(Id, NewId) ) HasNew = true;
        if ( MdoConfigViewEqual(Id, OldId) ) {
            bool Builtin, Editable, Removable;
            if ( !xrtValueGetBool(xrtValueObjectGet(Item, MdoConfigKey("builtin")), &Builtin) ||
                 !xrtValueGetBool(xrtValueObjectGet(Item, MdoConfigKey("editable")), &Editable) ||
                 !xrtValueGetBool(xrtValueObjectGet(Item, MdoConfigKey("removable")), &Removable) ||
                 !Builtin || Editable || Removable || ++OldCount != 1u ) return false;
        }
    }
    Replacement = MdoConfigFindById(xrtValueObjectGet(Defaults, MdoConfigKey(Key)), xrtStrView(NewId));
    After = xrtValueArray();
    if ( After == NULL || Replacement == NULL ) { xrtValueRelease(After); return false; }
    for ( i = 0u; Ok && i < xrtValueCount(Before); ++i ) {
        const xvalue* Item = xrtValueArrayGet(Before, i);
        xstrview Id, Provider;
        xvalue* Copy;
        if ( !MdoConfigString(xrtValueObjectGet(Item, MdoConfigKey("id")), &Id) ) { Ok = false; break; }
        if ( MdoConfigViewEqual(Id, OldId) && HasNew ) continue;
        Copy = xrtValueDeepClone(MdoConfigViewEqual(Id, OldId) ? Replacement : Item);
        if ( Copy != NULL && Models &&
             MdoConfigString(xrtValueObjectGet(Copy, MdoConfigKey("provider")), &Provider) &&
             MdoConfigViewEqual(Provider, "ling") )
            Ok = xrtValueObjectSetNew(Copy, MdoConfigKey("provider"), xrtValueString(XRT_STR_LITERAL(MDO_BUILTIN_PROVIDER_ID)));
        if ( Ok ) Ok = Copy != NULL && xrtValueArrayAppendTake(After, &Copy);
        xrtValueRelease(Copy);
    }
    if ( Ok ) Ok = xrtValueObjectSetTake(Patch, MdoConfigKey(Key), &After);
    xrtValueRelease(After);
    return Ok;
}

static bool MdoConfigUpgradeBuiltinPatch(xvalue* Patch)
{
    const xvalue* Defaults = xrtValueObjectGet(g_MdoConfig.Defaults, MdoConfigKey("models"));
    xstrview Id;
    if ( MdoConfigString(xrtValueObjectGet(Patch, MdoConfigKey("default_model")), &Id) &&
         (MdoConfigViewEqual(Id, MDO_LEGACY_BUILTIN_MODEL_ID) || MdoConfigViewEqual(Id, "ling-gpu")) &&
         !xrtValueObjectSetNew(Patch, MdoConfigKey("default_model"), xrtValueString(XRT_STR_LITERAL(MDO_BUILTIN_MODEL_ID))) ) return false;
    return MdoConfigUpgradeBuiltinArray(Patch, "providers", "ling", MDO_BUILTIN_PROVIDER_ID, Defaults, false) &&
        MdoConfigUpgradeBuiltinArray(Patch, "items", MDO_LEGACY_BUILTIN_MODEL_ID, MDO_BUILTIN_MODEL_ID, Defaults, true);
}
