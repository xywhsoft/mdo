#ifndef MDO_EXTENSION_UPDATES_H
#define MDO_EXTENSION_UPDATES_H
#include <xsbase.h>
bool MdoExtensionUpdatesInit(void);
void MdoExtensionUpdatesUnit(void);
bool MdoExtensionUpdatesRequest(void);
xvalue* MdoExtensionUpdatesSnapshot(void);
#endif
