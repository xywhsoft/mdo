#ifndef MDO_SESSION_EXTERNAL_H
#define MDO_SESSION_EXTERNAL_H

#include <xwork.h>

/* SDK members and extension orchestration can live outside sessions/<p>/<s>.
 * Until the portable format owns/rebases those resources, retain a durable,
 * conservative boundary in the parent session. This survives plugin removal.
 * Mark before publishing external state. Marking is idempotent, not a model
 * call; it deliberately has no clearing operation that could discard links. */
#define MDO_SESSION_EXTERNAL_STATE_ERROR "This session has member contexts or extension state outside its session directory. Portable backup and transfer cannot include them yet. Keep the complete mdo Home to preserve this collaboration."
bool MdoSessionExternalStateMark(const char* ProjectId, const char* SessionId, xwork_error* Error);
/* Called under the owning session's capture lease, before collecting files. */
bool MdoSessionExternalStatePortable(const char* ProjectId, const char* SessionId, xwork_error* Error);

#endif
