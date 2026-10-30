/* Anchored primitives shared by current Home purge and restore. */

static bool MdoHomeFsName(xstrview Name, cstr Text)
{
    return Name.Size == strlen(Text) && memcmp(Name.Data, Text, Name.Size) == 0;
}

static bool MdoHomeFsStat(cstr Path, bool* Exists, xfileinfo* Info)
{
    *Exists = false;
    if ( xrtRootStat(g_MdoHome.Root, Path, false, Info) ) {
        *Exists = true;
        return true;
    }
    if ( xrtGetError() != NULL && xrtErrorKind(xrtGetError()) == XERR_NOT_FOUND ) {
        xrtClearError();
        return true;
    }
    return false;
}

static bool MdoHomeFsWrite(cstr Path, const void* Bytes, size_t Size)
{
    xfileoptions Options;
    xfile File;
    bool Ok;
    xrtFileOptionsInit(&Options);
    Options.Flags = XFILE_WRITE | XFILE_CREATE | XFILE_EXCLUSIVE |
        XFILE_NOFOLLOW | XFILE_SYNC;
    Options.Mode = 0600u;
    File = xrtRootFileOpen(g_MdoHome.Root, Path, &Options);
    if ( File == NULL ) return false;
    Ok = xrtWriteFull(File, Bytes, Size, NULL) && xrtFlush(File);
    if ( !xrtClose(File) ) Ok = false;
    return Ok;
}

static bool MdoHomeFsHex(const char* Text, uint64* Value)
{
    size_t i;
    *Value = 0u;
    for ( i = 0u; i < 16u; ++i ) {
        unsigned char Ch = (unsigned char)Text[i];
        if ( Ch >= '0' && Ch <= '9' ) Ch -= '0';
        else if ( Ch >= 'a' && Ch <= 'f' ) Ch = (unsigned char)(Ch - 'a' + 10u);
        else return false;
        *Value = (*Value << 4u) | Ch;
    }
    return true;
}
