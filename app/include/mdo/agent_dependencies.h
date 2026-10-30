#ifndef MDO_AGENT_DEPENDENCIES_H
#define MDO_AGENT_DEPENDENCIES_H
#include "extensions.h"
#include "extension_files.h"
#include "agent_config_format.h"
#include "extension_manifest.h"

static inline bool MdoAgentDependencyDeclared(cstr Root,cstr Ref,char* Error,size_t Capacity)
{
    char Why[512]={0};char* Path=Root?xrtFormat("%s/extension.json",Root):NULL;
    char* Text=Path?MdoExtensionRead(Path,true,MDO_EXTENSION_MANIFEST_BYTES,NULL):NULL;xvalue* Meta=Text?xrtJsonParse(xrtStrView(Text)):NULL;
    cstr Self=MdoExtensionText(Meta,"id",MDO_EXTENSION_REF_BYTES);xvalue* Closure=Self?MdoExtensionsDependencies(Self,false,Why):NULL;bool Found=false;
    for(size_t I=1;I<xrtValueCount(Closure);I++){cstr Id=MdoExtensionText(xrtValueArrayGet(Closure,I),"id",MDO_EXTENSION_REF_BYTES);if(Id&&!strcmp(Id,Ref))Found=true;}
    xrtValueRelease(Closure);xrtValueRelease(Meta);xrtFree(Text);xrtFree(Path);
    if(!Found&&Error&&Capacity)snprintf(Error,Capacity,"Declare and install required dependency %.160s in extension.json",Ref?Ref:"");
    return Found;
}
static inline bool MdoAgentResolveSelections(xvalue* Config,cstr Root,char* Error,size_t Capacity)
{
    xrtValueObjectRemove(Config,XRT_STR_LITERAL("resolved_references"));xvalue* References=xrtValueArray();bool Ok=References!=NULL;
    const cstr Fields[]={"tools","skills","mcp_servers"};
    for(size_t F=0;Ok&&F<3;F++){
        const xvalue* Input=MdoAgentConfigGet(Config,Fields[F]);if(xrtValueType(Input)!=XVALUE_ARRAY)continue;
        xvalue* List=xrtValueArray();Ok=List&&MdoAgentConfigSelectionList(Input,Fields[F]);
        for(size_t I=0;Ok&&I<xrtValueCount(Input);I++){
            const xvalue* Item=xrtValueArrayGet(Input,I);xstrview Text;
            if(xrtValueGetString(Item,&Text)){Ok=xrtValueArrayAppend(List,Item);continue;}
            cstr Ref=MdoAgentConfigText(Item,"ref"),Export=MdoAgentConfigText(Item,"export");char Why[512]={0};
            xvalue* Target=MdoAgentDependencyDeclared(Root,Ref,Error,Capacity)?MdoExtensionsResolve(Ref,Why):NULL;
            cstr Alias=MdoExtensionText(Target,"alias",64),Resolved=F==2?Ref:Export?Export:Alias;
            xvalue* Record=xrtValueObject();Ok=Target&&Resolved&&Record&&MdoAgentConfigSetText(Record,"ref",Ref)&&MdoAgentConfigSetText(Record,"field",Fields[F])&&MdoAgentConfigSetText(Record,"resolved",Resolved)&&
                xrtValueObjectSetNew(Record,XRT_STR_LITERAL("explicit_export"),xrtValueBool(Export!=NULL))&&
                xrtValueArrayAppendNew(List,xrtValueString(xrtStrView(Resolved)))&&xrtValueArrayAppend(References,Record);
            xrtValueRelease(Record);xrtValueRelease(Target);
            if(!Ok&&Error&&Capacity&&!Error[0])snprintf(Error,Capacity,"Cannot resolve %.160s: %.300s",Ref?Ref:"",Why);
        }
        if(Ok)Ok=xrtValueObjectSet(Config,xrtStrView(Fields[F]),List);
        xrtValueRelease(List);
    }
    if(Ok&&xrtValueCount(References))Ok=xrtValueObjectSet(Config,XRT_STR_LITERAL("resolved_references"),References);
    xrtValueRelease(References);return Ok;
}
#endif
