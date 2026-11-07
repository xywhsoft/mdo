#ifndef MDO_RECOVERY_H
#define MDO_RECOVERY_H
#include <xsbase.h>

/* Recovery policy is read at operation boundaries. Waiting never resets the
 * failure streak; only successful completion of the failed step does. */
uint32 MdoRecoveryRetryLimit(void);

#ifndef MDO_RECOVERY_WAIT_MS
#define MDO_RECOVERY_WAIT_MS 300000u
#endif
#ifndef MDO_RECOVERY_RESOURCE_BASE_MS
#define MDO_RECOVERY_RESOURCE_BASE_MS 10000u
#endif

static inline uint32 MdoRecoveryResourceDelay(uint32 Failure)
{
    uint64 Delay=MDO_RECOVERY_RESOURCE_BASE_MS;
    for(uint32 I=1;I<Failure&&Delay<600000u;I++)Delay*=2;
    return (uint32)(Delay>600000u?600000u:Delay);
}
#endif
