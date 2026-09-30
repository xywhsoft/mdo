#ifndef MDO_API_WRITE_ADMISSION_H
#define MDO_API_WRITE_ADMISSION_H

#include "internal.h"

#define MDO_API_WRITE_TOKEN_CAPACITY 64u
bool MdoApiWriteInit(void);
void MdoApiWriteUnit(void);
bool MdoApiWriteToken(char Token[MDO_API_WRITE_TOKEN_CAPACITY]);
/* Admission holds a count, never a mutex through handler or socket callbacks.
 * Exclusive admission refuses instead of waiting for ordinary HTTP writers.
 * Recovery records have their own durable binding and bypass this gate. */
bool MdoApiWriteEnter(MdoApiContext* Context, bool Exclusive, bool Stop);
void MdoApiWriteLeave(MdoApiContext* Context);
/* Only an admitted fresh purge may advance the generation. Replays do not. */
bool MdoApiWriteInvalidate(MdoApiContext* Context);

#endif
