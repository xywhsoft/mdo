#include "../../include/mdo/local_models.h"
bool MdoApiLocalModelVerify(MdoApiContext* C,const xvalue* Input){
 cstr Id=MdoApiModelText(Input,"id",32);char Model[64];snprintf(Model,sizeof(Model),"local-model.%s",Id?Id:"");
 xvalue* Rows=MdoLocalModelCatalog();const xvalue* D=NULL;for(size_t K=0;Id&&K<xrtValueCount(Rows);K++){const xvalue* Row=xrtValueArrayGet(Rows,K);cstr Key=MdoExtensionText(Row,"id",32);if(Key&&!strcmp(Key,Id))D=Row;}
 cstr Hash=MdoExtensionText(D,"fingerprint",64);if(!Hash){xrtValueRelease(Rows);return MdoApiReplyError(C,422,"local_model_missing","Select an installed deployment",NULL);}
 MdoModelCatalog* Catalog=MdoModelCatalogSnapshot();MdoModelInfo Info={0};Info.Size=sizeof(Info);xllm_error Error={0};xllmErrorInit(&Error);uint64 Latency=0;char E[512]={0};xvalue* Record=NULL;
 bool Found=MdoModelCatalogModelFind(Catalog,Model,&Info);bool Reply=Found&&MdoApiModelProbeExecute(C,Catalog,Model,MdoModelCatalogOutputBudget(Catalog,Model,NULL),false,&Latency,&Error);
 bool Saved=MdoLocalModelVerification(Id,Hash,"reply",Reply,&Record,E);xrtValueRelease(Record);Record=NULL;bool Tools=false,NeedsTools=Found&&(Info.Capabilities&XLLM_CAP_TOOL_CALL_OUT);
 if(Reply&&NeedsTools){Tools=MdoApiModelProbeExecute(C,Catalog,Model,MdoModelCatalogOutputBudget(Catalog,Model,NULL),true,&Latency,&Error);Saved=Saved&&MdoLocalModelVerification(Id,Hash,"tools",Tools,&Record,E);}else Saved=Saved&&MdoLocalModelVerification(Id,Hash,"tools",false,&Record,E);
 MdoModelCatalogRelease(Catalog);xrtValueRelease(Rows);if(!Saved){xrtValueRelease(Record);return MdoApiReplyError(C,409,"local_model_changed",E,NULL);}
 bool Passed=Reply&&(!NeedsTools||Tools);MdoApiValueSetBool(Record,"verified",Passed);MdoApiValueSetUInt(Record,"latency_ms",Latency);if(!Passed){MdoApiValueSetString(Record,"error_kind",MdoModelErrorKind(&Error));MdoApiValueSetString(Record,"error",MdoModelErrorMessage(&Error));}
 return MdoApiReplySuccessTake(C,200,Record,NULL);
}
