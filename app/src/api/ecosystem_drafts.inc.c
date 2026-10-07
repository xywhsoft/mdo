/* Portable publication drafts hold metadata and resource references, not raw
 * credentials or a second copy of runtime files. The extension mutex owns CAS. */
#define ECO_DRAFT_DIRECTORY "data/extensions/drafts"
#define ECO_DRAFT_LIMIT (64u*1024u)
#define ECO_DRAFT_COUNT 64u
static bool EcoDraftPath(cstr id,char path[128])
{if(!MdoPackageId(id))return false;snprintf(path,128,ECO_DRAFT_DIRECTORY "/%s.json",id);return true;}
static xvalue* EcoDraftRead(cstr id,char revision[65],bool* missing)
{
    char path[128];if(!EcoDraftPath(id,path))return NULL;
    char* text=MdoExtensionRead(path,true,ECO_DRAFT_LIMIT,missing);if(!text)return NULL;
    xjsonreadconfig limits;xrtJsonReadConfigInit(&limits);limits.MaxInputBytes=ECO_DRAFT_LIMIT;limits.MaxDepth=8;limits.MaxValues=256;limits.MaxStringBytes=32768;
    xvalue* doc=xrtJsonRead(xrtStrView(text),&limits);
    if(!doc||!MdoExtensionHash(text,revision)){xrtValueRelease(doc);doc=NULL;}
    xrtFree(text);return doc;
}
static bool EcoDraftValid(const xvalue* draft)
{
    cstr id=MdoPackageText(draft,"id",64);const xvalue* fields=xrtValueObjectGet(draft,XRT_STR_LITERAL("fields"));
    const xvalue* refs=xrtValueObjectGet(draft,XRT_STR_LITERAL("references")),*platforms=xrtValueObjectGet(fields,XRT_STR_LITERAL("platforms"));
    if(!MdoPackageId(id)||xrtValueType(fields)!=XVALUE_OBJECT||xrtValueType(refs)!=XVALUE_ARRAY||xrtValueCount(refs)>16||xrtValueType(platforms)!=XVALUE_ARRAY||xrtValueCount(platforms)>4)return false;
    const char* keys[]={"slug","name","version","description","readme","license","changelog"};size_t sizes[]={64,100,32,600,32768,80,8192};
    for(size_t i=0;i<7;i++)if(!MdoPackageText(fields,keys[i],sizes[i]))return false;
    for(size_t i=0;i<xrtValueCount(platforms);i++){xstrview p;if(!xrtValueGetString(xrtValueArrayGet(platforms,i),&p)||!(xrtStrEqual(p,XRT_STR_LITERAL("windows-x86_64"))||xrtStrEqual(p,XRT_STR_LITERAL("android-arm64-v8a"))||xrtStrEqual(p,XRT_STR_LITERAL("linux-x86_64"))||xrtStrEqual(p,XRT_STR_LITERAL("macos-arm64"))))return false;}
    for(size_t i=0;i<xrtValueCount(refs);i++){const xvalue* r=xrtValueArrayGet(refs,i);cstr kind=MdoPackageText(r,"kind",16),resource=MdoPackageText(r,"id",64);char path[256];if(!EcoLocalPath(kind,resource,path))return false;
        for(size_t j=0;j<i;j++){const xvalue* q=xrtValueArrayGet(refs,j);if(!strcmp(kind,MdoPackageText(q,"kind",16))&&!strcmp(resource,MdoPackageText(q,"id",64)))return false;}}
    return true;
}
static xvalue* EcoDraftList(void)
{
    xvalue* items=xrtValueArray();bool exists=false;xfileinfo info;
    if(!items||!MdoHomeExternalStat(ECO_DRAFT_DIRECTORY,&exists,&info)){xrtValueRelease(items);return NULL;}
    if(!exists)return items;
    xdir dir=MdoHomeOpenDirectory(ECO_DRAFT_DIRECTORY,XDIR_STAT);if(!dir){xrtValueRelease(items);return NULL;}
    xdirentry entry;xdirnext next;bool ok=true;
    while((next=xrtDirNext(dir,&entry))==XDIR_NEXT_ITEM){size_t n=entry.Name.Size;if(entry.Info.Type!=XFILE_TYPE_FILE||n<6||n>69||memcmp(entry.Name.Data+n-5,".json",5))continue;
        char id[65],revision[65];memcpy(id,entry.Name.Data,n-5);id[n-5]=0;bool missing=false;xvalue* doc=EcoDraftRead(id,revision,&missing);
        if(!doc||!EcoDraftValid(doc)){xrtValueRelease(doc);ok=false;break;}
        const xvalue* fields=xrtValueObjectGet(doc,XRT_STR_LITERAL("fields"));xvalue* row=xrtValueObject();
        ok=row&&MdoApiValueSetString(row,"id",id)&&MdoApiValueSetString(row,"revision",revision)&&xrtValueObjectSet(row,XRT_STR_LITERAL("fields"),fields)&&xrtValueObjectSet(row,XRT_STR_LITERAL("updated_at"),xrtValueObjectGet(doc,XRT_STR_LITERAL("updated_at")));
        /* Keep list responses small; the complete draft is read on editing. */
        if(ok){xvalue* summary=xrtValueObject();const char* keys[]={"name","slug","version"};for(size_t i=0;ok&&i<3;i++)ok=xrtValueObjectSet(summary,xrtStrView(keys[i]),xrtValueObjectGet(fields,xrtStrView(keys[i])));if(ok)ok=xrtValueObjectSet(row,XRT_STR_LITERAL("fields"),summary);xrtValueRelease(summary);}
        ok=ok&&xrtValueArrayAppend(items,row);xrtValueRelease(row);xrtValueRelease(doc);
        if(!ok||xrtValueCount(items)>ECO_DRAFT_COUNT){ok=false;break;}
    }
    if(next==XDIR_NEXT_ERROR)ok=false;
    xrtDirClose(dir);if(!ok){xrtValueRelease(items);return NULL;}return items;
}
static bool EcoDraftAction(MdoApiContext* c,const xvalue* input)
{
    cstr action=MdoPackageText(input,"action",16);
    if(!strcmp(action,"drafts")){xvalue* items=EcoDraftList(),*out=xrtValueObject();if(items&&out){xrtValueObjectSet(out,XRT_STR_LITERAL("items"),items);xrtValueRelease(items);return MdoApiReplySuccessTake(c,200,out,NULL);}xrtValueRelease(items);xrtValueRelease(out);return MdoApiReplyError(c,503,"ecosystem_drafts","Cannot read local drafts",NULL);}
    const xvalue* draft=xrtValueObjectGet(input,XRT_STR_LITERAL("draft"));cstr id=!strcmp(action,"draft_save")?MdoPackageText(draft,"id",64):MdoPackageText(input,"id",64);
    char path[128],revision[65]={0};if(!EcoDraftPath(id,path))return MdoApiReplyError(c,422,"ecosystem_draft_invalid","Invalid draft ID",NULL);
    bool missing=false;xvalue* old=EcoDraftRead(id,revision,&missing);
    if(!strcmp(action,"draft_read")){if(old){MdoApiValueSetString(old,"revision",revision);return MdoApiReplySuccessTake(c,200,old,NULL);}return MdoApiReplyError(c,missing?404:503,"ecosystem_draft_missing","Draft unavailable",NULL);}
    cstr expected=MdoPackageText(input,"revision",64);bool matches=missing?(!expected||!expected[0]):old&&expected&&!strcmp(expected,revision);xrtValueRelease(old);
    if(!matches)return MdoApiReplyError(c,412,"ecosystem_draft_conflict","Draft changed on this device; reload before saving",NULL);
    if(!strcmp(action,"draft_delete")){bool ok=!missing&&MdoHomeRemove(path,false);return ok?MdoApiReplySuccessTake(c,200,xrtValueObject(),NULL):MdoApiReplyError(c,missing?404:503,"ecosystem_draft_delete","Cannot remove draft",NULL);}
    if(!EcoDraftValid(draft))return MdoApiReplyError(c,422,"ecosystem_draft_invalid","Invalid draft fields or resource references",NULL);
    if(missing){xvalue* list=EcoDraftList();bool allowed=list&&xrtValueCount(list)<ECO_DRAFT_COUNT;xrtValueRelease(list);if(!allowed)return MdoApiReplyError(c,409,"ecosystem_draft_limit","Keep at most 64 local plugin drafts",NULL);}
    xvalue* doc=xrtValueClone(draft);bool ok=doc&&MdoApiValueSetUInt(doc,"updated_at",xrtNow()/1000000u);size_t n=0;char* json=ok?xrtJsonStringify(doc,false,&n):NULL;
    ok=json&&n<=ECO_DRAFT_LIMIT&&MdoExtensionHash(json,revision)&&MdoHomeAtomicWrite(path,json,n,false);xrtFree(json);
    if(ok){MdoApiValueSetString(doc,"revision",revision);return MdoApiReplySuccessTake(c,200,doc,NULL);}xrtValueRelease(doc);return MdoApiReplyError(c,503,"ecosystem_draft_save","Cannot save portable draft",NULL);
}
