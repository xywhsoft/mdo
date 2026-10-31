#ifndef MDO_EXTENSION_TASK_INTERNAL_H
#define MDO_EXTENSION_TASK_INTERNAL_H
#include "../../include/mdo/extension_tasks.h"
typedef struct MdoExtensionTaskUse MdoExtensionTaskUse;
bool MdoExtensionTaskBeginWait(const xvalue* Input,xcancel* Cancel,xvalue** Result,char Error[512]);
MdoExtensionTaskUse* MdoExtensionTaskUseBegin(cstr Id,bool Sealing,xcancel* Parent,char Error[512]);
xcancel* MdoExtensionTaskUseCancel(MdoExtensionTaskUse* Use); /* borrowed */
void MdoExtensionTaskUseEnd(MdoExtensionTaskUse* Use);
bool MdoExtensionTaskUseWrite(MdoExtensionTaskUse* Use,uint64 Offset,const void* Data,size_t Bytes,xvalue** Result,char Error[512]);
bool MdoExtensionTaskOrigin(MdoExtensionTaskUse* Use,const xvalue* Origin,char Error[512]);
bool MdoExtensionTaskTransfer(MdoExtensionTaskUse* Use,uint64 Sent,cstr Phase,char Error[512]);
bool MdoExtensionTaskPublication(MdoExtensionTaskUse* Use,const xvalue* Request,char Error[512]);
#endif
