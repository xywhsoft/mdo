#include <string.h>

#include "internal.h"

void MdoApiSessionCaptureRelease(MdoApiSessionCaptureGuard* Guard)
{
    if ( Guard == NULL ) return;
    if ( Guard->Feedback ) MdoApiFeedbackCaptureUnlock();
    if ( Guard->Queue ) MdoApiQueueCaptureUnlock();
    if ( Guard->Draft ) MdoApiDraftCaptureUnlock();
    if ( Guard->Attachment ) MdoApiAttachmentUnlock();
    memset(Guard, 0, sizeof(*Guard));
}

bool MdoApiSessionCaptureAcquire(MdoApiSessionCaptureGuard* Guard)
{
    if ( Guard == NULL ) return false;
    memset(Guard, 0, sizeof(*Guard));
    Guard->Attachment = MdoApiAttachmentCaptureTryLock();
    if ( !Guard->Attachment ) goto busy;
    Guard->Draft = MdoApiDraftCaptureTryLock();
    if ( !Guard->Draft ) goto busy;
    Guard->Queue = MdoApiQueueCaptureTryLock();
    if ( !Guard->Queue ) goto busy;
    Guard->Feedback = MdoApiFeedbackCaptureTryLock();
    if ( !Guard->Feedback ) goto busy;
    return true;
busy:
    MdoApiSessionCaptureRelease(Guard);
    return false;
}
