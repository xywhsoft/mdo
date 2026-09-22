#ifndef MDO_API_H
#define MDO_API_H

#include <xsbase.h>

#define MDO_API_SCHEMA_VERSION 1u

/* Initializes process-generation API state before xs publishes RequestProc. */
bool MdoApiInit(void);

/* Called after xs has stopped dispatching requests to this generation. */
void MdoApiUnit(void);

/* Handles versioned application routes and returns XS_FALLBACK for static UI. */
XS_RequestResult MdoApiRequest(XS_HttpReq* pRequest);

#endif
