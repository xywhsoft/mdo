#include "../../include/mdo/local_models.h"
bool MdoLocalModelServiceRead(const xvalue* Input,xcancel* Cancel,xvalue** Result,char Error[512]){
 *Result=NULL;cstr Action=MdoExtensionText(Input,"action",16);char* Path=NULL;
 if(Action&&!strcmp(Action,"catalog")){xvalue* Q=xrtValueClone(Input);MdoExtServicePut(Q,"kind","local-models");bool Ok=MdoExtensionServiceRead(Q,Cancel,Result,Error);xrtValueRelease(Q);return Ok;}
 cstr Version=MdoExtensionText(Input,"version",32),Profile=MdoExtensionText(Input,"profile",64);
 if(!MdoExtServiceId(Version)||strspn(Version,"-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz")!=32||!Action)return MdoExtServiceError(Error,"Select a fixed model version");
 if(!strcmp(Action,"detail"))Path=xrtFormat("/api/v1/mdo/local-models/detail?version=%s",Version);
 else if(!strcmp(Action,"plan")&&LmId(Profile))Path=xrtFormat("/api/v1/mdo/local-models/plan?version=%s&platform=%s&profile=%s",Version,MdoExtensionRuntimePlatform(),Profile);
 if(!Path)return MdoExtServiceError(Error,"Select a model detail or deployment profile");
 *Result=MdoExtServiceJson(Path,"GET",NULL,NULL,Cancel,Error);xrtFree(Path);return *Result!=NULL;
}
