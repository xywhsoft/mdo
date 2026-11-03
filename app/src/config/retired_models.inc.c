/* Retire only product-supplied entries from existing Home patches. Never
 * rewrite the source file at startup or remove a personal provider/model.
 * Online models arrive exclusively from the account catalog. */
static bool MdoConfigRetiredModel(const xvalue* Item, bool Provider)
{
    xstrview Id; bool Builtin=false;
    return MdoConfigString(xrtValueObjectGet(Item,MdoConfigKey("id")),&Id) &&
        xrtValueGetBool(xrtValueObjectGet(Item,MdoConfigKey("builtin")),&Builtin) && Builtin &&
        (Provider ? (MdoConfigViewEqual(Id,"ornith") || MdoConfigViewEqual(Id,"ling")) :
         (MdoConfigViewEqual(Id,"ornith-1.5-35b") || MdoConfigViewEqual(Id,"ling-3.0-tiny") || MdoConfigViewEqual(Id,"ling-gpu")));
}
static bool MdoConfigRetireBuiltinPatch(xvalue* Patch)
{
    const char* Keys[]={"items","providers"};
    for(size_t k=0;k<2;k++){
        const xvalue* Before=xrtValueObjectGet(Patch,MdoConfigKey(Keys[k]));
        if(!Before)continue;
        if(xrtValueType(Before)!=XVALUE_ARRAY)return false;
        xvalue* After=xrtValueArray();bool Ok=After!=NULL;
        for(size_t i=0;Ok&&i<xrtValueCount(Before);i++){
            const xvalue* Item=xrtValueArrayGet(Before,i);bool Retired=MdoConfigRetiredModel(Item,k==1);
            bool Referenced=false;
            if(Retired&&k==1){
                const xvalue* Models=xrtValueObjectGet(Patch,MdoConfigKey("items"));xstrview Id;
                if(!MdoConfigString(xrtValueObjectGet(Item,MdoConfigKey("id")),&Id)){Ok=false;break;}
                for(size_t j=0;j<xrtValueCount(Models);j++){
                    xstrview Provider;if(MdoConfigString(xrtValueObjectGet(xrtValueArrayGet(Models,j),MdoConfigKey("provider")),&Provider)&&xrtStrEqual(Id,Provider))Referenced=true;
                }
            }
            if(Retired&&!Referenced)continue;
            xvalue* Copy=xrtValueDeepClone(Item);
            /* A user model may intentionally reference the old provider. Keep
             * that connection editable; the retired builtin: route never runs. */
            if(Copy&&Retired)Ok=xrtValueObjectSetNew(Copy,MdoConfigKey("builtin"),xrtValueBool(false))&&
                xrtValueObjectSetNew(Copy,MdoConfigKey("editable"),xrtValueBool(true))&&
                xrtValueObjectSetNew(Copy,MdoConfigKey("removable"),xrtValueBool(true));
            Ok=Ok&&Copy&&xrtValueArrayAppendTake(After,&Copy);xrtValueRelease(Copy);
        }
        if(Ok)Ok=xrtValueObjectSetTake(Patch,MdoConfigKey(Keys[k]),&After);
        xrtValueRelease(After);if(!Ok)return false;
    }
    xstrview Default;
    if(MdoConfigString(xrtValueObjectGet(Patch,MdoConfigKey("default_model")),&Default)&&
       (MdoConfigViewEqual(Default,"ornith-1.5-35b")||MdoConfigViewEqual(Default,"ling-3.0-tiny")||MdoConfigViewEqual(Default,"ling-gpu"))&&
       !MdoConfigFindById(xrtValueObjectGet(Patch,MdoConfigKey("items")),Default))
        return xrtValueObjectSetNew(Patch,MdoConfigKey("default_model"),xrtValueString(XRT_STR_LITERAL("")));
    return true;
}
