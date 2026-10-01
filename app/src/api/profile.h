#ifndef MDO_API_PROFILE_H
#define MDO_API_PROFILE_H

#include "../sessions/sidecars/profile.h"

typedef MdoComposerProfile MdoApiProfile;

/* Null clears an optional selection. Individual queue and submission items
 * reject null because a present snapshot must always be complete. */
bool MdoApiProfileRead(const xvalue* Value, MdoApiProfile* Profile);
bool MdoApiProfileSet(xvalue* Object, cstr Name,
    const MdoApiProfile* Profile);
bool MdoApiProfileEqual(const MdoApiProfile* A, const MdoApiProfile* B);

#endif
