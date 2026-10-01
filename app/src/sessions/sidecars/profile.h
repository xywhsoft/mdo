#ifndef MDO_SIDECAR_PROFILE_H
#define MDO_SIDECAR_PROFILE_H

#include "../../../include/mdo/sessions.h"

typedef struct MdoComposerProfile {
    char ModelId[MDO_SESSION_IDENTITY_CAPACITY];
    char ReasoningEffort[MDO_SESSION_REASONING_CAPACITY];
    char PermissionProfile[MDO_SESSION_REASONING_CAPACITY];
    bool Present;
} MdoComposerProfile;

/* Null clears a selection. Partial project choices may contain empty fields;
 * a session/submission snapshot must be complete. Failure clears the result. */
bool MdoComposerProfileParse(const xvalue* Value, bool Partial,
    MdoComposerProfile* Profile);
bool MdoComposerProfileEqual(const MdoComposerProfile* A,
    const MdoComposerProfile* B);

#endif
