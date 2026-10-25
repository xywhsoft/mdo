#ifndef MDO_DIAGNOSTICS_H
#define MDO_DIAGNOSTICS_H
#include <xsbase.h>
#include <string.h>

/* Current, nonfatal checks join the same diagnostics snapshot and warning
 * indicator. Never include document contents, credentials or raw responses. */
static inline bool MdoDiagnosticWarning(xvalue* Items,cstr Domain,cstr Code,
    cstr Subject,cstr Path,cstr Message,cstr Remedy,cstr Section)
{
    if(!Items)return true;
    for(size_t i=0;i<xrtValueCount(Items);i++){
        const xvalue* Row=xrtValueArrayGet(Items,i);xstrview D,C,S,P;
        if(xrtValueGetString(xrtValueObjectGet(Row,XRT_STR_LITERAL("domain")),&D)&&
           xrtValueGetString(xrtValueObjectGet(Row,XRT_STR_LITERAL("code")),&C)&&
           xrtValueGetString(xrtValueObjectGet(Row,XRT_STR_LITERAL("subject_id")),&S)&&
           xrtValueGetString(xrtValueObjectGet(Row,XRT_STR_LITERAL("path")),&P)&&
           xrtStrEqual(D,xrtStrView(Domain))&&xrtStrEqual(C,xrtStrView(Code))&&
           xrtStrEqual(S,xrtStrView(Subject))&&xrtStrEqual(P,xrtStrView(Path)))return true;
    }
    xvalue* Row=xrtValueObject();
    const char* Keys[]={"domain","stage","code","subject_id","path","message","remedy","settings_section"};
    const char* Values[]={Domain,"check",Code,Subject,Path,Message,Remedy,Section};bool Ok=Row!=NULL;
    for(size_t i=0;Ok&&i<8;i++)Ok=xrtValueObjectSetNew(Row,xrtStrView(Keys[i]),xrtValueString(xrtStrView(Values[i]?Values[i]:"")));
    if(Ok)Ok=xrtValueArrayAppend(Items,Row);xrtValueRelease(Row);return Ok;
}
#endif
