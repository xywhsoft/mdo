bool MdoApiMcpTemplatesRoute(MdoApiContext* C){
 char Error[512]={0};xvalue* Rows=MdoExtensionsList("mcp",Error),*Items=xrtValueArray(),*Data=xrtValueObject();bool Ok=Rows&&Items&&Data;
 for(size_t I=0;Ok&&I<xrtValueCount(Rows);I++){
  const xvalue* Row=xrtValueArrayGet(Rows,I);cstr Id=MdoExtensionText(Row,"directory_id",96),Alias=MdoExtensionText(Row,"alias",64);char* Entry=Id?MdoExtensionsEntryPath("mcp",Id,Error):NULL,*Text=Entry?MdoExtensionRead(Entry,true,MDO_MCP_FILE_LIMIT,NULL):NULL;
  xvalue* Template=Text?xrtJsonParse(xrtStrView(Text)):NULL,*Item=xrtValueClone(Row);bool Valid=Template&&MdoMcpTemplateValidate(Template,Alias,Error,sizeof(Error));
  Ok=Item&&MdoDirectoryApiItemFlags(Item)&&MdoApiValueSetBool(Item,"template_valid",Valid);if(Ok&&Valid)Ok=xrtValueObjectSet(Item,XRT_STR_LITERAL("template"),Template);else if(Ok)Ok=MdoApiValueSetString(Item,"template_error",Error[0]?Error:"Invalid service recipe");
  if(Ok)Ok=xrtValueArrayAppend(Items,Item);
  xrtValueRelease(Item);
  xrtValueRelease(Template);
  xrtFree(Text);
  xrtFree(Entry);
 }
 if(Ok)Ok=xrtValueObjectSet(Data,XRT_STR_LITERAL("items"),Items);
 xrtValueRelease(Rows);
 xrtValueRelease(Items);
 if(!Ok){xrtValueRelease(Data);return MdoApiReplyError(C,422,"mcp_templates_unavailable",Error[0]?Error:"Cannot read installed MCP directories",NULL);}return MdoApiReplySuccessTake(C,200,Data,NULL);
}
bool MdoApiMcpConnectionsRoute(MdoApiContext* C){
 char Error[512]={0},Id[97]={0};xvalue* Out=NULL;bool Ok=false;
 if(C->Target.Query.Size)return MdoApiReplyError(C,400,"mcp_connection_scope","Project scope is a private connection field; use project_id in its saved configuration",NULL);
 if(C->Request->head->MethodCode==XHTTP_METHOD_GET){
  if(C->ParamCount){if(C->Params[0].Size>96)return MdoApiReplyError(C,400,"mcp_connection_id","Invalid connection ID",NULL);memcpy(Id,C->Params[0].Data,C->Params[0].Size);Id[C->Params[0].Size]=0;}
  Out=MdoMcpConnections(Id[0]?Id:NULL,Error);return Out?MdoApiReplySuccessTake(C,200,Out,NULL):MdoApiReplyError(C,404,"mcp_connection_unavailable",Error,NULL);
 }
 MdoApiJsonBody Body={0};MdoApiBodyStatus Read=MdoApiJsonBodyRead(C,&Body);if(Read!=MDO_API_BODY_OK)return MdoApiReplyBodyError(C,Read);
 if(C->ApplicationResponse){Ok=MdoMcpConnectionManage(Body.Value,C->SendCancel,&Out,Error);ExtensionBodyUnit(&Body);return MdoArchiveApiResult(C,Ok,Out,Error);}
 bool Sent=MdoArchiveApiStart(C,"",Body.Value,4);ExtensionBodyUnit(&Body);return Sent;
}
