/* Stream identities are per-generation capabilities, additionally bound to the
 * originating task. A UI continuation must keep its caller's frozen context. */
static xvalue* BuStreamScopeCapture(const mdo_sdk_context_v1* C){
    const cstr Keys[]={"extension","device","project","session","namespace","member"};
    const cstr Values[]={C->ExtensionId,C->DeviceId,C->ProjectId&&C->ProjectId[0]?C->ProjectId:"default",C->SessionId,C->Namespace,C->MemberId};
    xvalue* Scope=xrtValueObject();if(!Scope)return NULL;
    for(size_t I=0;I<6;I++)if(!SdkPut(Scope,Keys[I],Values[I]?Values[I]:"")){xrtValueRelease(Scope);return NULL;}
    return Scope;
}
static bool BuStreamScopeMatches(const mdo_sdk_context_v1* C,const xvalue* Scope){
    const cstr Keys[]={"extension","device","project","session","namespace","member"};
    const cstr Values[]={C->ExtensionId,C->DeviceId,C->ProjectId&&C->ProjectId[0]?C->ProjectId:"default",C->SessionId,C->Namespace,C->MemberId};
    for(size_t I=0;I<6;I++){
        cstr Actual=SdkField(Scope,Keys[I]);if(!Actual||strcmp(Actual,Values[I]?Values[I]:""))return false;
    }
    return true;
}
