/* Report implemented contracts, never infer support from a version string. */
static const struct {cstr Id;uint32 Version;} SdkFeatures[]={
    {"execution.lineage",1},{"execution.outcomes",1},{"discovery.generation_bound",1},
    {"state.atomic",1},{"state.receipts",1},{"lease.fencing",1},
    {"mailbox.durable",1},{"mailbox.atomic_ack",1},{"events.wait",1},
    {"lifecycle.global",1},{"lifecycle.actual_completion",1}
};
static xvalue* SdkCapabilities(void){
    xvalue* R=xrtValueObject(),*Features=xrtValueObject(),*Limits=xrtValueObject();
    for(size_t I=0;I<sizeof(SdkFeatures)/sizeof(*SdkFeatures);I++)SdkPutUInt(Features,SdkFeatures[I].Id,SdkFeatures[I].Version);
    SdkPut(R,"schema","mdo.sdk.capabilities.v1");SdkPutUInt(R,"sdk_version",MDO_SDK_VERSION);
    SdkPut(R,"native_transport","in_process_only; isolated commands use Call/RPC");
    SdkPutUInt(Limits,"state_value_bytes",16384);SdkPutUInt(Limits,"state_namespace_bytes",SDK_STATE_BYTES);
    SdkPutUInt(Limits,"state_records",SDK_STATE_RECORDS);SdkPutUInt(Limits,"state_receipts",128);
    SdkPutUInt(Limits,"mailbox_body_bytes",8192);SdkPutUInt(Limits,"mailbox_pending",128);
    SdkPutUInt(Limits,"event_payload_bytes",8192);SdkPutUInt(Limits,"event_page_bytes",32768);
    SdkPutUInt(Limits,"wait_ms",60000);SdkPutUInt(Limits,"lease_ttl_ms",300000);
    xrtValueObjectSetNew(R,XRT_STR_LITERAL("features"),Features);xrtValueObjectSetNew(R,XRT_STR_LITERAL("limits"),Limits);
    return R;
}
static bool SdkRequire(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E){
    const xvalue* Services=SdkGet(P,"services"),*Native=SdkGet(P,"native"),*Features=SdkGet(P,"features");
    if((Services&&xrtValueType(Services)!=XVALUE_ARRAY)||(Native&&xrtValueType(Native)!=XVALUE_ARRAY)||
       (Features&&xrtValueType(Features)!=XVALUE_OBJECT)||xrtValueCount(Services)>128||xrtValueCount(Native)>128||xrtValueCount(Features)>128)
        return MdoApplicationError(E,"sdk_require_arguments","Supply up to 128 services, native names and feature versions","discovery",400,false);
    xvalue* Catalog=MdoApplicationCatalogFor(C),*NativeCatalog=SdkNativeCatalog(),*Capabilities=SdkCapabilities(),*Missing=xrtValueArray();bool Ok=true;
    for(size_t Group=0;Group<2;Group++){
        const xvalue* Names=Group?Native:Services;const xvalue* Directory=Group?NativeCatalog:Catalog;
        for(size_t I=0;Ok&&I<xrtValueCount(Names);I++){
            cstr Name=SdkText(xrtValueArrayGet(Names,I));if(!SdkIdValid(Name)){Ok=false;break;}
            bool Found=false;for(size_t J=0;J<xrtValueCount(Directory);J++){cstr Candidate=SdkField(xrtValueArrayGet(Directory,J),Group?"name":"id");if(Candidate&&!strcmp(Candidate,Name))Found=true;}
            if(!Found){xvalue* Item=xrtValueObject();SdkPut(Item,"kind",Group?"native":"service");SdkPut(Item,"id",Name);xrtValueArrayAppendNew(Missing,Item);}
        }
    }
    for(size_t I=0;Ok&&I<xrtValueCount(Features);I++){
        cstr Name=SdkKey(Features,I);uint64 Required=0,Available=0;
        if(!Name||!SdkUInt(SdkGet(Features,Name),&Required)||!Required){Ok=false;break;}
        SdkUInt(SdkGet(SdkGet(Capabilities,"features"),Name),&Available);
        if(Available<Required){xvalue* Item=xrtValueObject();SdkPut(Item,"kind","feature");SdkPut(Item,"id",Name);SdkPutUInt(Item,"required",Required);SdkPutUInt(Item,"available",Available);xrtValueArrayAppendNew(Missing,Item);}
    }
    xrtValueRelease(Catalog);xrtValueRelease(NativeCatalog);xrtValueRelease(Capabilities);
    if(!Ok){xrtValueRelease(Missing);return MdoApplicationError(E,"sdk_require_arguments","Requirements must contain portable names and positive integer feature versions","discovery",400,false);}
    *R=xrtValueObject();xrtValueObjectSetNew(*R,XRT_STR_LITERAL("satisfied"),xrtValueBool(xrtValueCount(Missing)==0));
    xrtValueObjectSetNew(*R,XRT_STR_LITERAL("missing"),Missing);return *R!=NULL;
}
