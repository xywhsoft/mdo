#include <string.h>

#include "../../include/mdo/power.h"

#if defined(_WIN32) || defined(_WIN64)

__declspec(dllimport) unsigned long __stdcall SetThreadExecutionState(
    unsigned long Flags);
#define MDO_ES_CONTINUOUS 0x80000000ul
#define MDO_ES_SYSTEM_REQUIRED 0x00000001ul

bool MdoPowerInhibitorBegin(MdoPowerInhibitor* Inhibitor)
{
    if ( Inhibitor == NULL ) return false;
    if ( Inhibitor->Active ) return true;
    Inhibitor->Active = SetThreadExecutionState(
        MDO_ES_CONTINUOUS | MDO_ES_SYSTEM_REQUIRED) != 0ul;
    return Inhibitor->Active;
}

void MdoPowerInhibitorEnd(MdoPowerInhibitor* Inhibitor)
{
    if ( Inhibitor == NULL || !Inhibitor->Active ) return;
    (void)SetThreadExecutionState(MDO_ES_CONTINUOUS);
    Inhibitor->Active = false;
}

#elif defined(__linux__)

#if defined(__TINYC__)
/* The bundled TCC SDK intentionally exposes only a small POSIX header set.
 * These stable libc ABI declarations keep the optional systemd adapter
 * available without a build-time libsystemd or full system header tree. */
#include <dlfcn.h>
extern int dup(int Descriptor);
extern int close(int Descriptor);
extern int fcntl(int Descriptor, int Command, ...);
#define F_SETFD 2
#define FD_CLOEXEC 1
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#endif

typedef struct MdoSdBus MdoSdBus;
typedef struct MdoSdMessage MdoSdMessage;

/* libsystemd is optional. Keep the returned descriptor, not the bus or reply:
 * logind releases the inhibitor only after every copy of that fd is closed. */
bool MdoPowerInhibitorBegin(MdoPowerInhibitor* Inhibitor)
{
    void* Library;
    int (*OpenSystem)(MdoSdBus**);
    int (*CallMethod)(MdoSdBus*, const char*, const char*, const char*,
        const char*, void*, MdoSdMessage**, const char*, ...);
    int (*ReadMessage)(MdoSdMessage*, const char*, ...);
    MdoSdMessage* (*UnrefMessage)(MdoSdMessage*);
    MdoSdBus* (*UnrefBus)(MdoSdBus*);
    int (*SetTimeout)(MdoSdBus*, uint64);
    MdoSdBus* Bus = NULL;
    MdoSdMessage* Reply = NULL;
    int Received = -1;
    int Descriptor = -1;
    if ( Inhibitor == NULL ) return false;
    if ( Inhibitor->Active ) return true;
    Library = dlopen("libsystemd.so.0", RTLD_NOW);
    if ( Library == NULL ) return false;
    OpenSystem = dlsym(Library, "sd_bus_open_system");
    CallMethod = dlsym(Library, "sd_bus_call_method");
    ReadMessage = dlsym(Library, "sd_bus_message_read");
    UnrefMessage = dlsym(Library, "sd_bus_message_unref");
    UnrefBus = dlsym(Library, "sd_bus_unref");
    SetTimeout = dlsym(Library, "sd_bus_set_method_call_timeout");
    if ( OpenSystem != NULL && CallMethod != NULL && ReadMessage != NULL &&
         UnrefMessage != NULL && UnrefBus != NULL &&
         OpenSystem(&Bus) >= 0 ) {
        if ( SetTimeout != NULL ) (void)SetTimeout(Bus, UINT64_C(1000000));
        if ( CallMethod(Bus, "org.freedesktop.login1",
            "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
            "Inhibit", NULL, &Reply, "ssss", "sleep", "mdo",
            "Agent task running", "block") >= 0 && Reply != NULL &&
             ReadMessage(Reply, "h", &Received) > 0 && Received >= 0 )
            Descriptor = dup(Received);
        if ( Descriptor >= 0 &&
             fcntl(Descriptor, F_SETFD, FD_CLOEXEC) < 0 ) {
            close(Descriptor);
            Descriptor = -1;
        }
    }
    if ( Reply != NULL ) (void)UnrefMessage(Reply);
    if ( Bus != NULL ) (void)UnrefBus(Bus);
    dlclose(Library);
    if ( Descriptor < 0 ) return false;
    Inhibitor->Descriptor = Descriptor;
    Inhibitor->Active = true;
    return true;
}

