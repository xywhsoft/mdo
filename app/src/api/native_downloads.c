#include "internal.h"
#include "../../include/mdo/downloads.h"
bool MdoApiNativeDownloadsRoute(MdoApiContext* Context){
    if(Context->ParamCount){
        char Id[33]={0},Action[24]={0};
        if(Context->ParamCount!=2||Context->Params[0].Size!=32||Context->Params[1].Size>=sizeof(Action))
            return MdoApiReplyError(Context,400,"download_invalid","Choose a download task and action",NULL);
        memcpy(Id,Context->Params[0].Data,32);memcpy(Action,Context->Params[1].Data,Context->Params[1].Size);
        char Error[512]={0};if(!MdoDownloadsAction(Id,Action,Error))return MdoApiReplyError(Context,409,"download_state_conflict",Error,NULL);
    }
    xvalue* Items=MdoDownloadsList();if(!Items)return MdoApiReplyError(Context,503,"downloads_unavailable","Download task service is unavailable",NULL);
    return MdoApiReplySuccessTake(Context,200,Items,NULL);
}
