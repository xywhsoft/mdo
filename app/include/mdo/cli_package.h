#ifndef MDO_CLI_PACKAGE_H
#define MDO_CLI_PACKAGE_H
#include "ecosystem_package.h"

/* Data-only archive: 8-byte magic, little-endian JSON length, bounded manifest,
 * then each file in manifest order. No executable installer or probe is run. */
#define MDO_CLI_MAGIC "MDOCLI1\n"
#define MDO_CLI_BYTES (512u * 1024u * 1024u)
#define MDO_CLI_JSON (1024u * 1024u)
#define MDO_CLI_FILES 4096u
static inline bool MdoCliHashValid(cstr S)
{if(!S||strlen(S)!=64)return false;for(size_t I=0;I<64;I++)if(!((S[I]>='0'&&S[I]<='9')||(S[I]>='a'&&S[I]<='f')))return false;return true;}
static inline bool MdoCliPlatformValid(cstr P)
{return P&&(!strcmp(P,"windows-x86_64")||!strcmp(P,"linux-x86_64")||!strcmp(P,"linux-arm64")||!strcmp(P,"android-arm64-v8a"));}
static inline bool MdoCliAliasEqual(cstr A,cstr B)
{while(*A&&*B){unsigned char X=(unsigned char)*A++,Y=(unsigned char)*B++;if(X>='A'&&X<='Z')X+=32;if(Y>='A'&&Y<='Z')Y+=32;if(X!=Y)return false;}return *A==*B;}
static inline bool MdoCliCommandName(cstr S)
{
    size_t N=S?strlen(S):0;if(!N||N>64||S[0]=='.'||S[N-1]=='.')return false;
    for(size_t I=0;I<N;I++)if(!((S[I]>='a'&&S[I]<='z')||(S[I]>='A'&&S[I]<='Z')||(S[I]>='0'&&S[I]<='9')||strchr("_.+-",S[I])))return false;
    char Base[65];size_t I=0;while(I<N&&S[I]!='.'){Base[I]=S[I];I++;}Base[I]=0;
    if(MdoCliAliasEqual(Base,"con")||MdoCliAliasEqual(Base,"prn")||MdoCliAliasEqual(Base,"aux")||MdoCliAliasEqual(Base,"nul"))return false;
    if(I==4&&(Base[3]>='1'&&Base[3]<='9')){Base[3]=0;if(MdoCliAliasEqual(Base,"com")||MdoCliAliasEqual(Base,"lpt"))return false;}
    return N<4||!MdoCliAliasEqual(S+N-4,".exe");
}
static inline bool MdoCliCommandsValid(const xvalue* Commands,const xvalue* Files)
{
    if(!Commands)return true;
    if(xrtValueType(Commands)!=XVALUE_ARRAY||xrtValueCount(Commands)>64)return false;
    for(size_t I=0;I<xrtValueCount(Commands);I++){
        const xvalue* C=xrtValueArrayGet(Commands,I);cstr Name=MdoPackageText(C,"name",64),Entry=MdoPackageText(C,"entry",220);
        if(!MdoCliCommandName(Name)||!Entry||!MdoPackagePath(Entry))return false;
        for(size_t J=0;J<I;J++)if(MdoCliAliasEqual(Name,MdoPackageText(xrtValueArrayGet(Commands,J),"name",64)))return false;
        if(Files){bool Found=false;for(size_t J=0;J<xrtValueCount(Files);J++){const xvalue* F=xrtValueArrayGet(Files,J);int64 Size=0;if(!strcmp(Entry,MdoPackageText(F,"path",220))&&MdoPackageNumber(F,"size",&Size)&&Size>0)Found=true;}if(!Found)return false;}
        const xvalue* Args=xrtValueObjectGet(C,XRT_STR_LITERAL("args"));if(Args){if(xrtValueType(Args)!=XVALUE_ARRAY||xrtValueCount(Args)>64)return false;for(size_t J=0;J<xrtValueCount(Args);J++){xstrview V;if(!xrtValueGetString(xrtValueArrayGet(Args,J),&V)||V.Size>32768||memchr(V.Data,0,V.Size))return false;}}
    }return true;
}
/* Optional for legacy records and archives. Never derive user-facing text
 * from the separately supplied model instructions. */
