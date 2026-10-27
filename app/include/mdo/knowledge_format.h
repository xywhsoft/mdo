#ifndef MDO_KNOWLEDGE_FORMAT_H
#define MDO_KNOWLEDGE_FORMAT_H
#include "extension_manifest.h"

/* Knowledge contains inert
 * documents, never installers, process definitions or machine paths. */
#define MDO_KNOWLEDGE_FORMAT "mdo.knowledge.v1"
static bool MdoKnowledgeMetadata(const xvalue* Doc,char* Error,size_t Capacity)
{
    const char* Format=MdoExtensionText(Doc,"format",32),*Name=MdoExtensionText(Doc,"name",100);
    const char* Description=MdoExtensionText(Doc,"description",600),*Index=MdoExtensionText(Doc,"index",MDO_EXTENSION_PATH_BYTES);
    const char* Prompt=MdoExtensionText(Doc,"prompt",8192);bool Inject=false;
    bool Ok=xrtValueType(Doc)==XVALUE_OBJECT&&Format&&!strcmp(Format,MDO_KNOWLEDGE_FORMAT)&&Name&&*Name&&
        Description&&*Description&&Index&&MdoExtensionRelativePath(Index)&&!xrtStrCaseEqual(xrtStrView(Index),XRT_STR_LITERAL("knowledge.json"))&&
        Prompt&&xrtValueGetBool(xrtValueObjectGet(Doc,XRT_STR_LITERAL("inject_prompt")),&Inject);
    const char* Credits[]={"author","copyright","license","version"};const size_t Sizes[]={100,4096,80,32};
    for(size_t i=0;Ok&&i<4;i++)if(xrtValueObjectHas(Doc,xrtStrView(Credits[i])))Ok=MdoExtensionText(Doc,Credits[i],Sizes[i])!=NULL;
    xvalueiter It={0};xvaluekey Key;xvalue* Value;
    if(Ok&&xrtValueIterBegin(Doc,&It)){while((Value=xrtValueIterNext(&It,&Key))){(void)Value;
        bool Known=false;const char* Keys[]={"format","name","description","index","prompt","inject_prompt","author","copyright","license","version"};
        for(size_t i=0;i<10;i++)if(xrtStrEqual(Key.String,xrtStrView(Keys[i])))Known=true;
        if(!Known){Ok=false;break;}
    }xrtValueIterEnd(&It);}
    if(!Ok&&Error&&Capacity)snprintf(Error,Capacity,"Invalid knowledge metadata: provide a name, description, relative index, optional prompt and inject_prompt flag; machine paths and executable hooks are unsupported");
    return Ok;
}
#endif
