#include <xs-image.h>
#include <xllm-session.h>
#include <stdio.h>
#include <string.h>
#include "backup_internal.h"

#define MDO_BACKUP_IMAGE_RGBA_BYTES (256u * 1024u * 1024u)
typedef struct MdoBackupImageCheck {
    MdoSessionBackupLimits Limits;
    const xcancel* Cancel;
    xwork_error* Error;
    MdoSessionBackupImages Facts;
    size_t EncodedBytes;
} MdoBackupImageCheck;

static int MdoBackupImageCancel(void* Context)
{
    MdoBackupImageCheck* Check = Context;
    return !MdoBackupCheck(&Check->Limits, Check->Cancel, Check->Error);
}

static bool MdoBackupImagePixels(MdoBackupImageCheck* Check,
    const void* Data, size_t Bytes, const char* Mime, const char* Path)
{
    xs_image_limits Limits;
    xs_image_info Info = {0};
    xs_image_status Status;
    size_t Count = Check->Facts.Attachments + Check->Facts.InlineImages + Check->Facts.UnverifiedImages;
    if ( Count >= Check->Limits.Files || Bytes > Check->Limits.TotalBytes - Check->EncodedBytes )
        return MdoBackupError(Check->Error, XWORK_ERROR_LIMIT, "backup image aggregate budget exceeded", Path);
    xsImageLimitsInit(&Limits); Info.Size = sizeof(Info);
    Limits.InputBytes = Check->Limits.FileBytes;
    Limits.Pixels = (MDO_BACKUP_IMAGE_RGBA_BYTES - Check->Facts.RgbaBytes) / 4u;
    Limits.Cancel = MdoBackupImageCancel; Limits.Context = Check;
    Status = xsImageValidate(Data, Bytes, Mime, &Limits, &Info);
    if ( Status != XS_IMAGE_OK ) {
        xwork_error_code Code = Status == XS_IMAGE_LIMIT ? XWORK_ERROR_LIMIT :
            Status == XS_IMAGE_OUT_OF_MEMORY ? XWORK_ERROR_OUT_OF_MEMORY :
            Status == XS_IMAGE_CANCELLED ? XWORK_ERROR_CANCELLED : XWORK_ERROR_IO;
        char Message[96];
        if ( Check->Error->eCode != XWORK_ERROR_NONE ) return false;
        (void)snprintf(Message, sizeof(Message), "cannot decode backup image: %s", xsImageStatusName(Status));
        return MdoBackupError(Check->Error, Code, Message, Path);
    }
    Check->EncodedBytes += Bytes; Check->Facts.RgbaBytes += Info.RgbaBytes;
    if ( Info.PeakMemoryBytes > Check->Facts.PeakDecoderMemoryBytes ) Check->Facts.PeakDecoderMemoryBytes = Info.PeakMemoryBytes;
    return MdoBackupCheck(&Check->Limits, Check->Cancel, Check->Error);
}

