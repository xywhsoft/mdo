/* Bounded SCM state regression using the real xs lifecycle adapter. No
 * service registration or administrator rights; two gated control threads. */
#include <windows.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
static atomic_bool g_XS_Stop;
static HANDLE Entered, Release, StopRequested;
static SERVICE_STATUS LastStatus;
static bool HoldRunning;
static BOOL MockSetServiceStatus(SERVICE_STATUS_HANDLE Handle, LPSERVICE_STATUS Status)
{
    (void)Handle;
    if (HoldRunning && Status->dwCurrentState == SERVICE_RUNNING) {
        SetEvent(Entered);
        if (WaitForSingleObject(Release, 5000) != WAIT_OBJECT_0) return FALSE;
    }
    LastStatus = *Status;
    return TRUE;
}
static void XS_RequestStop(void)
{
    atomic_store(&g_XS_Stop, true);
    if (StopRequested) SetEvent(StopRequested);
}
#define SetServiceStatus MockSetServiceStatus
#include "src/platform/windows_service.h"
#undef SetServiceStatus
static int XS_MainRun(int Count, char** Args) { (void)Count; (void)Args; return 0; }
static DWORD WINAPI ReadyThread(void* Data) { (void)Data; XS_ServiceReady(); return 0; }
static DWORD WINAPI StopThread(void* Data) { (void)Data; return XS_ServiceControl(SERVICE_CONTROL_STOP, 0, NULL, NULL); }
#define CHECK(Expression) do { if (!(Expression)) { fprintf(stderr,"FAIL line %d\n",__LINE__); return 1; } } while (0)
int main(void)
{
    XS_ServiceHandle = (SERVICE_STATUS_HANDLE)1;
    XS_ServiceReady(); CHECK(LastStatus.dwCurrentState == SERVICE_RUNNING);
    XS_ServiceControl(SERVICE_CONTROL_STOP, 0, NULL, NULL);
    XS_ServiceReport(SERVICE_RUNNING, NO_ERROR);
    CHECK(LastStatus.dwCurrentState == SERVICE_STOP_PENDING);
    atomic_store(&g_XS_Stop, false);
    Entered = CreateEventW(NULL, TRUE, FALSE, NULL);
    Release = CreateEventW(NULL, TRUE, FALSE, NULL);
    StopRequested = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(Entered && Release && StopRequested); HoldRunning = true;
    HANDLE Ready = CreateThread(NULL, 0, ReadyThread, NULL, 0, NULL); CHECK(Ready);
    CHECK(WaitForSingleObject(Entered, 5000) == WAIT_OBJECT_0);
    HANDLE Stop = CreateThread(NULL, 0, StopThread, NULL, 0, NULL); CHECK(Stop);
    CHECK(WaitForSingleObject(StopRequested, 5000) == WAIT_OBJECT_0);
    SetEvent(Release);
    CHECK(WaitForSingleObject(Ready, 5000) == WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(Stop, 5000) == WAIT_OBJECT_0);
    CHECK(LastStatus.dwCurrentState == SERVICE_STOP_PENDING);
    CloseHandle(Ready); CloseHandle(Stop); CloseHandle(Entered); CloseHandle(Release); CloseHandle(StopRequested);
    StopRequested = NULL; XS_ServiceExitCode = 7;
    XS_ServiceReport(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
    CHECK(LastStatus.dwServiceSpecificExitCode == 7);
    puts("Windows service state regression: PASS"); return 0;
}