static inline bool MdoCliDescriptionValid(const xvalue* M)
{return !xrtValueObjectGet(M,XRT_STR_LITERAL("description"))||MdoPackageText(M,"description",2048)!=NULL;}
static inline bool MdoCliCopyrightValid(const xvalue* M)
{return !xrtValueObjectGet(M,XRT_STR_LITERAL("copyright"))||MdoPackageText(M,"copyright",4096)!=NULL;}
static inline bool MdoCliManifestValid(const xvalue* M,char* Error,size_t Capacity)
{
    cstr Id=MdoPackageText(M,"id",64),Name=MdoPackageText(M,"name",120),Version=MdoPackageText(M,"version",64);
    cstr Prompt=MdoPackageText(M,"prompt",16384),Entry=MdoPackageText(M,"entry",220),Platform=MdoPackageText(M,"platform",32);
    cstr License=MdoPackageText(M,"license",512);uint32_t V[3];const xvalue* Files=xrtValueObjectGet(M,XRT_STR_LITERAL("files"));
    const xvalue* Links=xrtValueObjectGet(M,XRT_STR_LITERAL("links"));uint64 Total=0;bool Found=false;
    if(!MdoCliDescriptionValid(M)||!MdoCliCopyrightValid(M)||!MdoPackageId(Id)||!Name||!Name[0]||!Version||!MdoPackageVersion(Version,V)||!Prompt||!Prompt[0]||!Entry||!MdoPackagePath(Entry)||!MdoCliPlatformValid(Platform)||!License||!License[0])goto invalid;
    if(xrtValueType(Files)!=XVALUE_ARRAY||!xrtValueCount(Files)||xrtValueCount(Files)>MDO_CLI_FILES)goto invalid;
    for(size_t I=0;I<xrtValueCount(Files);I++){
        const xvalue* F=xrtValueArrayGet(Files,I);cstr P=MdoPackageText(F,"path",220),Hash=MdoPackageText(F,"sha256",64);int64 Size=0,Mode=0;
        if(!P||!MdoPackagePath(P)||!MdoCliHashValid(Hash)||!MdoPackageNumber(F,"size",&Size)||Size<0||(uint64)Size>MDO_CLI_BYTES)goto invalid;
        if(!MdoPackageNumber(F,"mode",&Mode)||(Mode!=0644&&Mode!=0755))goto invalid;
        for(size_t J=0;J<I;J++)if(MdoCliAliasEqual(P,MdoPackageText(xrtValueArrayGet(Files,J),"path",220)))goto invalid;
        Total+=(uint64)Size;if(Total>MDO_CLI_BYTES)goto invalid;
        if(!strcmp(P,Entry)){if(!Size)goto invalid;Found=true;}
    }
    if(!Found||!MdoCliCommandsValid(xrtValueObjectGet(M,XRT_STR_LITERAL("commands")),Files))goto invalid;
    if(Links){if(xrtValueType(Links)!=XVALUE_ARRAY||xrtValueCount(Links)>8)goto invalid;
        for(size_t I=0;I<xrtValueCount(Links);I++){xstrview Link;if(!xrtValueGetString(xrtValueArrayGet(Links,I),&Link)||Link.Size>2048||memchr(Link.Data,0,Link.Size)||(!xrtStrStarts(Link,XRT_STR_LITERAL("https://"))&&!xrtStrStarts(Link,XRT_STR_LITERAL("http://"))))goto invalid;}}
    return true;
invalid:if(Error&&Capacity)snprintf(Error,Capacity,"Invalid external tool identity, description, copyright, entry, prompt, platform, license, links or file manifest");return false;
}
#endif
