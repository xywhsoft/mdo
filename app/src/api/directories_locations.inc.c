#if defined(_WIN32)
/* Keep Windows headers out of the unity TU: other product adapters declare
 * their minimal Win32 ABI too. Load the system DLL without searching cwd. */
typedef struct { uint32 A; uint16 B, C; unsigned char D[8]; } MdoDirectoryFolderId;
typedef intptr_t (__stdcall* MdoDirectoryWinProc)(void);
__declspec(dllimport) void* __stdcall LoadLibraryExA(const char*, void*, unsigned long);
__declspec(dllimport) MdoDirectoryWinProc __stdcall GetProcAddress(void*, const char*);
__declspec(dllimport) int __stdcall FreeLibrary(void*);
__declspec(dllimport) void __stdcall CoTaskMemFree(void*);

static char* MdoDirectoryKnownFolder(const MdoDirectoryFolderId* Id)
{
    typedef long (__stdcall* KnownFolderProc)(const MdoDirectoryFolderId*, unsigned long, void*, uint16**);
    void* Module = LoadLibraryExA("shell32.dll", NULL, 0x800u);
    if (!Module) return NULL;
    MdoDirectoryWinProc Symbol = GetProcAddress(Module, "SHGetKnownFolderPath"); KnownFolderProc GetFolder = NULL;
    if (sizeof(GetFolder) == sizeof(Symbol)) memcpy(&GetFolder, &Symbol, sizeof(GetFolder));
    uint16* Wide = NULL; char* Path = NULL;
    if (GetFolder && GetFolder(Id, 0, NULL, &Wide) >= 0 && Wide) Path = xrtUtf16To8((const uint16*)Wide, NULL);
    if (Wide) CoTaskMemFree(Wide);
    FreeLibrary(Module); return Path;
}
#endif

bool MdoApiDirectoryLocationsRoute(MdoApiContext* Context)
{
    if (Context->Target.Query.Size) return MdoApiReplyError(Context, 400, "invalid_directory_path", "Locations do not take query fields", NULL);
    xvalue *Data = xrtValueObject(), *Items = xrtValueArray();
    char *Home = xrtPathHome(), *Cwd = xrtPathCwd(); xdirroots Roots = {0};
    bool Ok = Data && Items && MdoDirectoryShortcut(Items, "home", Home) && MdoDirectoryShortcut(Items, "cwd", Cwd);
#if defined(_WIN32)
    const struct { const char* Kind; MdoDirectoryFolderId Id; } Known[] = {
        {"desktop", {0xb4bfcc3a,0xdb2c,0x424c,{0xb0,0x29,0x7f,0xe9,0x9a,0x87,0xc6,0x41}}},
        {"documents", {0xfdd39ad0,0x238f,0x46af,{0xad,0xb4,0x6c,0x85,0x48,0x03,0x69,0xc7}}},
        {"downloads", {0x374de290,0x123f,0x4565,{0x91,0x64,0x39,0xc4,0x92,0x5e,0x46,0x7b}}}
    };
    for (size_t I = 0; Ok && I < sizeof(Known)/sizeof(*Known); ++I) {
        char* Path = MdoDirectoryKnownFolder(&Known[I].Id);
        Ok = MdoDirectoryShortcut(Items, Known[I].Kind, Path); xrtFree(Path);
    }
    cstr Platform = "windows";
#else
    const struct { cstr Kind; cstr Environment; cstr Fallback; } Known[] = {
        {"desktop", "XDG_DESKTOP_DIR", "Desktop"}, {"documents", "XDG_DOCUMENTS_DIR", "Documents"}, {"downloads", "XDG_DOWNLOAD_DIR", "Downloads"}
    };
    for (size_t I = 0; Ok && Home && I < sizeof(Known)/sizeof(*Known); ++I) {
        char *Value = xrtEnvGet(Known[I].Environment), *Path = Value && xrtPathIsAbs(Value) ? xrtStrDup(Value) : xrtPathJoin(Home, Known[I].Fallback);
        xfileinfo Info;
        if (Path && xrtPathStat(Path, true, &Info) && Info.Type == XFILE_TYPE_DIRECTORY)
            Ok = MdoDirectoryShortcut(Items, Known[I].Kind, Path);
        xrtFree(Value); xrtFree(Path); xrtClearError();
    }
#if defined(__ANDROID__)
    char* External = xrtEnvGet("EXTERNAL_STORAGE");
    Ok = Ok && MdoDirectoryShortcut(Items, "storage", External && External[0] ? External : "/storage/emulated/0");
    xrtFree(External);
    cstr Platform = "android";
#else
    cstr Platform = "posix";
#endif
#endif
    if (Ok && xrtDirRoots(&Roots))
        for (size_t I = 0; Ok && I < Roots.Count; ++I) Ok = MdoDirectoryShortcut(Items, "root", Roots.Items[I]);
    xrtDirRootsFree(&Roots); xrtClearError();
    Ok = Ok && MdoApiValueSetString(Data, "platform", Platform) && MdoApiValueSetTake(Data, "locations", &Items);
    xrtFree(Home); xrtFree(Cwd); xrtValueRelease(Items);
    if (Ok) return MdoApiReplySuccessTake(Context, 200, Data, NULL);
    xrtValueRelease(Data);
    return MdoApiReplyError(Context, 500, "directory_locations_unavailable", "Device locations could not be read", NULL);
}
