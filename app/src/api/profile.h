#ifndef MDO_API_PROFILE_H
#define MDO_API_PROFILE_H

#include "../../include/mdo/sessions.h"

typedef struct MdoApiProfile {
    char ModelId[MDO_SESSION_IDENTITY_CAPACITY];
    char ReasoningEffort[MDO_SESSION_REASONING_CAPACITY];
    char PermissionProfile[MDO_SESSION_REASONING_CAPACITY];
    bool Present;
} MdoApiProfile;

/* Null clears an optional selection. Individual queue and submission items
 * reject null because a present snapshot must always be complete. */
bool MdoApiProfileRead(const xvalue* Value, MdoApiProfile* Profile);
bool MdoApiProfileSet(xvalue* Object, cstr Name,
    const MdoApiProfile* Profile);
bool MdoApiProfileEqual(const MdoApiProfile* A, const MdoApiProfile* B);

#endif
