/* Scoped catalogs own their metadata and diagnostics. No project modifies the
 * process-wide catalog, and invalid shadows never expose a lower-priority Skill. */
static bool MdoSkillsScopedPut(MdoSkillCatalog* Catalog, const MdoSkillSource* Source,
    bool Enabled)
{
    for (size_t i=0;i<Catalog->Count;i++) if (!strcmp(Catalog->Entries[i].Id,Source->Id)) {
        MdoSkillsEntryUnit(&Catalog->Entries[i]);
        Catalog->Entries[i]=Catalog->Entries[--Catalog->Count];break;
    }
    if (!Enabled) return true;
    MdoSkillEntry Entry={0};
    MdoSkillLoadStatus Status=MdoSkillsLoad(Source,&Entry,Catalog->Diagnostics);
    if (Status!=MDO_SKILL_LOAD_OK) {
        MdoSkillsEntryUnit(&Entry);
        if (Status==MDO_SKILL_LOAD_FATAL) return false;
        xrtClearError();return true;
    }
    if (!MdoSkillsGrow((void**)&Catalog->Entries,&Catalog->Capacity,Catalog->Count+1,
        sizeof(Entry),MDO_SKILL_COUNT_LIMIT)) {MdoSkillsEntryUnit(&Entry);return false;}
    Catalog->Entries[Catalog->Count++]=Entry;return true;
}

static int MdoSkillsEntryCompare(const void* A,const void* B)
{return strcmp(((const MdoSkillEntry*)A)->Id,((const MdoSkillEntry*)B)->Id);}

/* Package receipts give stable qualified names without implementing client
 * plugins here. Bare names remain callable for existing Agent bindings. */
static bool MdoSkillsPackageNames(MdoSkillCatalog* Catalog)
{
    bool Missing=false;
    char* Text=MdoExtensionRead("data/extensions/installed.json",true,1024u*1024u,&Missing);
    if (!Text) return Missing;
    xvalue* Records=xrtJsonParse(xrtStrView(Text));xrtFree(Text);
    bool Ok=Records && xrtValueType(Records)==XVALUE_OBJECT;
    size_t Original=Catalog->Count;
    for(size_t r=0;Ok&&r<xrtValueCount(Records);r++) {
        xstrview Key={0},Slug={0};
        const xvalue* Record=xrtValueObjectAt(Records,r,&Key);
        const xvalue* Resources=xrtValueObjectGet(Record,XRT_STR_LITERAL("resources"));
        Ok=xrtValueGetString(xrtValueObjectGet(Record,XRT_STR_LITERAL("slug")),&Slug) &&
            Slug.Size && Slug.Size<=64 && !memchr(Slug.Data,0,Slug.Size);
        char* Namespace=Ok?xrtStrDupN(Slug.Data,Slug.Size):NULL;
        Ok=Ok && Namespace && MdoSkillsIdValid(Namespace);
        for(size_t i=0;Ok&&i<xrtValueCount(Resources);i++) {
            const xvalue* Resource=xrtValueArrayGet(Resources,i);xstrview Kind={0},Id={0};
            if(!xrtValueGetString(xrtValueObjectGet(Resource,XRT_STR_LITERAL("kind")),&Kind) ||
                !MdoSkillsViewEqual(Kind,"skills"))continue;
            if(!xrtValueGetString(xrtValueObjectGet(Resource,XRT_STR_LITERAL("id")),&Id)) {Ok=false;break;}
            for(size_t j=0;Ok&&j<Original;j++) {
                MdoSkillEntry* E=&Catalog->Entries[j];
                if(!E->External || strlen(E->Id)!=Id.Size || memcmp(E->Id,Id.Data,Id.Size))continue;
                char* Qualified=xrtFormat("%s:%s",Namespace,E->Id);
                if(!Qualified || strlen(Qualified)>192) {xrtFree(Qualified);Ok=false;break;}
                for(size_t k=0;k<Catalog->Count;k++)if(!strcmp(Catalog->Entries[k].Id,Qualified))Ok=false;
                MdoSkillSource Source={0};Source.External=true;Source.Id=Qualified;
                Source.RelativeDirectory=E->RelativeDirectory;Source.VirtualDirectory=E->VirtualDirectory;
                Source.Scope="plugin";
                if(Ok)Ok=MdoSkillsScopedPut(Catalog,&Source,true);
                /* ScopedPut may move the entries allocation. */
                if(Ok)Catalog->Entries[j].Visible=false;
                xrtFree(Qualified);
            }
        }
        xrtFree(Namespace);
    }
    xrtValueRelease(Records);return Ok;
}

