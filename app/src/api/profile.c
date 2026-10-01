#include "internal.h"
#include "profile.h"

bool MdoApiProfileRead(const xvalue* Value, MdoApiProfile* Profile)
{
    return MdoComposerProfileParse(Value, false, Profile);
}

bool MdoApiProfileSet(xvalue* Object, cstr Name,
    const MdoApiProfile* Profile)
{
    xvalue* Value;
    if ( Profile == NULL ) return false;
    if ( !Profile->Present ) return true;
    Value = xrtValueObject();
    if ( Value != NULL &&
         MdoApiValueSetString(Value, "model_id", Profile->ModelId) &&
         MdoApiValueSetString(Value, "reasoning_effort",
            Profile->ReasoningEffort) &&
         MdoApiValueSetString(Value, "permission_profile",
            Profile->PermissionProfile) &&
         MdoApiValueSetTake(Object, Name, &Value) ) return true;
    xrtValueRelease(Value);
    return false;
}

bool MdoApiProfileEqual(const MdoApiProfile* A, const MdoApiProfile* B)
{
    return MdoComposerProfileEqual(A, B);
}
