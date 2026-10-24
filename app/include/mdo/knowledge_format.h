#ifndef MDO_KNOWLEDGE_FORMAT_H
#define MDO_KNOWLEDGE_FORMAT_H

/* Included after the portable package primitives. Knowledge contains inert
 * documents, never installers, process definitions or machine paths. */
#define MDO_KNOWLEDGE_FORMAT "mdo.knowledge.v1"
static bool MdoKnowledgeMetadata(const xvalue* Doc,char* Error,size_t Capacity)
{
    const char* Format=MdoPackageText(Doc,"format",32),*Name=MdoPackageText(Doc,"name",100);
    const char* Description=MdoPackageText(Doc,"description",600),*Index=MdoPackageText(Doc,"index",220);
    const char* Prompt=MdoPackageText(Doc,"prompt",8192);bool Inject=false;
    bool Ok=xrtValueType(Doc)==XVALUE_OBJECT&&Format&&!strcmp(Format,MDO_KNOWLEDGE_FORMAT)&&Name&&*Name&&
        Description&&*Description&&Index&&MdoPackagePath(Index)&&!MdoPackageSamePath(Index,"knowledge.json")&&
        Prompt&&xrtValueGetBool(xrtValueObjectGet(Doc,XRT_STR_LITERAL("inject_prompt")),&Inject);
    const char* Credits[]={"author","copyright","license","version"};const size_t Sizes[]={100,4096,80,32};
    for(size_t i=0;Ok&&i<4;i++)if(xrtValueObjectHas(Doc,xrtStrView(Credits[i])))Ok=MdoPackageText(Doc,Credits[i],Sizes[i])!=NULL;
    xvalueiter It={0};xvaluekey Key;xvalue* Value;
    if(Ok&&xrtValueIterBegin(Doc,&It)){while((Value=xrtValueIterNext(&It,&Key))){(void)Value;
        bool Known=false;const char* Keys[]={"format","name","description","index","prompt","inject_prompt","author","copyright","license","version"};
        for(size_t i=0;i<10;i++)if(xrtStrEqual(Key.String,xrtStrView(Keys[i])))Known=true;
        if(!Known){Ok=false;break;}
    }xrtValueIterEnd(&It);}
    if(!Ok&&Error&&Capacity)snprintf(Error,Capacity,"Invalid knowledge metadata: provide a name, description, relative index, optional prompt and inject_prompt flag; machine paths and executable hooks are unsupported");
    return Ok;
}
static bool MdoKnowledgeResource(const xvalue* Resource,char* Error,size_t Capacity)
{
    const char* Text=MdoPackageText(Resource,"content",131072);xvalue* Doc=Text?xrtJsonParse(xrtStrView(Text)):NULL;
    bool Ok=Doc&&MdoKnowledgeMetadata(Doc,Error,Capacity),Found=false;
    const char* Index=MdoPackageText(Doc,"index",220);const xvalue* Files=xrtValueObjectGet(Resource,XRT_STR_LITERAL("files"));
    if(xrtValueType(Files)!=XVALUE_ARRAY||!xrtValueCount(Files)||xrtValueCount(Files)>128)Ok=false;
    for(size_t i=0;Ok&&i<xrtValueCount(Files);i++){
        const xvalue* F=xrtValueArrayGet(Files,i);const char* Path=MdoPackageText(F,"path",220),*B64=MdoPackageText(F,"base64",90u*1024u*1024u);
        if(!Path||!MdoPackagePath(Path)||(MdoPackageSamePath(Path,"knowledge.json")||(!strncmp(Path,"knowledge.json/",15)))||!B64){Ok=false;break;}
        for(size_t j=0;Ok&&j<i;j++){
            const char* Other=MdoPackageText(xrtValueArrayGet(Files,j),"path",220);size_t A=strlen(Path),B=strlen(Other),N=A<B?A:B;char Prefix[221];
            memcpy(Prefix,Path,N);Prefix[N]=0;char OtherPrefix[221];memcpy(OtherPrefix,Other,N);OtherPrefix[N]=0;
            if(MdoPackageSamePath(Prefix,OtherPrefix)&&(A==B||(A>B?Path[B]=='/':Other[A]=='/')))Ok=false;
        }
        if(Ok&&!strcmp(Path,Index)){size_t Size=0;void* Bytes=xrtBase64DecodeNew(B64,strlen(B64),&Size,NULL);
            Found=Bytes&&Size&&Size<=4u*1024u*1024u&&!memchr(Bytes,0,Size)&&xrtUtf8Valid(xrtStrViewN(Bytes,Size),NULL);xrtFree(Bytes);}
    }
    Ok=Ok&&Found;xrtValueRelease(Doc);
    if(!Ok&&Error&&Capacity)snprintf(Error,Capacity,"Knowledge requires 1-128 portable document files and an existing UTF-8 index (up to 4 MiB); duplicate paths, file/directory aliases and knowledge.json attachments are forbidden");
    return Ok;
}
#endif
