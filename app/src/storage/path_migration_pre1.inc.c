/* TEMPORARY: delete at 1.0. Only host-owned records have path semantics here.
 * Never scan/rewrite chat text, model ledgers, extension packages or workspaces.
 * Startup holds Home's exclusive lease, before application writers start.
 * Per-file atomic writes are idempotent, so interrupted upgrades resume safely. */
static bool MdoPathMigrateObject(xvalue* Object, const char* const* Keys, size_t Count, bool* Changed)
{
    for ( size_t I = 0; I < Count; ++I ) {
        xstrview Before = {0};
        const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Keys[I]));
        if ( !Value || xrtValueType(Value) == XVALUE_NULL ) continue;
        if ( !xrtValueGetString(Value, &Before) || memchr(Before.Data, 0, Before.Size) ) return false;
        if ( !xrtPathIsRooted(Before.Data) ) continue; /* includes legacy mdo-home: syntax */
        char* Stored = MdoPathStore(Before.Data);
        if ( !Stored ) return false;
        if ( strcmp(Stored, Before.Data) ) {
            *Changed = true;
            bool Ok = xrtValueObjectSetNew(Object, xrtStrView(Keys[I]), xrtValueString(xrtStrView(Stored)));
            xrtFree(Stored); if ( !Ok ) return false;
        } else xrtFree(Stored);
    }
    return true;
}
static bool MdoPathMigrateFile(cstr Path, const char* const* Keys, size_t Count, unsigned Kind)
{
    bool Exists = false; xfileinfo Info = {0};
    if ( !MdoHomeExternalStat(Path, &Exists, &Info) ) return false;
    if ( !Exists ) return true;
    /* Unknown/corrupt documents belong to their ordinary recovery readers. */
    if ( Info.Type != XFILE_TYPE_FILE || Info.Size > 1048576 ) return true;
    xfile File = MdoHomeOpenRead(Path); size_t Bytes = 0; char* Text = NULL;
    bool Ok = File && MdoHomeReadFileBounded(File, 1048576, &Text, &Bytes);
    if ( File ) xrtClose(File);
    if ( !Ok ) { xrtFree(Text); return false; }
    xjsonreadconfig Config; xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = 1048576; Config.MaxDepth = 24; Config.MaxValues = 50000;
    xvalue* Doc = xrtJsonRead(xrtStrViewN(Text, Bytes), &Config); xrtFree(Text);
    if ( !Doc || xrtValueType(Doc) != XVALUE_OBJECT ) { xrtValueRelease(Doc); xrtClearError(); return true; }
    bool Changed = false;
    if ( Kind == 1 ) {
        const xvalue* Rows = xrtValueObjectGet(Doc, XRT_STR_LITERAL("terminals"));
        static const char* const Fields[] = {"cwd", "program"}, *const Workspace[] = {"workspace"};
        for ( size_t I = 0; Ok && I < xrtValueCount(Rows); ++I ) {
            xvalue* Row = (xvalue*)xrtValueArrayGet(Rows, I);
            Ok = MdoPathMigrateObject(Row, Fields, 2, &Changed);
            xvalue* Context = (xvalue*)xrtValueObjectGet(Row, XRT_STR_LITERAL("context"));
            if ( Ok && Context ) Ok = MdoPathMigrateObject(Context, Workspace, 1, &Changed);
        }
    } else if ( Kind == 2 ) {
        xvalue* Transport = (xvalue*)xrtValueObjectGet(Doc, XRT_STR_LITERAL("transport"));
        static const char* const Fields[] = {"program", "working_directory"};
        if ( Transport ) Ok = MdoPathMigrateObject(Transport, Fields, 2, &Changed);
    } else Ok = MdoPathMigrateObject(Doc, Keys, Count, &Changed);
    if ( Ok && Changed ) {
        Text = xrtJsonStringify(Doc, true, &Bytes);
        Ok = Text && MdoHomeAtomicWrite(Path, Text, Bytes, false); xrtFree(Text);
    }
    xrtValueRelease(Doc); return Ok;
}
static bool MdoPathMigrateTree(cstr Path, unsigned Depth, size_t* Budget, bool Baseline)
{
    bool Exists = false; xfileinfo Info = {0};
    if ( !MdoHomeExternalStat(Path, &Exists, &Info) ) return false;
    if ( !Exists || Info.Type != XFILE_TYPE_DIRECTORY ) return true;
    xdir Directory = MdoHomeOpenDirectory(Path, XDIR_STAT);
    if ( !Directory ) return false;
    bool Ok = true; xdirentry Item; xdirnext Next = XDIR_NEXT_END;
    static const char* const Workspace[] = {"workspace_root"};
    static const char* const Fields[] = {"workspace", "repository"};
    while ( Ok && (Next = xrtDirNext(Directory, &Item)) == XDIR_NEXT_ITEM ) {
        if ( !*Budget ) { Ok = false; break; } --*Budget;
        if ( !(Item.Flags & XDIR_ENTRY_UTF8) || Item.Name.Size > 160 ) continue;
        char Name[161]; memcpy(Name, Item.Name.Data, Item.Name.Size); Name[Item.Name.Size] = 0;
        if ( !strcmp(Name, ".") || !strcmp(Name, "..") || !strcmp(Name, "artifacts") || !strcmp(Name, "images") ) continue;
        char* Child = xrtFormat("%s/%s", Path, Name);
        if ( !Child ) { Ok = false; break; }
        if ( Item.Info.Type == XFILE_TYPE_DIRECTORY && Depth ) Ok = MdoPathMigrateTree(Child, Depth - 1, Budget, Baseline);
        else if ( Item.Info.Type == XFILE_TYPE_FILE ) {
            size_t N = strlen(Name);
            bool Metadata = !strcmp(Name, "meta.json") || !strcmp(Name, "meta.json.bak");
            bool Json = N >= 5 && !strcmp(Name + N - 5, ".json");
            bool Backup = N >= 9 && !strcmp(Name + N - 9, ".json.bak");
            if ( Metadata || (!strcmp(Path, "projects") && (Json || Backup)) ||
                 ((!strcmp(Path, "schedules") || !strcmp(Path, "config/mcp")) && (Json || Backup)) || (Baseline && (Json || Backup)) )
                Ok = MdoPathMigrateFile(Child, Baseline ? Fields : Workspace, Baseline ? 2 : 1, !strcmp(Path, "config/mcp") ? 2 : 0);
        }
        xrtFree(Child);
    }
    if ( Next == XDIR_NEXT_ERROR ) Ok = false;
    if ( !xrtDirClose(Directory) ) Ok = false;
    return Ok;
}
bool MdoPathMigrate(void)
{
    MdoHomeSnapshot Home = {0}; Home.Size = sizeof(Home);
    if ( !MdoHomeGetSnapshot(&Home) ) return false;
    if ( Home.Persistence != MDO_PERSISTENCE_EXTERNAL ) return true;
    size_t Budget = 100000;
    return MdoPathMigrateTree("projects", 0, &Budget, false) &&
        MdoPathMigrateTree("sessions", 2, &Budget, false) &&
        MdoPathMigrateTree("schedules", 0, &Budget, false) &&
        MdoPathMigrateTree("config/mcp", 0, &Budget, false) &&
        MdoPathMigrateTree("workbench-baselines", 2, &Budget, true) &&
        MdoPathMigrateFile("data/application/terminals.json", NULL, 0, true);
}
