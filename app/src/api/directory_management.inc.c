#include "../../include/mdo/extensions.h"
#include "../../include/mdo/extension_manifest.h"
#include "../../include/mdo/extension_tasks.h"

static bool MdoDirectoryApiParams(MdoApiContext* C,char Kind[33],char Id[97]){
 if(!C->ParamCount||C->Params[0].Size>32)return false;
 memcpy(Kind,C->Params[0].Data,C->Params[0].Size);
 Kind[C->Params[0].Size]=0;
 Id[0]=0;
 if(!MdoExtensionDirectoryKind(Kind))return false;
 if(C->ParamCount>=2){if(C->Params[1].Size>96)return false;
 memcpy(Id,C->Params[1].Data,C->Params[1].Size);
 Id[C->Params[1].Size]=0;
 if(!MdoExtensionDirectoryId(Id))return false;
 }
 return true;
}
static xvalue* MdoDirectoryApiQuery(MdoApiContext* C){
 xvalue* Out=xrtValueObject();bool Ok=Out!=NULL;for(size_t At=0;Ok&&At<C->Target.Query.Size;){size_t End=At,Equal=At;while(End<C->Target.Query.Size&&C->Target.Query.Data[End]!='&')End++;while(Equal<End&&C->Target.Query.Data[Equal]!='=')Equal++;
  size_t KN=0,VN=0;char* K=Equal<End?(char*)xrtPercentDecodeNew(xrtStrViewN(C->Target.Query.Data+At,Equal-At),&KN):NULL,*V=Equal<End?(char*)xrtPercentDecodeNew(xrtStrViewN(C->Target.Query.Data+Equal+1,End-Equal-1),&VN):NULL;
  Ok=K&&V&&KN&&KN<=16&&VN<=4096&&!memchr(K,0,KN)&&!memchr(V,0,VN)&&xrtUtf8Valid(xrtStrViewN(V,VN),NULL)&&(!strcmp(K,"offset")||!strcmp(K,"limit")||!strcmp(K,"path")||!strcmp(K,"q")||!strcmp(K,"state")||!strcmp(K,"project"))&&!xrtValueObjectHas(Out,xrtStrView(K));
  if(Ok)Ok=MdoApiValueSetString(Out,K,V);
  xrtFree(K);
  xrtFree(V);
  At=End+1;
  }
 if(!Ok){xrtValueRelease(Out);return NULL;}return Out;
}
static bool MdoDirectoryApiPage(const xvalue* Q,size_t* Offset,size_t* Limit){
 *Offset=0;*Limit=50;const cstr Names[]={"offset","limit"};for(size_t I=0;I<2;I++){cstr Text=MdoExtensionText(Q,Names[I],10);if(!Text)continue;uint64 N=0;if(!MdoArchiveApiOffset(xrtStrView(Text),&N)||N>(I?200:50000)||(I&&!N))return false;if(I)*Limit=(size_t)N;else *Offset=(size_t)N;}return true;
}
static bool MdoDirectoryApiItemFlags(xvalue* Row){
 cstr Source=MdoExtensionText(Row,"source",16);bool Manual=Source&&!strcmp(Source,"manual");return MdoApiValueSetBool(Row,"editable",Manual)&&MdoApiValueSetBool(Row,"shareable",Manual)&&MdoApiValueSetBool(Row,"store_managed",Source&&!strcmp(Source,"store"));
}
static xvalue* MdoDirectoryApiItem(cstr Kind,cstr Id,char Error[512]){
 xvalue* Item=MdoExtensionsRead(Kind,Id,Error);if(!Item){xvalue* Rows=MdoExtensionsList(Kind,Error);for(size_t I=0;I<xrtValueCount(Rows);I++){const xvalue* Row=xrtValueArrayGet(Rows,I);cstr Found=MdoExtensionText(Row,"id",96);if(Found&&!strcmp(Found,Id)){Item=xrtValueClone(Row);break;}}xrtValueRelease(Rows);}if(Item)MdoDirectoryApiItemFlags(Item);return Item;
}
static bool MdoDirectoryApiList(MdoApiContext* C,cstr Kind,const xvalue* Q,bool Files,cstr Id){
 char Error[512]={0};size_t Offset,Limit;if(!MdoDirectoryApiPage(Q,&Offset,&Limit))return MdoApiReplyError(C,400,"extension_page_invalid","Use an offset and a limit from 1 to 200",NULL);
 xvalue* All=Files?MdoExtensionsFiles(Kind,Id,C->SendCancel,Error):MdoExtensionsList(Kind,Error),*Rows=xrtValueArray(),*Data=xrtValueObject();bool Ok=All&&Rows&&Data;size_t Total=0;
 cstr Search=MdoExtensionText(Q,"q",4096),State=MdoExtensionText(Q,"state",16);
 for(size_t I=0;Ok&&I<xrtValueCount(All);I++){
  const xvalue* Row=xrtValueArrayGet(All,I);bool Enabled=false;xrtValueGetBool(xrtValueObjectGet(Row,XRT_STR_LITERAL("enabled")),&Enabled);
  if(!Files&&State&&((!strcmp(State,"enabled")&&!Enabled)||(!strcmp(State,"disabled")&&Enabled)))continue;
  if(Search&&Search[0]){char* Text=xrtJsonStringify(Row,false,NULL);bool Found=Text&&xrtStrCaseContains(xrtStrView(Text),xrtStrView(Search));xrtFree(Text);if(!Found)continue;}
  if(Total>=Offset&&xrtValueCount(Rows)<Limit){xvalue* Copy=xrtValueClone(Row);Ok=Copy&&(Files||MdoDirectoryApiItemFlags(Copy))&&xrtValueArrayAppend(Rows,Copy);xrtValueRelease(Copy);}Total++;
 }
 if(Ok)Ok=xrtValueObjectSet(Data,XRT_STR_LITERAL("items"),Rows)&&MdoApiValueSetUInt(Data,"total",Total)&&MdoApiValueSetUInt(Data,"offset",Offset)&&MdoApiValueSetUInt(Data,"limit",Limit);
 xrtValueRelease(All);xrtValueRelease(Rows);if(!Ok){xrtValueRelease(Data);return MdoApiReplyError(C,422,"extension_list_failed",Error[0]?Error:"Cannot list extension directories",NULL);}return MdoApiReplySuccessTake(C,200,Data,NULL);
}
static bool MdoDirectoryApiFile(MdoApiContext* C,cstr Kind,cstr Id,const xvalue* Q){
 char Error[512]={0};cstr Path=MdoExtensionText(Q,"path",4096);if(!MdoExtensionRelativePath(Path))return MdoApiReplyError(C,400,"extension_file_invalid","Choose a relative extension file",NULL);
 xvalue* Item=MdoDirectoryApiItem(Kind,Id,Error);char* Root=Item?MdoExtensionsRoot(Kind,Id):NULL,*Full=Root?xrtFormat("%s/%s",Root,Path):NULL;xfile F=Full?MdoHomeOpenRead(Full):NULL;xfileinfo Info;
 bool Ok=F&&xrtFileStat(F,&Info)&&Info.Type==XFILE_TYPE_FILE;char* Text=Ok&&Info.Size<=1048576?xrtMalloc((size_t)Info.Size+1):NULL;
 bool Read=Text&&xrtReadFull(F,Text,(size_t)Info.Size,NULL);if(Text)Text[Info.Size]=0;xrtClose(F);xrtFree(Full);xrtFree(Root);xrtValueRelease(Item);
 xvalue* Data=Ok?xrtValueObject():NULL;Ok=Ok&&Data&&MdoApiValueSetString(Data,"path",Path)&&MdoApiValueSetUInt(Data,"bytes",Info.Size);
 bool Utf8=Read&&!memchr(Text,0,(size_t)Info.Size)&&xrtUtf8Valid(xrtStrViewN(Text,(size_t)Info.Size),NULL);
 if(Ok)Ok=MdoApiValueSetBool(Data,"binary",!Utf8);
 if(Ok&&Utf8)Ok=MdoApiValueSetString(Data,"text",Text);
 xrtFree(Text);
 if(!Ok){xrtValueRelease(Data);return MdoApiReplyError(C,404,"extension_file_unavailable","File is unavailable; preview is limited to 1 MiB of UTF-8 text",NULL);}return MdoApiReplySuccessTake(C,200,Data,NULL);
}
bool MdoApiExtensionsRoute(MdoApiContext* C){
 char Kind[33],Id[97],Error[512]={0};if(!MdoDirectoryApiParams(C,Kind,Id))return MdoApiReplyError(C,400,"extension_path_invalid","Choose one of the nine types and a portable directory ID",NULL);
 xvalue* Q=MdoDirectoryApiQuery(C);if(!Q)return MdoApiReplyError(C,400,"extension_query_invalid","Unsupported or repeated extension query field",NULL);xhttpmethod Method=C->Request->head->MethodCode;bool Result=false;
 if(Method==XHTTP_METHOD_GET||Method==XHTTP_METHOD_HEAD){
  if(!Id[0]||MdoArchiveApiSuffix(C,"/files"))Result=MdoDirectoryApiList(C,Kind,Q,Id[0]!=0,Id);
  else if(MdoArchiveApiSuffix(C,"/file"))Result=MdoDirectoryApiFile(C,Kind,Id,Q);
  else{xvalue* Item=MdoDirectoryApiItem(Kind,Id,Error);char Hash[65];if(Item&&MdoExtensionsFingerprint(Kind,Id,Hash,C->SendCancel,Error))MdoApiValueSetString(Item,"revision",Hash);
   char* Root=Item?MdoExtensionsRoot(Kind,Id):NULL,*Path=Root?xrtFormat("%s/extension.json",Root):NULL,*Text=Path?MdoExtensionRead(Path,true,MDO_EXTENSION_MANIFEST_BYTES,NULL):NULL;xvalue* Manifest=Text?xrtJsonParse(xrtStrView(Text)):NULL;
   if(Manifest&&Item)xrtValueObjectSet(Item,XRT_STR_LITERAL("manifest"),Manifest);
   xrtValueRelease(Manifest);
   xrtFree(Text);
   xrtFree(Path);
   xrtFree(Root);
   Result=Item?MdoApiReplySuccessTake(C,200,Item,NULL):MdoApiReplyError(C,404,"extension_not_found",Error[0]?Error:"Extension directory is not installed",NULL);}
 }else{
  MdoApiJsonBody Body={0};MdoApiBodyStatus Status=MdoApiJsonBodyRead(C,&Body);
  if(Status!=MDO_API_BODY_OK){xrtValueRelease(Q);return MdoApiReplyBodyError(C,Status);}xvalue* Input=xrtValueType(Body.Value)==XVALUE_OBJECT?xrtValueClone(Body.Value):NULL;
  cstr Action=Method==XHTTP_METHOD_PUT?"save":Method==XHTTP_METHOD_DELETE?"remove":MdoArchiveApiSuffix(C,"/enabled")?"enable":MdoExtensionText(Input,"action",32);bool Pack=MdoArchiveApiSuffix(C,"/pack");
  bool Ok=Input&&Id[0]&&(Pack||Action)&&MdoApiValueSetString(Input,"kind",Kind)&&MdoApiValueSetString(Input,"id",Id)&&(Pack||MdoApiValueSetString(Input,"action",Action));
  /* Arbitrary archive paths and store provenance are not request-controlled.
   * Install uploaded ZIPs only through their sealed task endpoint. */
  if(Ok&&!Pack&&(!strcmp(Action,"install")||!strcmp(Action,"import")||!strcmp(Action,"create")))Ok=false;
  Result=Ok?MdoApiDirectoryOperation(C,Input,Pack):MdoApiReplyError(C,422,"extension_operation_invalid","Use directory save/edit/activation, or the sealed ZIP task install endpoint",NULL);
  xrtValueRelease(Input);MdoApiJsonBodyUnit(&Body);
 }xrtValueRelease(Q);return Result;
}
