#ifndef MDO_PLUGIN_FORMAT_H
#define MDO_PLUGIN_FORMAT_H
/* Pure shared validation; included after ecosystem_package.h path/version
 * helpers. No code execution or device/credential access on the website. */
#define MDO_PLUGIN_FORMAT "mdo.plugin.v1"
#define MDO_PLUGIN_MIN_BUILD 70000049u
#define MDO_PLUGIN_COUNT 32u
#define MDO_PLUGIN_FILES 128u
#define MDO_PLUGIN_BYTES (64u*1024u*1024u)
static bool MdoPluginPath(const char* path)
{
    if(!path||!path[0]||strlen(path)>220||strchr(path,'\\')||strchr(path,':')||path[0]=='/')return false;
    const char* p=path;
    for(;;){const char* slash=strchr(p,'/');size_t n=slash?(size_t)(slash-p):strlen(p);char component[221];
        if(!n||n>220||(n==1&&p[0]=='.')||(n==2&&p[0]=='.'&&p[1]=='.')||p[n-1]=='.'||p[n-1]==' ')return false;
        memcpy(component,p,n);component[n]=0;
        /* Case-insensitive filesystem aliases and control bytes are rejected. */
        for(size_t i=0;i<n;i++){unsigned char c=(unsigned char)component[i];if(c<32||c==127||strchr("<>\"|?*",c))return false;}
        char lower[221];for(size_t i=0;i<=n;i++)lower[i]=(component[i]>='A'&&component[i]<='Z')?component[i]+32:component[i];
        size_t b=strcspn(lower,".");lower[b]=0;
        if(!strcmp(lower,"con")||!strcmp(lower,"prn")||!strcmp(lower,"aux")||!strcmp(lower,"nul")||
           (b==4&&(!strncmp(lower,"com",3)||!strncmp(lower,"lpt",3))&&lower[3]>='1'&&lower[3]<='9'))return false;
        if(!slash)break;
        p=slash+1;
    }return true;
}
static const xvalue* MdoPluginGet(const xvalue* V,const char* K)
{return xrtValueObjectGet(V,xrtStrView(K));}
static bool MdoPluginFail(char* E,size_t N,const char* Why)
{if(E&&N)snprintf(E,N,"%s",Why);return false;}
static bool MdoPluginKeys(const xvalue* V,const char* const* Keys,size_t Count)
{
    if(xrtValueType(V)!=XVALUE_OBJECT)return false;
    xvalueiter It={0};xvaluekey Key;xvalue* Value;bool Ok=true;
    if(xrtValueIterBegin(V,&It)){while((Value=xrtValueIterNext(&It,&Key))){(void)Value;
        bool Known=false;for(size_t I=0;I<Count;I++)if(xrtStrEqual(Key.String,xrtStrView(Keys[I])))Known=true;
        if(!Known){Ok=false;break;}
    }xrtValueIterEnd(&It);}return Ok;
}
static bool MdoPluginPathOverlap(const char* A,const char* B)
{
    if(!A||!B)return false;
    size_t I=0;while(A[I]&&B[I]){unsigned char X=A[I],Y=B[I];
        if(X>='A'&&X<='Z')X+=32;if(Y>='A'&&Y<='Z')Y+=32;if(X!=Y)return false;I++;}
    return (!A[I]&&(!B[I]||B[I]=='/'))||(!B[I]&&(!A[I]||A[I]=='/'));
}
static const xvalue* MdoPluginFile(const xvalue* Files,const char* Path)
{
    for(size_t I=0;Path&&I<xrtValueCount(Files);I++){
        const xvalue* F=xrtValueArrayGet(Files,I);const char* P=MdoPackageText(F,"path",220);
        if(P&&!strcmp(P,Path))return F;
    }return NULL;
}
static bool MdoPluginModern(const xvalue* Doc)
{const char* F=MdoPackageText(Doc,"format",32);return F&&!strcmp(F,MDO_PLUGIN_FORMAT);}
static bool MdoPluginVersionMatches(const xvalue* Relation,const char* Version)
{
    const char* Min=MdoPackageText(Relation,"min_version",32),*Max=MdoPackageText(Relation,"max_version_exclusive",32);
    if(!Min&&!Max)return true;
    if(!MdoPackageVersion(Version,NULL))return false;
    return (!Min||MdoPackageVersionCompare(Version,Min)>=0)&&(!Max||MdoPackageVersionCompare(Version,Max)<0);
}
static bool MdoPluginRelations(const xvalue* Doc,const char* Kind,const char* Self,char* E,size_t N)
{
    const xvalue* Rows=MdoPluginGet(Doc,Kind);if(!Rows)return true;
    if(xrtValueType(Rows)!=XVALUE_ARRAY||xrtValueCount(Rows)>32)return MdoPluginFail(E,N,"Plugin relations must contain at most 32 entries");
    const char* const Keys[]={"id","min_version","max_version_exclusive","reason"};
    for(size_t I=0;I<xrtValueCount(Rows);I++){
        const xvalue* R=xrtValueArrayGet(Rows,I);const char* Id=MdoPackageText(R,"id",64);
        if(!MdoPluginKeys(R,Keys,4)||!MdoPackageId(Id)||!strcmp(Self,Id))return MdoPluginFail(E,N,"Plugin relation needs another portable plugin ID");
        const char* Min=MdoPackageText(R,"min_version",32),*Max=MdoPackageText(R,"max_version_exclusive",32);
        if((MdoPluginGet(R,"min_version")&&(!Min||!MdoPackageVersion(Min,NULL)))||
           (MdoPluginGet(R,"max_version_exclusive")&&(!Max||!MdoPackageVersion(Max,NULL)))||
           (Min&&Max&&MdoPackageVersionCompare(Min,Max)>=0)||
           (MdoPluginGet(R,"reason")&&!MdoPackageText(R,"reason",600)))return MdoPluginFail(E,N,"Invalid plugin relation version bounds or reason");
        for(size_t J=0;J<I;J++)if(!strcmp(Id,MdoPackageText(xrtValueArrayGet(Rows,J),"id",64)))return MdoPluginFail(E,N,"Duplicate plugin relation");
    }return true;
}
static bool MdoPluginValidate(const xvalue* Doc,const xvalue* Files,const char* ExpectedId,char* E,size_t N)
{
    const char* const Keys[]={"format","id","name","version","description","author","copyright","license","overrides","entry","exclusive","requires","conflicts"};
    const char* Id=MdoPackageText(Doc,"id",64),*Name=MdoPackageText(Doc,"name",100),*Version=MdoPackageText(Doc,"version",32);
    if(!MdoPluginModern(Doc)||!MdoPluginKeys(Doc,Keys,13)||!MdoPackageId(Id)||(ExpectedId&&strcmp(Id,ExpectedId))||
       !Name||!Name[0]||!MdoPackageVersion(Version,NULL)||!MdoPackageText(Doc,"description",600))return MdoPluginFail(E,N,"Invalid mdo.plugin.v1 identity, version or description");
    const char* Optional[]={"author","copyright","license"};const size_t Limits[]={100,4096,80};
    for(size_t I=0;I<3;I++)if(MdoPluginGet(Doc,Optional[I])&&!MdoPackageText(Doc,Optional[I],Limits[I]))return MdoPluginFail(E,N,"Invalid plugin author, copyright or license");
    if(xrtValueType(Files)!=XVALUE_ARRAY||!xrtValueCount(Files)||xrtValueCount(Files)>MDO_PLUGIN_FILES)return MdoPluginFail(E,N,"Plugin needs 1..128 inventory files");
    for(size_t I=0;I<xrtValueCount(Files);I++){
        const char* Path=MdoPackageText(xrtValueArrayGet(Files,I),"path",220);
        if(!MdoPluginPath(Path))return MdoPluginFail(E,N,"Invalid plugin inventory path");
        for(size_t J=0;J<I;J++)if(MdoPluginPathOverlap(Path,MdoPackageText(xrtValueArrayGet(Files,J),"path",220)))return MdoPluginFail(E,N,"Plugin file paths overlap or alias");
    }
    const xvalue* Overrides=MdoPluginGet(Doc,"overrides");
    if(Overrides&&(xrtValueType(Overrides)!=XVALUE_ARRAY||xrtValueCount(Overrides)>MDO_PLUGIN_FILES))return MdoPluginFail(E,N,"Invalid plugin file mappings");
    const char* const MappingKeys[]={"source","target"};
    for(size_t I=0;I<xrtValueCount(Overrides);I++){
        const xvalue* O=xrtValueArrayGet(Overrides,I);const char* S=MdoPackageText(O,"source",220),*T=MdoPackageText(O,"target",220);
        if(!MdoPluginKeys(O,MappingKeys,2)||!MdoPluginPath(T)||!MdoPluginFile(Files,S))return MdoPluginFail(E,N,"Plugin mapping needs an inventory source and portable app-relative target");
        /* Private loader addresses are allocated by the host, never by a plugin. */
        if(MdoPluginPathOverlap(T,"web/__mdo_plugins")||MdoPluginPathOverlap(T,"__mdo_plugins"))return MdoPluginFail(E,N,"Plugin target uses a loader-owned address");
        for(size_t J=0;J<I;J++)if(MdoPluginPathOverlap(T,MdoPackageText(xrtValueArrayGet(Overrides,J),"target",220)))return MdoPluginFail(E,N,"Plugin target mappings overlap or alias");
    }
    const xvalue* Entry=MdoPluginGet(Doc,"entry");const char* const EntryKeys[]={"frontend","native"};
    if(Entry&&!MdoPluginKeys(Entry,EntryKeys,2))return MdoPluginFail(E,N,"Plugin entries support frontend and native only");
    bool HasEntry=false;
    for(size_t I=0;I<2;I++)if(MdoPluginGet(Entry,EntryKeys[I])){
        const char* P=MdoPackageText(Entry,EntryKeys[I],220);size_t L=P?strlen(P):0;const char* Ext=I?".c":".js";size_t S=I?2:3;
        if(!MdoPluginFile(Files,P)||L<=S||strcmp(P+L-S,Ext))return MdoPluginFail(E,N,"Plugin entry must reference its own .js or .c file");
        HasEntry=true;
        for(size_t J=0;J<xrtValueCount(Overrides);J++)if(!strcmp(P,MdoPackageText(xrtValueArrayGet(Overrides,J),"source",220)))return MdoPluginFail(E,N,"Explicit plugin entry cannot also be published as an app override");
    }
    if(!HasEntry&&!xrtValueCount(Overrides))return MdoPluginFail(E,N,"Plugin needs a file mapping or execution entry");
    const xvalue* Exclusive=MdoPluginGet(Doc,"exclusive");
    if(Exclusive&&(xrtValueType(Exclusive)!=XVALUE_ARRAY||xrtValueCount(Exclusive)>16))return MdoPluginFail(E,N,"Plugin exclusive markers must be an array of at most 16 keys");
    for(size_t I=0;I<xrtValueCount(Exclusive);I++){
        xstrview V;const xvalue* K=xrtValueArrayGet(Exclusive,I);
        if(!xrtValueGetString(K,&V)||memchr(V.Data,0,V.Size)||!MdoPackageId(V.Data))return MdoPluginFail(E,N,"Invalid plugin exclusive marker");
        for(size_t J=0;J<I;J++)if(xrtValueEqual(K,xrtValueArrayGet(Exclusive,J)))return MdoPluginFail(E,N,"Duplicate plugin exclusive marker");
    }
    return MdoPluginRelations(Doc,"requires",Id,E,N)&&MdoPluginRelations(Doc,"conflicts",Id,E,N);
}
#endif