static bool MdoSkillsProjectDirectory(MdoSkillCatalog* Catalog,cstr Root,cstr Directory,
    const xvalue* Settings)
{
    xroot Anchor=xrtRootOpen(Root);if(!Anchor)return false;
    xdir Dir=xrtRootDirOpen(Anchor,Directory,XDIR_STAT);
    xrtRootClose(Anchor);
    if(!Dir) {
        if(xrtGetError()&&xrtErrorKind(xrtGetError())==XERR_NOT_FOUND){xrtClearError();return true;}
        return false;
    }
    xdirentry Item;xdirnext Next;bool Ok=true;
    while(Ok&&(Next=xrtDirNext(Dir,&Item))==XDIR_NEXT_ITEM) {
        if(!(Item.Flags&XDIR_ENTRY_UTF8))continue;
        char* Id=xrtStrDupN(Item.Name.Data,Item.Name.Size);
        if(!Id){Ok=false;break;}
        if(!MdoSkillsIdValid(Id)){xrtFree(Id);continue;}
        char *Relative=xrtPathJoin(Directory,Id),*Native=Relative?xrtPathJoin(Root,Relative):NULL;
        MdoSkillSource Source={0};Source.External=true;Source.Id=Id;Source.Scope="project";
        Source.NativeRoot=(char*)Root;Source.RelativeDirectory=Relative;Source.VirtualDirectory=Native;
        bool Enabled=true;const xvalue* Flag=xrtValueObjectGet(Settings,xrtStrView(Id));
        Ok=Relative&&Native&&(!Flag||xrtValueGetBool(Flag,&Enabled));
        /* Files and links also shadow lower scopes; Load diagnoses them. */
        if(Ok)Ok=MdoSkillsScopedPut(Catalog,&Source,Enabled);
        xrtFree(Id);xrtFree(Relative);xrtFree(Native);
    }
    if(Next==XDIR_NEXT_ERROR)Ok=false;
    if(!xrtDirClose(Dir))Ok=false;
    return Ok;
}

static bool MdoSkillsProjectRoot(MdoSkillCatalog* Catalog,cstr Root)
{
    static const char* Dirs[]={".opencode/skills",".zcode/skills",".codex/skills",".claude/skills",".agents/skills",".mdo/skills"};
    xroot Anchor=xrtRootOpen(Root);if(!Anchor)return false;
    xfileoptions Opt;xrtFileOptionsInit(&Opt);Opt.Flags=XFILE_READ|XFILE_NOFOLLOW;
    xfile File=xrtRootFileOpen(Anchor,".mdo/skills.json",&Opt);xrtRootClose(Anchor);
    xvalue* Settings=NULL;bool Ok=true;
    if(File) {
        uint64 Size=0;size_t Got=0;char* Text=NULL;
        Ok=xrtFileSize(File,&Size)&&Size<=65536;
        Text=Ok?xrtMalloc((size_t)Size+1):NULL;
        Ok=Ok&&Text&&xrtReadAtFull(File,0,Text,(size_t)Size,&Got)&&Got==Size;
        if(Ok){Text[Size]=0;Settings=xrtJsonParse(xrtStrViewN(Text,(size_t)Size));Ok=Settings&&xrtValueType(Settings)==XVALUE_OBJECT;}
        xrtFree(Text);if(!xrtClose(File))Ok=false;
    } else if(xrtGetError()&&xrtErrorKind(xrtGetError())==XERR_NOT_FOUND)xrtClearError();
    else Ok=false;
    for(size_t i=0;Ok&&i<sizeof(Dirs)/sizeof(*Dirs);i++)Ok=MdoSkillsProjectDirectory(Catalog,Root,Dirs[i],Settings);
    xrtValueRelease(Settings);return Ok;
}

