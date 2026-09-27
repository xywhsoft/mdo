#include <string.h>

#include "internal.h"
#include "profile.h"

static bool MdoApiProfileString(const xvalue* Object, cstr Name,
    char* Output, size_t Capacity)
{
    const xvalue* Field = xrtValueObjectGet(Object, xrtStrView(Name));
    xstrview Text;
    if ( xrtValueType(Field) != XVALUE_STRING ||
         !xrtValueGetString(Field, &Text) || Text.Size == 0u ||
         Text.Size >= Capacity || memchr(Text.Data, 0, Text.Size) != NULL ||
         !xrtUtf8Valid(Text, NULL) ) return false;
    memcpy(Output, Text.Data, Text.Size);
    Output[Text.Size] = '\0';
    return true;
}

bool MdoApiProfileRead(const xvalue* Value, MdoApiProfile* Profile)
{
    if ( Profile == NULL ) return false;
    memset(Profile, 0, sizeof(*Profile));
    if ( xrtValueType(Value) == XVALUE_NULL ) return true;
    if ( xrtValueType(Value) != XVALUE_OBJECT ||
         xrtValueCount(Value) != 3u ||
         !MdoApiProfileString(Value, "model_id", Profile->ModelId,
            sizeof(Profile->ModelId)) ||
         !MdoApiProfileString(Value, "reasoning_effort",
            Profile->ReasoningEffort, sizeof(Profile->ReasoningEffort)) ||
         !MdoApiProfileString(Value, "permission_profile",
            Profile->PermissionProfile, sizeof(Profile->PermissionProfile)) ||
         (strcmp(Profile->PermissionProfile, "read-only") != 0 &&
          strcmp(Profile->PermissionProfile, "balanced") != 0 &&
          strcmp(Profile->PermissionProfile, "full-access") != 0) )
        return false;
    Profile->Present = true;
    return true;
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
    return A != NULL && B != NULL && A->Present == B->Present &&
        (!A->Present || (strcmp(A->ModelId, B->ModelId) == 0 &&
         strcmp(A->ReasoningEffort, B->ReasoningEffort) == 0 &&
         strcmp(A->PermissionProfile, B->PermissionProfile) == 0));
}
