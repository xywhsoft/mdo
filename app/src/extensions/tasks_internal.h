#ifndef MDO_EXTENSION_TASK_INTERNAL_H
#define MDO_EXTENSION_TASK_INTERNAL_H
#include "../../include/mdo/extension_tasks.h"
typedef struct MdoExtensionTaskUse MdoExtensionTaskUse;
MdoExtensionTaskUse* MdoExtensionTaskUseBegin(cstr Id,bool Sealing,xcancel* Parent,char Error[512]);
xcancel* MdoExtensionTaskUseCancel(MdoExtensionTaskUse* Use); /* borrowed */
void MdoExtensionTaskUseEnd(MdoExtensionTaskUse* Use);
#endif
