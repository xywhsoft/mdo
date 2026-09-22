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

/* Borrowed immutable bytes captured from the application VFS before the
 * external Home overlay is mounted.  The view remains valid until Unit. */
bool MdoBuiltinDefaults(xstrview* pText);

/* Path is a portable, relative resource path using '/'. */
xfile MdoResourceOpenRead(cstr Path);

/* Reads only from an already-existing external Home.  It never creates the
 * Home and never falls back to a built-in resource. */
xfile MdoHomeOpenRead(cstr Path);

/* Writes only below the external Home. The Home and required parent
 * directories are created lazily on the first call that needs them. */
xfile MdoHomeOpenWrite(cstr Path, uint32 Flags);

/* Atomically replaces one external Home file through a same-directory
 * temporary file.  When Backup is true, the previous regular file is copied
 * to Path + ".bak" before publication. */
bool MdoHomeAtomicWrite(cstr Path, const void* pData, size_t iSize,
    bool Backup);

/* Removes one external Home file without creating Home.  Missing files are a
 * successful no-op.  Backup has the same meaning as for AtomicWrite. */
bool MdoHomeRemove(cstr Path, bool Backup);

/* Copies the current resource fallback into the external Home if the external
 * path does not already exist. */
bool MdoResourceMaterialize(cstr Path);

#endif
