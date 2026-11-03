static bool MdoExtServicePlanBool(const xvalue* V,cstr K){bool B=false;return xrtValueGetBool(xrtValueObjectGet(V,xrtStrView(K)),&B)&&B;}
/* Download only the versions/hashes shown in the inspected plan. All owned
 * archives are sealed and retained before any directory is suspended. */
bool MdoExtensionServicePlan(const xvalue* Input,xcancel* Cancel,xvalue** Result,char Error[512]){
    *Result=NULL;xvalue* Query=xrtValueClone(Input);bool Ok=Query&&MdoExtServicePut(Query,"action","plan");xvalue* Remote=NULL;
    if(Ok)Ok=MdoExtensionServiceRead(Query,Cancel,&Remote,Error);
    xrtValueRelease(Query);uint64 Protocol=0;
    cstr Root=MdoExtensionText(Remote,"root",MDO_EXTENSION_REF_BYTES);const xvalue* Items=xrtValueObjectGet(Remote,XRT_STR_LITERAL("items"));
    Ok=Ok&&Root&&MdoExtServiceNumber(Remote,"dependency_protocol",&Protocol)&&Protocol==1&&xrtValueType(Items)==XVALUE_ARRAY;
    xvalue* Request=xrtValueObject();Ok=Ok&&Request&&MdoExtServicePut(Request,"action","install")&&MdoExtServicePut(Request,"ref",Root)&&MdoExtensionsPlan(Request,Items,Result,Error);
    xrtValueRelease(Request);xrtValueRelease(Remote);if(!Ok&&Error&&!Error[0])MdoExtServiceError(Error,"The store does not provide a complete dependency protocol 1 plan; update the platform before installing");return Ok;
}
static bool MdoExtServiceApplyPlanAttempt(const xvalue* Input,xcancel* Cancel,xvalue** Result,unsigned* Retries,char Error[512]){
    *Result=NULL;if(Error)Error[0]=0;cstr Id=MdoExtensionText(Input,"plan_id",32),Revision=MdoExtensionText(Input,"revision",64);xvalue* Plan=MdoExtensionsPlanRead(Id,Revision,Error);if(!Plan)return false;
    *Result=MdoExtensionsPlanReceipt(Id,Revision,Error);if(*Result){xrtValueRelease(Plan);return true;}if(Error&&Error[0]){xrtValueRelease(Plan);return false;}
    const xvalue* Items=xrtValueObjectGet(Plan,XRT_STR_LITERAL("items"));size_t Count=xrtValueCount(Items);bool Ok=MdoExtServicePlanBool(Input,"trust_code")&&MdoExtServicePlanBool(Plan,"ready")&&Count&&Count<=128;uint64 Total=0;
    for(size_t I=0;Ok&&I<Count;I++){
        const xvalue* Item=xrtValueArrayGet(Items,I);cstr Op=MdoExtensionText(Item,"operation",16);if(!Op||strcmp(Op,"install"))continue;uint64 Size=0;Ok=MdoExtServiceNumber(Item,"bytes",&Size)&&Size&&Size<=MDO_EXTENSION_ARCHIVE_BYTES&&Total<=1073741824ULL-Size;if(Ok)Total+=Size;
    }
    MdoExtensionTaskUse** Uses=Ok?xrtCalloc(Count,sizeof(*Uses)):NULL;xvalue* Downloads=xrtValueArray(),*Tasks=xrtValueArray();Ok=Ok&&Uses&&Downloads&&Tasks;
    for(size_t I=0;Ok&&I<Count;I++){
        const xvalue* Item=xrtValueArrayGet(Items,I);cstr Op=MdoExtensionText(Item,"operation",16);if(strcmp(Op,"install"))continue;
        xvalue* Task=NULL;Ok=MdoExtServiceDownloadAttempt(MdoExtensionText(Item,"kind",32),MdoExtensionText(Item,"version_id",32),Cancel,&Task,Retries,Error);cstr TaskId=MdoExtensionText(Task,"id",32);
        if(Ok)Ok=TaskId&&xrtValueArrayAppendNew(Tasks,xrtValueString(xrtStrView(TaskId)));
        if(Ok)Uses[I]=MdoExtensionTaskUseBegin(TaskId,false,Cancel,Error);
        char* Archive=Uses[I]?MdoExtensionTaskArchive(TaskId,Error):NULL;
        const xvalue* Origin=xrtValueObjectGet(Task,XRT_STR_LITERAL("store"));xvalue* Credits=Origin?xrtValueClone(Origin):NULL,*D=xrtValueObject();cstr Ref=MdoExtensionText(Item,"id",MDO_EXTENSION_REF_BYTES),Hash=MdoExtensionText(Item,"sha256",64);
        Ok=Ok&&Uses[I]&&Archive&&Credits&&D&&Ref&&Hash&&MdoExtensionText(Credits,"id",MDO_EXTENSION_REF_BYTES)&&!strcmp(Ref,MdoExtensionText(Credits,"id",MDO_EXTENSION_REF_BYTES))&&MdoExtensionText(Credits,"sha256",64)&&!strcmp(Hash,MdoExtensionText(Credits,"sha256",64))&&MdoExtServicePut(Credits,"source","store")&&MdoExtServicePut(D,"id",Ref)&&MdoExtServicePut(D,"archive",Archive)&&xrtValueObjectSet(D,XRT_STR_LITERAL("credits"),Credits)&&xrtValueArrayAppend(Downloads,D);
        xrtValueRelease(D);xrtValueRelease(Credits);xrtValueRelease(Task);xrtFree(Archive);
    }
    if(Ok)Ok=MdoExtensionsApplyPlan(Input,Downloads,Cancel,Result,Error);
    for(size_t I=0;I<Count;I++)if(Uses)MdoExtensionTaskUseEnd(Uses[I]);
    for(size_t I=0;I<xrtValueCount(Tasks);I++){
        xstrview Task;xrtValueGetString(xrtValueArrayGet(Tasks,I),&Task);xvalue* Receipt=NULL;char Why[512]={0};bool Saved=Ok?MdoExtensionTaskComplete(Task.Data,*Result,&Receipt,Why):Cancel&&xrtCancelRequested(Cancel)?MdoExtensionTaskCancel(Task.Data,&Receipt,Why):MdoExtensionTaskFail(Task.Data,Error&&Error[0]?Error:"Extension group installation failed",&Receipt,Why);if(!Saved&&Ok&&*Result)MdoExtServicePut(*Result,"transfer_receipt_error",Why);xrtValueRelease(Receipt);
    }
    xrtFree(Uses);xrtValueRelease(Downloads);xrtValueRelease(Tasks);xrtValueRelease(Plan);if(!Ok&&Error&&!Error[0])MdoExtServiceError(Error,"Confirm code approval for the complete inspected plan; the group must fit the 1 GiB transfer budget");return Ok;
}

