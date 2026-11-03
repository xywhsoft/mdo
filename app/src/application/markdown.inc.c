/* md4c owns syntax; the UI receives data, never executable HTML. */
#include <md4c.h>
#define MDO_MD_SOURCE_MAX (512u * 1024u)
#define MDO_MD_NODE_MAX 32768u
#define MDO_MD_DEPTH_MAX 96u
typedef struct MdoMdParse {
    xvalue* Roots;
    xvalue* Children[MDO_MD_DEPTH_MAX];
    unsigned Depth, Count;
    bool Limit, Failed;
    const mdo_sdk_context_v1* Context;
} MdoMdParse;

static xvalue* MdoMdNode(MdoMdParse* S,cstr Type) {
    if (++S->Count > MDO_MD_NODE_MAX || (S->Count % 128u == 0 && SdkCancelled(S->Context))) {
        S->Limit=true;return NULL;
    }
    xvalue* N=xrtValueObject();
    bool Ok=N&&SdkPut(N,"t",Type)&&xrtValueArrayAppend(S->Children[S->Depth],N);
    xrtValueRelease(N);
    if(!Ok){S->Failed=true;return NULL;}
    return xrtValueArrayGet(S->Children[S->Depth],xrtValueCount(S->Children[S->Depth])-1u);
}
static bool MdoMdAttribute(xvalue* N,cstr Key,const MD_ATTRIBUTE* A) {
    return !A||!A->size||xrtValueObjectSetNew(N,xrtStrView(Key),
        xrtValueString(xrtStrViewN(A->text,A->size)));
}
static xvalue* MdoMdEnter(MdoMdParse* S,cstr Type) {
    if(S->Depth+1u>=MDO_MD_DEPTH_MAX){S->Limit=true;return NULL;}
    xvalue* N=MdoMdNode(S,Type),*Children=xrtValueArray();
    bool Ok=N&&Children&&xrtValueObjectSet(N,XRT_STR_LITERAL("c"),Children);
    xrtValueRelease(Children);
    if(!Ok){S->Failed=true;return NULL;}
    S->Children[++S->Depth]=xrtValueObjectGet(N,XRT_STR_LITERAL("c"));return N;
}
static int MdoMdBlockEnter(MD_BLOCKTYPE Type,void* Detail,void* User) {
    MdoMdParse* S=User;
    if(Type==MD_BLOCK_DOC)return 0;
    cstr Name=NULL;
    switch(Type){
    case MD_BLOCK_QUOTE:Name="blockquote";break;
    case MD_BLOCK_UL:Name="ul";break;case MD_BLOCK_OL:Name="ol";break;
    case MD_BLOCK_LI:Name="li";break;case MD_BLOCK_HR:Name="hr";break;
    case MD_BLOCK_H:Name="heading";break;case MD_BLOCK_CODE:Name="codeblock";break;
    case MD_BLOCK_P:Name="p";break;case MD_BLOCK_TABLE:Name="table";break;
    case MD_BLOCK_THEAD:Name="thead";break;case MD_BLOCK_TBODY:Name="tbody";break;
    case MD_BLOCK_TR:Name="tr";break;case MD_BLOCK_TH:Name="th";break;case MD_BLOCK_TD:Name="td";break;
    default:S->Failed=true;return 1;
    }
    xvalue* N=MdoMdEnter(S,Name);if(!N)return 1;
    if(Type==MD_BLOCK_H)SdkPutUInt(N,"level",((MD_BLOCK_H_DETAIL*)Detail)->level);
    if(Type==MD_BLOCK_OL){MD_BLOCK_OL_DETAIL* D=Detail;SdkPutUInt(N,"start",D->start);SdkPutUInt(N,"tight",D->is_tight?1:0);}
    if(Type==MD_BLOCK_UL)SdkPutUInt(N,"tight",((MD_BLOCK_UL_DETAIL*)Detail)->is_tight?1:0);
    if(Type==MD_BLOCK_LI){MD_BLOCK_LI_DETAIL* D=Detail;if(D->is_task)SdkPut(N,"task",D->task_mark==' '?"unchecked":"checked");}
    if(Type==MD_BLOCK_CODE&&!MdoMdAttribute(N,"lang",&((MD_BLOCK_CODE_DETAIL*)Detail)->lang))return 1;
    if(Type==MD_BLOCK_TH||Type==MD_BLOCK_TD){MD_ALIGN A=((MD_BLOCK_TD_DETAIL*)Detail)->align;
        if(A!=MD_ALIGN_DEFAULT)SdkPut(N,"align",A==MD_ALIGN_CENTER?"center":A==MD_ALIGN_RIGHT?"right":"left");}
    return 0;
}
static int MdoMdBlockLeave(MD_BLOCKTYPE Type,void* Detail,void* User) {
    (void)Detail;MdoMdParse* S=User;if(Type!=MD_BLOCK_DOC&&S->Depth)S->Depth--;return 0;
}
static int MdoMdSpanEnter(MD_SPANTYPE Type,void* Detail,void* User) {
    MdoMdParse* S=User;cstr Name=NULL;
    switch(Type){case MD_SPAN_EM:Name="em";break;case MD_SPAN_STRONG:Name="strong";break;
    case MD_SPAN_A:Name="a";break;case MD_SPAN_IMG:Name="img";break;
    case MD_SPAN_CODE:Name="code";break;case MD_SPAN_DEL:Name="del";break;
    default:S->Failed=true;return 1;}
    xvalue* N=MdoMdEnter(S,Name);if(!N)return 1;
    if(Type==MD_SPAN_A){MD_SPAN_A_DETAIL* D=Detail;return !(MdoMdAttribute(N,"url",&D->href)&&MdoMdAttribute(N,"title",&D->title));}
    if(Type==MD_SPAN_IMG){MD_SPAN_IMG_DETAIL* D=Detail;return !(MdoMdAttribute(N,"url",&D->src)&&MdoMdAttribute(N,"title",&D->title));}
    return 0;
}
static int MdoMdSpanLeave(MD_SPANTYPE Type,void* Detail,void* User) {
    (void)Type;(void)Detail;MdoMdParse* S=User;if(S->Depth)S->Depth--;return 0;
}
static int MdoMdText(MD_TEXTTYPE Type,const MD_CHAR* Text,MD_SIZE Size,void* User) {
    MdoMdParse* S=User;
    xvalue* N=MdoMdNode(S,Type==MD_TEXT_BR?"br":Type==MD_TEXT_ENTITY?"entity":"text");
    if(!N)return 1;
    if(Type==MD_TEXT_BR)return 0;
    if(Type==MD_TEXT_NULLCHAR){Text="\xef\xbf\xbd";Size=3;}
    return !xrtValueObjectSetNew(N,XRT_STR_LITERAL("s"),xrtValueString(xrtStrViewN(Text,Size)));
}
static bool SdkMarkdownParse(const mdo_sdk_context_v1* C,const xvalue* P,xvalue** R,mdo_error_v1* E) {
    xstrview Source={0};
    if(!xrtValueGetString(SdkGet(P,"source"),&Source)||Source.Size>MDO_MD_SOURCE_MAX||
        memchr(Source.Data,0,Source.Size)||!xrtUtf8Valid(Source,NULL))
        return MdoApplicationError(E,"markdown_source_invalid","Markdown must be UTF-8 text up to 512 KiB","markdown",413,false);
    MdoMdParse State={0};State.Context=C;State.Roots=xrtValueArray();State.Children[0]=State.Roots;
    if(!State.Roots)return MdoApplicationError(E,"markdown_memory","Cannot allocate Markdown nodes","markdown",500,false);
    MD_PARSER Parser={0};Parser.flags=MD_FLAG_TABLES|MD_FLAG_STRIKETHROUGH|MD_FLAG_TASKLISTS|MD_FLAG_PERMISSIVEAUTOLINKS|MD_FLAG_NOHTML;
    Parser.enter_block=MdoMdBlockEnter;Parser.leave_block=MdoMdBlockLeave;
    Parser.enter_span=MdoMdSpanEnter;Parser.leave_span=MdoMdSpanLeave;Parser.text=MdoMdText;
    int Code=md_parse(Source.Data,(MD_SIZE)Source.Size,&Parser,&State);
    if(Code||State.Depth||State.Failed||State.Limit){xrtValueRelease(State.Roots);
        return MdoApplicationError(E,State.Limit?"markdown_limit":"markdown_parse_failed",
            State.Limit?"Markdown nesting or node budget exceeded":"Markdown could not be parsed","markdown",State.Limit?413:500,false);}
    *R=xrtValueObject();bool Ok=*R&&SdkPut(*R,"engine","md4c")&&SdkPutUInt(*R,"schema_version",1)&&
        xrtValueObjectSet(*R,XRT_STR_LITERAL("nodes"),State.Roots);
    xrtValueRelease(State.Roots);return Ok;
}
