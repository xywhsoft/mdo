#ifndef MDO_LOCAL_MODEL_PLAN_H
#define MDO_LOCAL_MODEL_PLAN_H
#include "extension_manifest.h"
/* Revalidate every authoritative relationship. A matching fingerprint alone
 * is not proof of a valid profile, asset set or fixed runtime closure. */
static bool MdoLocalModelPlanHash(cstr Text,char Hash[65]){xsha256 S;uint8 D[32];xrtSha256Init(&S);if(!xrtSha256Update(&S,Text,strlen(Text))||!xrtSha256Final(&S,D))return false;for(size_t I=0;I<32;I++)snprintf(Hash+I*2,3,"%02x",D[I]);return true;}
static bool MdoLocalModelPlanValid(const xvalue* Plan,char Error[512]){
 const xvalue* Recipe=xrtValueObjectGet(Plan,XRT_STR_LITERAL("recipe")),*D=xrtValueObjectGet(Plan,XRT_STR_LITERAL("document")),*P=xrtValueObjectGet(Plan,XRT_STR_LITERAL("profile"));
 cstr Schema=MdoExtensionText(Plan,"schema",64),Platform=MdoExtensionText(Plan,"platform",32),Hash=MdoExtensionText(Plan,"fingerprint",64);
 bool Ok=Schema&&!strcmp(Schema,"mdo.local-model-plan.v1")&&Platform&&MdoExtensionPlatform(Platform)&&Hash&&LmHex(Hash,64)&&MdoExtensionManifest(Recipe,Error,512)&&!strcmp(MdoExtensionText(Recipe,"kind",32),"local-models")&&MdLocalModelValidate(D,Recipe,Error);
 const xvalue* Selected=LmFind(xrtValueObjectGet(D,XRT_STR_LITERAL("profiles")),MdoExtensionText(P,"id",64));
 const xvalue* W=LmFind(xrtValueObjectGet(D,XRT_STR_LITERAL("weights")),MdoExtensionText(P,"weight",64)),*R=LmFind(xrtValueObjectGet(D,XRT_STR_LITERAL("runtimes")),MdoExtensionText(P,"runtime",64));
 cstr Target=MdoExtensionText(R,"platform",32);Ok=Ok&&Selected&&xrtValueEqual(P,Selected)&&W&&R&&Target&&(!strcmp(Target,Platform)||!strcmp(Target,"linux-x86_64")&&!strncmp(Platform,"linux-x86_64-",13))&&xrtValueEqual(xrtValueObjectGet(Plan,XRT_STR_LITERAL("assets")),xrtValueObjectGet(W,XRT_STR_LITERAL("files")));
 const xvalue* Runtime=xrtValueObjectGet(Plan,XRT_STR_LITERAL("runtime_plan")),*Items=xrtValueObjectGet(Runtime,XRT_STR_LITERAL("items"));
 int64 Protocol=0,Bytes=0;uint64 Total=0;const xvalue* Files=xrtValueObjectGet(W,XRT_STR_LITERAL("files"));
 for(size_t I=0;I<xrtValueCount(Files);I++){int64 N=0;MdoExtensionNumber(xrtValueArrayGet(Files,I),"bytes",&N);if(N>0&&Total<=MD_LOCAL_ASSET_BYTES-(uint64)N)Total+=(uint64)N;else Ok=false;}
 cstr Ref=MdoExtensionText(R,"ref",160),Root=MdoExtensionText(Runtime,"root",160);bool Found=false;
 Ok=Ok&&MdoExtensionNumber(Runtime,"dependency_protocol",&Protocol)&&Protocol==1&&Root&&Ref&&!strcmp(Root,Ref)&&MdoExtensionNumber(Plan,"asset_bytes",&Bytes)&&Bytes>0&&(uint64)Bytes==Total&&xrtValueType(Items)==XVALUE_ARRAY&&xrtValueCount(Items)<=128;
 for(size_t I=0;Ok&&I<xrtValueCount(Items);I++){const xvalue* Item=xrtValueArrayGet(Items,I);cstr Id=MdoExtensionText(Item,"id",160);if(Id&&!strcmp(Id,Ref)){cstr V=MdoExtensionText(Item,"version_id",32),H=MdoExtensionText(Item,"sha256",64);Found=V&&H&&!strcmp(V,MdoExtensionText(R,"version_id",32))&&!strcmp(H,MdoExtensionText(R,"sha256",64));}}
 xvalue* Canonical=Ok&&Found?xrtValueClone(Plan):NULL;if(Canonical)xrtValueObjectRemove(Canonical,XRT_STR_LITERAL("fingerprint"));char* Json=Canonical?xrtJsonStringify(Canonical,false,NULL):NULL;char Actual[65];
 Ok=Ok&&Found&&Json&&MdoLocalModelPlanHash(Json,Actual)&&!strcmp(Actual,Hash);xrtFree(Json);xrtValueRelease(Canonical);
 if(!Ok&&Error&&!Error[0])snprintf(Error,512,"The local model plan has inconsistent files, profile, runtime pin or fingerprint");return Ok;
}
#endif