MdoSkillCatalog* MdoSkillCatalogForWorkspace(cstr Workspace)
{
    MdoSkillCatalog* Base=MdoSkillCatalogSnapshot();
    if(!Base)return NULL;
    MdoSkillCatalog* Catalog=MdoSkillsCatalogCreate(Base->Generation);
    bool Ok=Catalog!=NULL;
    if(Ok){Catalog->Diagnostics=MdoSkillsDiagnosticsCreate();Ok=Catalog->Diagnostics!=NULL;}
    for(size_t i=0;Ok&&i<Base->Count;i++) {
        const MdoSkillEntry* E=&Base->Entries[i];MdoSkillSource S={0};
        S.External=E->External;S.Id=E->Id;S.VirtualDirectory=E->VirtualDirectory;S.RelativeDirectory=E->RelativeDirectory;
        Ok=MdoSkillsScopedPut(Catalog,&S,true);
    }
    if(Ok)Ok=MdoSkillsPackageNames(Catalog);
    char* Roots[33]={0};size_t Count=0;bool Git=false;
    if(Ok&&Workspace&&Workspace[0]) {
        Roots[Count++]=xrtPathReal(Workspace);Ok=Roots[0]!=NULL;
        while(Ok&&Count<=32) {
            xroot Anchor=xrtRootOpen(Roots[Count-1]);xfileinfo Info={0};
            Ok=Anchor!=NULL;
            if(Ok&&xrtRootStat(Anchor,".git",false,&Info)){Git=true;xrtRootClose(Anchor);break;}
            if(Anchor)xrtRootClose(Anchor);
            if(Ok&&xrtGetError()&&xrtErrorKind(xrtGetError())!=XERR_NOT_FOUND){Ok=false;break;}
            xrtClearError();char* Parent=xrtPathParent(Roots[Count-1]);
            if(!Parent||!strcmp(Parent,Roots[Count-1])){xrtFree(Parent);break;}
            Roots[Count++]=Parent;
        }
        /* Outside a repository only the exact workspace is eligible. */
        for(size_t n=Git?Count:1;Ok&&n>0;n--)Ok=MdoSkillsProjectRoot(Catalog,Roots[n-1]);
    }
    for(size_t i=0;i<Count;i++)xrtFree(Roots[i]);
    MdoSkillCatalogRelease(Base);
    if(!Ok){MdoSkillCatalogRelease(Catalog);MdoSkillsSetError("Cannot build scoped Skills; check project paths, package receipts, limits and .mdo/skills.json");return NULL;}
    if(Catalog->Count>1)qsort(Catalog->Entries,Catalog->Count,sizeof(*Catalog->Entries),MdoSkillsEntryCompare);
    return Catalog;
}

MdoSkillDiagnostics* MdoSkillCatalogDiagnostics(const MdoSkillCatalog* Catalog)
{return Catalog?MdoSkillDiagnosticsRef(Catalog->Diagnostics):NULL;}

/* Explicit, bounded import discovery. No ancestor search or global publication. */
MdoSkillCatalog* MdoSkillCatalogDirectory(cstr Root,cstr Directory)
{
    MdoSkillCatalog* Catalog=MdoSkillsCatalogCreate(0);
    if(!Catalog)return NULL;
    Catalog->Diagnostics=MdoSkillsDiagnosticsCreate();
    if(!Catalog->Diagnostics || !MdoSkillsProjectDirectory(Catalog,Root,Directory,NULL)) {
        MdoSkillCatalogRelease(Catalog);return NULL;
    }
    if(Catalog->Count>1)qsort(Catalog->Entries,Catalog->Count,sizeof(*Catalog->Entries),MdoSkillsEntryCompare);
    return Catalog;
}

MdoSkillCatalog* MdoSkillCatalogSingle(cstr Root,cstr Directory,cstr Id)
{
    MdoSkillCatalog* Catalog=MdoSkillsCatalogCreate(0);if(!Catalog)return NULL;
    Catalog->Diagnostics=MdoSkillsDiagnosticsCreate();
    char* Native=xrtPathJoin(Root,Directory);
    MdoSkillSource Source={0};Source.External=true;Source.NativeRoot=(char*)Root;
    Source.Id=(char*)Id;Source.RelativeDirectory=(char*)Directory;Source.VirtualDirectory=Native;Source.Scope="project";
    bool Ok=Catalog->Diagnostics&&Native&&MdoSkillsScopedPut(Catalog,&Source,true)&&Catalog->Count==1;
    xrtFree(Native);if(!Ok){MdoSkillCatalogRelease(Catalog);return NULL;}return Catalog;
}

char* MdoSkillCatalogDocument(const MdoSkillCatalog* Catalog,cstr Id)
{
    const MdoSkillEntry* Entry=MdoSkillsFind(Catalog,Id);
    char *Path=NULL,*Text=NULL;size_t Got=0;xfileinfo Info={0};
    xfile File=Entry?MdoSkillsOpenEntry(Entry,"SKILL.md",&Path):NULL;
    bool Ok=File&&xrtFileStat(File,&Info)&&MdoSkillsSameFile(&Entry->DocumentInfo,&Info);
    if(Ok)Text=xrtMalloc((size_t)Info.Size+1);
    Ok=Ok&&Text&&xrtReadAtFull(File,0,Text,(size_t)Info.Size,&Got)&&Got==Info.Size;
    if(Ok){Text[Got]=0;Ok=!memchr(Text,0,Got)&&xrtUtf8Valid(xrtStrViewN(Text,Got),NULL);}
    xfileinfo After={0};
    if(Ok)Ok=xrtFileStat(File,&After)&&MdoSkillsSameFile(&Info,&After);
    if(File&&!xrtClose(File))Ok=false;
    xrtFree(Path);if(!Ok){xrtFree(Text);return NULL;}return Text;
}
