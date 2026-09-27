#include <string.h>

#include "../../include/mdo/config.h"
#include "../../include/mdo/power.h"
#include "../../include/mdo/runs.h"
#include "../../include/mdo/schedules.h"

#define MDO_POWER_POLL_MILLISECONDS 250u
#define MDO_POWER_RETRY_MICROSECONDS UINT64_C(5000000)

typedef struct MdoPowerManagerState {
    xmutex* Lock;
    xthread* Thread;
    MdoPowerManagerStatus Status;
} MdoPowerManagerState;

static MdoPowerManagerState g_MdoPower;

static int32 MdoPowerManagerThread(ptr Data)
{
    MdoPowerInhibitor Inhibitor;
    uint64 RetryAfter = 0u;
    bool Available = g_MdoPower.Status.Available;
    (void)Data;
    memset(&Inhibitor, 0, sizeof(Inhibitor));
    while ( !xrtThreadStopping() ) {
        MdoConfigPowerSettings Settings;
        MdoRunManagerStatus Runs;
        MdoScheduleExecutorSnapshot Schedules;
        bool Configured = false;
        bool Running = false;
        bool Ready;
        memset(&Settings, 0, sizeof(Settings));
        memset(&Runs, 0, sizeof(Runs));
        memset(&Schedules, 0, sizeof(Schedules));
        Settings.Size = sizeof(Settings);
        Runs.Size = sizeof(Runs);
        Schedules.Size = sizeof(Schedules);
        Ready = MdoConfigGetPowerSettings(&Settings) &&
            MdoRunManagerGetStatus(&Runs) &&
            MdoScheduleExecutorGetSnapshot(&Schedules);
        if ( Ready ) {
            Configured = Settings.PreventSleep;
            Running = Runs.ActiveRuns != 0u || Runs.StartingRuns != 0u ||
                Schedules.ActiveRuns != 0u;
        }
        if ( Configured && Running ) {
            if ( !Inhibitor.Active && xrtClock() >= RetryAfter ) {
                Available = MdoPowerInhibitorBegin(&Inhibitor);
                if ( !Available ) RetryAfter = xrtClock() +
                    MDO_POWER_RETRY_MICROSECONDS;
            }
        } else MdoPowerInhibitorEnd(&Inhibitor);
        if ( xrtMutexLock(g_MdoPower.Lock) ) {
            g_MdoPower.Status.Configured = Configured;
            g_MdoPower.Status.Running = Running;
            g_MdoPower.Status.Active = Inhibitor.Active;
            g_MdoPower.Status.Available = Available;
            xrtMutexUnlock(g_MdoPower.Lock);
        }
        xrtSleep(MDO_POWER_POLL_MILLISECONDS);
    }
    MdoPowerInhibitorEnd(&Inhibitor);
    if ( xrtMutexLock(g_MdoPower.Lock) ) {
        g_MdoPower.Status.Active = false;
        xrtMutexUnlock(g_MdoPower.Lock);
    }
    return 0;
}

bool MdoPowerManagerInit(void)
{
    MdoPowerInhibitor Probe;
    if ( g_MdoPower.Thread != NULL ) return true;
    memset(&g_MdoPower, 0, sizeof(g_MdoPower));
    g_MdoPower.Lock = xrtMutexCreate();
    if ( g_MdoPower.Lock == NULL ) return false;
    g_MdoPower.Status.Size = sizeof(g_MdoPower.Status);
    memset(&Probe, 0, sizeof(Probe));
    g_MdoPower.Status.Available = MdoPowerInhibitorBegin(&Probe);
    MdoPowerInhibitorEnd(&Probe);
    g_MdoPower.Status.Checked = true;
    g_MdoPower.Thread = xrtThreadCreate(MdoPowerManagerThread, NULL, 0u);
    if ( g_MdoPower.Thread == NULL ) {
        xrtMutexDestroy(g_MdoPower.Lock);
        memset(&g_MdoPower, 0, sizeof(g_MdoPower));
        return false;
    }
    return true;
}

void MdoPowerManagerUnit(void)
{
    if ( g_MdoPower.Thread != NULL ) {
        (void)xrtThreadStop(g_MdoPower.Thread);
        (void)xrtThreadWait(g_MdoPower.Thread);
        xrtThreadDestroy(g_MdoPower.Thread);
    }
    if ( g_MdoPower.Lock != NULL ) xrtMutexDestroy(g_MdoPower.Lock);
    memset(&g_MdoPower, 0, sizeof(g_MdoPower));
}

bool MdoPowerManagerGetStatus(MdoPowerManagerStatus* Status)
{
    uint32 Size;
    if ( Status == NULL || Status->Size < sizeof(*Status) ||
         g_MdoPower.Lock == NULL || !xrtMutexLock(g_MdoPower.Lock) )
        return false;
    Size = Status->Size;
    *Status = g_MdoPower.Status;
    Status->Size = Size;
    xrtMutexUnlock(g_MdoPower.Lock);
    return true;
}
