#ifndef MDO_TIME_H
#define MDO_TIME_H

#include <xsbase.h>
#include <xllm-time.h>

/* Product records and request deadlines keep their microsecond contract.
 * Native XRT APIs accept relative, signed milliseconds. */
#define MDO_DEADLINE_NEVER XLLM_DEADLINE_NEVER
static inline uint64 MdoMonotonicUs(void) { return xllmClockUs(); }
static inline int64 MdoUtcUs(void) { return xllmUnixUs(); }
static inline uint64 MdoDeadlineAfterUs(uint64 Us) { return xllmDeadlineAfterUs(Us); }
static inline uint64 MdoDeadlineRemainingUs(uint64 End) { return xllmDeadlineRemainingUs(End); }
static inline bool MdoDeadlineExpired(uint64 End) { return xllmDeadlineExpired(End); }
static inline int64 MdoDeadlineWaitMs(uint64 End) { return xllmDeadlineWaitMs(End); }
static inline int64 MdoDurationMs(uint64 Us)
{
    return Us == UINT64_MAX ? XRT_WAIT_FOREVER :
        (int64)(Us / 1000u + (Us % 1000u != 0u));
}

#endif
