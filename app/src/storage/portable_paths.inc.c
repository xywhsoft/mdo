/* Home-relative persistence. Keep native resolution at OS/library boundaries. */
static bool MdoPathSeparator(char C) { return C == '/' || C == '\\'; }
static bool MdoPathCharacterEqual(char A, char B)
{
    if ( MdoPathSeparator(A) && MdoPathSeparator(B) ) return true;
#if defined(_WIN32) || defined(_WIN64)
    if ( A >= 'A' && A <= 'Z' ) A += 'a' - 'A';
    if ( B >= 'A' && B <= 'Z' ) B += 'a' - 'A';
#endif
    return A == B;
}
static char* MdoPathUnderHome(cstr Path)
{
    cstr Root = g_MdoHome.Path;
    if ( !Root || !Path ) return NULL;
    size_t N = strlen(Root), I = 0;
    while ( N && MdoPathSeparator(Root[N - 1]) ) --N;
    while ( I < N && Path[I] && MdoPathCharacterEqual(Root[I], Path[I]) ) ++I;
    if ( I != N || (Path[I] && !MdoPathSeparator(Path[I])) ) return NULL;
    while ( MdoPathSeparator(Path[I]) ) ++I;
    char* Result = xrtStrDup(Path[I] ? Path + I : ".");
    if ( Result ) for ( I = 0; Result[I]; ++I ) if ( Result[I] == '\\' ) Result[I] = '/';
    return Result;
}

/* Delete this leaf and MdoPathMigrate at 1.0; the relative contract remains. */
#include "path_compat_pre1.inc.c"

str MdoPathResolve(cstr Path)
{
    if ( !Path || !Path[0] || !g_MdoHome.Initialized ) return NULL;
    if ( xrtPathIsRooted(Path) ) return MdoPathLegacyResolve(Path);
    if ( !strcmp(Path, ".") ) return xrtStrDup(g_MdoHome.Path);
    char* Relative = MdoPathStore(Path);
    char* Native = Relative ? MdoHomeExternalPath(Relative) : NULL;
    xrtFree(Relative); return Native;
}
str MdoPathStore(cstr Path)
{
    if ( !Path ) return NULL;
    if ( !Path[0] ) return xrtStrDup(""); /* optional workspace override */
    if ( !xrtPathIsRooted(Path) ) {
        char* Relative = xrtStrDup(Path);
        if ( !Relative ) return NULL;
        for ( size_t I = 0; Relative[I]; ++I ) if ( Relative[I] == '\\' ) Relative[I] = '/';
        if ( strcmp(Relative, ".") && !MdoHomePathValid(Relative) ) { xrtFree(Relative); return NULL; }
        return Relative;
    }
    char* Native = xrtPathAbs(Path);
    char* Relative = Native ? MdoPathUnderHome(Native) : NULL;
    if ( !Relative ) {
        xrtFree(Native); Native = MdoPathResolve(Path);
        Relative = Native ? MdoPathUnderHome(Native) : NULL;
    }
    if ( !Relative ) return Native; /* external projects are outside this contract */
    xrtFree(Native);
    if ( strcmp(Relative, ".") && !MdoHomePathValid(Relative) ) {
        xrtFree(Relative); return NULL;
    }
    return Relative;
}
bool MdoPathCopy(cstr Path, char* Output, size_t Capacity)
{
    char* Native = Path && Path[0] ? MdoPathResolve(Path) : xrtStrDup("");
    bool Ok = Native && Output && Capacity && strlen(Native) < Capacity;
    if ( Ok ) memcpy(Output, Native, strlen(Native) + 1);
    xrtFree(Native); return Ok;
}
bool MdoPathField(xvalue* Object, cstr Key, bool Resolve)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    if ( !Value || xrtValueType(Value) == XVALUE_NULL ) return true;
    xstrview Text = {0};
    if ( !xrtValueGetString(Value, &Text) || memchr(Text.Data, 0, Text.Size) ) return false;
    char* Path = Resolve && Text.Size ? MdoPathResolve(Text.Data) : MdoPathStore(Text.Data);
    bool Ok = Path && xrtValueObjectSetNew(Object, xrtStrView(Key), xrtValueString(xrtStrView(Path)));
    xrtFree(Path); return Ok;
}

#include "path_migration_pre1.inc.c"
