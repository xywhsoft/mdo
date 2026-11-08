/* Prepare every allocating result before consuming bytes, appending input,
 * changing EOF or releasing storage. An allocation failure is effect-free. */
static xvalue* SdkStreamInfo(const SdkStream* S){
    xvalue* R=xrtValueObject();
    bool Ok=R&&SdkPut(R,"handle",S->Id)&&SdkPutUInt(R,"buffered",S->Size)&&SdkPutUInt(R,"capacity",S->Capacity)&&
        xrtValueObjectSetNew(R,XRT_STR_LITERAL("closed"),xrtValueBool(S->Closed))&&
        xrtValueObjectSetNew(R,XRT_STR_LITERAL("eof"),xrtValueBool(S->Closed&&!S->Size));
    if(!Ok){xrtValueRelease(R);return NULL;}return R;
}
static bool BuStreamExchange(SdkStream* S,cstr Method,bool Read,bool Write,
    const unsigned char* Input,size_t Bytes,size_t Limit,xvalue** Result,mdo_error_v1* E){
    SdkStream Next=*S;xvalue* R=NULL;unsigned char Chunk[16384];size_t Count=0;
    bool Release=!strcmp(Method,"stream.release"),Ok=false;
    if(Read){
        Count=S->Size<Limit?S->Size:Limit;
        for(size_t I=0;I<Count;I++)Chunk[I]=S->Bytes[(S->Start+I)%S->Capacity];
        Next.Start=(S->Start+Count)%S->Capacity;Next.Size-=Count;
    }else if(Write)Next.Size+=Bytes;
    else if(!strcmp(Method,"stream.close"))Next.Closed=true;
    R=Release?xrtValueObject():SdkStreamInfo(&Next);
    if(!R||(Read&&!SdkByteValue(R,Chunk,Count))||(Write&&!SdkPutUInt(R,"written",Bytes)))goto done;
    /* There are no allocations or fallible operations after this point. */
    if(Read){
        for(size_t I=0;I<Count;I++)S->Bytes[(S->Start+I)%S->Capacity]=0;
        S->Start=Next.Start;S->Size=Next.Size;
    }else if(Write){
        for(size_t I=0;I<Bytes;I++)S->Bytes[(S->Start+S->Size+I)%S->Capacity]=Input[I];
        S->Size=Next.Size;
    }else if(Release)SdkStreamFree(S);
    else S->Closed=Next.Closed;
    *Result=R;R=NULL;Ok=true;
done:
    xrtSecureZero(Chunk,sizeof(Chunk));xrtValueRelease(R);
    return Ok||MdoApplicationError(E,"sdk_stream_result_failed","Cannot prepare stream acknowledgement; no bytes or EOF were changed","stream",503,false);
}