/* One native operation owns all read retries. Keep earlier sealed downloads,
 * trust, pinned versions and the approval revision throughout the transfer. */
bool MdoExtensionServiceApplyPlan(const xvalue* Input,xcancel* Cancel,xvalue** Result,char Error[512]){
 unsigned Retries=0;bool Ok=MdoExtServiceApplyPlanAttempt(Input,Cancel,Result,&Retries,Error);
 if(Ok&&Retries&&*Result)xrtValueObjectSetNew(*Result,XRT_STR_LITERAL("retry_count"),xrtValueUInt(Retries));
 return Ok;
}

/* Rebuild only from the already inspected, pinned package metadata. Earlier
 * successes in this batch may have supplied a shared dependency; no newly
 * published version is selected implicitly. All other changes require review. */
static xvalue* MdoExtServiceBatchReplan(const xvalue* Old,const xvalue* Completed,char Error[512]){
 const xvalue* Before=xrtValueObjectGet(Old,XRT_STR_LITERAL("items"));xvalue* Packages=xrtValueArray(),*Request=xrtValueObject(),*Fresh=NULL;bool Ok=Packages&&Request;
 for(size_t I=0;Ok&&I<xrtValueCount(Before);I++){const xvalue* R=xrtValueArrayGet(Before,I);if(xrtValueObjectHas(R,XRT_STR_LITERAL("manifest")))Ok=xrtValueArrayAppend(Packages,R);}
 Ok=Ok&&MdoExtServicePut(Request,"action","install")&&MdoExtServicePut(Request,"ref",MdoExtensionText(Old,"root",MDO_EXTENSION_REF_BYTES))&&MdoExtensionsPlan(Request,Packages,&Fresh,Error)&&MdoExtServicePlanBool(Fresh,"ready");
 const xvalue* After=xrtValueObjectGet(Fresh,XRT_STR_LITERAL("items"));Ok=Ok&&xrtValueCount(Before)==xrtValueCount(After);
 for(size_t I=0;Ok&&I<xrtValueCount(After);I++){const xvalue* B=xrtValueArrayGet(After,I),*A=NULL;cstr Id=MdoExtensionText(B,"id",MDO_EXTENSION_REF_BYTES);
  for(size_t J=0;Id&&J<xrtValueCount(Before);J++){const xvalue* R=xrtValueArrayGet(Before,J);cstr Ref=MdoExtensionText(R,"id",MDO_EXTENSION_REF_BYTES);if(Ref&&!strcmp(Id,Ref)){A=R;break;}}
  cstr Committed=Id?MdoExtensionText(Completed,Id,64):NULL,Current=MdoExtensionText(B,"expected_revision",64);
  bool Shared=A&&Committed&&Current&&!strcmp(Committed,Current);
  Ok=A&&MdoExtServicePlanBool(A,"desired_enabled")==MdoExtServicePlanBool(B,"desired_enabled");
  const cstr Keys[]={"kind","directory_id","version_id","sha256","expected_revision","operation"};
  for(size_t J=0;Ok&&J<6;J++){cstr X=MdoExtensionText(A,Keys[J],4096),Y=MdoExtensionText(B,Keys[J],4096);
   if(Shared&&J==4)continue;
   if(Shared&&J==5&&X&&Y&&!strcmp(X,"install")&&!strcmp(Y,"flags"))continue;
   Ok=(!X&&!Y)||(X&&Y&&!strcmp(X,Y));
  }
 }
 xrtValueRelease(Request);xrtValueRelease(Packages);
 if(!Ok){if(Fresh){char Why[512]={0};MdoExtensionsPlanDiscard(MdoExtensionText(Fresh,"id",32),MdoExtensionText(Fresh,"revision",64),Why);}xrtValueRelease(Fresh);Fresh=NULL;if(Error&&!Error[0])MdoExtServiceError(Error,"The inspected update or its dependencies changed; check updates again");}return Fresh;
}
bool MdoExtensionServiceUpdateBatch(const xvalue* Input,xcancel* Cancel,xvalue** Result,char Error[512]){
 *Result=NULL;const xvalue* Plans=xrtValueObjectGet(Input,XRT_STR_LITERAL("plans"));size_t Count=xrtValueCount(Plans);
 if(!MdoExtServicePlanBool(Input,"trust_code")||xrtValueType(Plans)!=XVALUE_ARRAY||!Count||Count>128)return MdoExtServiceError(Error,"Confirm between one and 128 inspected update plans");
 xvalue* Saved=xrtValueArray(),*Seen=xrtValueObject(),*Completed=xrtValueObject(),*Rows=xrtValueArray(),*Items=xrtValueArray();bool Ok=Saved&&Seen&&Completed&&Rows&&Items;
 for(size_t I=0;Ok&&I<Count;I++){const xvalue* R=xrtValueArrayGet(Plans,I);xvalue* Plan=MdoExtensionsPlanRead(MdoExtensionText(R,"id",32),MdoExtensionText(R,"revision",64),Error);cstr Root=MdoExtensionText(Plan,"root",MDO_EXTENSION_REF_BYTES);
  Ok=Root&&!xrtValueObjectHas(Seen,xrtStrView(Root))&&MdoExtServicePlanBool(Plan,"ready")&&xrtValueObjectSetNew(Seen,xrtStrView(Root),xrtValueBool(true))&&xrtValueArrayAppend(Saved,Plan);xrtValueRelease(Plan);
 }
 size_t Failed=0;
 for(size_t I=0;Ok&&I<Count;I++){const xvalue* Old=xrtValueArrayGet(Saved,I);char Why[512]={0};bool Cancelled=Cancel&&xrtCancelRequested(Cancel);xvalue* Fresh=!Cancelled?MdoExtServiceBatchReplan(Old,Completed,Why):NULL,*Request=xrtValueObject(),*Receipt=NULL;
  bool Applied=Fresh&&Request&&MdoExtServicePut(Request,"plan_id",MdoExtensionText(Fresh,"id",32))&&MdoExtServicePut(Request,"revision",MdoExtensionText(Fresh,"revision",64))&&xrtValueObjectSetNew(Request,XRT_STR_LITERAL("trust_code"),xrtValueBool(true))&&MdoExtensionServiceApplyPlan(Request,Cancel,&Receipt,Why);
  bool Ready=Applied&&MdoExtServicePlanBool(Receipt,"runtime_ready");const xvalue* Changes=xrtValueObjectGet(Receipt,XRT_STR_LITERAL("items"));
  for(size_t J=0;Applied&&J<xrtValueCount(Changes);J++){const xvalue* R=xrtValueArrayGet(Changes,J);cstr Id=MdoExtensionText(R,"id",MDO_EXTENSION_REF_BYTES),Digest=MdoExtensionText(R,"new_digest",64);if(!Digest)Digest=MdoExtensionText(R,"old_digest",64);Ok=Ok&&Id&&Digest&&MdoExtServicePut(Completed,Id,Digest)&&xrtValueArrayAppend(Items,R);}
  xvalue* Row=xrtValueObject();Ok=Ok&&Row&&MdoExtServicePut(Row,"id",MdoExtensionText(Old,"root",MDO_EXTENSION_REF_BYTES))&&MdoExtServicePut(Row,"state",Cancelled?"cancelled":Ready?"complete":"failed")&&MdoExtServicePut(Row,"error",Ready?"":Why[0]?Why:Applied?"Files updated; runtime requires attention":"Extension update cancelled")&&xrtValueArrayAppend(Rows,Row);xrtValueRelease(Row);if(!Ready)Failed++;
  if(Fresh&&!Applied){char Ignore[512]={0};MdoExtensionsPlanDiscard(MdoExtensionText(Fresh,"id",32),MdoExtensionText(Fresh,"revision",64),Ignore);}
  if(Applied){char Ignore[512]={0};MdoExtensionsPlanDiscard(MdoExtensionText(Old,"id",32),MdoExtensionText(Old,"revision",64),Ignore);}
  xrtValueRelease(Receipt);xrtValueRelease(Request);xrtValueRelease(Fresh);
 }
 xvalue* Out=Ok?xrtValueObject():NULL;Ok=Ok&&Out&&xrtValueObjectSet(Out,XRT_STR_LITERAL("results"),Rows)&&xrtValueObjectSet(Out,XRT_STR_LITERAL("items"),Items)&&xrtValueObjectSetNew(Out,XRT_STR_LITERAL("runtime_ready"),xrtValueBool(!Failed));
 if(Ok&&Failed){char Message[128];snprintf(Message,sizeof(Message),"%zu of %zu extension updates need attention",Failed,Count);Ok=MdoExtServicePut(Out,"runtime_error",Message);}
 xrtValueRelease(Saved);xrtValueRelease(Seen);xrtValueRelease(Completed);xrtValueRelease(Rows);xrtValueRelease(Items);if(!Ok){xrtValueRelease(Out);return false;}*Result=Out;return true;
}
