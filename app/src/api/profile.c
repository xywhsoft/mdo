#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "profile.h"
#include "../../include/mdo/models.h"

/* A rejected local selection is final and actionable, not a transient model
 * outage. Keep one policy for session creation, profile edits and run starts. */
bool MdoApiReplyModelProfileFailure(MdoApiContext* Context,
    const xllm_error* Error, cstr UnavailableCode)
{
    const char* Kind = MdoModelErrorKind(Error);
    char Code[64];
    if (!strncmp(Kind, "profile_", 8u)) {
        snprintf(Code, sizeof(Code), "model_%s", Kind);
        return MdoApiReplyError(Context, 422u, Code,
            MdoModelErrorMessage(Error), NULL);
    }
    if (Error && (Error->eCode == XLLM_ERROR_AUTH ||
            Error->eCode == XLLM_ERROR_OUT_OF_MEMORY))
        return MdoApiReplyError(Context, 503u, UnavailableCode,
            "The session model service is temporarily unavailable", NULL);
    return MdoApiReplyError(Context, 422u, "session_profile_invalid",
        "The session model profile is invalid", NULL);
}

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
