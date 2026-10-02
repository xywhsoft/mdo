/* Copied into a small real xs/TCC site. Hooks alter only that copy. A single
 * delayed writer exposes the POSIX process-scoped file-lock boundary without
 * stress, model calls, or timing-dependent many-writer contention. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xsbase.h>

static bool BindingProbeWriteCheckpoint(void);
static bool BindingProbeClose(xfile File);
#include "src/storage/home.c"
#include "src/projects/lifecycle.c"
#include "src/projects/manager.c"
#include "src/projects/binding.c"

static xatomic32 g_BindingHold, g_BindingReady, g_BindingStop;
static bool g_BindingCloseFault, g_BindingThreadOk, g_BindingUnitAllowed = true;
static unsigned g_BindingChecks, g_BindingCalls;

#define BINDING_CHECK(Condition) do { ++g_BindingChecks; if ( !(Condition) ) { \
    printf("binding_failure=%d\n", __LINE__); goto done; } } while (0)

static void BindingProbeOptions(MdoProjectCreateOptions* Options, cstr Id, cstr Workspace)
{
    MdoProjectCreateOptionsInit(Options);
    Options->Id = Id; Options->Name = Id; Options->WorkspaceRoot = Workspace;
}

static bool BindingProbeWriteCheckpoint(void)
{
    xdeadline Deadline;
    if ( !xrtAtomic32Load(&g_BindingHold, XMEMORY_ACQUIRE) ) return true;
    Deadline = xrtDeadlineAfter(3000000u);
    xrtAtomic32Store(&g_BindingReady, 1u, XMEMORY_RELEASE);
    while ( !xrtAtomic32Load(&g_BindingStop, XMEMORY_ACQUIRE) ) {
        if ( xrtDeadlineExpired(Deadline) ) return false;
        xrtSleep(1000u);
    }
    return true;
}

static bool BindingProbeClose(xfile File)
{
    bool Ok = xrtClose(File);
    if ( g_BindingCloseFault ) { g_BindingCloseFault = false; return false; }
    return Ok;
}

static int32 BindingProbeWriter(ptr Data)
{
    MdoProjectCreateOptions Options; xwork_error Error;
    (void)Data;
    BindingProbeOptions(&Options, "pending", "workspaces/other");
    g_BindingThreadOk = MdoProjectCreate(&Options, NULL, &Error);
    return 0;
}

static bool BindingProbeCommit(const MdoProjectBinding* Current, void* Data, xwork_error* Error)
{
    MdoProjectCreateOptions Options; MdoProjectInfo Info = {0};
    bool Found, Ok;
    ++g_BindingCalls;
    Info.Size = sizeof(Info);
    BindingProbeOptions(&Options, Current->ProjectId, Current->WorkspaceRoot);
    Ok = MdoProjectGet(Current->ProjectId, &Info, &Found, Error) && Found &&
        Info.Revision == Current->Revision && Info.CreatedAt == Current->CreatedAt &&
        MdoProjectReplace(&Options, Current->Revision, NULL, NULL) == MDO_PROJECT_MUTATION_BUSY &&
        MdoProjectUnregister(Current->ProjectId, Current->Revision, NULL) == MDO_PROJECT_MUTATION_BUSY;
    BindingProbeOptions(&Options, "callback-new", Current->WorkspaceRoot);
    Ok = Ok && !MdoProjectCreate(&Options, NULL, Error) && Error->eCode == XWORK_ERROR_CONTEXT;
    if ( Data != NULL ) { xworkErrorInit(Error); Error->eCode = XWORK_ERROR_IO; return false; }
    xworkErrorInit(Error);
    return Ok && MdoHomeAtomicWrite("data/binding-commit.txt", "committed", 9u, true);
}

void ServiceInit(XS_HostInfo* Host)
{
    MdoProjectBinding Default = {0}, Binding = {0}, Changed = {0}, Bad, Before;
    MdoProjectCreateOptions Options;
    MdoProjectInfo Info = {0};
    MdoProjectLease *Owner = NULL, *Other = NULL, *OldOwner = NULL;
    MdoProjectDefinitionLease *Guard = NULL, *OldGuard = NULL, *Second = NULL;
    xwork_error Error;
    xroot Site = NULL, OldRoot = NULL;
    xthread* Thread = NULL;
    xdeadline Deadline;
    unsigned Calls;
    bool Found, Ok = false;
    (void)Host;
    xrtAtomic32Init(&g_BindingHold, 0u); xrtAtomic32Init(&g_BindingReady, 0u); xrtAtomic32Init(&g_BindingStop, 0u);
    BINDING_CHECK(MdoHomeInit() && MdoProjectLifecycleInit());
    Default.Size = sizeof(Default); Binding.Size = sizeof(Binding); Changed.Size = sizeof(Changed);
    BINDING_CHECK(MdoProjectBindingGet("default", &Default, &Error) && Default.Revision == 0u && Default.CreatedAt == 0);
    BINDING_CHECK(Default.WorkspaceIdentity.Type == XFILE_TYPE_DIRECTORY && xrtPathIsAbs(Default.WorkspaceRoot));
    memset(&Bad, 0x31, sizeof(Bad)); Bad.Size = sizeof(Bad) - 1u; Before = Bad;
    BINDING_CHECK(!MdoProjectBindingGet("default", &Bad, &Error) && memcmp(&Bad, &Before, sizeof(Bad)) == 0);
    BINDING_CHECK(!MdoProjectBindingGet("missing", &Binding, &Error) && Error.eCode == XWORK_ERROR_CONTEXT &&
        Binding.Size == sizeof(Binding) && Binding.ProjectId[0] == '\0');
    BINDING_CHECK(!MdoProjectBindingGet("../bad", &Binding, &Error));

    /* Busy writers and invalid/foreign owners must not materialize Home. */
    Owner = MdoProjectLeaseAcquire("default", MDO_PROJECT_LEASE_SHARED, &Error);
    Guard = MdoProjectDefinitionAcquire(Owner, &Error);
    BINDING_CHECK(Owner != NULL && Guard != NULL);
    BindingProbeOptions(&Options, "new", "workspaces/other");
    BINDING_CHECK(!MdoProjectCreate(&Options, NULL, &Error) && Error.eCode == XWORK_ERROR_CONTEXT);
    BINDING_CHECK(!MdoProjectWithBinding(&Default, Owner, BindingProbeCommit, NULL, &Error) && g_BindingCalls == 0u);
    MdoProjectDefinitionRelease(Guard); Guard = NULL;
    Other = MdoProjectLeaseAcquire("other", MDO_PROJECT_LEASE_SHARED, &Error);
    BINDING_CHECK(Other != NULL && !MdoProjectWithBinding(&Default, Other, BindingProbeCommit, NULL, &Error) && g_BindingCalls == 0u);
    BINDING_CHECK(!MdoProjectWithBinding(&Bad, Owner, BindingProbeCommit, NULL, &Error));
    if ( strcmp(getenv("MDO_BINDING_MODE"), "readonly") == 0 ) { Ok = true; goto done; }

    MdoProjectLeaseRelease(Other); Other = NULL;
    MdoProjectLeaseRelease(Owner); Owner = NULL;
    Info.Size = sizeof(Info);
    BindingProbeOptions(&Options, "alpha", "workspaces/空间 alpha");
    BINDING_CHECK(MdoProjectCreate(&Options, &Info, &Error) && Info.Revision == 1u);
    printf("binding_app_path=%s\n", xsAppPath());
    BINDING_CHECK(MdoProjectBindingGet("alpha", &Binding, &Error) && Binding.Revision == 1u && Binding.CreatedAt == Info.CreatedAt);
    BINDING_CHECK(strstr(Binding.WorkspaceRoot, "空间 alpha") != NULL);
    Owner = MdoProjectLeaseAcquire("alpha", MDO_PROJECT_LEASE_SHARED, &Error);
    BINDING_CHECK(Owner != NULL);
    BindingProbeOptions(&Options, "other", "workspaces/other");
    BINDING_CHECK(MdoProjectCreate(&Options, NULL, &Error));
    Guard = MdoProjectDefinitionAcquire(Owner, &Error);
    BINDING_CHECK(Guard != NULL && MdoProjectReplace(&Options, 1u, NULL, NULL) == MDO_PROJECT_MUTATION_BUSY &&
        MdoProjectUnregister("other", 1u, NULL) == MDO_PROJECT_MUTATION_BUSY);
    MdoProjectDefinitionRelease(Guard); Guard = NULL;

    /* Hold a real manager writer after native lock, before atomic publication. */
    xrtAtomic32Store(&g_BindingHold, 1u, XMEMORY_RELEASE);
    Thread = xrtThreadCreate(BindingProbeWriter, NULL, 0u);
    BINDING_CHECK(Thread != NULL);
    Deadline = xrtDeadlineAfter(3000000u);
    while ( !xrtAtomic32Load(&g_BindingReady, XMEMORY_ACQUIRE) && !xrtDeadlineExpired(Deadline) ) xrtSleep(1000u);
    BINDING_CHECK(xrtAtomic32Load(&g_BindingReady, XMEMORY_ACQUIRE) != 0u);
    Info.Size = sizeof(Info);
    BINDING_CHECK(MdoProjectGet("pending", &Info, &Found, &Error) && !Found);
    BINDING_CHECK(MdoProjectReplace(&Options, 1u, NULL, NULL) == MDO_PROJECT_MUTATION_BUSY &&
        MdoProjectUnregister("alpha", 1u, &Error) == MDO_PROJECT_MUTATION_BUSY);
    BindingProbeOptions(&Options, "contender", "workspaces/other");
    BINDING_CHECK(!MdoProjectCreate(&Options, NULL, &Error) && Error.eCode == XWORK_ERROR_CONTEXT);
    BINDING_CHECK(!MdoProjectWithBinding(&Binding, Owner, BindingProbeCommit, NULL, &Error) && g_BindingCalls == 0u);
    xrtAtomic32Store(&g_BindingStop, 1u, XMEMORY_RELEASE);
    BINDING_CHECK(xrtThreadWaitFor(Thread, 3000000u) == XWAIT_OK && g_BindingThreadOk);
    xrtThreadDestroy(Thread); Thread = NULL;
    xrtAtomic32Store(&g_BindingHold, 0u, XMEMORY_RELEASE);
    BINDING_CHECK(MdoProjectCreate(&Options, NULL, &Error));

    BINDING_CHECK(MdoProjectWithBinding(&Binding, Owner, BindingProbeCommit, NULL, &Error) && g_BindingCalls == 1u);
    BINDING_CHECK(!MdoProjectWithBinding(&Binding, Owner, BindingProbeCommit, &Info, &Error) &&
        Error.eCode == XWORK_ERROR_IO && g_BindingCalls == 2u);
    BINDING_CHECK(MdoProjectWithBinding(&Binding, Owner, BindingProbeCommit, NULL, &Error) && g_BindingCalls == 3u);
    /* Closing the native lock cannot undo an already executed callback. */
    g_BindingCloseFault = true;
    BINDING_CHECK(!MdoProjectWithBinding(&Binding, Owner, BindingProbeCommit, NULL, &Error) &&
        Error.eCode == XWORK_ERROR_IO && g_BindingCalls == 4u && !g_BindingCloseFault);
    BINDING_CHECK(MdoProjectWithBinding(&Binding, Owner, BindingProbeCommit, NULL, &Error) && g_BindingCalls == 5u);
    Calls = g_BindingCalls;
    BindingProbeOptions(&Options, "alpha", Binding.WorkspaceRoot);
    BINDING_CHECK(MdoProjectReplace(&Options, 1u, &Info, &Error) == MDO_PROJECT_MUTATION_OK && Info.Revision == 2u);
    BINDING_CHECK(!MdoProjectWithBinding(&Binding, Owner, BindingProbeCommit, NULL, &Error) && g_BindingCalls == Calls);
    BINDING_CHECK(MdoProjectBindingGet("alpha", &Changed, &Error));
    BINDING_CHECK(MdoProjectUnregister("alpha", 2u, &Error) == MDO_PROJECT_MUTATION_OK);
    xrtSleep(1000u);
    BINDING_CHECK(MdoProjectCreate(&Options, &Info, &Error) && Info.Revision == 1u && Info.CreatedAt != Binding.CreatedAt);
    BINDING_CHECK(!MdoProjectWithBinding(&Binding, Owner, BindingProbeCommit, NULL, &Error) && g_BindingCalls == Calls);
    BINDING_CHECK(!MdoProjectWithBinding(&Changed, Owner, BindingProbeCommit, NULL, &Error) && g_BindingCalls == Calls);
    BINDING_CHECK(MdoProjectBindingGet("alpha", &Binding, &Error));

    /* Same configured path, different physical directory: keep old identity
     * pinned while replacing the fixture directory, preventing inode reuse. */
    Site = xrtRootOpen(xsAppPath()); OldRoot = xrtRootOpen(Binding.WorkspaceRoot);
    BINDING_CHECK(Site != NULL && OldRoot != NULL &&
        xrtRootRenameNoReplace(Site, "workspaces/空间 alpha", "workspaces/alpha-old") &&
        xrtRootDirCreate(Site, "workspaces/空间 alpha", 0700u));
    BINDING_CHECK(!MdoProjectWithBinding(&Binding, Owner, BindingProbeCommit, NULL, &Error) && g_BindingCalls == Calls);
    BINDING_CHECK(MdoProjectBindingGet("alpha", &Changed, &Error) &&
        (Changed.WorkspaceIdentity.Identity != Binding.WorkspaceIdentity.Identity ||
         Changed.WorkspaceIdentity.Device != Binding.WorkspaceIdentity.Device));
    BINDING_CHECK(MdoProjectWithBinding(&Changed, Owner, BindingProbeCommit, NULL, &Error));
    BINDING_CHECK(xrtRootClose(OldRoot)); OldRoot = NULL;
    BINDING_CHECK(xrtRootClose(Site)); Site = NULL;

    BindingProbeOptions(&Options, "absolute", Changed.WorkspaceRoot);
    BINDING_CHECK(MdoProjectCreate(&Options, NULL, &Error) && MdoProjectBindingGet("absolute", &Binding, &Error) &&
        strcmp(Binding.WorkspaceRoot, Changed.WorkspaceRoot) == 0);
    BindingProbeOptions(&Options, "missing-root", "workspaces/absent");
    BINDING_CHECK(MdoProjectCreate(&Options, NULL, &Error) && !MdoProjectBindingGet("missing-root", &Binding, &Error) &&
        Error.eCode == XWORK_ERROR_IO && Binding.WorkspaceRoot[0] == '\0');
    BindingProbeOptions(&Options, "file-root", "workspaces/not-directory.txt");
    BINDING_CHECK(MdoProjectCreate(&Options, NULL, &Error) && !MdoProjectBindingGet("file-root", &Binding, &Error));

    Other = MdoProjectLeaseAcquire("default", MDO_PROJECT_LEASE_SHARED, &Error);
    BindingProbeOptions(&Options, "default", Changed.WorkspaceRoot);
    BINDING_CHECK(Other != NULL && MdoProjectCreate(&Options, NULL, &Error));
    Calls = g_BindingCalls;
    BINDING_CHECK(!MdoProjectWithBinding(&Default, Other, BindingProbeCommit, NULL, &Error) && g_BindingCalls == Calls);
    MdoProjectLeaseRelease(Other); Other = NULL;

    /* Old guards pin their old registry, not the new registry's writer flag. */
    OldOwner = Owner; Owner = NULL;
    OldGuard = MdoProjectDefinitionAcquire(OldOwner, &Error);
    BINDING_CHECK(OldGuard != NULL);
    MdoProjectLifecycleUnit(); BINDING_CHECK(MdoProjectLifecycleInit());
    BINDING_CHECK(MdoProjectDefinitionAcquire(OldOwner, &Error) == NULL &&
        !MdoProjectLeaseProtects(OldOwner, "alpha", MDO_PROJECT_LEASE_SHARED));
    Owner = MdoProjectLeaseAcquire("alpha", MDO_PROJECT_LEASE_SHARED, &Error);
    Guard = MdoProjectDefinitionAcquire(Owner, &Error);
    BINDING_CHECK(Owner != NULL && Guard != NULL);
    MdoProjectLeaseRelease(OldOwner); OldOwner = NULL; /* guard retains owner */
    MdoProjectDefinitionRelease(OldGuard); OldGuard = NULL;
    Second = MdoProjectDefinitionAcquire(Owner, &Error);
    BINDING_CHECK(Second == NULL && Error.eCode == XWORK_ERROR_CONTEXT);
    MdoProjectDefinitionRelease(Guard); Guard = NULL;
    Guard = MdoProjectDefinitionAcquire(Owner, &Error);
    BINDING_CHECK(Guard != NULL);
    MdoProjectDefinitionRelease(Guard); Guard = NULL;
    Other = MdoProjectLeaseAcquire("exclusive", MDO_PROJECT_LEASE_EXCLUSIVE, &Error);
    BINDING_CHECK(Other != NULL && MdoProjectDefinitionAcquire(Other, &Error) == NULL);
    BINDING_CHECK(MdoProjectBindingGet("alpha", &Changed, &Error));
    (void)MdoHomeRequireRestart("synthetic bounded publication freeze");
    Calls = g_BindingCalls;
    BINDING_CHECK(!MdoProjectWithBinding(&Changed, Owner, BindingProbeCommit, NULL, &Error) && g_BindingCalls == Calls);
    Guard = MdoProjectDefinitionAcquire(Owner, &Error);
    BINDING_CHECK(Guard != NULL); /* failure released the process writer gate */
    Ok = true;
done:
    if ( Thread != NULL ) {
        xrtAtomic32Store(&g_BindingStop, 1u, XMEMORY_RELEASE);
        if ( xrtThreadWaitFor(Thread, 3000000u) != XWAIT_OK ) g_BindingUnitAllowed = false;
        xrtThreadDestroy(Thread);
    }
    if ( OldRoot != NULL ) (void)xrtRootClose(OldRoot);
    if ( Site != NULL ) (void)xrtRootClose(Site);
    MdoProjectDefinitionRelease(Second); MdoProjectDefinitionRelease(Guard); MdoProjectDefinitionRelease(OldGuard);
    MdoProjectLeaseRelease(Owner); MdoProjectLeaseRelease(Other); MdoProjectLeaseRelease(OldOwner);
    printf("probe_ok=%d binding_checks=%u binding_calls=%u\n", Ok, g_BindingChecks, g_BindingCalls); fflush(stdout);
}

void ServiceUnit(XS_HostInfo* Host)
{
    (void)Host;
    if ( g_BindingUnitAllowed ) { MdoProjectLifecycleUnit(); MdoHomeUnit(); }
}
