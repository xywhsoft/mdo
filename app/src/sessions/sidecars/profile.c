#include <string.h>
#include "profile.h"

static bool MdoComposerProfileString(const xvalue* Object, cstr Name,
    char* Output, size_t Capacity, bool Partial)
{
    const xvalue* Field = xrtValueObjectGet(Object, xrtStrView(Name));
    xstrview Text;
    if ( xrtValueType(Field) != XVALUE_STRING ||
         !xrtValueGetString(Field, &Text) || (!Partial && Text.Size == 0u) ||
         Text.Size >= Capacity ||
         (Text.Size != 0u && memchr(Text.Data, 0, Text.Size) != NULL) ||
         !xrtUtf8Valid(Text, NULL) ) return false;
    if ( Text.Size != 0u ) memcpy(Output, Text.Data, Text.Size);
    Output[Text.Size] = '\0';
    return true;
}

bool MdoComposerProfileParse(const xvalue* Value, bool Partial,
    MdoComposerProfile* Profile)
{
    MdoComposerProfile Parsed = {0};
    if ( Profile == NULL ) return false;
    memset(Profile, 0, sizeof(*Profile));
    if ( xrtValueType(Value) == XVALUE_NULL ) return true;
    if ( xrtValueType(Value) != XVALUE_OBJECT || xrtValueCount(Value) != 3u ||
         !MdoComposerProfileString(Value, "model_id", Parsed.ModelId, sizeof(Parsed.ModelId), Partial) ||
         !MdoComposerProfileString(Value, "reasoning_effort", Parsed.ReasoningEffort, sizeof(Parsed.ReasoningEffort), Partial) ||
         !MdoComposerProfileString(Value, "permission_profile", Parsed.PermissionProfile, sizeof(Parsed.PermissionProfile), Partial) ||
         ((!Partial || Parsed.PermissionProfile[0] != '\0') &&
          strcmp(Parsed.PermissionProfile, "read-only") != 0 &&
          strcmp(Parsed.PermissionProfile, "balanced") != 0 &&
          strcmp(Parsed.PermissionProfile, "full-access") != 0) ||
         (Partial && Parsed.ModelId[0] == '\0' && Parsed.ReasoningEffort[0] == '\0' &&
          Parsed.PermissionProfile[0] == '\0') ) return false;
    Parsed.Present = true; *Profile = Parsed;
    return true;
}

bool MdoComposerProfileEqual(const MdoComposerProfile* A, const MdoComposerProfile* B)
{
    return A != NULL && B != NULL && A->Present == B->Present &&
        (!A->Present || (strcmp(A->ModelId, B->ModelId) == 0 &&
         strcmp(A->ReasoningEffort, B->ReasoningEffort) == 0 &&
         strcmp(A->PermissionProfile, B->PermissionProfile) == 0));
}
