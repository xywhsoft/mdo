#ifndef MDO_EXTENSION_SETTINGS_H
#define MDO_EXTENSION_SETTINGS_H
#include <xsbase.h>

/* Management reads never expose credentials. Mutations use both the opaque
 * revision and schema_digest returned by Read. Runtime snapshots are owned. */
xvalue* MdoExtensionSettingsRead(cstr Kind,cstr Directory,char Error[512]);
bool MdoExtensionSettingsSave(cstr Kind,cstr Directory,const xvalue* Input,
    bool Preview,xvalue** Result,char Error[512]);
xvalue* MdoExtensionSettingsSnapshot(cstr Ref,bool Secrets,char Error[512]);
/* Identity comes from an anchored, verified extension directory. */
xvalue* MdoExtensionSettingsAt(cstr Root,bool Secrets,char Error[512]);
bool MdoExtensionSettingsForget(cstr Kind,cstr Directory,char Error[512]);
#endif
