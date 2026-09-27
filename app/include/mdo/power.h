#ifndef MDO_POWER_H
#define MDO_POWER_H

#include <xsbase.h>

/* This owner is confined to one thread. On Windows the system sleep request
 * belongs to the calling thread, so Begin and End must run on that thread. */
typedef struct MdoPowerInhibitor {
    bool Active;
#if defined(__linux__)
    int Descriptor;
#elif defined(__APPLE__)
    void* Iokit;
    uint32 AssertionId;
#endif
} MdoPowerInhibitor;

bool MdoPowerInhibitorBegin(MdoPowerInhibitor* Inhibitor);
void MdoPowerInhibitorEnd(MdoPowerInhibitor* Inhibitor);

/* The service polls interactive and scheduled runs on its own owner thread.
 * An unavailable platform API never prevents mdo from starting. */
typedef struct MdoPowerManagerStatus {
    uint32 Size;
    bool Configured;
    bool Running;
    bool Active;
    bool Available;
    bool Checked;
} MdoPowerManagerStatus;

bool MdoPowerManagerInit(void);
void MdoPowerManagerUnit(void);
bool MdoPowerManagerGetStatus(MdoPowerManagerStatus* Status);

#endif
