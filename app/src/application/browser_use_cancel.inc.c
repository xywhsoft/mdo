/* Called with SdkUi.Lock held. Cancellation stops future work; it cannot undo
 * a browser input which has already reached the renderer. */
static bool BuUiCancel(const mdo_sdk_context_v1* C,SdkUiAction* A,xvalue** R,mdo_error_v1* E){
    if(!A)return MdoApplicationError(E,"sdk_ui_receipt_expired","UI receipt expired; inspect effects before retrying","ui",410,true);
    const xvalue* Scope=SdkGet(A->Value,"context");
    const char* Project=SdkField(Scope,"project");
    if(strcmp(C->ExtensionId?C->ExtensionId:"",SdkField(Scope,"extension")?SdkField(Scope,"extension"):"")||
       strcmp(C->DeviceId?C->DeviceId:"",SdkField(Scope,"device")?SdkField(Scope,"device"):"")||
       strcmp(C->ProjectId&&*C->ProjectId?C->ProjectId:"default",Project&&*Project?Project:"default")||
       strcmp(C->SessionId?C->SessionId:"",SdkField(Scope,"session")?SdkField(Scope,"session"):""))
        return MdoApplicationError(E,"context_mismatch","Cannot cancel another task's UI action","ui",409,false);
    if(!A->Terminal){A->Terminal=true;SdkPut(A->Value,"state","cancelled");
        SdkPut(A->Value,"error","Cancelled; an input already dispatched may have completed. Inspect the page before retrying.");}
    *R=xrtValueClone(A->Value);return *R!=NULL;
}

/* Workbench -> UI is the same lock order used for command admission. Never
 * call back into the workbench while holding SdkUi.Lock. Browser ownership is
 * task-wide, so all runs/members in that exact task stop together. */
void MdoApplicationUiCancelBrowser(const mdo_sdk_context_v1* C,cstr Browser){
    if(!SdkUi.Lock)return;
    xrtMutexLock(SdkUi.Lock);
    for(size_t I=0;I<128;I++){
        SdkUiAction* A=&SdkUi.Actions[I];if(!A->Id[0]||A->Terminal)continue;
        const xvalue* Scope=SdkGet(A->Value,"context"),*Params=SdkGet(A->Value,"params");
        cstr Action=SdkField(A->Value,"action"),Id=SdkField(Params,"id");
        if(!Action||strcmp(Action,"workbench.browser")||!Id||strcmp(Id,Browser))continue;
        const cstr Actual[]={C->ExtensionId,C->DeviceId,C->ProjectId&&*C->ProjectId?C->ProjectId:"default",C->SessionId};
        const cstr Keys[]={"extension","device","project","session"};bool Match=true;
        for(size_t J=0;J<4;J++){
            cstr Saved=SdkField(Scope,Keys[J]);if(!Saved)Saved=J==2?"default":"";
            if(strcmp(Actual[J]?Actual[J]:"",Saved))Match=false;
        }
        if(!Match)continue;
        A->Terminal=true;SdkPut(A->Value,"state","cancelled");
        SdkPut(A->Value,"error","Browser was closed; an input already dispatched may have completed. Inspect effects before retrying.");
    }
    xrtMutexUnlock(SdkUi.Lock);
}
