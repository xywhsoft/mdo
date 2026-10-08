"""Pause one real run's cleanup to verify completion and shutdown ordering.

The pause wraps destruction only in the disposable TCC probe. This is a
bounded lifecycle test, using a synthetic model callback and no load traffic.
"""
from __future__ import annotations

import argparse
import json
import tempfile
from pathlib import Path

from test_run_manager_runtime import ROOT, PROBE_SOURCE, write_site, run_probe


HOOK = r'''
static void ProbeDestroy(MdoAgentRun* Run);
static bool ProbeGetInfo(MdoAgentRun* Run, MdoAgentRunInfo* Info);
static xwaitresult ProbeShutdownWait(xcond* Cond, xmutex* Lock);
#define MdoAgentRunDestroy ProbeDestroy
#define MdoAgentRunGetInfo ProbeGetInfo
#define xrtCondWait ProbeShutdownWait
#include "src/runs/manager.c"
#undef xrtCondWait
#undef MdoAgentRunGetInfo
#undef MdoAgentRunDestroy
'''

CHECKS = r'''
/* The compact host exports mutex/condition primitives, but not xevent.
 * A manual-reset gate keeps this probe usable with that production host. */
typedef struct ProbeGate { xmutex* Lock; xcond* Changed; bool Signaled; } ProbeGate;
static ProbeGate* ProbeGateCreate(bool Manual, bool Signaled) {
    ProbeGate* gate = (ProbeGate*)calloc(1, sizeof(*gate));
    (void)Manual;
    if (gate == NULL) return NULL;
    gate->Lock = xrtMutexCreate(); gate->Changed = xrtCondCreate(); gate->Signaled = Signaled;
    if (gate->Lock == NULL || gate->Changed == NULL) {
        xrtMutexDestroy(gate->Lock); xrtCondDestroy(gate->Changed); free(gate); return NULL;
    }
    return gate;
}
static bool ProbeGateSet(ProbeGate* Gate) {
    xrtMutexLock(Gate->Lock); Gate->Signaled = true;
    xrtCondBroadcast(Gate->Changed); xrtMutexUnlock(Gate->Lock); return true;
}
static bool ProbeGateReset(ProbeGate* Gate) {
    xrtMutexLock(Gate->Lock); Gate->Signaled = false; xrtMutexUnlock(Gate->Lock); return true;
}
static xwaitresult ProbeGateWaitFor(ProbeGate* Gate, uint64 Timeout) {
    uint64 end = xrtClock() + Timeout;
    xwaitresult result = XWAIT_TIMEOUT;
    xrtMutexLock(Gate->Lock);
    while (!Gate->Signaled) {
        uint64 now = xrtClock();
        if (now >= end) break;
        if (xrtCondWaitFor(Gate->Changed, Gate->Lock, end - now) == XWAIT_ERROR) break;
    }
    if (Gate->Signaled) result = XWAIT_OK;
    xrtMutexUnlock(Gate->Lock); return result;
}
static xwaitresult ProbeGateTryWait(ProbeGate* Gate) { return ProbeGateWaitFor(Gate, 0u); }
static void ProbeGateDestroy(ProbeGate* Gate) {
    if (Gate == NULL) return;
    xrtCondDestroy(Gate->Changed); xrtMutexDestroy(Gate->Lock); free(Gate);
}
static xevent* CleanupEntered;
static xevent* CleanupRelease;
static xevent* ShutdownWaiting;
static xatomic32 PauseCleanup;
static xatomic32 ShutdownDone;
static xatomic32 CleanupTimedOut;
static xatomic32 FinishBeforeStartReturns;

static bool ProbeGetInfo(MdoAgentRun* Run, MdoAgentRunInfo* Info) {
    bool ok = MdoAgentRunGetInfo(Run, Info);
    if (ok && Info->Run.eState != XWORK_RUN_CREATED &&
        xrtAtomic32Load(&FinishBeforeStartReturns, XMEMORY_ACQUIRE)) {
        xwork_run_result result;
        xwork_error error;
        xrtAtomic32Store(&FinishBeforeStartReturns, 0u, XMEMORY_RELEASE);
        memset(&result, 0, sizeof(result));
        (void)MdoAgentRunWait(Run, xrtDeadlineAfter(UINT64_C(5000000)), &result, &error);
        xworkRunResultUnit(&result);
        ok = MdoAgentRunGetInfo(Run, Info);
        printf("finished_before_publish=state:%d\n", (int)Info->Run.eState);
    }
    return ok;
}

static void ProbeDestroy(MdoAgentRun* Run) {
    if (Run != NULL && xrtAtomic32Load(&PauseCleanup, XMEMORY_ACQUIRE)) {
        (void)xrtEventSet(CleanupEntered);
        if (xrtEventWaitFor(CleanupRelease, UINT64_C(10000000)) != XWAIT_OK)
            xrtAtomic32Store(&CleanupTimedOut, 1u, XMEMORY_RELEASE);
    }
    MdoAgentRunDestroy(Run);
}

static xwaitresult ProbeShutdownWait(xcond* Cond, xmutex* Lock) {
    /* Called under the manager lock, immediately before its real wait. */
    (void)xrtEventSet(ShutdownWaiting);
    return xrtCondWait(Cond, Lock);
}

static int32 PumpWorker(ptr Data) {
    unsigned i;
    (void)Data;
    for (i = 0; i < 1000u; ++i) {
        xwork_error error;
        if (!MdoRunManagerPump(NULL, &error)) return 1;
        if (xrtEventTryWait(CleanupEntered) == XWAIT_OK) return 0;
        xrtSleep(5u);
    }
    return 2;
}

static int32 ShutdownWorker(ptr Data) {
    (void)Data;
    MdoRunManagerUnit();
    xrtAtomic32Store(&ShutdownDone, 1u, XMEMORY_RELEASE);
    return 0;
}

static bool GatesInit(void) {
    CleanupEntered = xrtEventCreate(true, false);
    CleanupRelease = xrtEventCreate(true, false);
    ShutdownWaiting = xrtEventCreate(true, false);
    xrtAtomic32Init(&PauseCleanup, 0u);
    xrtAtomic32Init(&ShutdownDone, 0u);
    xrtAtomic32Init(&CleanupTimedOut, 0u);
    xrtAtomic32Init(&FinishBeforeStartReturns, 1u);
    return CleanupEntered != NULL && CleanupRelease != NULL && ShutdownWaiting != NULL;
}

static xthread* HoldCleanup(void) {
    xthread* worker;
    (void)xrtEventReset(CleanupEntered);
    (void)xrtEventReset(CleanupRelease);
    xrtAtomic32Store(&PauseCleanup, 1u, XMEMORY_RELEASE);
    worker = xrtThreadCreate(PumpWorker, NULL, 0u);
    if (worker == NULL) return NULL;
    if (xrtEventWaitFor(CleanupEntered, UINT64_C(5000000)) == XWAIT_OK)
        return worker;
    (void)xrtEventSet(CleanupRelease);
    (void)xrtThreadWait(worker); xrtThreadDestroy(worker);
    return NULL;
}

static void ReleaseCleanup(xthread* Worker) {
    (void)xrtEventSet(CleanupRelease);
    (void)xrtThreadWait(Worker); xrtThreadDestroy(Worker);
    xrtAtomic32Store(&PauseCleanup, 0u, XMEMORY_RELEASE);
}

static bool CompletionBarrier(const char* Id, const MdoRunStartOptions* Start) {
    xthread* worker = HoldCleanup();
    MdoRunSnapshot* snapshot;
    MdoRunInfo info, cancelled, refused;
    MdoRunManagerStatus status;
    xwork_error error;
    size_t completed = 99u;
    bool may_execute = true, started, cancel_ok, pump_ok, found;
    if (worker == NULL) { printf("gate_error=completion\n"); return false; }
    memset(&info, 0, sizeof(info)); info.Size = sizeof(info);
    memset(&cancelled, 0, sizeof(cancelled)); cancelled.Size = sizeof(cancelled);
    memset(&refused, 0, sizeof(refused)); refused.Size = sizeof(refused);
    memset(&status, 0, sizeof(status)); status.Size = sizeof(status);
    snapshot = MdoRunSnapshotCreate(&error);
    found = snapshot != NULL && MdoRunSnapshotFind(snapshot, Id, &info);
    MdoRunSnapshotRelease(snapshot);
    (void)MdoRunManagerGetStatus(&status);
    pump_ok = MdoRunManagerPump(&completed, &error);
    cancel_ok = MdoRunCancel(Id, &cancelled, &error);
    started = MdoRunStartWithOutcome(Start, &refused, &error, &may_execute);
    printf("cleanup_held=found:%d terminal:%d state:%d active:%zu completed:%llu\n",
        found, info.Terminal, (int)info.State, status.ActiveRuns,
        (unsigned long long)status.RunsCompleted);
    printf("cleanup_reentry=pump:%d completed:%zu cancel:%d requested:%d premature_start:%d may_execute:%d\n",
        pump_ok, completed, cancel_ok, cancelled.CancelRequested, started, may_execute);
    ReleaseCleanup(worker);
    /* No delay/retry between completion and the caller's next start. */
    return found;
}

static bool ShutdownBarrier(const MdoRunStartOptions* Start) {
    xthread* pump = NULL;
    xthread* shutdown = NULL;
    MdoRunInfo info;
    MdoRunSnapshot* snapshot;
    xwork_error error;
    bool held = false, waiting = false, done_before_release = true;
    memset(&info, 0, sizeof(info)); info.Size = sizeof(info);
    if (!MdoRunStart(Start, &info, &error)) return false;
    pump = HoldCleanup();
    if (pump == NULL) return false;
    snapshot = MdoRunSnapshotCreate(&error);
    if (snapshot != NULL && MdoRunSnapshotFind(snapshot, info.Id, &info))
        held = !info.Terminal && !MdoRunsTerminal(info.State);
    MdoRunSnapshotRelease(snapshot);
    /* The old implementation frees the manager while this pump is paused.
     * Avoid that destructive baseline path; the completion check exposes it. */
    if (held) {
        shutdown = xrtThreadCreate(ShutdownWorker, NULL, 0u);
        if (shutdown != NULL) {
            waiting = xrtEventWaitFor(ShutdownWaiting, UINT64_C(5000000)) == XWAIT_OK;
            done_before_release = xrtAtomic32Load(&ShutdownDone, XMEMORY_ACQUIRE) != 0u;
        }
    }
    ReleaseCleanup(pump);
    if (shutdown != NULL) {
        (void)xrtThreadWait(shutdown); xrtThreadDestroy(shutdown);
    }
    printf("shutdown_held=waiting:%d done_before_release:%d done_after_release:%u\n",
        waiting, done_before_release, xrtAtomic32Load(&ShutdownDone, XMEMORY_ACQUIRE));
    return held && waiting && !done_before_release && shutdown != NULL;
}

static bool EntryMovementBarrier(xwork_runtime* Runtime,
    const MdoRunManagerOptions* Options, const MdoSessionCreateOptions* Create,
    const MdoRunStartOptions* Start) {
    MdoRunManagerOptions options = *Options;
    MdoRunStartOptions other = *Start;
    MdoRunInfo warmup, held, added, info;
    MdoSessionInfo session_info;
    MdoSession* session;
    MdoRunSnapshot* snapshot;
    MdoRunManagerStatus status;
    xwork_error error;
    xthread* pump;
    char other_id[MDO_SESSION_ID_CAPACITY];
    bool admitted, evicted, protected_entry, finished = false;
    MdoRunManagerUnit();
    options.MaxActive = 2u; options.MaxRetained = 2u;
    if (!MdoRunManagerInit(Runtime, &options, &error)) return false;
    session = MdoSessionCreate(Create, &error);
    if (session == NULL) return false;
    memset(&session_info, 0, sizeof(session_info)); session_info.Size = sizeof(session_info);
    if (!MdoSessionGetInfo(session, &session_info)) { MdoSessionRelease(session); return false; }
    snprintf(other_id, sizeof(other_id), "%s", session_info.Id);
    MdoSessionRelease(session);
    memset(&warmup, 0, sizeof(warmup)); warmup.Size = sizeof(warmup);
    memset(&held, 0, sizeof(held)); held.Size = sizeof(held);
    memset(&added, 0, sizeof(added)); added.Size = sizeof(added);
    if (!MdoRunStart(Start, &warmup, &error) || !WaitForTerminal(warmup.Id, &info) ||
        !MdoRunStart(Start, &held, &error)) return false;
    pump = HoldCleanup();
    if (pump == NULL) return false;
    other.SessionId = other_id; other.Prompt = "other session during cleanup";
    admitted = MdoRunStart(&other, &added, &error);
    snapshot = MdoRunSnapshotCreate(&error);
    memset(&info, 0, sizeof(info)); info.Size = sizeof(info);
    evicted = snapshot != NULL && !MdoRunSnapshotFind(snapshot, warmup.Id, &info);
    protected_entry = snapshot != NULL && MdoRunSnapshotFind(snapshot, held.Id, &info) && !info.Terminal;
    MdoRunSnapshotRelease(snapshot);
    ReleaseCleanup(pump);
    if (admitted) finished = WaitForTerminal(held.Id, &info) && info.State == XWORK_RUN_SUCCEEDED &&
        WaitForTerminal(added.Id, &info) && info.State == XWORK_RUN_SUCCEEDED;
    memset(&status, 0, sizeof(status)); status.Size = sizeof(status);
    (void)MdoRunManagerGetStatus(&status);
    printf("entry_movement=admitted:%d evicted:%d protected:%d finished:%d active:%zu completed:%llu\n",
        admitted, evicted, protected_entry, finished, status.ActiveRuns,
        (unsigned long long)status.RunsCompleted);
    return admitted && evicted && protected_entry && finished;
}
'''


