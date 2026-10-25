/* Ownership comes from durable installation receipts, never from request
 * fields or content hashes. Offline edits do not erase store provenance. */
static xvalue* ExtensionStoreRead(bool* Known)
{
    bool Missing=false;*Known=false;
    char* Text=MdoExtensionRead("data/extensions/installed.json",true,1024u*1024u,&Missing);
    xvalue* Records=Text?xrtJsonParse(xrtStrView(Text)):Missing?xrtValueObject():NULL;xrtFree(Text);
    if(!Records||xrtValueType(Records)!=XVALUE_OBJECT){xrtValueRelease(Records);return NULL;}
    xvalueiter It={0};xvaluekey Key;xvalue* R;bool Ok=true;
    if(xrtValueIterBegin(Records,&It)){while((R=xrtValueIterNext(&It,&Key))){const xvalue* Files=xrtValueObjectGet(R,XRT_STR_LITERAL("files"));
        if(xrtValueType(R)!=XVALUE_OBJECT||xrtValueType(Files)!=XVALUE_ARRAY){Ok=false;break;}
        for(size_t i=0;Ok&&i<xrtValueCount(Files);i++)Ok=MdoPackageText(xrtValueArrayGet(Files,i),"path",255)!=NULL;
        if(!Ok)break;
    }xrtValueIterEnd(&It);}
    if(!Ok){xrtValueRelease(Records);return NULL;}*Known=true;return Records;
}
static const xvalue* ExtensionStoreOwner(const xvalue* Records,cstr Path)
{
    xvalueiter It={0};xvaluekey Key;xvalue* R;const xvalue* Owner=NULL;
    if(xrtValueIterBegin(Records,&It)){while((R=xrtValueIterNext(&It,&Key))){cstr Source=MdoPackageText(R,"source",16);
        if(Source&&!strcmp(Source,"local"))continue;
        const xvalue* Files=xrtValueObjectGet(R,XRT_STR_LITERAL("files"));
        for(size_t i=0;i<xrtValueCount(Files);i++)if(MdoPackageSamePath(Path,MdoPackageText(xrtValueArrayGet(Files,i),"path",255))){Owner=R;break;}
        if(Owner)break;
    }xrtValueIterEnd(&It);}return Owner;
}
static bool ExtensionStoreCanCustomize(MdoApiContext* C,cstr Kind,cstr Id)
{
    Kind=MdoPackageSystem(Kind);
    if(!MdoPackageSingleKind(Kind))return true;
    if(!strcmp(Kind,"mcp")){char P[160];ExtensionPath(Kind,Id,P);bool Missing=false;char* S=MdoExtensionRead(P,false,MDO_EXTENSION_TEXT_LIMIT,&Missing);xvalue* D=S?xrtJsonParse(xrtStrView(S)):NULL;
        const xvalue* B=McpConnectionBinding(D);const xvalue* R=xrtValueObjectGet(B,XRT_STR_LITERAL("receipt"));cstr Source=MdoPackageText(R,"source",16);bool Managed=B&&(!Source||strcmp(Source,"local"));xrtFree(S);xrtValueRelease(D);
        if(Managed){MdoApiReplyError(C,403,"store_resource_readonly","Configure this MCP connection through its template; editing, exporting and republishing store templates is unavailable",NULL);return false;}}

    char Path[160];ExtensionPath(Kind,Id,Path);bool Known=false;xvalue* Records=ExtensionStoreRead(&Known);
    bool Managed=ExtensionStoreOwner(Records,Path)!=NULL;
    if(MdoPackageRoleKind(Kind)){snprintf(Path,sizeof(Path),"modules/%s/%s.c",Kind,Id);Managed=Managed||ExtensionStoreOwner(Records,Path)!=NULL;}
    xrtValueRelease(Records);
    if(!Known){MdoApiReplyError(C,503,"ecosystem_receipts","Cannot verify resource ownership; restore installation receipts before customizing",NULL);return false;}
    if(Managed){MdoApiReplyError(C,403,"store_resource_readonly","Store resources support enable, update and uninstall; editing, copying and republishing are unavailable",NULL);return false;}
    return true;
}
static bool ExtensionStoreInfo(xvalue* Item,cstr Path,const xvalue* Records,bool Known,bool Full)
{
    const xvalue* Owner=ExtensionStoreOwner(Records,Path);
    bool Ok=MdoApiValueSetBool(Item,"management_known",Known)&&MdoApiValueSetBool(Item,"store_managed",Owner!=NULL);
    if(!Owner)return Ok;
    const xvalue* Manifest=xrtValueObjectGet(Owner,XRT_STR_LITERAL("manifest"));
    const char* Keys[]={"submitter","author","copyright","license","version"};const size_t Limits[]={100,100,4096,80,32};
    for(size_t i=0;Ok&&i<5;i++){
        cstr Text=i?MdoPackageText(Manifest,Keys[i],Limits[i]):NULL;
        if(!Text||!Text[0])Text=MdoPackageText(Owner,Keys[i],Limits[i]);
        size_t N=Text?strlen(Text):0;
        if(!Full&&i==2&&N>256){N=256;while(N&&((unsigned char)Text[N]&0xc0)==0x80)N--;}
        Ok=MdoApiValueSetStringView(Item,Keys[i],xrtStrViewN(Text?Text:"",N));
    }return Ok;
}
