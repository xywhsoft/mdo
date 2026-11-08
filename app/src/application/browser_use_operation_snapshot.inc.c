/* Called with SdkJobs.Lock held. Final metadata can be persisted before event
 * callbacks/lease teardown finish. Only J->Terminal permits result/release. */
static xvalue* BuOperationSnapshot(const SdkJob* J){
    xvalue* V=xrtValueClone(J->Info);if(!V)return NULL;
    if(!J->Terminal){
        bool Ok=xrtValueObjectSetNew(V,XRT_STR_LITERAL("terminal"),xrtValueBool(false));
        Ok=xrtValueObjectSetNew(V,XRT_STR_LITERAL("result_available"),xrtValueBool(false))&&Ok;
        Ok=xrtValueObjectSetNew(V,XRT_STR_LITERAL("state"),xrtValueString(XRT_STR_LITERAL("running")))&&Ok;
        if(!Ok){xrtValueRelease(V);return NULL;}
        const cstr Hidden[]={"outcome","finished_us","result_bytes","error","receipt_uncertain"};
        for(size_t I=0;I<sizeof(Hidden)/sizeof(*Hidden);I++)xrtValueObjectRemove(V,xrtStrView(Hidden[I]));
    }
    return V;
}