void MdoPowerInhibitorEnd(MdoPowerInhibitor* Inhibitor)
{
    if ( Inhibitor == NULL || !Inhibitor->Active ) return;
    close(Inhibitor->Descriptor);
    Inhibitor->Descriptor = -1;
    Inhibitor->Active = false;
}

#elif defined(__APPLE__)

#if defined(__TINYC__)
/* xs supplies dlopen/dlsym/dlclose through its bundled TCC header. */
#else
#include <dlfcn.h>
#endif

typedef const void* MdoCfString;
typedef MdoCfString (*MdoCfStringCreate)(const void*, const char*, uint32);
typedef void (*MdoCfRelease)(const void*);
typedef int32 (*MdoAssertionCreate)(MdoCfString, MdoCfString,
    MdoCfString, MdoCfString, MdoCfString, double, MdoCfString, uint32*);
typedef int32 (*MdoAssertionRelease)(uint32);

bool MdoPowerInhibitorBegin(MdoPowerInhibitor* Inhibitor)
{
    void* Iokit;
    void* CoreFoundation;
    MdoCfStringCreate StringCreate;
    MdoCfRelease StringRelease;
    MdoAssertionCreate Create;
    MdoCfString Type = NULL;
    MdoCfString Name = NULL;
    uint32 Id = 0u;
    bool Ok = false;
    if ( Inhibitor == NULL ) return false;
    if ( Inhibitor->Active ) return true;
    Iokit = dlopen("/System/Library/Frameworks/IOKit.framework/IOKit",
        RTLD_NOW);
    CoreFoundation = dlopen(
        "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation",
        RTLD_NOW);
    if ( Iokit == NULL || CoreFoundation == NULL ) goto done;
    StringCreate = dlsym(CoreFoundation, "CFStringCreateWithCString");
    StringRelease = dlsym(CoreFoundation, "CFRelease");
    Create = dlsym(Iokit, "IOPMAssertionCreateWithDescription");
    if ( StringCreate == NULL || StringRelease == NULL || Create == NULL ||
         dlsym(Iokit, "IOPMAssertionRelease") == NULL ) goto done;
    Type = StringCreate(NULL, "PreventUserIdleSystemSleep", 0x08000100u);
    Name = StringCreate(NULL, "mdo Agent task running", 0x08000100u);
    if ( Type != NULL && Name != NULL )
        Ok = Create(Type, Name, NULL, NULL, NULL, 0.0, NULL, &Id) == 0;
    if ( Type != NULL ) StringRelease(Type);
    if ( Name != NULL ) StringRelease(Name);
done:
    if ( CoreFoundation != NULL ) dlclose(CoreFoundation);
    if ( Ok ) {
        Inhibitor->Iokit = Iokit;
        Inhibitor->AssertionId = Id;
        Inhibitor->Active = true;
    } else if ( Iokit != NULL ) dlclose(Iokit);
    return Ok;
}

void MdoPowerInhibitorEnd(MdoPowerInhibitor* Inhibitor)
{
    MdoAssertionRelease Release;
    if ( Inhibitor == NULL || !Inhibitor->Active ) return;
    Release = dlsym(Inhibitor->Iokit, "IOPMAssertionRelease");
    if ( Release != NULL ) (void)Release(Inhibitor->AssertionId);
    dlclose(Inhibitor->Iokit);
    Inhibitor->Iokit = NULL;
    Inhibitor->AssertionId = 0u;
    Inhibitor->Active = false;
}

#else

bool MdoPowerInhibitorBegin(MdoPowerInhibitor* Inhibitor)
{
    (void)Inhibitor;
    return false;
}

void MdoPowerInhibitorEnd(MdoPowerInhibitor* Inhibitor)
{
    (void)Inhibitor;
}

#endif
