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
static inline bool MdoCliManifestValid(const xvalue* M,char* Error,size_t Capacity)
{
    cstr Id=MdoPackageText(M,"id",64),Name=MdoPackageText(M,"name",120),Version=MdoPackageText(M,"version",64);
    cstr Prompt=MdoPackageText(M,"prompt",16384),Entry=MdoPackageText(M,"entry",220),Platform=MdoPackageText(M,"platform",32);
    cstr License=MdoPackageText(M,"license",512);uint32_t V[3];const xvalue* Files=xrtValueObjectGet(M,XRT_STR_LITERAL("files"));
    const xvalue* Links=xrtValueObjectGet(M,XRT_STR_LITERAL("links"));uint64 Total=0;bool Found=false;
    if(!MdoPackageId(Id)||!Name||!Name[0]||!Version||!MdoPackageVersion(Version,V)||!Prompt||!Prompt[0]||!Entry||!MdoPackagePath(Entry)||!MdoCliPlatformValid(Platform)||!License||!License[0])goto invalid;
    if(xrtValueType(Files)!=XVALUE_ARRAY||!xrtValueCount(Files)||xrtValueCount(Files)>MDO_CLI_FILES)goto invalid;
    for(size_t I=0;I<xrtValueCount(Files);I++){
        const xvalue* F=xrtValueArrayGet(Files,I);cstr P=MdoPackageText(F,"path",220),Hash=MdoPackageText(F,"sha256",64);int64 Size=0,Mode=0;
        if(!P||!MdoPackagePath(P)||!MdoCliHashValid(Hash)||!MdoPackageNumber(F,"size",&Size)||Size<0||(uint64)Size>MDO_CLI_BYTES)goto invalid;
        if(!MdoPackageNumber(F,"mode",&Mode)||(Mode!=0644&&Mode!=0755))goto invalid;
        for(size_t J=0;J<I;J++)if(MdoCliAliasEqual(P,MdoPackageText(xrtValueArrayGet(Files,J),"path",220)))goto invalid;
        Total+=(uint64)Size;if(Total>MDO_CLI_BYTES)goto invalid;
        if(!strcmp(P,Entry)){if(!Size)goto invalid;Found=true;}
    }
    if(!Found)goto invalid;
    if(Links){if(xrtValueType(Links)!=XVALUE_ARRAY||xrtValueCount(Links)>8)goto invalid;
        for(size_t I=0;I<xrtValueCount(Links);I++){xstrview Link;if(!xrtValueGetString(xrtValueArrayGet(Links,I),&Link)||Link.Size>2048||memchr(Link.Data,0,Link.Size)||(!xrtStrStarts(Link,XRT_STR_LITERAL("https://"))&&!xrtStrStarts(Link,XRT_STR_LITERAL("http://"))))goto invalid;}}
    return true;
invalid:if(Error&&Capacity)snprintf(Error,Capacity,"Invalid external tool identity, entry, prompt, platform, license, links or file manifest");return false;
}
#endif
