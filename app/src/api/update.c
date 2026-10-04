#include "internal.h"
#include "../../include/mdo/update.h"

static bool MdoApiUpdateStatus(MdoApiContext* Context)
{
    MdoUpdateStatus Status;
    if (!MdoUpdateGetStatus(&Status)) return MdoApiReplyError(Context,503,"update_unavailable","Update manager unavailable",NULL);
    xvalue* Data = xrtValueObject();
    bool Ok = Data && MdoApiValueSetBool(Data,"enabled",Status.Enabled) &&
        MdoApiValueSetBool(Data,"busy",Status.Busy) && MdoApiValueSetBool(Data,"ready",Status.Ready) &&
        MdoApiValueSetString(Data,"status",Status.State) &&
        MdoApiValueSetString(Data,"platform",Status.Platform) &&
        MdoApiValueSetString(Data,"local_sha256",Status.LocalHash) &&
        MdoApiValueSetString(Data,"sha256",Status.Hash) &&
        MdoApiValueSetUInt(Data,"size",Status.Bytes) && MdoApiValueSetString(Data,"notes",Status.Notes) &&
        MdoApiValueSetString(Data,"message",Status.Message) &&
        MdoApiValueSetString(Data,"last_install_message",Status.LastInstallMessage);
    if (!Ok) { xrtValueRelease(Data); return MdoApiReplyError(Context,503,"out_of_memory","Cannot read update status",NULL); }
    return MdoApiReplySuccessTake(Context,200,Data,NULL);
}
bool MdoApiUpdateRoute(MdoApiContext* Context)
{
    if (Context->Request->head->MethodCode == XHTTP_METHOD_POST && !MdoUpdateCheck())
        return MdoApiReplyError(Context,409,"update_busy","Update is disabled or busy",NULL);
    return MdoApiUpdateStatus(Context);
}
bool MdoApiUpdateDownloadRoute(MdoApiContext* Context)
{
    if (Context->Request->head->MethodCode == XHTTP_METHOD_DELETE) MdoUpdateCancel();
    else if (!MdoUpdateDownload()) return MdoApiReplyError(Context,409,"update_busy","Check for an update before downloading",NULL);
    return MdoApiUpdateStatus(Context);
}
bool MdoApiUpdateInstallRoute(MdoApiContext* Context)
{
    if (!MdoUpdateInstall()) return MdoApiReplyError(Context,409,"update_not_ready","Download and verify the update first",NULL);
    MdoUpdateStatus Status; MdoUpdateGetStatus(&Status);
    xvalue* Data = xrtValueObject(); MdoApiValueSetString(Data,"status",Status.State);
    return MdoApiReplySuccessTake(Context,202,Data,NULL);
}
