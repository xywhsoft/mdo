#ifndef MDO_SIDECAR_BINDING_H
#define MDO_SIDECAR_BINDING_H

#include <xsbase.h>
#define MDO_IMAGE_RUN_RECORD_MAX 320u

/* Binding parsing never reads Home or updates event projections. */
bool MdoImageHexId(xstrview Value, char Output[33]);
bool MdoImageIdsRead(const xvalue* Array, char Ids[4][33],
    size_t* Count);
bool MdoImageBindingParse(xstrview Json, uint64 ExpectedRunId,
    uint64* RunId, char Ids[4][33], size_t* Count);

#endif
