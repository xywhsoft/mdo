#ifndef MDO_PROJECT_RUNTIME_GUARD_H
#define MDO_PROJECT_RUNTIME_GUARD_H
#include <xsbase.h>
/* Installed by bootstrap before serving calls. Invoked under the global
 * definition writer gate; native resource admissions use that same gate. */
void MdoProjectRuntimeGuardSet(bool (*Busy)(cstr Project));
#endif
