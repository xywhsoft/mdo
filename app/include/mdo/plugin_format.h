#ifndef MDO_PLUGIN_FORMAT_H
#define MDO_PLUGIN_FORMAT_H
#include "extension_manifest.h"
/* A plugin recipe refers only to files in its extension directory. Identity,
 * display metadata and installed source are supplied by extension.json/host. */
#define MDO_PLUGIN_COUNT 32u
#define MDO_PLUGIN_FILES MDO_EXTENSION_DIRECTORY_ENTRIES
#define MDO_PLUGIN_BYTES MDO_EXTENSION_DIRECTORY_BYTES
/* Removed outer-package code still compiles during the staged replacement. */
#define MDO_PLUGIN_MIN_BUILD 0u
static bool MdoPluginPath(cstr P){return MdoExtensionRelativePath(P);}
static const xvalue* MdoPluginGet(const xvalue* V,cstr K){return xrtValueObjectGet(V,xrtStrView(K));}
static bool MdoPluginFail(char* E,size_t N,cstr Why){if(E&&N)snprintf(E,N,"%s",Why);return false;}
static bool MdoPluginKeys(const xvalue* V,const cstr* Keys,size_t Count){
 if(xrtValueType(V)!=XVALUE_OBJECT)return false;
 for(size_t I=0;I<xrtValueCount(V);I++){xstrview K={0};xrtValueObjectAt(V,I,&K);bool Known=false;
  for(size_t J=0;J<Count;J++)if(xrtStrEqual(K,xrtStrView(Keys[J])))Known=true;if(!Known)return false;}
 return true;
}
static bool MdoPluginCoreTarget(cstr T){
 if(!T||!strncmp(T,"default-home/",13))return false;size_t N=strlen(T);
 return !strncmp(T,"src/",4)||!strncmp(T,"include/",8)||!strncmp(T,"generated/",10)||!strcmp(T,"main.c")||!strcmp(T,"sources.json")||(N>2&&(!strcmp(T+N-2,".c")||!strcmp(T+N-2,".h")));
}
static bool MdoPluginPathOverlap(cstr A,cstr B){
 if(!A||!B)return false;char *KA=MdoExtensionPathKey(A),*KB=MdoExtensionPathKey(B);bool Overlap=false;
 if(KA&&KB){size_t N=strlen(KA),M=strlen(KB);Overlap=(N==M&&!strcmp(KA,KB))||(N<M&&!memcmp(KA,KB,N)&&KB[N]=='/')||(M<N&&!memcmp(KA,KB,M)&&KA[M]=='/');}else Overlap=true;
 xrtFree(KA);xrtFree(KB);return Overlap;
}
static const xvalue* MdoPluginFile(const xvalue* Files,cstr Path){
 for(size_t I=0;Path&&I<xrtValueCount(Files);I++){const xvalue* F=xrtValueArrayGet(Files,I);cstr P=MdoExtensionText(F,"path",MDO_EXTENSION_PATH_BYTES);bool D=false;xrtValueGetBool(MdoPluginGet(F,"directory"),&D);if(!D&&P&&!strcmp(P,Path))return F;}return NULL;
}
static bool MdoPluginModern(const xvalue* Doc){return xrtValueType(Doc)==XVALUE_OBJECT;}
static bool MdoPluginRelations(const xvalue* D,cstr Kind,cstr Self,char* E,size_t N){
 const xvalue* Rows=MdoPluginGet(D,Kind);if(!Rows)return true;
 if(xrtValueType(Rows)!=XVALUE_ARRAY||xrtValueCount(Rows)>32)return MdoPluginFail(E,N,"Plugin relations allow at most 32 entries");
 const cstr Keys[]={"id","reason"};
 for(size_t I=0;I<xrtValueCount(Rows);I++){const xvalue* R=xrtValueArrayGet(Rows,I);cstr Id=MdoExtensionText(R,"id",96);
  if(!MdoPluginKeys(R,Keys,2)||!MdoExtensionDirectoryId(Id)||(Self&&!strcmp(Self,Id))||(MdoPluginGet(R,"reason")&&!MdoExtensionText(R,"reason",600)))return MdoPluginFail(E,N,"A relation needs another plugin identity and optional reason; version bounds are not supported");
  for(size_t J=0;J<I;J++)if(!strcmp(Id,MdoExtensionText(xrtValueArrayGet(Rows,J),"id",96)))return MdoPluginFail(E,N,"Duplicate plugin relation");}
 return true;
}
static bool MdoPluginValidate(const xvalue* D,const xvalue* Files,cstr ExpectedAlias,char* E,size_t N){
 const cstr Keys[]={"entry","overrides","exclusive","requires","conflicts"};
 if(!MdoPluginKeys(D,Keys,5)||xrtValueType(Files)!=XVALUE_ARRAY||!xrtValueCount(Files)||xrtValueCount(Files)>MDO_PLUGIN_FILES)return MdoPluginFail(E,N,"A plugin recipe contains entries, overrides and optional relationships; its files belong to the directory ZIP");
 const xvalue* Overrides=MdoPluginGet(D,"overrides");const cstr MappingKeys[]={"source","target"};
 if(Overrides&&(xrtValueType(Overrides)!=XVALUE_ARRAY||xrtValueCount(Overrides)>4096))return MdoPluginFail(E,N,"Invalid plugin overrides");
 for(size_t I=0;I<xrtValueCount(Overrides);I++){const xvalue* O=xrtValueArrayGet(Overrides,I);cstr S=MdoExtensionText(O,"source",MDO_EXTENSION_PATH_BYTES),T=MdoExtensionText(O,"target",MDO_EXTENSION_PATH_BYTES);
  if(!MdoPluginKeys(O,MappingKeys,2)||!MdoPluginPath(T)||!MdoPluginFile(Files,S)||MdoPluginPathOverlap(T,"web/__mdo_plugins")||MdoPluginPathOverlap(T,"__mdo_plugins"))return MdoPluginFail(E,N,"An override needs an own file and a portable application target outside loader addresses");
  for(size_t J=0;J<I;J++)if(MdoPluginPathOverlap(T,MdoExtensionText(xrtValueArrayGet(Overrides,J),"target",MDO_EXTENSION_PATH_BYTES)))return MdoPluginFail(E,N,"Overlapping plugin targets");}
 const xvalue* Entry=MdoPluginGet(D,"entry");const cstr EntryKeys[]={"frontend","native"};bool HasEntry=false;
 if(Entry&&!MdoPluginKeys(Entry,EntryKeys,2))return MdoPluginFail(E,N,"Plugin entries support frontend and native only");
 for(size_t I=0;I<2;I++)if(MdoPluginGet(Entry,EntryKeys[I])){cstr P=MdoExtensionText(Entry,EntryKeys[I],MDO_EXTENSION_PATH_BYTES);size_t L=P?strlen(P):0,S=I?2:3;
  if(!MdoPluginFile(Files,P)||L<=S||strcmp(P+L-S,I?".c":".js"))return MdoPluginFail(E,N,"An entry must refer to an own .c or .js file");HasEntry=true;
  for(size_t J=0;J<xrtValueCount(Overrides);J++)if(!strcmp(P,MdoExtensionText(xrtValueArrayGet(Overrides,J),"source",MDO_EXTENSION_PATH_BYTES)))return MdoPluginFail(E,N,"An entry cannot also be an override");}
 if(!HasEntry&&!xrtValueCount(Overrides))return MdoPluginFail(E,N,"A plugin needs an override or an execution entry");
 const xvalue* Exclusive=MdoPluginGet(D,"exclusive");if(Exclusive&&(xrtValueType(Exclusive)!=XVALUE_ARRAY||xrtValueCount(Exclusive)>16))return MdoPluginFail(E,N,"Invalid exclusive markers");
 for(size_t I=0;I<xrtValueCount(Exclusive);I++){xstrview K={0};if(!xrtValueGetString(xrtValueArrayGet(Exclusive,I),&K)||memchr(K.Data,0,K.Size)||!MdoExtensionAlias(K.Data))return MdoPluginFail(E,N,"Invalid exclusive marker");
  for(size_t J=0;J<I;J++)if(xrtValueEqual(xrtValueArrayGet(Exclusive,I),xrtValueArrayGet(Exclusive,J)))return MdoPluginFail(E,N,"Duplicate exclusive marker");}
 return MdoPluginRelations(D,"requires",ExpectedAlias,E,N)&&MdoPluginRelations(D,"conflicts",ExpectedAlias,E,N);
}
#endif
