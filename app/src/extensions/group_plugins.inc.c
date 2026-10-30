#include "../../include/mdo/plugin_format.h"
typedef struct MdoExtPluginTarget {char* Key;cstr Owner;} MdoExtPluginTarget;
static int MdoExtPluginTargetOrder(const void* A,const void* B){return strcmp(((const MdoExtPluginTarget*)A)->Key,((const MdoExtPluginTarget*)B)->Key);}
static bool MdoExtGroupPluginRow(xvalue* Plugins,const xvalue* Meta,cstr Root,char Error[512]){
    cstr Entry=MdoExtensionText(Meta,"entry",4096),Ref=MdoExtensionText(Meta,"id",MDO_EXTENSION_REF_BYTES),Alias=MdoExtensionText(Meta,"alias",64);char* Path=Entry&&Root?xrtFormat("%s/%s",Root,Entry):NULL;
    xvalue* Recipe=Path?MdoExtReadJson(Path,1048576,false):NULL,*Files=xrtValueArray();MdoExtTree Tree={0};bool Ok=Recipe&&Files&&MdoExtCollect(Root,&Tree,NULL,Error);
    for(size_t I=0;Ok&&I<Tree.Count;I++){xvalue* F=xrtValueObject();Ok=F&&MdoExtPut(F,"path",Tree.Files[I].Path)&&xrtValueObjectSetNew(F,XRT_STR_LITERAL("directory"),xrtValueBool(Tree.Files[I].Directory))&&xrtValueArrayAppend(Files,F);xrtValueRelease(F);}MdoExtTreeUnit(&Tree);
    Ok=Ok&&MdoPluginValidate(Recipe,Files,Alias,Error,512);xvalue* Row=xrtValueObject();Ok=Ok&&Row&&MdoExtPut(Row,"id",Ref)&&MdoExtPut(Row,"alias",Alias)&&xrtValueObjectSet(Row,XRT_STR_LITERAL("recipe"),Recipe)&&xrtValueArrayAppend(Plugins,Row);
    xrtValueRelease(Row);xrtValueRelease(Recipe);xrtValueRelease(Files);xrtFree(Path);return Ok;
}
static bool MdoExtGroupPlugins(const xvalue* Items,cstr Base,const xvalue* Before,char Error[512]){
    bool Needed=false;for(size_t I=0;I<xrtValueCount(Items);I++){cstr Kind=MdoExtensionText(xrtValueArrayGet(Items,I),"kind",32);if(Kind&&!strcmp(Kind,"plugins"))Needed=true;}if(!Needed)return true;
    xvalue* Rows=MdoExtListRawLocked("plugins",Before,Error),*Plugins=xrtValueArray();bool Ok=Rows&&Plugins;size_t Count=0;
    for(size_t I=0;Ok&&I<xrtValueCount(Rows);I++){
        const xvalue* Row=xrtValueArrayGet(Rows,I);cstr Ref=MdoExtensionText(Row,"id",MDO_EXTENSION_REF_BYTES);bool Changed=false;
        for(size_t J=0;Ref&&J<xrtValueCount(Items);J++){cstr Id=MdoExtensionText(xrtValueArrayGet(Items,J),"id",MDO_EXTENSION_REF_BYTES);if(Id&&!strcmp(Ref,Id))Changed=true;}
        if(Changed)continue;
        Count++;if(MdoGraphBool(Row,"enabled",false))Ok=MdoExtGroupPluginRow(Plugins,Row,MdoExtensionText(Row,"root",4096),Error);
    }
    for(size_t I=0;Ok&&I<xrtValueCount(Items);I++){
        const xvalue* Item=xrtValueArrayGet(Items,I);cstr Kind=MdoExtensionText(Item,"kind",32),Op=MdoExtensionText(Item,"operation",16);if(strcmp(Kind,"plugins")||!strcmp(Op,"remove"))continue;Count++;if(!MdoGraphBool(Item,"desired_enabled",false))continue;
        bool Install=!strcmp(Op,"install");xvalue* Existing=Install?NULL:MdoExtRead("plugins",MdoExtensionText(Item,"directory_id",96),Before,Error);const xvalue* Meta=Install?xrtValueObjectGet(Item,XRT_STR_LITERAL("manifest")):Existing;
        char* Root=Install?xrtFormat("%s/%u/incoming",Base,(unsigned)I):MdoExtensionDirectoryPath("plugins",MdoExtensionText(Item,"directory_id",96));Ok=Meta&&Root&&MdoExtGroupPluginRow(Plugins,Meta,Root,Error);xrtValueRelease(Existing);xrtFree(Root);
    }
    Ok=Ok&&Count<=MDO_PLUGIN_COUNT;MdoExtPluginTarget* Targets=Ok?xrtCalloc(8192,sizeof(*Targets)):NULL;Ok=Ok&&Targets;size_t Used=0;
    for(size_t I=0;Ok&&I<xrtValueCount(Plugins);I++){
        const xvalue* A=xrtValueArrayGet(Plugins,I),*Recipe=xrtValueObjectGet(A,XRT_STR_LITERAL("recipe")),*Exclusive=xrtValueObjectGet(Recipe,XRT_STR_LITERAL("exclusive"));cstr Owner=MdoExtensionText(A,"id",MDO_EXTENSION_REF_BYTES);
        for(size_t J=0;Ok&&J<I;J++){
            const xvalue* B=xrtValueArrayGet(Plugins,J),*Other=xrtValueObjectGet(xrtValueObjectGet(B,XRT_STR_LITERAL("recipe")),XRT_STR_LITERAL("exclusive"));
            if(!strcmp(MdoExtensionText(A,"alias",64),MdoExtensionText(B,"alias",64)))Ok=MdoExtError(Error,"Enabled plugin aliases collide");
            for(size_t X=0;Ok&&X<xrtValueCount(Exclusive);X++)for(size_t Y=0;Ok&&Y<xrtValueCount(Other);Y++)if(xrtValueEqual(xrtValueArrayGet(Exclusive,X),xrtValueArrayGet(Other,Y))){if(Error)snprintf(Error,512,"Plugin exclusive conflict: %.160s and %.160s",Owner,MdoExtensionText(B,"id",MDO_EXTENSION_REF_BYTES));Ok=false;}
        }
        const xvalue* Overrides=xrtValueObjectGet(Recipe,XRT_STR_LITERAL("overrides"));
        for(size_t J=0;Ok&&J<xrtValueCount(Overrides);J++){
            cstr Target=MdoExtensionText(xrtValueArrayGet(Overrides,J),"target",4096);Ok=Used<8192;if(Ok){Targets[Used].Owner=Owner;Targets[Used].Key=MdoExtensionPathKey(Target);Ok=Targets[Used].Key!=NULL;if(Ok)Used++;}
        }
    }
    if(Ok&&Used>1)qsort(Targets,Used,sizeof(*Targets),MdoExtPluginTargetOrder);
    xvalue* Owners=xrtValueObject();Ok=Ok&&Owners;
    for(size_t I=0;Ok&&I<Used;I++){
        char* Key=Targets[I].Key;cstr Prior=MdoExtensionText(Owners,Key,MDO_EXTENSION_REF_BYTES);
        for(char* P=Key;!Prior&&*P;P++)if(*P=='/'){*P=0;Prior=MdoExtensionText(Owners,Key,MDO_EXTENSION_REF_BYTES);*P='/';}
        if(Prior){if(Error)snprintf(Error,512,"Plugin override conflict: %.160s and %.160s",Prior,Targets[I].Owner);Ok=false;}else Ok=MdoExtPut(Owners,Key,Targets[I].Owner);
    }xrtValueRelease(Owners);
    for(size_t I=0;I<Used;I++)xrtFree(Targets[I].Key);
    xrtFree(Targets);xrtValueRelease(Rows);xrtValueRelease(Plugins);if(!Ok&&Error&&!Error[0])MdoExtError(Error,"The complete plugin set exceeds its count/target budget or contains an invalid recipe");return Ok;
}
