static bool LmStateValid(const xvalue* State){
 const xvalue* Roots=LmGet(State,"roots"),*Jobs=LmGet(State,"installations");
 if(!LmFields(State,"schema owner roots installations default_root deployments")||strcmp(LmStr(State,"schema"),"mdo.local-model-state.v1")||!LmXid(LmStr(State,"owner"))||xrtValueType(Roots)!=XVALUE_OBJECT||xrtValueCount(Roots)>LM_LIMIT||xrtValueType(Jobs)!=XVALUE_OBJECT||xrtValueCount(Jobs)>LM_LIMIT)return false;
 const xvalue* Deployments=LmGet(State,"deployments");if(Deployments){if(xrtValueType(Deployments)!=XVALUE_OBJECT||xrtValueCount(Deployments)>LM_LIMIT)return false;for(size_t I=0;I<xrtValueCount(Deployments);I++){xstrview K={0};const xvalue* D=xrtValueObjectAt(Deployments,I,&K);if(!LmDeploymentValid(D)||strcmp(K.Data,LmStr(D,"id"))||!LmGet(Jobs,LmStr(D,"installation")))return false;}}
 cstr Default=LmStr(State,"default_root");if(Default[0]&&!LmGet(Roots,Default))return false;
 for(size_t I=0;I<xrtValueCount(Roots);I++){xstrview K={0};const xvalue* R=xrtValueObjectAt(Roots,I,&K);cstr Path=LmStr(R,"path");if(!LmXid(K.Data)||!LmFields(R,"id path")||strcmp(K.Data,LmStr(R,"id"))||!Path[0]||(!xrtPathIsAbs(Path)&&!MdoExtensionRelativePath(Path)))return false;}
 for(size_t I=0;I<xrtValueCount(Jobs);I++){
  xstrview K={0};const xvalue* J=xrtValueObjectAt(Jobs,I,&K);cstr Kind=LmStr(J,"kind");
  if(!LmXid(K.Data)||strcmp(K.Data,LmStr(J,"id"))||!LmFields(J,"id name kind state store plan runtime_plan assets source_path source_store install_plan_id error updated_at")||!LmGet(Roots,LmStr(J,"store"))||!LmOneOf(Kind,"download copy reference")||!LmOneOf(LmStr(J,"state"),"queued installing installed interrupted failed attention removing cleanup_pending"))return false;
  const xvalue* SourceStore=LmGet(J,"source_store");if(SourceStore&&(strcmp(Kind,"reference")||!MdoExtensionText(J,"source_store",32)||(LmStr(J,"source_store")[0]&&(!LmGet(Roots,LmStr(J,"source_store"))||strcmp(LmStr(J,"source_store"),LmStr(J,"store"))))))return false;
  const xvalue* Assets=LmGet(J,"assets");
  if(!strcmp(Kind,"download")){char E[512]={0};if(!MdoLocalModelPlanValid(LmGet(J,"plan"),E)||!xrtValueEqual(Assets,LmGet(LmGet(J,"plan"),"assets"))||!LmXid(LmStr(LmGet(J,"runtime_plan"),"id"))||!LmHex(LmStr(LmGet(J,"runtime_plan"),"revision"),64))return false;}
  else {if(!xrtPathIsAbs(LmStr(J,"source_path")))return false;if(Assets&&(xrtValueType(Assets)!=XVALUE_ARRAY||xrtValueCount(Assets)!=1||!LmHex(LmStr(xrtValueArrayGet(Assets,0),"sha256"),64)||!LmUInt(xrtValueArrayGet(Assets,0),"bytes")))return false;}
 }
 return true;
}
static bool LmStoreLease(xroot Root,cstr Id,cstr Native,char E[512]){
 xrtMutexLock(g_Lm.Lock);size_t Slot=LM_LIMIT;
 for(size_t I=0;I<LM_LIMIT;I++){if(!strcmp(g_Lm.Stores[I].Id,Id)){bool Same=!strcmp(g_Lm.Stores[I].Path,Native);xrtMutexUnlock(g_Lm.Lock);return Same||LmFail(E,"This resource identity is already open at another path");}if(!g_Lm.Stores[I].Id[0]&&Slot==LM_LIMIT)Slot=I;}
 xfile F=Slot<LM_LIMIT?LmFile(Root,".mdo-resource-store.lock",XFILE_READ|XFILE_WRITE|XFILE_CREATE):NULL;
 bool Ok=F&&xrtFileLock(F,XFILE_LOCK_EXCLUSIVE,false);char* Path=Ok?xrtStrDup(Native):NULL;
 if(Ok&&Path){g_Lm.Stores[Slot].File=F;g_Lm.Stores[Slot].Path=Path;snprintf(g_Lm.Stores[Slot].Id,33,"%s",Id);}
 else{xrtClose(F);xrtFree(Path);Ok=false;LmFail(E,"The resource directory is in use by another mdo Home or cannot be locked");}
 xrtMutexUnlock(g_Lm.Lock);return Ok;
}
