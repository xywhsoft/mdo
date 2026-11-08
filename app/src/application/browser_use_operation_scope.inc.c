/* Persist only scope metadata, never operation arguments or page results. */
static bool BuOperationScope(const mdo_sdk_context_v1* C,const xvalue* Info){
    uint64 Version=0;if(!SdkUInt(SdkGet(Info,"scope_version"),&Version)||Version!=1)return false;
    const char* Project=SdkField(Info,"project");
    const char* Expected[]={C->ExtensionId,C->DeviceId,C->SessionId,C->Namespace,C->MemberId};
    const char* Keys[]={"extension","device","session","namespace","member"};
    for(size_t I=0;I<5;I++){
        const char* Actual=SdkField(Info,Keys[I]);if(strcmp(Expected[I]?Expected[I]:"",Actual?Actual:""))return false;
    }
    return !strcmp(C->ProjectId&&*C->ProjectId?C->ProjectId:"default",Project&&*Project?Project:"default");
}
static void BuOperationStamp(xvalue* Info,const mdo_sdk_context_v1* C){
    SdkPutUInt(Info,"scope_version",1);SdkPut(Info,"device",C->DeviceId);
    SdkPut(Info,"project",C->ProjectId&&*C->ProjectId?C->ProjectId:"default");
    SdkPut(Info,"session",C->SessionId);SdkPut(Info,"namespace",C->Namespace);SdkPut(Info,"member",C->MemberId);
}
