#ifndef MDO_EXTERNAL_TOOL_H
#define MDO_EXTERNAL_TOOL_H
#include "extension_manifest.h"
static bool MdoCliAliasEqual(cstr A,cstr B)
{return A&&B&&xrtStrCaseEqual(xrtStrView(A),xrtStrView(B));}
static bool MdoCliHashValid(cstr S)
{if(!S||strlen(S)!=64)return false;for(size_t I=0;I<64;I++)if(!((S[I]>='0'&&S[I]<='9')||(S[I]>='a'&&S[I]<='f')))return false;return true;}
static bool MdoCliCommandName(cstr S)
{
    size_t N=S?strlen(S):0;if(!N||N>64||S[0]=='.'||S[N-1]=='.'||!MdoExtensionRelativePath(S)||strchr(S,'/'))return false;
    for(size_t I=0;I<N;I++)if(!((S[I]>='a'&&S[I]<='z')||(S[I]>='A'&&S[I]<='Z')||(S[I]>='0'&&S[I]<='9')||strchr("_.+-",S[I])))return false;
    return N<4||!MdoCliAliasEqual(S+N-4,".exe");
}
static bool MdoCliCommandsValid(const xvalue* Commands,const xvalue* Unused)
{
    (void)Unused;if(!Commands)return true;
    if(xrtValueType(Commands)!=XVALUE_ARRAY||xrtValueCount(Commands)>64)return false;
    for(size_t I=0;I<xrtValueCount(Commands);I++){
        const xvalue* C=xrtValueArrayGet(Commands,I);cstr Name=MdoExtensionText(C,"name",64),Entry=MdoExtensionText(C,"entry",MDO_EXTENSION_PATH_BYTES);
        if(!MdoCliCommandName(Name)||!MdoExtensionRelativePath(Entry))return false;
        for(size_t J=0;J<I;J++)if(MdoCliAliasEqual(Name,MdoExtensionText(xrtValueArrayGet(Commands,J),"name",64)))return false;
        const xvalue* Args=xrtValueObjectGet(C,XRT_STR_LITERAL("args"));
        if(Args){if(xrtValueType(Args)!=XVALUE_ARRAY||xrtValueCount(Args)>64)return false;
            for(size_t J=0;J<xrtValueCount(Args);J++){xstrview V={0};if(!xrtValueGetString(xrtValueArrayGet(Args,J),&V)||V.Size>32768||memchr(V.Data,0,V.Size))return false;}}
    }return true;
}
static bool MdoExternalToolConfig(const xvalue* Config,char Error[512])
{
    cstr Program=MdoExtensionText(Config,"program",MDO_EXTENSION_PATH_BYTES),Prompt=MdoExtensionText(Config,"prompt",16384),Command=MdoExtensionText(Config,"command",64);
    bool Ok=xrtValueType(Config)==XVALUE_OBJECT&&MdoExtensionRelativePath(Program)&&Prompt&&Prompt[0]&&(!Command||MdoCliCommandName(Command))&&MdoCliCommandsValid(xrtValueObjectGet(Config,XRT_STR_LITERAL("commands")),NULL);
    const xvalue* Aliases=xrtValueObjectGet(Config,XRT_STR_LITERAL("aliases"));
    if(Aliases){Ok=Ok&&xrtValueType(Aliases)==XVALUE_ARRAY&&xrtValueCount(Aliases)<=32;
        for(size_t I=0;Ok&&I<xrtValueCount(Aliases);I++){xstrview S={0};Ok=xrtValueGetString(xrtValueArrayGet(Aliases,I),&S)&&!memchr(S.Data,0,S.Size)&&MdoCliCommandName(S.Data);}}
    const xvalue* Links=xrtValueObjectGet(Config,XRT_STR_LITERAL("links"));
    if(Links){Ok=Ok&&xrtValueType(Links)==XVALUE_ARRAY&&xrtValueCount(Links)<=16;
        for(size_t I=0;Ok&&I<xrtValueCount(Links);I++){xstrview S={0};Ok=xrtValueGetString(xrtValueArrayGet(Links,I),&S)&&S.Size<=2048&&!memchr(S.Data,0,S.Size)&&(xrtStrStarts(S,XRT_STR_LITERAL("https://"))||xrtStrStarts(S,XRT_STR_LITERAL("http://")));}}
    const char* const Fields[]={"program","prompt","command","commands","aliases","links"};
    for(size_t I=0;Ok&&I<xrtValueCount(Config);I++){xstrview K={0};xrtValueObjectAt(Config,I,&K);bool Known=false;
        for(size_t J=0;J<sizeof(Fields)/sizeof(*Fields);J++)if(xrtStrEqual(K,xrtStrView(Fields[J])))Known=true;Ok=Known;}
    if(!Ok&&Error)snprintf(Error,512,"External tool configuration requires a relative program, model prompt and valid command names; file contents belong to the directory ZIP");return Ok;
}
#endif
