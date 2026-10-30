#define MDO_EXT_BINDINGS "data/extensions/bindings.json"
static bool MdoExtBindingRevision(const xvalue* Value,char Hash[65]){
    char* Text=MdoExtJson(Value,MDO_EXT_STATE_LIMIT);uint8 Digest[32];bool Ok=Text&&xrtSha256(Text,strlen(Text),Digest);xrtFree(Text);
    if(Ok)for(size_t I=0;I<32;I++)snprintf(Hash+I*2,3,"%02x",Digest[I]);
    return Ok;
}
static bool MdoExtBindingScope(cstr Scope){return !Scope||!Scope[0]||!strcmp(Scope,"global")||MdoExtensionDirectoryId(Scope);}
static xvalue* MdoExtBindingsReadLocked(cstr Id,cstr Scope,char Error[512]){
    xvalue* All=MdoExtReadJson(MDO_EXT_BINDINGS,MDO_EXT_STATE_LIMIT,true),*Out=xrtValueObject();char Hash[65];
    bool Ok=All&&xrtValueType(All)==XVALUE_OBJECT&&Out&&MdoExtBindingRevision(All,Hash);const xvalue* Role=xrtValueObjectGet(All,xrtStrView(Id));Ok=Ok&&(!Role||xrtValueType(Role)==XVALUE_OBJECT);
    const xvalue* Global=xrtValueObjectGet(Role,XRT_STR_LITERAL("global")),*Specific=Scope&&Scope[0]&&strcmp(Scope,"global")?xrtValueObjectGet(Role,xrtStrView(Scope)):NULL;
    xvalue* Bindings=Global?xrtValueClone(Global):xrtValueObject();Ok=Ok&&Bindings&&xrtValueType(Bindings)==XVALUE_OBJECT&&(!Specific||xrtValueType(Specific)==XVALUE_OBJECT);
    for(size_t I=0;Ok&&I<xrtValueCount(Specific);I++){xstrview Key;const xvalue* Value=xrtValueObjectAt(Specific,I,&Key);Ok=xrtValueObjectSet(Bindings,Key,Value);}
    for(size_t I=0;Ok&&I<xrtValueCount(Bindings);I++){xstrview K;const xvalue* V=xrtValueObjectAt(Bindings,I,&K);xstrview Connection={0};cstr Kind=MdoExtensionRefKind(K.Data);Ok=Kind&&!strcmp(Kind,"mcp")&&xrtValueGetString(V,&Connection)&&Connection.Size<=96&&!memchr(Connection.Data,0,Connection.Size)&&MdoExtensionDirectoryId(Connection.Data);}
    Ok=Ok&&MdoExtPut(Out,"revision",Hash)&&xrtValueObjectSet(Out,XRT_STR_LITERAL("bindings"),Bindings);xrtValueRelease(Bindings);xrtValueRelease(All);
    if(!Ok){xrtValueRelease(Out);MdoExtError(Error,"Private dependency bindings are invalid or unreadable");return NULL;}return Out;
}
xvalue* MdoExtensionsBindings(cstr Id,cstr Scope,char Error[512]){
    if(Error)Error[0]=0;
    if(!MdoExtLock||!MdoExtensionRefValid(Id)||!MdoExtBindingScope(Scope))return MdoExtError(Error,"Choose an extension identity and binding scope"),NULL;
    xrtMutexLock(MdoExtLock);xvalue* Out=MdoExtBindingsReadLocked(Id,Scope,Error);xrtMutexUnlock(MdoExtLock);return Out;
}
char* MdoExtensionsBinding(cstr Id,cstr Dependency,cstr Scope,char Error[512]){
    if(Error)Error[0]=0;
    if(!MdoExtensionRefValid(Id)||!MdoExtensionRefValid(Dependency))return NULL;
    xvalue* Out=MdoExtensionsBindings(Id,Scope,Error);cstr Value=MdoExtensionText(xrtValueObjectGet(Out,XRT_STR_LITERAL("bindings")),Dependency,96);char* Result=Value?xrtStrDup(Value):NULL;xrtValueRelease(Out);return Result;
}
bool MdoExtensionsBind(cstr Id,cstr Dependency,cstr Scope,cstr Connection,cstr Expected,xvalue** Result,char Error[512]){
    *Result=NULL;if(Error)Error[0]=0;cstr Kind=MdoExtensionRefKind(Dependency);
    if(!MdoExtLock||!MdoExtensionRefValid(Id)||!Kind||strcmp(Kind,"mcp")||!MdoExtBindingScope(Scope)||!Connection||(*Connection&&!MdoExtensionDirectoryId(Connection))||!Expected||strlen(Expected)!=64)return MdoExtError(Error,"Bind a declared MCP dependency, private connection and current binding revision");
    xvalue* Closure=MdoExtensionsDependencies(Id,false,Error);bool Declared=false;
    for(size_t I=1;I<xrtValueCount(Closure);I++){cstr Ref=MdoExtensionText(xrtValueArrayGet(Closure,I),"id",MDO_EXTENSION_REF_BYTES);if(Ref&&!strcmp(Ref,Dependency))Declared=true;}xrtValueRelease(Closure);
    if(!Declared)return MdoExtError(Error,"The MCP template is not a required dependency of this extension");
    xrtMutexLock(MdoExtLock);xvalue* All=MdoExtReadJson(MDO_EXT_BINDINGS,MDO_EXT_STATE_LIMIT,true);char Hash[65];bool Ok=All&&xrtValueType(All)==XVALUE_OBJECT&&MdoExtBindingRevision(All,Hash)&&!strcmp(Hash,Expected);
    xvalue* Role=Ok?xrtValueObjectGet(All,xrtStrView(Id)):NULL;
    if(Ok&&!Role){Role=xrtValueObject();Ok=Role&&xrtValueObjectSetNew(All,xrtStrView(Id),Role);}
    cstr Key=Scope&&Scope[0]?Scope:"global";xvalue* Bindings=Ok?xrtValueObjectGet(Role,xrtStrView(Key)):NULL;
    if(Ok&&!Bindings){Bindings=xrtValueObject();Ok=Bindings&&xrtValueObjectSetNew(Role,xrtStrView(Key),Bindings);}
    Ok=Ok&&xrtValueType(Bindings)==XVALUE_OBJECT;
    if(Ok){if(*Connection)Ok=MdoExtPut(Bindings,Dependency,Connection);else xrtValueObjectRemove(Bindings,xrtStrView(Dependency));}
    Ok=Ok&&MdoExtSaveJson(MDO_EXT_BINDINGS,All,MDO_EXT_STATE_LIMIT);xrtValueRelease(All);
    if(Ok)*Result=MdoExtBindingsReadLocked(Id,Scope,Error);
    xrtMutexUnlock(MdoExtLock);return (Ok&&*Result)||MdoExtError(Error,"Dependency bindings changed; refresh before saving");
}
