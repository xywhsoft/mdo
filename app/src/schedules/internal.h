#ifndef MDO_SCHEDULES_INTERNAL_H
#define MDO_SCHEDULES_INTERNAL_H

#include "../../include/mdo/schedules.h"

/* Schema bridge used by the offline legacy migration staging pipeline. */
char* MdoSchedulesInternalJson(const MdoScheduleInfo* Info, size_t* Size);
bool MdoSchedulesInternalParse(const char* ExpectedId, xstrview Json,
    MdoScheduleInfo* Info);
bool MdoSchedulesInternalValidate(const MdoScheduleInfo* Info,
    xwork_error* Error);

#endif
