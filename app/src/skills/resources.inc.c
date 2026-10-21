bool MdoSkillTextPage(const void* Text, size_t Size, uint64 Cursor, size_t Limit, xvalue* Result)
{
    const unsigned char* Bytes=Text;
    if (!Text || !Result || memchr(Text,0,Size) || !xrtUtf8Valid((xstrview){Text,Size},NULL)) {
        MdoSkillsSetError("Skill resource is binary or invalid UTF-8; use its native path with an appropriate tool");return false;
    }
    size_t Start=Cursor>Size?Size:(size_t)Cursor;
    while (Start && Start<Size && (Bytes[Start]&0xc0)==0x80) --Start;
    if (!Limit || Limit>48u*1024u) Limit=48u*1024u;
    if (Limit<4) Limit=4;
    size_t End=Size-Start>Limit?Start+Limit:Size;
    while (End>Start && End<Size && (Bytes[End]&0xc0)==0x80) --End;
    return xrtValueObjectSetNew(Result,XRT_STR_LITERAL("content"),xrtValueString(xrtStrViewN((cstr)Bytes+Start,End-Start))) &&
        xrtValueObjectSetNew(Result,XRT_STR_LITERAL("cursor"),xrtValueUInt(Start)) &&
        xrtValueObjectSetNew(Result,XRT_STR_LITERAL("next_cursor"),xrtValueUInt(End)) &&
        xrtValueObjectSetNew(Result,XRT_STR_LITERAL("eof"),xrtValueBool(End==Size));
}

/* Content-addressed immutable cache for files that only exist inside XRTPACK.
 * Listing never creates it. Native tools need real paths, not VFS locators. */
char* MdoSkillCatalogBaseDirectory(const MdoSkillCatalog* Catalog, cstr Id)
{
    const MdoSkillEntry* Entry=MdoSkillsFind(Catalog,Id);
    if (!Entry) return NULL;
    if (Entry->External) return MdoHomeExternalPath(Entry->RelativeDirectory);
    MdoSkillResourceContent* Files=xrtCalloc(Entry->ResourceCount,sizeof(*Files));
    char *DocumentPath=NULL,*Directory=NULL; bytes Document=NULL;
    xfile File=NULL; uint64 Size=0; size_t Got=0;
    xsha256 Hash;unsigned char Digest[32];char Hex[65],Root[160],Path[1280];
    bool Ok=(!Entry->ResourceCount||Files!=NULL);
    File=Ok?MdoSkillsOpenEntry(Entry,"SKILL.md",&DocumentPath):NULL;
    Ok=File && xrtFileSize(File,&Size) && Size<=MDO_SKILL_TEXT_LIMIT;
    Document=Ok?xrtMalloc((size_t)Size+1):NULL;
    Ok=Ok && Document && xrtReadAtFull(File,0,Document,(size_t)Size,&Got) && Got==Size;
    xrtSha256Init(&Hash);
    Ok=Ok && xrtSha256Update(&Hash,Document,(size_t)Size);
    for(size_t i=0;Ok&&i<Entry->ResourceCount;i++) {
        Files[i].Size=sizeof(Files[i]);
        Ok=MdoSkillCatalogLoadResource(Catalog,Id,Entry->Resources[i].Path,MDO_SKILL_FILE_LIMIT,&Files[i]) &&
            xrtSha256Update(&Hash,Entry->Resources[i].Path,strlen(Entry->Resources[i].Path)+1) &&
            xrtSha256Update(&Hash,&Files[i].Bytes,sizeof(Files[i].Bytes)) &&
            xrtSha256Update(&Hash,Files[i].Data,Files[i].Bytes);
    }
    Ok=Ok && xrtSha256Final(&Hash,Digest);
    if(Ok) {
        for(size_t i=0;i<32;i++)snprintf(Hex+i*2,3,"%02x",Digest[i]);
        snprintf(Root,sizeof(Root),"data/cache/skills/%s",Hex);
        snprintf(Path,sizeof(Path),"%s/SKILL.md",Root);
        Ok=MdoHomeAtomicWrite(Path,Document,(size_t)Size,false);
        for(size_t i=0;Ok&&i<Entry->ResourceCount;i++) {
            snprintf(Path,sizeof(Path),"%s/%s",Root,Entry->Resources[i].Path);
            Ok=MdoHomeAtomicWrite(Path,Files[i].Data,Files[i].Bytes,false);
            if(Ok && Entry->Resources[i].Kind==MDO_SKILL_RESOURCE_SCRIPT) Ok=MdoHomeSetMode(Path,0700);
        }
        if(Ok)Directory=MdoHomeExternalPath(Root);
    }
    for(size_t i=0;Files&&i<Entry->ResourceCount;i++)MdoSkillResourceContentUnit(&Files[i]);
    if(File)xrtClose(File);xrtFree(Files);xrtFree(Document);xrtFree(DocumentPath);
    if(!Directory)MdoSkillsSetError("Skill resources could not be prepared in the local cache");
    return Directory;
}
