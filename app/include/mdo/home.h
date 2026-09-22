#ifndef MDO_HOME_H
#define MDO_HOME_H

#include <xsbase.h>

typedef enum MdoPersistenceMode {
    MDO_PERSISTENCE_LAZY = 0,
    MDO_PERSISTENCE_EXTERNAL,
    MDO_PERSISTENCE_EPHEMERAL
} MdoPersistenceMode;

typedef struct MdoHomeSnapshot {
    uint32 Size;
    MdoPersistenceMode Persistence;
    bool ExternalOverlay;
    const char* Path;
    char Message[256];
} MdoHomeSnapshot;

bool MdoHomeInit(void);
void MdoHomeUnit(void);
bool MdoHomeGetSnapshot(MdoHomeSnapshot* pSnapshot);

/* Path is a portable, relative resource path using '/'. */
xfile MdoResourceOpenRead(cstr Path);

/* Writes only below the external Home. The Home and required parent
 * directories are created lazily on the first call that needs them. */
xfile MdoHomeOpenWrite(cstr Path, uint32 Flags);

/* Copies the current resource fallback into the external Home if the external
 * path does not already exist. */
bool MdoResourceMaterialize(cstr Path);

#endif
