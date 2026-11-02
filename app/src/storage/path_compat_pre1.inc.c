/* TEMPORARY: remove at 1.0. This is component matching, never a regex replace
 * over a document. Existing absolute locations win, including external projects.
 * Only NOT_FOUND permits rebasing; permission/I/O errors must not redirect I/O. */
#if defined(MDO_VERSION_CODE) && ((MDO_VERSION_CODE & 0x00FFFFFFu) >= 0x00010000u)
#error Remove pre-1.0 Home path compatibility before building mdo 1.0
#endif
static char* MdoPathLegacyResolve(cstr Path)
{
    char* Native = xrtPathAbs(Path);
    xfileinfo Info = {0};
    if ( !Native ) return NULL;
    if ( xrtPathStat(Native, false, &Info) ) return Native;
    const xerror* Error = xrtGetError();
    if ( !Error || xrtErrorKind(Error) != XERR_NOT_FOUND ) { xrtClearError(); return Native; }
    xrtClearError();
    cstr Tail = NULL, Start = Path;
    while ( *Start ) {
        while ( MdoPathSeparator(*Start) ) ++Start;
        cstr End = Start; while ( *End && !MdoPathSeparator(*End) ) ++End;
        if ( (size_t)(End - Start) == 8u ) {
            bool Match = true;
            for ( size_t I = 0; I < 8; ++I ) {
                char C = Start[I];
                if ( C >= 'A' && C <= 'Z' ) C += 'a' - 'A';
                if ( C != "mdo-home"[I] ) Match = false;
            }
            if ( Match ) Tail = *End ? End + 1 : End;
        }
        Start = End;
    }
    if ( !Tail ) return Native;
    char* Relative = xrtStrDup(*Tail ? Tail : ".");
    if ( !Relative ) { xrtFree(Native); return NULL; }
    for ( size_t I = 0; Relative[I]; ++I ) if ( Relative[I] == '\\' ) Relative[I] = '/';
    bool Valid = !strcmp(Relative, ".") || MdoHomePathValid(Relative);
    char* Rebased = Valid ? (!strcmp(Relative, ".") ? xrtStrDup(g_MdoHome.Path) : MdoHomeExternalPath(Relative)) : NULL;
    xrtFree(Relative); xrtFree(Native);
    return Rebased;
}
