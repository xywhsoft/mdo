#ifndef MDO_VERSION_H
#define MDO_VERSION_H

#ifndef MDO_BUILD_ID
#define MDO_BUILD_ID 60000017u
#endif

/* Hexadecimal release identity, independent of legacy/Android install order. */
#ifndef MDO_VERSION_CODE
#define MDO_VERSION_CODE 0x05000600u
#endif
#include "version_code.h"

#ifndef MDO_TOOLPACK_REVISION
#define MDO_TOOLPACK_REVISION 1u
#endif
#define MDO_VERSION_MAJOR 0
#define MDO_VERSION_MINOR 6
#define MDO_VERSION_PATCH 0
#ifndef MDO_VERSION_TEXT
#define MDO_VERSION_TEXT "0.6.0"
#endif

#define MDO_CONFIG_SCHEMA_VERSION 1u
#define MDO_SESSION_SCHEMA_VERSION 3u
#define MDO_MODULE_ABI_VERSION 1u

#endif
