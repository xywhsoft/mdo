#include "internal.h"
#include "../../include/mdo/local_models.h"
static bool LmApiInput(MdoApiContext* C,xvalue* Input){
 char E[512]={0};xvalue* Out=NULL;xstrview A={0};xrtValueGetString(xrtValueObjectGet(Input,XRT_STR_LITERAL("action")),&A);
 bool Read=A.Data&&(!strcmp(A.Data,"catalog")||!strcmp(A.Data,"detail")||!strcmp(A.Data,"plan"));
 bool Ok=Read?MdoLocalModelsRead(Input,C->SendCancel,&Out,E):MdoLocalModelsCommand(Input,&Out,E);xrtValueRelease(Input);
 if(Ok)return MdoApiReplySuccessTake(C,200,Out,NULL);xrtValueRelease(Out);return MdoApiReplyError(C,422,"local_model_operation_failed",E[0]?E:"The local model operation could not be completed",NULL);
}
bool MdoApiLocalModelsRoute(MdoApiContext* C){
 if(C->Request->head->MethodCode==XHTTP_METHOD_GET){char E[512]={0};xvalue* Out=MdoLocalModelsSnapshot(E);if(Out)return MdoApiReplySuccessTake(C,200,Out,NULL);return MdoApiReplyError(C,503,"local_model_unavailable",E,NULL);}
 MdoApiJsonBody Body={0};MdoApiBodyStatus S=MdoApiJsonBodyRead(C,&Body);if(S!=MDO_API_BODY_OK)return MdoApiReplyBodyError(C,S);
 xvalue* Input=xrtValueClone(Body.Value);MdoApiJsonBodyUnit(&Body);return MdoApiQueueOperation(C,Input,LmApiInput);
}
