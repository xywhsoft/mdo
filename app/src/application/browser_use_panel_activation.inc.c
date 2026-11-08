/* Activation is a presentation request, not a panel-content revision. Reusing
 * a keyed panel must not remount its native document or replay navigation. */
static bool WbRequestPanelActivation(xvalue* Panel,mdo_error_v1* E) {
    const xvalue* Previous=SdkGet(Panel,"reveal_revision");uint64 Revision=0;
    if(Previous&&(!SdkUInt(Previous,&Revision)||Revision>=9007199254740991ULL))
        return WbFail(E,"panel_activation_invalid","The retained activation revision is invalid or exhausted",409);
    return (SdkPutUInt(Panel,"reveal_revision",Revision+1)&&SdkPut(Panel,"reveal","activate"))||
        WbFail(E,"allocation_failed","Cannot retain the panel activation request",500);
}
