/* Only injected into an isolated source copy. Pins exercise immutable ownership
 * and store generations; expiry has no sleeps or wall-clock changes. */
static MdoBackupUploadDocument* g_UploadFixturePin;

static void BackupUploadFixtureUnit(void)
{
    MdoApiBackupUploadRelease(g_UploadFixturePin); g_UploadFixturePin = NULL;
}

static bool BackupUploadFixtureControl(XS_HttpReq* Request)
{
    static const char Prefix[] = "/__fixture/backup-upload/";
    MdoApiContext Context = {0};
    MdoBackupUploadAccess Access = MDO_BACKUP_UPLOAD_ACCESS_UNAVAILABLE;
    xstrview Target = Request->head->Target;
    xvalue* Value;
    uint8 Digest[XRT_SHA256_SIZE];
    char Hash[65] = {0}, Id[33];
    if ( Target.Size < sizeof(Prefix) - 1u || memcmp(Target.Data, Prefix, sizeof(Prefix) - 1u) != 0 ) return false;
    Context.Request = Request;
    snprintf(Context.RequestId, sizeof(Context.RequestId), "upload-fixture");
    if ( MdoApiViewEqualText(Target, "/__fixture/backup-upload/release") ) BackupUploadFixtureUnit();
    else if ( MdoApiViewEqualText(Target, "/__fixture/backup-upload/expire") ) {
        xrtMutexLock(g_MdoBackupUploads->Lock);
        if (g_MdoBackupUploads->Slot) g_MdoBackupUploads->Slot->Deadline = xrtClock();
        xrtMutexUnlock(g_MdoBackupUploads->Lock);
    } else if ( MdoApiViewEqualText(Target, "/__fixture/backup-upload/reset") ) {
        MdoApiBackupUploadsUnit();
        if (!MdoApiBackupUploadsInit()) return false;
    } else if (Target.Size == sizeof(Prefix) - 1u + 4u + 32u &&
               memcmp(Target.Data + sizeof(Prefix) - 1u, "pin/", 4u) == 0) {
        memcpy(Id, Target.Data + sizeof(Prefix) - 1u + 4u, 32u); Id[32] = '\0';
        BackupUploadFixtureUnit();
        g_UploadFixturePin = MdoApiBackupUploadAcquire(Id, &Access);
    }
    if ( g_UploadFixturePin && xrtSha256(MdoApiBackupUploadData(g_UploadFixturePin),
            MdoApiBackupUploadBytes(g_UploadFixturePin), Digest) ) MdoUploadHash(Digest, Hash);
    Value = xrtValueObject();
    (void)MdoApiValueSetBool(Value, "pinned", g_UploadFixturePin != NULL);
    (void)MdoApiValueSetUInt(Value, "bytes", MdoApiBackupUploadBytes(g_UploadFixturePin));
    (void)MdoApiValueSetString(Value, "sha256", Hash);
    (void)MdoApiValueSetUInt(Value, "access", Access);
    (void)MdoApiReplySuccessTake(&Context, 200u, Value, NULL);
    return true;
}
