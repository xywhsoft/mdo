/* Called only after an owner-bound service mutation has returned success.
 * Publish an independent cache value; OOM invalidates the cache, never turns a
 * confirmed remote mutation into a retryable failure. */
static xvalue* MdoRemoteListingAfterMutation(const xvalue* Listing,cstr Id,bool Remove)
{
    const xvalue* Before=xrtValueObjectGet(Listing,XRT_STR_LITERAL("devices"));
    if(!Id||xrtValueType(Before)!=XVALUE_ARRAY)return NULL;
    xvalue* Next=xrtValueDeepClone(Listing);
    xvalue* Rows=(xvalue*)xrtValueObjectGet(Next,XRT_STR_LITERAL("devices"));
    bool Ok=Next&&Rows;
    for(size_t I=xrtValueCount(Rows);Ok&&I>0;I--){
        xvalue* Row=(xvalue*)xrtValueArrayGet(Rows,I-1);xstrview Key={0};
        if(!xrtValueGetString(xrtValueObjectGet(Row,XRT_STR_LITERAL("id")),&Key)||
            Key.Size!=strlen(Id)||memcmp(Key.Data,Id,Key.Size))continue;
        if(Remove)Ok=xrtValueArrayRemove(Rows,I-1,1);
        else Ok=xrtValueObjectSetNew(Row,XRT_STR_LITERAL("revoked"),xrtValueBool(true))&&
            xrtValueObjectSetNew(Row,XRT_STR_LITERAL("allow_remote"),xrtValueBool(false));
    }
    if(!Ok){xrtValueRelease(Next);return NULL;}
    return Next;
}
