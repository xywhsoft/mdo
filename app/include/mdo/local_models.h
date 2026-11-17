#ifndef MDO_LOCAL_MODELS_H
#define MDO_LOCAL_MODELS_H
#include <xsbase.h>
bool MdoLocalModelsInit(xnetengine* Engine);
void MdoLocalModelsStop(void);
void MdoLocalModelsUnit(void);
xvalue* MdoLocalModelsSnapshot(char Error[512]);
bool MdoLocalModelsRead(const xvalue* Input,xcancel* Cancel,xvalue** Result,char Error[512]);
bool MdoLocalModelsCommand(const xvalue* Input,xvalue** Result,char Error[512]);
/* Called synchronously by download recovery, never a second file writer. */
bool MdoLocalModelsResume(cstr Installation,xcancel* Cancel,char Error[512]);
bool MdoLocalModelServiceRead(const xvalue* Input,xcancel* Cancel,xvalue** Result,char Error[512]);
#endif
