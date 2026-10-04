#include "internal.h"
#include "../account/internal.h"
#include "../../include/mdo/remote.h"

bool MdoApiRemoteRoute(MdoApiContext* Context)
{
    if (Context->Request->head->MethodCode == XHTTP_METHOD_GET)
        return MdoApiReplySuccessTake(Context,200u,MdoRemoteSnapshot(),NULL);
    MdoApiJsonBody body;
    MdoApiBodyStatus status = MdoApiJsonBodyRead(Context,&body);
    if (status != MDO_API_BODY_OK) return MdoApiReplyBodyError(Context,status);
    bool valid = xrtValueType(body.Value) == XVALUE_OBJECT;
    MdoRemoteAction action = 0; cstr argument = NULL;
    if (xrtStrEqual(Context->Target.Path,XRT_STR_LITERAL("/api/v1/connector/ticket"))) {
        cstr id = MdoAccountText(body.Value,"job_id",32u);
        xvalue* ticket = valid && xrtValueCount(body.Value) == 1u ? MdoRemoteTicketTake(id) : NULL;
        MdoApiJsonBodyUnit(&body);
        if (!ticket) return MdoApiReplyError(Context,409u,"remote_ticket_unavailable",
            "Connection ticket is unavailable; request a new connection",NULL);
        return MdoApiReplySecretSuccessTake(Context,ticket);
    }
    if (xrtStrEqual(Context->Target.Path,XRT_STR_LITERAL("/api/v1/remote"))) {
        bool allow = false;
        valid = valid && xrtValueGetBool(xrtValueObjectGet(body.Value,XRT_STR_LITERAL("allow_remote")),&allow);
        argument = MdoAccountText(body.Value,"name",96u);
        valid = valid && xrtValueCount(body.Value) == (allow ? 2u : 1u);
        action = allow ? MDO_REMOTE_ENABLE : MDO_REMOTE_DISABLE;
    } else {
        cstr command = MdoAccountText(body.Value,"action",16u);
        argument = MdoAccountText(body.Value,"device_id",32u);
        size_t count = 2u;
        if (command && !strcmp(command,"refresh")) { action = MDO_REMOTE_LIST; count = 1u; }
        else if (command && !strcmp(command,"revoke")) action = MDO_REMOTE_REVOKE;
        else if (command && !strcmp(command,"remove")) action = MDO_REMOTE_REMOVE;
        else if (command && !strcmp(command,"connect")) {
            cstr mode = MdoAccountText(body.Value,"mode",16u); count = 3u;
            if (mode && !strcmp(mode,"control")) action = MDO_REMOTE_CONTROL;
            else if (mode && !strcmp(mode,"view")) action = MDO_REMOTE_VIEW;
        }
        valid = valid && action && xrtValueCount(body.Value) == count;
    }
    bool accepted = valid && MdoRemoteRequest(action,argument);
    MdoApiJsonBodyUnit(&body);
    if (!accepted) return MdoApiReplyError(Context,valid ? 409u : 400u,"remote_action_unavailable",
        "Check the request or wait for the current device operation",NULL);
    return MdoApiReplySuccessTake(Context,202u,MdoRemoteSnapshot(),NULL);
}
