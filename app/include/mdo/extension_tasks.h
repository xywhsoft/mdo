#ifndef MDO_EXTENSION_TASKS_H
#define MDO_EXTENSION_TASKS_H
#include <xsbase.h>
#define MDO_EXTENSION_TASK_CHUNK (1024u*1024u)
bool MdoExtensionTasksInit(void);
/* Drain callers and owned API workers before releasing the task manager. */
void MdoExtensionTasksUnit(void);
xvalue* MdoExtensionTasksList(char Error[512]);
xvalue* MdoExtensionTaskRead(cstr Id,char Error[512]);
bool MdoExtensionTaskBegin(const xvalue* Input,xvalue** Result,char Error[512]);
bool MdoExtensionTaskWrite(cstr Id,uint64 Offset,const void* Data,size_t Bytes,xvalue** Result,char Error[512]);
bool MdoExtensionTaskSeal(cstr Id,xcancel* Cancel,xvalue** Result,char Error[512]);
bool MdoExtensionTaskCancel(cstr Id,xvalue** Result,char Error[512]);
bool MdoExtensionTaskForget(cstr Id,char Error[512]);
bool MdoExtensionTaskComplete(cstr Id,const xvalue* Outcome,xvalue** Result,char Error[512]);
/* Returns an owned Home-relative path only for a sealed archive. */
char* MdoExtensionTaskArchive(cstr Id,char Error[512]);
#endif
