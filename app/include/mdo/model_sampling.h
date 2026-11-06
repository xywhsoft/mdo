#ifndef MDO_MODEL_SAMPLING_H
#define MDO_MODEL_SAMPLING_H
/* JSON integers and fractional numbers share one bounded sampling contract. */
static inline bool MdoModelTemperatureRead(const xvalue* Value,double* Number)
{
    int64 Integer;uint64 Unsigned;
    if(!xrtValueGetFloat(Value,Number)){
        if(xrtValueGetInt(Value,&Integer))*Number=(double)Integer;
        else if(xrtValueGetUInt(Value,&Unsigned))*Number=(double)Unsigned;
        else return false;
    }
    return *Number>=0.0 && *Number<=2.0;
}
#endif
