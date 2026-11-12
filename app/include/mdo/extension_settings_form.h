#ifndef MDO_EXTENSION_SETTINGS_FORM_H
#define MDO_EXTENSION_SETTINGS_FORM_H
#include <xsbase.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define MDO_ES_FORM_BYTES (64u*1024u)
#define MDO_ES_VALUE_BYTES (64u*1024u)
#define MDO_ES_FIELDS 128u
static inline const xvalue* MdoEsGet(const xvalue* V,cstr K){return xrtValueObjectGet(V,xrtStrView(K));}
static inline cstr MdoEsText(const xvalue* V,cstr K){xstrview S={0};return xrtValueGetString(MdoEsGet(V,K),&S)&&!memchr(S.Data,0,S.Size)?S.Data:NULL;}
static inline bool MdoEsBool(const xvalue* V,cstr K){bool B=false;xrtValueGetBool(MdoEsGet(V,K),&B);return B;}
static inline bool MdoEsUInt(const xvalue* V,uint64* N){int64 I;if(xrtValueType(V)==XVALUE_UINT)return xrtValueGetUInt(V,N);if(xrtValueType(V)!=XVALUE_INT||!xrtValueGetInt(V,&I)||I<0)return false;*N=(uint64)I;return true;}
static inline bool MdoEsNumber(const xvalue* V,double* N){int64 I;uint64 U;if(xrtValueType(V)==XVALUE_INT&&xrtValueGetInt(V,&I))*N=(double)I;else if(xrtValueType(V)==XVALUE_UINT&&xrtValueGetUInt(V,&U))*N=(double)U;else if(!xrtValueGetFloat(V,N))return false;return isfinite(*N);}
static inline bool MdoEsName(cstr S){size_t N=S?strlen(S):0;if(!N||N>64||!strcmp(S,"__proto__")||!strcmp(S,"constructor")||!strcmp(S,"prototype"))return false;for(size_t I=0;I<N;I++){unsigned char C=S[I];if(!((C>='a'&&C<='z')||(C>='A'&&C<='Z')||C=='_'||(I&&C>='0'&&C<='9')))return false;}return true;}
static inline bool MdoEsKeys(const xvalue* V,cstr Allowed){if(xrtValueType(V)!=XVALUE_OBJECT)return false;for(size_t I=0;I<xrtValueCount(V);I++){xstrview K={0};xrtValueObjectAt(V,I,&K);char Name[100];if(K.Size>90||memchr(K.Data,0,K.Size))return false;snprintf(Name,sizeof(Name),"|%.*s|",(int)K.Size,K.Data);if(!strstr(Allowed,Name))return false;}return true;}
static inline bool MdoEsLocalized(const xvalue* V,bool Required){xstrview S={0};if(!V)return !Required;if(xrtValueGetString(V,&S))return (!Required||S.Size)&&S.Size<=4096&&!memchr(S.Data,0,S.Size)&&xrtUtf8Valid(S,NULL);if(xrtValueType(V)!=XVALUE_OBJECT||!xrtValueCount(V)||xrtValueCount(V)>32)return false;for(size_t I=0;I<xrtValueCount(V);I++){xstrview K={0};const xvalue* X=xrtValueObjectAt(V,I,&K);if(!K.Size||K.Size>32||!MdoEsLocalized(X,Required)||xrtValueType(X)!=XVALUE_STRING)return false;}return true;}
static inline const xvalue* MdoEsFind(const xvalue* Form,cstr Name){const xvalue* Groups=MdoEsGet(Form,"groups");for(size_t I=0;I<xrtValueCount(Groups);I++){const xvalue* Fields=MdoEsGet(xrtValueArrayGet(Groups,I),"fields");for(size_t J=0;J<xrtValueCount(Fields);J++){const xvalue* F=xrtValueArrayGet(Fields,J);cstr K=MdoEsText(F,"name");if(K&&Name&&!strcmp(K,Name))return F;}}return NULL;}
static inline bool MdoEsSecret(const xvalue* F){cstr T=MdoEsText(F,"type");return T&&!strcmp(T,"secret");}
static inline bool MdoEsChoice(const xvalue* F,const xvalue* V){const xvalue* Options=MdoEsGet(F,"options");for(size_t I=0;I<xrtValueCount(Options);I++){const xvalue* O=xrtValueArrayGet(Options,I),*X=xrtValueType(O)==XVALUE_OBJECT?MdoEsGet(O,"value"):O;if(xrtValueEqual(V,X))return true;}return false;}
/* Return a stable code; never include a value (especially a secret) in errors. */
static inline cstr MdoEsValueError(const xvalue* F,const xvalue* V){
 cstr T=MdoEsText(F,"type");xstrview S={0};double N,Bound;uint64 U;bool B;
 if(!V||xrtValueType(V)==XVALUE_NULL)return MdoEsBool(F,"required")?"required":NULL;
 if(!strcmp(T,"text")||!strcmp(T,"textarea")||!strcmp(T,"url")||!strcmp(T,"secret")){
  if(!xrtValueGetString(V,&S)||memchr(S.Data,0,S.Size)||!xrtUtf8Valid(S,NULL))return "type";
  if(MdoEsBool(F,"required")&&!xrtStrTrim(S).Size)return "required";
  if(S.Size>(!strcmp(T,"secret")?4096u:16384u))return "length";
  if(MdoEsUInt(MdoEsGet(F,"max_length"),&U)&&S.Size>U)return "length";
  if(MdoEsUInt(MdoEsGet(F,"min_length"),&U)&&S.Size<U)return "length";
  if(!strcmp(T,"url")&&S.Size){if((S.Size<7||memcmp(S.Data,"http://",7))&&(S.Size<8||memcmp(S.Data,"https://",8)))return "url";for(size_t I=0;I<S.Size;I++)if((unsigned char)S.Data[I]<=32)return "url";}
 }else if(!strcmp(T,"integer")||!strcmp(T,"number")){
  if(!MdoEsNumber(V,&N)||(!strcmp(T,"integer")&&xrtValueType(V)!=XVALUE_INT&&xrtValueType(V)!=XVALUE_UINT))return "type";
  if(MdoEsNumber(MdoEsGet(F,"min"),&Bound)&&N<Bound)return "range";
  if(MdoEsNumber(MdoEsGet(F,"max"),&Bound)&&N>Bound)return "range";
 }else if(!strcmp(T,"switch")){if(!xrtValueGetBool(V,&B))return "type";
 }else if(!strcmp(T,"select")){if(!MdoEsChoice(F,V))return "choice";
 }else if(!strcmp(T,"multiselect")){
  if(xrtValueType(V)!=XVALUE_ARRAY||xrtValueCount(V)>64)return "type";
  if(MdoEsBool(F,"required")&&!xrtValueCount(V))return "required";
  for(size_t I=0;I<xrtValueCount(V);I++){const xvalue* X=xrtValueArrayGet(V,I);if(!MdoEsChoice(F,X))return "choice";for(size_t J=0;J<I;J++)if(xrtValueEqual(X,xrtValueArrayGet(V,J)))return "choice";}
 }else return "type";
 return NULL;
}
static inline bool MdoEsVisible(const xvalue* F,const xvalue* Values){const xvalue* W=MdoEsGet(F,"visible_if");if(!W)return true;const xvalue* V=MdoEsGet(Values,MdoEsText(W,"field"));return V&&xrtValueEqual(V,MdoEsGet(W,"equals"));}
static inline bool MdoExtensionSettingsFormValid(const xvalue* Form,char* Error,size_t Capacity){
 cstr Why="Invalid extension settings form";uint64 Version;size_t Count=0;
 if(!MdoEsKeys(Form,"|schema||config_version||default_locale||title||description||groups|")||!MdoEsText(Form,"schema")||strcmp(MdoEsText(Form,"schema"),"mdo.extension-settings.v1")||!MdoEsUInt(MdoEsGet(Form,"config_version"),&Version)||!Version||Version>UINT32_MAX)goto bad;
 if(!MdoEsLocalized(MdoEsGet(Form,"title"),false)||!MdoEsLocalized(MdoEsGet(Form,"description"),false))goto bad;
 cstr Locale=MdoEsText(Form,"default_locale");if(Locale&&(!*Locale||strlen(Locale)>32))goto bad;
 const xvalue* Groups=MdoEsGet(Form,"groups");if(xrtValueType(Groups)!=XVALUE_ARRAY||!xrtValueCount(Groups)||xrtValueCount(Groups)>16)goto bad;
 for(size_t I=0;I<xrtValueCount(Groups);I++){
  const xvalue* G=xrtValueArrayGet(Groups,I);if(!MdoEsKeys(G,"|id||title||description||fields|")||!MdoEsName(MdoEsText(G,"id"))||!MdoEsLocalized(MdoEsGet(G,"title"),true)||!MdoEsLocalized(MdoEsGet(G,"description"),false))goto bad;
  for(size_t J=0;J<I;J++)if(!strcmp(MdoEsText(G,"id"),MdoEsText(xrtValueArrayGet(Groups,J),"id")))goto bad;
  const xvalue* Fields=MdoEsGet(G,"fields");if(xrtValueType(Fields)!=XVALUE_ARRAY||!xrtValueCount(Fields))goto bad;
  for(size_t J=0;J<xrtValueCount(Fields);J++){
   const xvalue* F=xrtValueArrayGet(Fields,J);cstr Name=MdoEsText(F,"name"),Type=MdoEsText(F,"type");
   if(++Count>MDO_ES_FIELDS||!MdoEsKeys(F,"|name||type||label||description||placeholder||default||required||min||max||min_length||max_length||options||visible_if||expose_to_prompt|")||!MdoEsName(Name)||!Type||MdoEsFind(Form,Name)!=F||!MdoEsLocalized(MdoEsGet(F,"label"),true)||!MdoEsLocalized(MdoEsGet(F,"description"),false)||!MdoEsLocalized(MdoEsGet(F,"placeholder"),false))goto bad;
   const cstr Types[]={"text","textarea","url","secret","integer","number","switch","select","multiselect"};bool Known=false;for(size_t K=0;K<9;K++)if(!strcmp(Type,Types[K]))Known=true;if(!Known)goto bad;
   const cstr Flags[]={"required","expose_to_prompt"};for(size_t K=0;K<2;K++){bool B;const xvalue* V=MdoEsGet(F,Flags[K]);if(V&&!xrtValueGetBool(V,&B))goto bad;}
   if(MdoEsSecret(F)&&(MdoEsGet(F,"default")||MdoEsBool(F,"expose_to_prompt"))){Why="Secret fields cannot declare defaults or prompt exposure";goto bad;}
   const cstr Lengths[]={"min_length","max_length"};for(size_t K=0;K<2;K++){uint64 U;const xvalue* V=MdoEsGet(F,Lengths[K]);if(V&&(!MdoEsUInt(V,&U)||U>16384))goto bad;}
   double Low,High;const xvalue* L=MdoEsGet(F,"min"),*H=MdoEsGet(F,"max");if((L&&!MdoEsNumber(L,&Low))||(H&&!MdoEsNumber(H,&High))||(L&&H&&Low>High))goto bad;
   const xvalue* Options=MdoEsGet(F,"options");if(!strcmp(Type,"select")||!strcmp(Type,"multiselect")){
    if(xrtValueType(Options)!=XVALUE_ARRAY||!xrtValueCount(Options)||xrtValueCount(Options)>64)goto bad;
    for(size_t K=0;K<xrtValueCount(Options);K++){const xvalue* O=xrtValueArrayGet(Options,K),*V=xrtValueType(O)==XVALUE_OBJECT?MdoEsGet(O,"value"):O;xstrview S={0};if(!xrtValueGetString(V,&S)||S.Size>512||memchr(S.Data,0,S.Size))goto bad;if(xrtValueType(O)==XVALUE_OBJECT&&(!MdoEsKeys(O,"|value||label|")||!MdoEsLocalized(MdoEsGet(O,"label"),true)))goto bad;for(size_t Q=0;Q<K;Q++){const xvalue* P=xrtValueArrayGet(Options,Q);if(xrtValueEqual(V,xrtValueType(P)==XVALUE_OBJECT?MdoEsGet(P,"value"):P))goto bad;}}
   }else if(Options)goto bad;
   const xvalue* W=MdoEsGet(F,"visible_if");if(W){const xvalue* Other=MdoEsFind(Form,MdoEsText(W,"field"));if(!MdoEsKeys(W,"|field||equals|")||!Other||Other==F||MdoEsSecret(Other)||MdoEsGet(Other,"visible_if")||!MdoEsGet(W,"equals")||MdoEsValueError(Other,MdoEsGet(W,"equals")))goto bad;}
   if(MdoEsGet(F,"default")&&MdoEsValueError(F,MdoEsGet(F,"default"))){Why="A form default does not satisfy its field constraints";goto bad;}
  }
 }
 return true;
bad:if(Error&&Capacity)snprintf(Error,Capacity,"%s",Why);return false;
}
#endif
