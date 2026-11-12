#include "../../include/mdo/extension_settings.h"

static bool MdoApiExtensionSettings(MdoApiContext* C,cstr Kind,cstr Id){
 char Error[512]={0};xvalue* Out=NULL;bool Read=C->Request->head->MethodCode==XHTTP_METHOD_GET;
 if(Read)Out=MdoExtensionSettingsRead(Kind,Id,Error);
 else{
  MdoApiJsonBody Body={0};MdoApiBodyStatus Status=MdoApiJsonBodyRead(C,&Body);if(Status!=MDO_API_BODY_OK)return MdoApiReplyBodyError(C,Status);
  const xhttpfield* Header=NULL;xhttpnext Next=xrtHttpFieldGetUnique(C->Request->head->Fields,C->Request->head->FieldCount,XRT_STR_LITERAL("If-Match"),&Header);
  if(Next==XHTTP_NEXT_END){ExtensionBodyUnit(&Body);return MdoApiReplyError(C,428,"precondition_required","Read the current extension settings before saving",NULL);}
  cstr Revision=MdoEsText(Body.Value,"revision");char Tag[68]={0};if(Revision&&strlen(Revision)==64)snprintf(Tag,sizeof(Tag),"\"%s\"",Revision);
  xstrview Match=Header?xrtStrTrim(Header->Value):xrtStrView("");
  if(Next!=XHTTP_NEXT_ITEM||!Tag[0]||!xrtStrEqual(Match,xrtStrView(Tag))){ExtensionBodyUnit(&Body);return MdoApiReplyError(C,412,"extension_settings_conflict","The settings revision changed; your edits were retained",NULL);}
  bool Preview=MdoArchiveApiSuffix(C,"/preview");bool Ok=Body.Size<=128u*1024u&&MdoExtensionSettingsSave(Kind,Id,Body.Value,Preview,&Out,Error);ExtensionBodyUnit(&Body);
  if(!Ok){xrtValueRelease(Out);Out=NULL;}
 }
 if(!Out){bool Conflict=strstr(Error,"extension_settings_conflict:")!=NULL;bool Disabled=strstr(Error,"extension_settings_disabled:")!=NULL;return MdoApiReplyError(C,Conflict?412:Disabled?404:422,Conflict?"extension_settings_conflict":Disabled?"extension_settings_disabled":"extension_settings_invalid",Error[0]?Error:"Extension settings could not be read or saved",NULL);}
 char Tag[68];snprintf(Tag,sizeof(Tag),"\"%s\"",MdoEsText(Out,"revision"));return MdoApiReplySuccessTakeEntityTag(C,200,Out,Tag);
}
