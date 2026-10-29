#ifndef MDO_MODEL_PLUGIN_FREE_FIXTURE_H
#define MDO_MODEL_PLUGIN_FREE_FIXTURE_H
#include <xsbase.h>
/* Model/account fixtures intentionally do not initialize the Application SDK
 * or load plugins. Supply that empty file-overlay boundary explicitly. */
bool MdoApplicationPluginHasFile(cstr Path) { (void)Path; return false; }
#endif
