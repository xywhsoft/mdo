#include <string.h>
#include "backup_inputs.h"

/* Match the actual browser's String.trim(), including Unicode whitespace.
 * Submission bytes themselves are never normalized or changed. */
static xstrview MdoInputTrim(xstrview Text)
{
    size_t Offset = 0u, First = SIZE_MAX, Last = 0u;
    while ( Offset < Text.Size ) {
        uint32 Scalar;
        size_t Read = 0u;
        bool Space;
        if ( xrtUtf8Decode(xrtStrViewN(Text.Data + Offset, Text.Size - Offset), &Scalar, &Read) != XUTF_OK ) return Text;
        Space = (Scalar >= 9u && Scalar <= 13u) || Scalar == 0x20u || Scalar == 0xa0u ||
            Scalar == 0x1680u || (Scalar >= 0x2000u && Scalar <= 0x200au) ||
            Scalar == 0x2028u || Scalar == 0x2029u || Scalar == 0x202fu || Scalar == 0x205fu ||
            Scalar == 0x3000u || Scalar == 0xfeffu;
        if ( !Space ) { if ( First == SIZE_MAX ) First = Offset; Last = Offset + Read; }
        Offset += Read;
    }
    return First == SIZE_MAX ? xrtStrViewN(Text.Data, 0u) : xrtStrViewN(Text.Data + First, Last - First);
}

bool MdoBackupInputPayloadEqual(const MdoQueueItem* Queue, const MdoDraftSubmission* Draft)
{
    xstrview Text = MdoInputTrim(xrtStrViewN(Draft->Text, Draft->TextSize));
    size_t i;
    if ( Queue->TextSize != Text.Size || memcmp(Queue->Text, Text.Data, Text.Size) != 0 ||
         Queue->Priority != Draft->Interrupt || Queue->AttachmentCount != Draft->AttachmentCount ||
         !MdoComposerProfileEqual(&Queue->Profile, &Draft->Profile) ) return false;
    for ( i = 0u; i < Queue->AttachmentCount; ++i )
        if ( strcmp(Queue->Attachments[i], Draft->Attachments[i]) != 0 ) return false;
    return true;
}
