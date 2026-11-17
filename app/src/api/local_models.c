#include "internal.h"
#include "../../include/mdo/local_models.h"
static bool LmApiInput(MdoApiContext* C,xvalue* Input){
 char E[512]={0};xvalue* Out=NULL;xstrview A={0};xrtValueGetString(xrtValueObjectGet(Input,XRT_STR_LITERAL("action")),&A);
 if(A.Data&&!strcmp(A.Data,"verify")){bool Ok=MdoApiLocalModelVerify(C,Input);xrtValueRelease(Input);return Ok;}
 bool Read=A.Data&&(!strcmp(A.Data,"catalog")||!strcmp(A.Data,"detail")||!strcmp(A.Data,"plan")||!strcmp(A.Data,"updates"));
 bool Ok=Read?MdoLocalModelsRead(Input,C->SendCancel,&Out,E):MdoLocalModelsCommand(Input,&Out,E);xrtValueRelease(Input);
 if(Ok)return MdoApiReplySuccessTake(C,200,Out,NULL);xrtValueRelease(Out);return MdoApiReplyError(C,422,"local_model_operation_failed",E[0]?E:"The local model operation could not be completed",NULL);
}
bool MdoApiLocalModelsRoute(MdoApiContext* C){
 if(C->Request->head->MethodCode==XHTTP_METHOD_GET){char E[512]={0};xvalue* Out=MdoLocalModelsSnapshot(E);if(Out)return MdoApiReplySuccessTake(C,200,Out,NULL);return MdoApiReplyError(C,503,"local_model_unavailable",E,NULL);}
 MdoApiJsonBody Body={0};MdoApiBodyStatus S=MdoApiJsonBodyRead(C,&Body);if(S!=MDO_API_BODY_OK)return MdoApiReplyBodyError(C,S);
 xvalue* Input=xrtValueClone(Body.Value);MdoApiJsonBodyUnit(&Body);
 /* Lifecycle controls must remain responsive while a model probe or store
  * transfer occupies the single network operation worker. These operations
  * do no nested HTTP and only mutate short, locked state. */
 cstr Action=MdoExtensionText(Input,"action",32);if(Action&&(!strcmp(Action,"start")||!strcmp(Action,"stop")||!strcmp(Action,"logs")||!strcmp(Action,"web_access")))return LmApiInput(C,Input);
 return MdoApiQueueOperation(C,Input,LmApiInput);
}
