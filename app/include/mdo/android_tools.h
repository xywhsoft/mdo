#ifndef MDO_ANDROID_TOOLS_H
#define MDO_ANDROID_TOOLS_H
#include <xsbase.h>
bool MdoAndroidToolsInit(char Error[512]);
void MdoAndroidToolsUnit(void);
bool MdoAndroidToolsActive(void);
cstr MdoAndroidToolsWarning(void);
bool MdoAndroidToolPrepare(cstr Id,char Error[512]);
char* MdoAndroidToolProgram(cstr Id,cstr Path);
bool MdoAndroidToolLink(cstr Relative,cstr NativeTarget);
bool MdoAndroidToolOwnedLink(cstr Relative,cstr NativeTarget);
char* MdoAndroidForwarder(void);
bool MdoAndroidToolsEnvironment(const xvalue* Items);
bool MdoAndroidToolClean(cstr Id);
#endif
