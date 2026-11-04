"""Catalog warnings during real session creation, fork and rollback callbacks."""
import argparse
from pathlib import Path
import tempfile

from test_session_runtime import ROOT, write_site, run_probe


SOURCE = r'''
#include <xsbase.h>
#include <stdio.h>
#define ServiceInit ProductServiceInit
#define ServiceUnit ProductServiceUnit
#include "generated/mdo_unity.c"
#undef ServiceInit
#undef ServiceUnit

typedef struct Owner { const char* Project; const char* Session; bool Residue; } Owner;
static bool Ok=true;
static size_t ExpectedDiagnostics, Retains, Releases, Captures;
static bool Inspect(void) {
    xwork_error Error;
    MdoSessionCatalog* C=MdoSessionCatalogSnapshot(&Error);
    bool Valid=C&&MdoSessionCatalogDiagnosticCount(C)==ExpectedDiagnostics;
    if(!Valid)printf("unexpected_diagnostics=%zu expected=%zu\n",MdoSessionCatalogDiagnosticCount(C),ExpectedDiagnostics);
    MdoSessionCatalogRelease(C);Ok=Ok&&Valid;return Valid;
}
static bool Retain(void* Value) { (void)Value;++Retains;Inspect();return true; }
static void Release(void* Value) {
    Owner* O=Value;++Releases;Inspect();
    if(O->Residue){char Path[512];snprintf(Path,sizeof(Path),"sessions/%s/%s/keep.txt",O->Project,O->Session);
        Ok=MdoHomeAtomicWrite(Path,"preserve",8,false)&&Ok;}
}
static void Capture(cstr Project,cstr Session,cstr Workspace) {
    (void)Project;(void)Session;(void)Workspace;++Captures;Inspect();
}
static MdoSession* Create(Owner* O,bool Fail) {
    MdoSessionCreateOptions C;MdoSessionCreateOptionsInit(&C);
    C.ProjectId=O->Project;C.RequestedId=O->Session;C.Title="Publication probe";
    C.Agent.WorkspaceRoot=".";C.Agent.OwnerUserData=O;C.Agent.OnOwnerRetain=Retain;C.Agent.OnOwnerRelease=Release;
    if(Fail)C.Agent.AgentId="missing.agent";
    xwork_error E;MdoSession* S=MdoSessionCreate(&C,&E);
    if(!S&&!Fail)printf("create_error=%s\n",E.sMessage);
    Ok=Ok&&(Fail?!S:S!=NULL);return S;
}
static void Count(size_t N) {
    xwork_error E;MdoSessionCatalog* C=MdoSessionCatalogSnapshot(&E);
    Ok=Ok&&C&&MdoSessionCatalogCount(C)==N&&MdoSessionCatalogDiagnosticCount(C)==ExpectedDiagnostics;
    MdoSessionCatalogRelease(C);
}
void ServiceInit(XS_HostInfo* Host) {
    (void)Host;xwork_error E;xwork_runtime_config RC;xwork_runtime* R=NULL;
    MdoSession *S=NULL,*F=NULL,*Other=NULL,*Retry=NULL;char* Meta=NULL;size_t Bytes=0;
    Owner A={"publication","source",false},B={"publication","fork",false},
        Bad={"publication","failed-create",false},OtherOwner={"other-publication","source",false},
        Residual={"publication","failed-residue",true};
    if(!MdoHomeInit()||!MdoExtensionsInit()||!MdoExtensionsSeedBuiltins((char[512]){0})||
       !MdoProjectLifecycleInit()||!MdoConfigInit()||!MdoModelManagerInit()||!MdoSkillManagerInit()||!MdoApprovalManagerInit())goto done;
    xworkRuntimeConfigInit(&RC);R=xworkRuntimeCreate(&RC,&E);
    if(!R||!MdoMemoryManagerInit(R)||!MdoModuleManagerInit(R)||!MdoSessionManagerInit(R))goto done;
    MdoSessionsWorkspaceCaptureSet(Capture);
    S=Create(&A,false);if(!S)goto done;Count(1);
    MdoSessionForkOptions Fork;MdoSessionForkOptionsInit(&Fork);Fork.RequestedId=B.Session;Fork.ThroughSequence=0;
    Fork.Runtime.OwnerUserData=&B;Fork.Runtime.OnOwnerRetain=Retain;Fork.Runtime.OnOwnerRelease=Release;
    F=MdoSessionFork(S,&Fork,&E);if(!F){printf("fork_error=%s\n",E.sMessage);goto done;}Count(2);
    MdoSession* Failed=Create(&Bad,true);MdoSessionRelease(Failed);Count(2);
    /* Genuine corruption and orphan directories remain diagnostic entries. */
    Ok=MdoHomeAtomicWrite("sessions/publication/broken/meta.json","{}",2,false)&&Ok;
    Ok=MdoHomeCreateDirectory("sessions/publication/orphan")&&Ok;
    ExpectedDiagnostics=2;Inspect();
    /* An active runtime is not an excuse to hide a damaged published session. */
    Ok=MdoSessionsReadBounded("sessions/publication/source/meta.json",65536,&Meta,&Bytes)&&Ok;
    Ok=MdoHomeAtomicWrite("sessions/publication/source/meta.json","{}",2,true)&&Ok;
    ExpectedDiagnostics=3;Inspect();Count(1);
    /* A pending directory with the same ID in another project cannot suppress
     * diagnostics for this published, damaged conversation. */
    Other=Create(&OtherOwner,false);if(!Other)goto done;Count(2);
    Ok=MdoHomeAtomicWrite("sessions/publication/source/meta.json",Meta,Bytes,true)&&Ok;
    ExpectedDiagnostics=2;Inspect();Count(3);
    /* A failed operation leaves no stale in-memory exemption on a later retry. */
    Retry=Create(&Bad,false);if(!Retry)goto done;Count(4);
    Failed=Create(&Residual,true);MdoSessionRelease(Failed);
    /* Rollback must retain unexpected files. Once it finishes, this genuinely
     * incomplete directory must become visible to diagnostics again. */
    ExpectedDiagnostics=3;Inspect();Count(4);
    Ok=Ok&&Retains>=6&&Captures>=6;
    printf("publication_diagnostics=%u retains=%zu captures=%zu\n",Ok,Retains,Captures);
done:
    MdoSessionsWorkspaceCaptureSet(NULL);xrtFree(Meta);
    MdoSessionRelease(Retry);MdoSessionRelease(Other);MdoSessionRelease(F);MdoSessionRelease(S);
    if(g_MdoSessions.Initialized)Ok=Inspect()&&Ok;
    Ok=Ok&&Retains==Releases;
    MdoSessionManagerUnit();MdoModuleManagerUnit();MdoMemoryManagerUnit();MdoSkillManagerUnit();
    xworkRuntimeRelease(R);MdoModelManagerUnit();MdoApprovalManagerUnit();MdoConfigUnit();MdoProjectLifecycleUnit();MdoHomeUnit();
    printf("publication_complete=%u probe_done=1\n",Ok&&Retains>=6);
}
void ServiceUnit(XS_HostInfo* Host){(void)Host;}
'''


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / ".build/host/xs.exe")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="session-publication-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        write_site(base / "site")
        (base / "site/probe.c").write_text(SOURCE, encoding="utf-8")
        output = run_probe(args.host.resolve(), base / "site", base / "home")
        assert "publication_diagnostics=1" in output and "publication_complete=1" in output, output
    print("PASS creation, fork, preparation, rollback, retry and genuine session diagnostics")
