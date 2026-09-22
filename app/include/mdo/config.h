#ifndef MDO_CONFIG_H
#define MDO_CONFIG_H

#include <xsbase.h>

#define MDO_CONFIG_SCHEMA_VERSION 1u

typedef enum MdoConfigDomain {
    MDO_CONFIG_SETTINGS = 0,
    MDO_CONFIG_MODELS,
    MDO_CONFIG_PERMISSIONS,
    MDO_CONFIG_DOMAIN_COUNT
} MdoConfigDomain;

typedef struct MdoConfigSnapshot {
    uint32 Size;
    uint32 SchemaVersion;
    uint64 Revision;
    bool UserPatch[MDO_CONFIG_DOMAIN_COUNT];
    bool RuntimeOverride;
    size_t EffectiveBytes;
} MdoConfigSnapshot;

typedef struct MdoConfigPreview {
    uint32 Size;
    bool Valid;
    bool Changes;
    size_t PatchBytes;
    char Message[256];
} MdoConfigPreview;

bool MdoConfigInit(void);
void MdoConfigUnit(void);
bool MdoConfigGetSnapshot(MdoConfigSnapshot* pSnapshot);

/* Returned strings are owned by the caller and released with xrtFree. */
str MdoConfigEffectiveJson(size_t* pSize);
str MdoConfigExport(MdoConfigDomain Domain, bool Effective, size_t* pSize);

/* Import replaces one complete user patch document.  Preview and Import use
 * the same parser, normalizer, secret policy and effective validator. */
bool MdoConfigPreviewImport(MdoConfigDomain Domain, xstrview Document,
    MdoConfigPreview* pPreview);
bool MdoConfigImport(MdoConfigDomain Domain, xstrview Document);

/* Restore removes one user patch and returns that domain to built-in defaults
 * before temporary runtime overrides are applied. */
bool MdoConfigPreviewRestore(MdoConfigDomain Domain,
    MdoConfigPreview* pPreview);
bool MdoConfigRestore(MdoConfigDomain Domain);

#endif
