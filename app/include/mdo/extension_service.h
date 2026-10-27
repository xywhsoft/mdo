#ifndef MDO_EXTENSION_SERVICE_H
#define MDO_EXTENSION_SERVICE_H
#include <xsbase.h>
/* Fixed first-party authority. Returns small metadata/owned archive task
 * receipts; account credentials never enter the returned objects. */
bool MdoExtensionServiceRead(const xvalue* Input,xcancel* Cancel,xvalue** Result,char Error[512]);
bool MdoExtensionServiceDownload(cstr Kind,cstr Version,xcancel* Cancel,xvalue** Result,char Error[512]);
bool MdoExtensionServiceInstall(cstr Task,const xvalue* Input,xcancel* Cancel,xvalue** Result,char Error[512]);
bool MdoExtensionServicePublish(const xvalue* Input,xcancel* Cancel,xvalue** Result,char Error[512]);
bool MdoExtensionServiceWrite(const xvalue* Input,xcancel* Cancel,xvalue** Result,char Error[512]);
#endif
