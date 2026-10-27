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

MdoSkillCatalog* MdoSkillCatalogForWorkspace(cstr Workspace)
{
    MdoSkillCatalog* Base=MdoSkillCatalogSnapshot();if(!Base||!Workspace||!Workspace[0])return Base;
    xroot Root=xrtRootOpen(Workspace);if(!Root){MdoSkillCatalogRelease(Base);return NULL;}
    xfileoptions Options;xrtFileOptionsInit(&Options);Options.Flags=XFILE_READ|XFILE_NOFOLLOW;
    xfile File=xrtRootFileOpen(Root,".mdo/skills.json",&Options);xrtRootClose(Root);
    if(!File){
        if(xrtGetError()&&xrtErrorKind(xrtGetError())==XERR_NOT_FOUND){xrtClearError();return Base;}
        MdoSkillCatalogRelease(Base);return NULL;
    }
    uint64 Bytes=0;bool Ok=xrtFileSize(File,&Bytes)&&Bytes<=65536;char* Text=Ok?xrtMalloc((size_t)Bytes+1):NULL;
    Ok=Text&&xrtReadFull(File,Text,(size_t)Bytes,NULL);if(!xrtClose(File))Ok=false;
    xvalue* Settings=Ok?xrtJsonParse(xrtStrViewN(Text,(size_t)Bytes)):NULL;xrtFree(Text);
    Ok=Settings&&xrtValueType(Settings)==XVALUE_OBJECT;
    for(size_t I=0;Ok&&I<xrtValueCount(Settings);I++){bool Flag=false;Ok=xrtValueGetBool(xrtValueObjectAt(Settings,I,NULL),&Flag);}
    MdoSkillCatalog* Catalog=Ok?MdoSkillsCatalogCreate(Base->Generation):NULL;
    if(Catalog)Catalog->Diagnostics=MdoSkillsDiagnosticsCreate();Ok=Ok&&Catalog&&Catalog->Diagnostics;
    for(size_t I=0;Ok&&I<Base->Count;I++){
        MdoSkillEntry* E=&Base->Entries[I];bool Enabled=true;const xvalue* Flag=xrtValueObjectGet(Settings,xrtStrView(E->Id));
        if(Flag)xrtValueGetBool(Flag,&Enabled);if(!Enabled)continue;
        MdoSkillSource S={0};S.External=E->External;S.Id=E->Id;S.Scope=E->Scope;S.NativeRoot=E->NativeRoot;
        S.DirectoryId=E->DirectoryId;S.EntryFile=E->EntryFile;S.VirtualDirectory=E->VirtualDirectory;S.RelativeDirectory=E->RelativeDirectory;
        Ok=MdoSkillsScopedPut(Catalog,&S,true);
    }
    xrtValueRelease(Settings);MdoSkillCatalogRelease(Base);
    if(!Ok){MdoSkillCatalogRelease(Catalog);MdoSkillsSetError("Cannot read project Skill selection; only installed Home directories can be selected");return NULL;}
    if(Catalog->Count>1)qsort(Catalog->Entries,Catalog->Count,sizeof(*Catalog->Entries),MdoSkillsEntryCompare);return Catalog;
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
    if(Entry&&!MdoSkillsInstalledAvailable(Entry))return NULL;
    char *Path=NULL,*Text=NULL;size_t Got=0;xfileinfo Info={0};
    xfile File=Entry?MdoSkillsOpenEntry(Entry,Entry->EntryFile,&Path):NULL;
    bool Ok=File&&xrtFileStat(File,&Info)&&MdoSkillsSameFile(&Entry->DocumentInfo,&Info);
    if(Ok)Text=xrtMalloc((size_t)Info.Size+1);
    Ok=Ok&&Text&&xrtReadAtFull(File,0,Text,(size_t)Info.Size,&Got)&&Got==Info.Size;
    if(Ok){Text[Got]=0;Ok=!memchr(Text,0,Got)&&xrtUtf8Valid(xrtStrViewN(Text,Got),NULL);}
    xfileinfo After={0};
    if(Ok)Ok=xrtFileStat(File,&After)&&MdoSkillsSameFile(&Info,&After);
    if(File&&!xrtClose(File))Ok=false;
    xrtFree(Path);if(!Ok){xrtFree(Text);return NULL;}return Text;
}
