#ifndef MDO_LOCAL_MODELS_H
#define MDO_LOCAL_MODELS_H
#include <xsbase.h>
#include "models.h"
bool MdoLocalModelsInit(xnetengine* Engine);
void MdoLocalModelsStop(void);
void MdoLocalModelsUnit(void);
xvalue* MdoLocalModelsSnapshot(char Error[512]);
bool MdoLocalModelsRead(const xvalue* Input,xcancel* Cancel,xvalue** Result,char Error[512]);
bool MdoLocalModelsCommand(const xvalue* Input,xvalue** Result,char Error[512]);
/* Called synchronously by download recovery, never a second file writer. */
bool MdoLocalModelsResume(cstr Installation,xcancel* Cancel,char Error[512]);
bool MdoLocalModelServiceRead(const xvalue* Input,xcancel* Cancel,xvalue** Result,char Error[512]);
bool MdoLocalModelsServicesInit(void);
void MdoLocalModelsServicesStop(void);
xvalue* MdoLocalModelCatalog(void);
bool MdoLocalModelOwned(cstr ModelId);
typedef MdoModelManagedEndpoint MdoLocalModelEndpoint;
bool MdoLocalModelResolve(cstr ModelId,MdoLocalModelEndpoint*,char Error[512]);
bool MdoLocalModelEnsure(cstr ModelId,xcancel* Cancel,uint64 Deadline,char Error[512]);
bool MdoLocalModelCallBegin(cstr ModelId,cstr Alias,xcancel* Parent,xcancel** Cancel,void** Handle,char Error[512]);
void MdoLocalModelCallEnd(void* Handle);
bool MdoLocalModelVerification(cstr Id,cstr Fingerprint,cstr Phase,bool Passed,xvalue** Result,char Error[512]);
#endif
