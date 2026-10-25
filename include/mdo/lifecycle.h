#ifndef MDO_PUBLIC_LIFECYCLE_H
#define MDO_PUBLIC_LIFECYCLE_H
#include "sdk.h"
/* Registration belongs to an immutable module generation. No manager lock is
 * held while callbacks run. Finished/error/cancelled notifications have a
 * complete result; they cannot veto an effect that already happened. */
static inline mdo_result mdoRegisterLifecycle(const mdo_registrar_v1* Registrar,
    const mdo_lifecycle_v1* Hook,char* Error,size_t Capacity)
{
    if(!Registrar||Registrar->Size<offsetof(mdo_registrar_v1,AddLifecycle)+sizeof(Registrar->AddLifecycle)||!Registrar->AddLifecycle)return MDO_RESULT_ERROR;
    return Registrar->AddLifecycle(Registrar->Context,Hook,Error,Capacity);
}
#endif
