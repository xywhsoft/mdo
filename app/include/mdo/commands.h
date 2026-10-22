#ifndef MDO_COMMANDS_H
#define MDO_COMMANDS_H
#include <xsbase.h>
#include "extension_files.h"
#define MDO_COMMAND_LIMIT 256u
#define MDO_COMMAND_TEXT_LIMIT (128u*1024u)
#define MDO_COMMAND_ARGS_LIMIT 65536u
/* Owned JSON. Discovery never compiles or executes C. Workspace is host-resolved. */
xvalue* MdoCommandMetadata(cstr Id,cstr Source,char Error[512]);
xvalue* MdoCommandArguments(cstr Raw,char Error[512]);
xvalue* MdoCommandCatalog(cstr Workspace);
bool MdoCommandResolve(cstr Workspace,cstr Id,char** Source,xvalue** Metadata,
    char Revision[65],char Error[512]);
#endif

