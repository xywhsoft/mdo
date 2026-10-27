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

/* All installed Skills already own a real directory. Explicit import previews
 * may resolve their selected source, but are never runtime-discovered. */
char* MdoSkillCatalogBaseDirectory(const MdoSkillCatalog* Catalog,cstr Id)
{
    const MdoSkillEntry* Entry=MdoSkillsFind(Catalog,Id);
    if(!Entry||!MdoSkillsInstalledAvailable(Entry))return NULL;
    return Entry->NativeRoot?xrtPathJoin(Entry->NativeRoot,Entry->RelativeDirectory):
        Entry->DirectoryId?MdoHomeExternalPath(Entry->RelativeDirectory):NULL;
}
