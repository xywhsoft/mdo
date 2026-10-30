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
bool MdoExtensionServiceApplyPlan(const xvalue* Input,xcancel* Cancel,xvalue** Result,char Error[512]){
    *Result=NULL;if(Error)Error[0]=0;cstr Id=MdoExtensionText(Input,"plan_id",32),Revision=MdoExtensionText(Input,"revision",64);xvalue* Plan=MdoExtensionsPlanRead(Id,Revision,Error);if(!Plan)return false;
    *Result=MdoExtensionsPlanReceipt(Id,Revision,Error);if(*Result){xrtValueRelease(Plan);return true;}if(Error&&Error[0]){xrtValueRelease(Plan);return false;}
    const xvalue* Items=xrtValueObjectGet(Plan,XRT_STR_LITERAL("items"));size_t Count=xrtValueCount(Items);bool Ok=MdoExtServicePlanBool(Input,"trust_code")&&MdoExtServicePlanBool(Plan,"ready")&&Count&&Count<=128;uint64 Total=0;
    for(size_t I=0;Ok&&I<Count;I++){
        const xvalue* Item=xrtValueArrayGet(Items,I);cstr Op=MdoExtensionText(Item,"operation",16);if(!Op||strcmp(Op,"install"))continue;uint64 Size=0;Ok=MdoExtServiceNumber(Item,"bytes",&Size)&&Size&&Size<=MDO_EXTENSION_ARCHIVE_BYTES&&Total<=1073741824ULL-Size;if(Ok)Total+=Size;
    }
    MdoExtensionTaskUse** Uses=Ok?xrtCalloc(Count,sizeof(*Uses)):NULL;xvalue* Downloads=xrtValueArray(),*Tasks=xrtValueArray();Ok=Ok&&Uses&&Downloads&&Tasks;
    for(size_t I=0;Ok&&I<Count;I++){
        const xvalue* Item=xrtValueArrayGet(Items,I);cstr Op=MdoExtensionText(Item,"operation",16);if(strcmp(Op,"install"))continue;
        xvalue* Task=NULL;Ok=MdoExtensionServiceDownload(MdoExtensionText(Item,"kind",32),MdoExtensionText(Item,"version_id",32),Cancel,&Task,Error);cstr TaskId=MdoExtensionText(Task,"id",32);
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
        xstrview Task;xrtValueGetString(xrtValueArrayGet(Tasks,I),&Task);xvalue* Receipt=NULL;char Why[512]={0};bool Saved=Ok?MdoExtensionTaskComplete(Task.Data,*Result,&Receipt,Why):MdoExtensionTaskCancel(Task.Data,&Receipt,Why);if(!Saved&&Ok&&*Result)MdoExtServicePut(*Result,"transfer_receipt_error",Why);xrtValueRelease(Receipt);
    }
    xrtFree(Uses);xrtValueRelease(Downloads);xrtValueRelease(Tasks);xrtValueRelease(Plan);if(!Ok&&Error&&!Error[0])MdoExtServiceError(Error,"Confirm code approval for the complete inspected plan; the group must fit the 1 GiB transfer budget");return Ok;
}