def probe_source() -> str:
    source = PROBE_SOURCE.replace('#include "src/runs/manager.c"', HOOK)
    checks = CHECKS.replace("xevent", "ProbeGate").replace("xrtEvent", "ProbeGate")
    source = source.replace("void ServiceInit(XS_HostInfo *host) {", checks + "\nvoid ServiceInit(XS_HostInfo *host) {")
    source = source.replace("    (void)host;\n", "    (void)host;\n    if (!GatesInit()) return;\n", 1)
    source = source.replace("    if (!WaitForTerminal(first.Id, &found)) {",
        "    if (!CompletionBarrier(first.Id, &start)) goto done;\n"
        "    if (!WaitForTerminal(first.Id, &found)) {")
    source = source.replace('    printf("first_started=id:',
        '    printf("fast_start=state:%d terminal:%d\\n", (int)first.State, first.Terminal);\n'
        '    printf("first_started=id:', 1)
    source = source.replace("    MdoRunManagerUnit();\n    printf(\"manager_unit=",
        "    if (!EntryMovementBarrier(runtime, &manager, &create, &start)) goto done;\n"
        "    start.Prompt = \"shutdown cleanup prompt\";\n"
        "    (void)ShutdownBarrier(&start);\n"
        "    MdoRunManagerUnit();\n    printf(\"manager_unit=", 1)
    source = source.replace('    printf("probe_done=1\\n");\n', "")
    source = source.replace("    MdoHomeUnit();\n}", "    MdoHomeUnit();\n"
        "    printf(\"cleanup_timeout=%u\\n\", xrtAtomic32Load(&CleanupTimedOut, XMEMORY_ACQUIRE));\n"
        "    ProbeGateDestroy(CleanupEntered); ProbeGateDestroy(CleanupRelease);\n"
        "    ProbeGateDestroy(ShutdownWaiting);\n"
        "    printf(\"probe_done=1\\n\");\n}")
    return source


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / ".build/host/xs.exe")
    parser.add_argument("--record", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="run-completion-", dir=ROOT / ".build") as raw:
        base = Path(raw)
        write_site(base / "site")
        (base / "site/probe.c").write_text(probe_source(), encoding="utf-8")
        output = run_probe(args.host.resolve(), base / "site", base / "home")
    expected = (
        "finished_before_publish=state:2",
        "fast_start=state:1 terminal:0",
        "cleanup_held=found:1 terminal:0 state:1 active:1 completed:0",
        "cleanup_reentry=pump:1 completed:0 cancel:1 requested:0 premature_start:0 may_execute:0",
        "first_done=state:2 result:0 terminal:1 bytes:24 text:interactive-agent-result refs:2",
        "second_done=state:4 result:-2 terminal:1 cancel:1 refs:2",
        "entry_movement=admitted:1 evicted:1 protected:1 finished:1 active:0 completed:3",
        "shutdown_held=waiting:1 done_before_release:0 done_after_release:1",
        "manager_unit=refs:1 balanced:1",
        "cleanup_timeout=0", "probe_done=1",
    )
    passed = all(item in output for item in expected)
    if args.record:
        args.record.parent.mkdir(parents=True, exist_ok=True)
        args.record.write_text(json.dumps({"passed": passed, "expected": expected, "output": output},
            ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    assert passed, output
    print("run completion and shutdown barrier runtime probe: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