bool MdoSessionBackupCheckImages(const MdoSessionBackup* Backup,
    const MdoSessionBackupLimits* Limits, const xcancel* Cancel,
    MdoSessionBackupImages* Images, xwork_error* Error)
{
    MdoBackupImageCheck Check = {0};
    xwork_error OwnError;
    xllm_session* Model = NULL;
    size_t i, j;
    bool Ok = false;
    if ( Error != NULL ) xworkErrorInit(Error);
    if ( Images == NULL || Images->Size != sizeof(*Images) ) return MdoBackupError(Error,
        XWORK_ERROR_INVALID_ARGUMENT, "initialize backup image facts size", NULL);
    memset(Images, 0, sizeof(*Images)); Images->Size = sizeof(*Images);
    Check.Error = Error != NULL ? Error : &OwnError; xworkErrorInit(Check.Error);
    Check.Cancel = Cancel; Check.Facts.Size = sizeof(Check.Facts);
    if ( Backup == NULL || !Backup->Decoded ) return MdoBackupError(Check.Error,
        XWORK_ERROR_INVALID_ARGUMENT, "image checking requires a decoded backup", NULL);
    if ( !MdoBackupLimits(Limits, &Check.Limits, 30000000u, Check.Error) ||
         !MdoBackupCheck(&Check.Limits, Cancel, Check.Error) ) return false;
    Model = MdoSessionBackupReplayModel(Backup, &Check.Limits, Cancel, Check.Error);
    if ( Model == NULL ) return false;
    for ( i = 0u; i < Backup->Count; ++i ) {
        const MdoBackupOwnedFile* File = &Backup->Files[i];
        const MdoBackupOwnedFile* Binary;
        xvalue* Meta;
        xstrview View;
        char Path[64], Mime[16];
        if ( !MdoBackupCheck(&Check.Limits, Cancel, Check.Error) ) goto done;
        if ( strlen(File->Path) != 49u || strncmp(File->Path, "attachments/", 12u) != 0 ||
             strcmp(File->Path + 44u, ".json") != 0 ) continue;
        xrtClearError(); Meta = MdoBackupJson(File->Data, File->Bytes);
        if ( Meta == NULL || !MdoBackupView(Meta, "mime_type", &View) || View.Size >= sizeof(Mime) ) {
            const xerror* Cause = xrtGetError();
            xwork_error_code Code = Meta == NULL && Cause != NULL && xrtErrorKind(Cause) == XERR_MEMORY ?
                XWORK_ERROR_OUT_OF_MEMORY : XWORK_ERROR_IO;
            xrtValueRelease(Meta); (void)MdoBackupError(Check.Error, Code, "cannot inspect backup image metadata", File->Path); goto done;
        }
        memcpy(Mime, View.Data, View.Size); Mime[View.Size] = '\0'; xrtValueRelease(Meta);
        (void)snprintf(Path, sizeof(Path), "attachments/%.32s.bin", File->Path + 12u);
        Binary = MdoBackupFind(Backup, Path);
        if ( Binary == NULL ) { (void)MdoBackupError(Check.Error, XWORK_ERROR_IO, "backup image bytes are missing", Path); goto done; }
        if ( !MdoBackupImagePixels(&Check, Binary->Data, Binary->Bytes, Mime, Binary->Path) ) goto done;
        ++Check.Facts.Attachments;
    }
    for ( i = 0u; i < xllmSessionEntryCount(Model); ++i ) {
        xllm_session_entry_view View = {0};
        char Path[96];
        View.iSize = sizeof(View);
        if ( !MdoBackupCheck(&Check.Limits, Cancel, Check.Error) ) goto done;
        if ( !xllmSessionEntryAt(Model, i, &View) || View.pMessage == NULL ) {
            (void)MdoBackupError(Check.Error, XWORK_ERROR_IO, "cannot inspect retained image parts", "snapshot.json"); goto done;
        }
        for ( j = 0u; j < View.pMessage->iPartCount; ++j ) {
            const xllm_part* Part = &View.pMessage->pParts[j];
            if ( !MdoBackupCheck(&Check.Limits, Cancel, Check.Error) ) goto done;
            if ( Part->eKind != XLLM_PART_IMAGE ) continue;
            (void)snprintf(Path, sizeof(Path), "model/sequence-%llu/part-%llu",
                (unsigned long long)View.uSequence, (unsigned long long)j);
            if ( Part->pData == NULL || Part->iDataSize == 0u ) {
                size_t Count = Check.Facts.Attachments + Check.Facts.InlineImages + Check.Facts.UnverifiedImages;
                if ( Count >= Check.Limits.Files ) {
                    (void)MdoBackupError(Check.Error, XWORK_ERROR_LIMIT, "backup image operation budget exceeded", Path); goto done;
                }
                ++Check.Facts.UnverifiedImages; continue;
            }
            if ( !MdoBackupImagePixels(&Check, Part->pData, Part->iDataSize, Part->sMediaType, Path) ) goto done;
            ++Check.Facts.InlineImages;
        }
    }
    Ok = MdoBackupCheck(&Check.Limits, Cancel, Check.Error);
    if ( Ok ) *Images = Check.Facts;
done:
    xllmSessionDestroy(Model); return Ok;
}
