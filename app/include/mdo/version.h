#ifndef MDO_VERSION_H
#define MDO_VERSION_H

#ifndef MDO_BUILD_ID
#define MDO_BUILD_ID 70000110u
#endif

/* Hexadecimal release identity, independent of legacy/Android install order. */
#ifndef MDO_VERSION_CODE
#define MDO_VERSION_CODE 0x05000802u
#endif
#include "version_code.h"

#ifndef MDO_TOOLPACK_REVISION
#define MDO_TOOLPACK_REVISION 4u
#endif
#define MDO_VERSION_MAJOR 0
#define MDO_VERSION_MINOR 8
#define MDO_VERSION_PATCH 2
#ifndef MDO_VERSION_TEXT
#define MDO_VERSION_TEXT "0.8.2"
#endif

#define MDO_CONFIG_SCHEMA_VERSION 1u
#define MDO_SESSION_SCHEMA_VERSION 3u
#define MDO_MODULE_ABI_VERSION 1u

#endif
