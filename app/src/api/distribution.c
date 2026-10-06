#include "internal.h"
#include "../../include/mdo/distribution.h"
bool MdoApiDistributionRoute(MdoApiContext* Context)
{
    if(Context->Request->head->MethodCode==XHTTP_METHOD_POST) {
        MdoApiJsonBody Body={0}; MdoApiBodyStatus Read=MdoApiJsonBodyRead(Context,&Body);
        if(Read!=MDO_API_BODY_OK)return MdoApiReplyBodyError(Context,Read);
        xstrview Action={0},Id={0};
        bool Ok=xrtValueGetString(xrtValueObjectGet(Body.Value,XRT_STR_LITERAL("action")),&Action)&&Action.Size<16&&!memchr(Action.Data,0,Action.Size);
        (void)xrtValueGetString(xrtValueObjectGet(Body.Value,XRT_STR_LITERAL("id")),&Id);
        char A[16]={0},I[16]={0}; if(Ok)memcpy(A,Action.Data,Action.Size);
        if(Id.Size<16&&Id.Data&&!memchr(Id.Data,0,Id.Size))memcpy(I,Id.Data,Id.Size);
        Ok=Ok&&MdoDistributionRequest(A,I); MdoApiJsonBodyUnit(&Body);
        if(!Ok)return MdoApiReplyError(Context,409,"distribution_busy","Operation unavailable or already in progress",NULL);
    }
    xvalue* Data=MdoDistributionSnapshot();
    return Data?MdoApiReplySuccessTake(Context,200,Data,NULL):MdoApiReplyError(Context,503,"distribution_unavailable","Distribution manager unavailable",NULL);
}
