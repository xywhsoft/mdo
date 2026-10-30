#ifndef MDO_EXTENSION_GRAPH_H
#define MDO_EXTENSION_GRAPH_H
#include "extension_manifest.h"

/* Inert metadata only. This evaluator never starts code, fetches a package or
 * grants access. Shared by the desktop and store; all memory is owned by G. */
#define MDO_EXTENSION_GRAPH_NODES 8192u
#define MDO_EXTENSION_GRAPH_EDGES 131072u
#define MDO_EXTENSION_GRAPH_DEPTH 64u
#define MDO_EXTENSION_GRAPH_BYTES (32u*1024u*1024u)
#define MDO_EXTENSION_GRAPH_ISSUE_BYTES (16u*1024u*1024u)
typedef struct MdoExtensionGraphNode {
    xvalue* Row; xvalue* Issues; size_t Cursor,Height; unsigned State; bool Wanted,Valid,Ready,Duplicate,Platform;
} MdoExtensionGraphNode;
typedef struct MdoExtensionGraph {
    xvalue* Rows; xvalue* Index; MdoExtensionGraphNode* Nodes; size_t Count,Edges,MetadataBytes,IssueBytes;
    size_t* Stack; size_t Depth; bool Failed;
} MdoExtensionGraph;
static bool MdoGraphBool(const xvalue* V,cstr K,bool Default){bool B=Default;xrtValueGetBool(xrtValueObjectGet(V,xrtStrView(K)),&B);return B;}
static bool MdoGraphText(xvalue* V,cstr K,cstr S){return S&&xrtValueObjectSetNew(V,xrtStrView(K),xrtValueString(xrtStrView(S)));}
static size_t MdoGraphFind(const MdoExtensionGraph* G,cstr Id){uint64 N=0;if(Id)xrtValueGetUInt(xrtValueObjectGet(G->Index,xrtStrView(Id)),&N);return N&&N<=G->Count?(size_t)N-1:SIZE_MAX;}
static void MdoGraphUnit(MdoExtensionGraph* G){
    if(G->Nodes)for(size_t I=0;I<G->Count;I++)xrtValueRelease(G->Nodes[I].Issues);
    xrtFree(G->Nodes);xrtFree(G->Stack);xrtValueRelease(G->Rows);xrtValueRelease(G->Index);memset(G,0,sizeof(*G));
}
static bool MdoGraphIssue(MdoExtensionGraph* G,size_t At,cstr Code,cstr Target,cstr Message,bool Optional,const xvalue* Tail){
    if(G->Failed)return false;
    MdoExtensionGraphNode* N=&G->Nodes[At];if(xrtValueCount(N->Issues)>=MDO_EXTENSION_RELATIONS+4)return true;
    cstr Self=MdoExtensionText(N->Row,"id",MDO_EXTENSION_REF_BYTES);xvalue* V=xrtValueObject(),*Chain=xrtValueArray();
    bool Ok=V&&Chain&&MdoGraphText(V,"code",Code)&&MdoGraphText(V,"id",Self?Self:"")&&MdoGraphText(V,"dependency_id",Target?Target:"")&&MdoGraphText(V,"message",Message)&&
        xrtValueObjectSetNew(V,XRT_STR_LITERAL("optional"),xrtValueBool(Optional))&&xrtValueArrayAppendNew(Chain,xrtValueString(xrtStrView(Self?Self:"")));
    if(Tail){for(size_t I=0;Ok&&I<xrtValueCount(Tail)&&I<MDO_EXTENSION_GRAPH_DEPTH;I++)Ok=xrtValueArrayAppend(Chain,xrtValueArrayGet(Tail,I));}
    else if(Target&&(!Self||strcmp(Self,Target)))Ok=Ok&&xrtValueArrayAppendNew(Chain,xrtValueString(xrtStrView(Target)));
    Ok=Ok&&xrtValueObjectSet(V,XRT_STR_LITERAL("chain"),Chain);size_t Bytes=0;char* Json=Ok?xrtJsonStringify(V,false,&Bytes):NULL;Ok=Ok&&Json&&Bytes<=MDO_EXTENSION_GRAPH_ISSUE_BYTES-G->IssueBytes;
    if(Ok){G->IssueBytes+=Bytes;Ok=xrtValueArrayAppend(N->Issues,V);}xrtFree(Json);
    xrtValueRelease(V);xrtValueRelease(Chain);if(!Ok)G->Failed=true;return Ok;
}
static bool MdoGraphInit(MdoExtensionGraph* G,const xvalue* Rows,char Error[512]){
    memset(G,0,sizeof(*G));if(xrtValueType(Rows)!=XVALUE_ARRAY||xrtValueCount(Rows)>MDO_EXTENSION_GRAPH_NODES){if(Error)snprintf(Error,512,"Extension dependency graph exceeds its node budget");return false;}
    G->Count=xrtValueCount(Rows);G->Rows=xrtValueClone(Rows);G->Index=xrtValueObject();G->Nodes=xrtCalloc(G->Count?G->Count:1,sizeof(*G->Nodes));
    G->Stack=xrtCalloc(G->Count?G->Count:1,sizeof(size_t));if(!G->Rows||!G->Index||!G->Nodes||!G->Stack)goto failed;
    for(size_t I=0;I<G->Count;I++){
        MdoExtensionGraphNode* N=&G->Nodes[I];N->Row=xrtValueArrayGet(G->Rows,I);N->Issues=xrtValueArray();
        size_t Bytes=0;char* Json=xrtJsonStringify(N->Row,false,&Bytes);bool Bounded=Json&&Bytes<=MDO_EXTENSION_GRAPH_BYTES-G->MetadataBytes;xrtFree(Json);if(!Bounded)goto failed;G->MetadataBytes+=Bytes;
        N->Wanted=MdoGraphBool(N->Row,"desired_enabled",MdoGraphBool(N->Row,"enabled",true));N->Valid=MdoGraphBool(N->Row,"valid",true);
        N->Platform=MdoGraphBool(N->Row,"platform_supported",true);
        cstr Id=MdoExtensionText(N->Row,"id",MDO_EXTENSION_REF_BYTES);if(!N->Issues)goto failed;
        if(!MdoExtensionRefValid(Id)){N->Valid=false;continue;}
        size_t Prior=MdoGraphFind(G,Id);
        if(Prior!=SIZE_MAX){
            bool A=MdoGraphBool(G->Nodes[Prior].Row,"registered",false),B=MdoGraphBool(N->Row,"registered",false);
            if(A&&!B)N->Duplicate=true;
            else if(!A&&B){G->Nodes[Prior].Duplicate=true;if(!xrtValueObjectSetNew(G->Index,xrtStrView(Id),xrtValueUInt(I+1)))goto failed;}
            else {G->Nodes[Prior].Duplicate=true;N->Duplicate=true;}
        }else if(!xrtValueObjectSetNew(G->Index,xrtStrView(Id),xrtValueUInt(I+1)))goto failed;
        G->Edges+=xrtValueCount(xrtValueObjectGet(N->Row,XRT_STR_LITERAL("dependencies")));
        if(G->Edges>MDO_EXTENSION_GRAPH_EDGES)goto failed;
    }
    return true;
failed:if(Error)snprintf(Error,512,"Cannot allocate a complete bounded extension dependency graph");MdoGraphUnit(G);return false;
}
static bool MdoGraphVisit(MdoExtensionGraph* G,size_t Root){
    if(G->Nodes[Root].State==2)return G->Nodes[Root].Ready;
    G->Depth=0;G->Stack[G->Depth++]=Root;
    while(G->Depth){
        size_t At=G->Stack[G->Depth-1];MdoExtensionGraphNode* N=&G->Nodes[At];
        cstr Self=MdoExtensionText(N->Row,"id",MDO_EXTENSION_REF_BYTES);
        if(!N->State){N->State=1;N->Height=1;N->Ready=N->Wanted&&N->Valid&&N->Platform&&!N->Duplicate;
            if(N->Wanted&&!N->Platform)MdoGraphIssue(G,At,"dependency_platform",Self,"This extension does not support the current device platform",false,NULL);
            if(N->Wanted&&!N->Valid)MdoGraphIssue(G,At,"invalid_extension",Self,"Repair the extension manifest or its installation",false,NULL);
            if(N->Duplicate)MdoGraphIssue(G,At,"duplicate_identity",Self,"Another installation claims the same logical extension ID",false,NULL);}
        const xvalue* Deps=xrtValueObjectGet(N->Row,XRT_STR_LITERAL("dependencies"));
        if(!N->Wanted||N->Cursor>=xrtValueCount(Deps)){N->State=2;G->Depth--;continue;}
        const xvalue* D=xrtValueArrayGet(Deps,N->Cursor);cstr Id=MdoExtensionText(D,"id",MDO_EXTENSION_REF_BYTES);bool Optional=MdoGraphBool(D,"optional",false);size_t Target=MdoGraphFind(G,Id);
        cstr Code=NULL,Message=NULL;const xvalue* Tail=NULL;
        if(Target==SIZE_MAX){Code="dependency_missing";Message="Install the required extension";}
        else if(G->Nodes[Target].Duplicate){Code="duplicate_identity";Message="Resolve duplicate provider identities";}
        else if(!G->Nodes[Target].Wanted){Code="dependency_disabled";Message="The dependency is disabled in the current scope";}
        else if(MdoExtensionText(N->Row,"source",16)&&!strcmp(MdoExtensionText(N->Row,"source",16),"store")&&MdoExtensionText(G->Nodes[Target].Row,"source",16)&&!strcmp(MdoExtensionText(G->Nodes[Target].Row,"source",16),"manual")){Code="dependency_source_mismatch";Message="A local directory cannot impersonate a managed dependency";}
        else if(Optional){if(!G->Nodes[Target].Valid){Code="invalid_extension";Message="The optional dependency is invalid";}}
        else if(!G->Nodes[Target].State){G->Stack[G->Depth++]=Target;continue;}
        else if(G->Nodes[Target].State==1){Code="dependency_cycle";Message="Required extension dependencies contain a cycle";}
        else {
            if(N->Height<=G->Nodes[Target].Height)N->Height=G->Nodes[Target].Height+1;
            if(!G->Nodes[Target].Ready){
                Code="dependency_blocked";Message="A required dependency is not ready";
                const xvalue* First=xrtValueArrayGet(G->Nodes[Target].Issues,0);
                if(First){cstr C=MdoExtensionText(First,"code",80),M=MdoExtensionText(First,"message",600);if(C)Code=C;if(M)Message=M;Tail=xrtValueObjectGet(First,XRT_STR_LITERAL("chain"));}
            }else if(N->Height>MDO_EXTENSION_GRAPH_DEPTH){Code="dependency_depth";Message="Extension dependencies exceed the depth budget";}
        }
        N->Cursor++;
        if(Code){MdoGraphIssue(G,At,Code,Id,Message,Optional,Tail);if(!Optional)N->Ready=false;}
    }
    return G->Nodes[Root].Ready;
}
static bool MdoGraphEvaluate(MdoExtensionGraph* G,char Error[512]){
    for(size_t I=0;I<G->Count;I++)MdoGraphVisit(G,I);
    /* Explicit conflicts apply symmetrically, including a one-sided declaration. */
    for(size_t I=0;I<G->Count;I++)if(G->Nodes[I].Wanted){
        const xvalue* Conflicts=xrtValueObjectGet(G->Nodes[I].Row,XRT_STR_LITERAL("conflicts"));
        for(size_t J=0;J<xrtValueCount(Conflicts);J++){
            cstr Id=MdoExtensionText(xrtValueArrayGet(Conflicts,J),"id",MDO_EXTENSION_REF_BYTES);size_t Other=MdoGraphFind(G,Id);
            if(Other!=SIZE_MAX&&G->Nodes[Other].Wanted){G->Nodes[I].Ready=G->Nodes[Other].Ready=false;MdoGraphIssue(G,I,"dependency_conflict",Id,"These extensions cannot be enabled together",false,NULL);MdoGraphIssue(G,Other,"dependency_conflict",MdoExtensionText(G->Nodes[I].Row,"id",MDO_EXTENSION_REF_BYTES),"These extensions cannot be enabled together",false,NULL);}
        }
    }
    /* Propagate late conflicts through reverse edges in O(nodes + edges). */
    size_t Count=G->Count?G->Count:1,Edges=G->Edges?G->Edges:1;
    size_t* Heads=xrtMalloc(Count*sizeof(size_t)),*Next=xrtMalloc(Edges*sizeof(size_t)),*Parents=xrtMalloc(Edges*sizeof(size_t)),*Queue=xrtMalloc(Count*sizeof(size_t));
    if(!Heads||!Next||!Parents||!Queue)G->Failed=true;
    else {
        for(size_t I=0;I<G->Count;I++)Heads[I]=SIZE_MAX;
        size_t Used=0,Head=0,Tail=0;
        for(size_t I=0;I<G->Count;I++){
            if(!G->Nodes[I].Ready)Queue[Tail++]=I;
            const xvalue* Deps=xrtValueObjectGet(G->Nodes[I].Row,XRT_STR_LITERAL("dependencies"));
            for(size_t J=0;J<xrtValueCount(Deps);J++){const xvalue* D=xrtValueArrayGet(Deps,J);if(MdoGraphBool(D,"optional",false))continue;size_t Target=MdoGraphFind(G,MdoExtensionText(D,"id",MDO_EXTENSION_REF_BYTES));
                if(Target!=SIZE_MAX){Parents[Used]=I;Next[Used]=Heads[Target];Heads[Target]=Used++;}}
        }
        while(Head<Tail){size_t Target=Queue[Head++];for(size_t Edge=Heads[Target];Edge!=SIZE_MAX;Edge=Next[Edge]){size_t Parent=Parents[Edge];if(!G->Nodes[Parent].Ready)continue;
            G->Nodes[Parent].Ready=false;Queue[Tail++]=Parent;const xvalue* First=xrtValueArrayGet(G->Nodes[Target].Issues,0);MdoGraphIssue(G,Parent,"dependency_blocked",MdoExtensionText(G->Nodes[Target].Row,"id",MDO_EXTENSION_REF_BYTES),"A required dependency is blocked",false,xrtValueObjectGet(First,XRT_STR_LITERAL("chain")));}}
    }
    xrtFree(Heads);xrtFree(Next);xrtFree(Parents);xrtFree(Queue);
    /* Optional providers do not determine ordering or readiness. Diagnose their
     * final state after all required edges and late conflicts are resolved. */
    for(size_t I=0;I<G->Count;I++)if(G->Nodes[I].Wanted){const xvalue* Deps=xrtValueObjectGet(G->Nodes[I].Row,XRT_STR_LITERAL("dependencies"));
        for(size_t J=0;J<xrtValueCount(Deps);J++){const xvalue* D=xrtValueArrayGet(Deps,J);if(!MdoGraphBool(D,"optional",false))continue;cstr Ref=MdoExtensionText(D,"id",MDO_EXTENSION_REF_BYTES);size_t Target=MdoGraphFind(G,Ref);if(Target==SIZE_MAX||G->Nodes[Target].Ready)continue;
            bool Seen=false;for(size_t K=0;K<xrtValueCount(G->Nodes[I].Issues);K++){cstr Id=MdoExtensionText(xrtValueArrayGet(G->Nodes[I].Issues,K),"dependency_id",MDO_EXTENSION_REF_BYTES);if(Id&&Ref&&!strcmp(Id,Ref))Seen=true;}
            if(!Seen)MdoGraphIssue(G,I,"dependency_blocked",Ref,"The optional provider is not ready; its capabilities remain unavailable",true,NULL);
        }
    }
    for(size_t I=0;I<G->Count;I++){
        MdoExtensionGraphNode* N=&G->Nodes[I];const xvalue* First=xrtValueArrayGet(N->Issues,0);
        cstr State=!N->Wanted?"disabled":N->Ready?"ready":First?MdoExtensionText(First,"code",80):"invalid_extension";
        if(!xrtValueObjectSetNew(N->Row,XRT_STR_LITERAL("desired_enabled"),xrtValueBool(N->Wanted))||!xrtValueObjectSetNew(N->Row,XRT_STR_LITERAL("enabled"),xrtValueBool(N->Ready))||
            !xrtValueObjectSetNew(N->Row,XRT_STR_LITERAL("effective_enabled"),xrtValueBool(N->Ready))||!MdoGraphText(N->Row,"dependency_state",State)||!xrtValueObjectSet(N->Row,XRT_STR_LITERAL("dependency_issues"),N->Issues))G->Failed=true;
    }
    if(G->Failed&&Error)snprintf(Error,512,"Cannot allocate complete dependency diagnostics");
    return !G->Failed;
}
static xvalue* MdoGraphClosure(MdoExtensionGraph* G,cstr Id,char Error[512]){
    size_t Root=MdoGraphFind(G,Id);if(Root==SIZE_MAX){if(Error)snprintf(Error,512,"Unknown logical extension ID");return NULL;}
    bool* Seen=xrtCalloc(G->Count?G->Count:1,sizeof(bool));size_t* Queue=xrtCalloc(G->Count?G->Count:1,sizeof(size_t));xvalue* Out=xrtValueArray();bool Ok=Seen&&Queue&&Out;size_t Head=0,Tail=0;
    if(Ok){Queue[Tail++]=Root;Seen[Root]=true;}
    while(Ok&&Head<Tail){size_t At=Queue[Head++];const xvalue* Row=G->Nodes[At].Row,*Deps=xrtValueObjectGet(Row,XRT_STR_LITERAL("dependencies"));Ok=xrtValueArrayAppend(Out,Row);
        for(size_t I=0;Ok&&I<xrtValueCount(Deps);I++){const xvalue* D=xrtValueArrayGet(Deps,I);if(MdoGraphBool(D,"optional",false))continue;size_t Target=MdoGraphFind(G,MdoExtensionText(D,"id",MDO_EXTENSION_REF_BYTES));if(Target!=SIZE_MAX&&!Seen[Target]){Seen[Target]=true;Queue[Tail++]=Target;}}
    }
    xrtFree(Seen);xrtFree(Queue);if(!Ok){xrtValueRelease(Out);if(Error)snprintf(Error,512,"Cannot allocate dependency closure");return NULL;}return Out;
}
#endif
