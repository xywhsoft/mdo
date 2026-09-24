#include <stdio.h>
#include <string.h>

#include "../../include/mdo/attachments.h"
#include "../../include/mdo/home.h"
#include "../../include/mdo/sessions.h"

#define MDO_IMAGE_RUN_RECORD_MAX 320u

static bool MdoImageRunId(xstrview Value, char Output[33])
{
    size_t i;
    if ( Value.Size != 32u ) return false;
    for ( i = 0u; i < Value.Size; ++i ) {
        unsigned char Byte = (unsigned char)Value.Data[i];
        if ( !((Byte >= '0' && Byte <= '9') ||
               (Byte >= 'a' && Byte <= 'f')) ) return false;
    }
    memcpy(Output, Value.Data, 32u);
    Output[32] = '\0';
    return true;
}

static bool MdoImageRunPath(char Output[MDO_SESSION_PATH_CAPACITY],
    const char* ProjectId, const char* SessionId, uint64 AgentRunId)
{
    int Written;
    if ( ProjectId == NULL || SessionId == NULL || AgentRunId == 0u )
        return false;
    Written = snprintf(Output, MDO_SESSION_PATH_CAPACITY,
        "sessions/%s/%s/attachments/runs/%llu.json",
        ProjectId, SessionId, (unsigned long long)AgentRunId);
    return Written > 0 && (size_t)Written < MDO_SESSION_PATH_CAPACITY;
}

static bool MdoImageEventPath(char Output[MDO_SESSION_PATH_CAPACITY],
    const char* ProjectId, const char* SessionId, uint64 EventId)
{
    int Written;
    if ( ProjectId == NULL || SessionId == NULL || EventId == 0u )
        return false;
    Written = snprintf(Output, MDO_SESSION_PATH_CAPACITY,
        "sessions/%s/%s/attachments/events/%llu.json",
        ProjectId, SessionId, (unsigned long long)EventId);
    return Written > 0 && (size_t)Written < MDO_SESSION_PATH_CAPACITY;
}

static bool MdoImageRunUnsigned(const xvalue* Value, uint64* Number)
{
    int64 Signed;
    if ( xrtValueType(Value) == XVALUE_UINT )
        return xrtValueGetUInt(Value, Number);
    if ( xrtValueType(Value) != XVALUE_INT ||
         !xrtValueGetInt(Value, &Signed) || Signed < 0 ) return false;
    *Number = (uint64)Signed;
    return true;
}

static bool MdoImageRecordWrite(const char* Path, uint64 AgentRunId,
    const char Ids[4][33], size_t Count)
{
    char Document[MDO_IMAGE_RUN_RECORD_MAX];
    size_t Used;
    size_t i;
    int Written;
    if ( Path == NULL || AgentRunId == 0u ||
         (Count != 0u && Ids == NULL) || Count > 4u ) return false;
    Written = snprintf(Document, sizeof(Document),
        "{\"schema_version\":1,\"run_id\":%llu,\"attachments\":[",
        (unsigned long long)AgentRunId);
    if ( Written <= 0 || (size_t)Written >= sizeof(Document) ) return false;
    Used = (size_t)Written;
    for ( i = 0u; i < Count; ++i ) {
        char Checked[33];
        size_t j;
        if ( !MdoImageRunId(xrtStrView(Ids[i]), Checked) ) return false;
        for ( j = 0u; j < i; ++j )
            if ( strcmp(Ids[i], Ids[j]) == 0 ) return false;
        Written = snprintf(Document + Used, sizeof(Document) - Used,
            "%s\"%s\"", i != 0u ? "," : "", Ids[i]);
        if ( Written <= 0 || (size_t)Written >= sizeof(Document) - Used )
            return false;
        Used += (size_t)Written;
    }
    Written = snprintf(Document + Used, sizeof(Document) - Used, "]}");
    if ( Written != 2 ) return false;
    Used += (size_t)Written;
    return MdoHomeAtomicWrite(Path, Document, Used, false);
}

static bool MdoImageRecordRead(const char* Path, uint64 AgentRunId,
    char Ids[4][33], size_t* Count)
{
    char Document[MDO_IMAGE_RUN_RECORD_MAX + 1u];
    bool Exists = false;
    xfileinfo Info;
    xfile File = NULL;
    xjsonreadconfig Config;
    xvalue* Root = NULL;
    const xvalue* Value;
    const xvalue* Array;
    uint64 Number;
    size_t i;
    bool Ok = false;
    if ( Path == NULL || AgentRunId == 0u || Ids == NULL || Count == NULL )
        return false;
    *Count = 0u;
    if ( !MdoHomeExternalStat(Path, &Exists, &Info) ) return false;
    if ( !Exists ) return true;
    if ( Info.Type != XFILE_TYPE_FILE ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Size == 0u || Info.Size > MDO_IMAGE_RUN_RECORD_MAX )
        return false;
    File = MdoHomeOpenRead(Path);
    if ( File == NULL ||
         !xrtReadFull(File, Document, (size_t)Info.Size, NULL) ) goto done;
    Document[Info.Size] = '\0';
    xrtJsonReadConfigInit(&Config);
    Config.MaxInputBytes = MDO_IMAGE_RUN_RECORD_MAX;
    Config.MaxDepth = 4u;
    Config.MaxValues = 12u;
    Config.MaxContainerItems = 4u;
    Root = xrtJsonRead(xrtStrViewN(Document, (size_t)Info.Size), &Config);
    if ( xrtValueType(Root) != XVALUE_OBJECT ||
         xrtValueCount(Root) != 3u ) goto done;
    Value = xrtValueObjectGet(Root, XRT_STR_LITERAL("schema_version"));
    if ( !MdoImageRunUnsigned(Value, &Number) || Number != 1u ) goto done;
    Value = xrtValueObjectGet(Root, XRT_STR_LITERAL("run_id"));
    if ( !MdoImageRunUnsigned(Value, &Number) || Number != AgentRunId ) goto done;
    Array = xrtValueObjectGet(Root, XRT_STR_LITERAL("attachments"));
    if ( xrtValueType(Array) != XVALUE_ARRAY ||
         xrtValueCount(Array) > 4u )
        goto done;
    for ( i = 0u; i < xrtValueCount(Array); ++i ) {
        const xvalue* Item = xrtValueArrayGet(Array, i);
        xstrview Text;
        size_t j;
        if ( !xrtValueGetString(Item, &Text) ||
             !MdoImageRunId(Text, Ids[i]) ) goto done;
        for ( j = 0u; j < i; ++j )
            if ( strcmp(Ids[i], Ids[j]) == 0 ) goto done;
    }
    *Count = xrtValueCount(Array);
    Ok = true;
done:
    xrtValueRelease(Root);
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    if ( !Ok ) *Count = 0u;
    return Ok;
}

bool MdoSessionAttachmentRunRead(const char* ProjectId,
    const char* SessionId, uint64 AgentRunId,
    char Ids[4][33], size_t* Count)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    return MdoImageRunPath(Path, ProjectId, SessionId, AgentRunId) &&
        MdoImageRecordRead(Path, AgentRunId, Ids, Count);
}

bool MdoSessionAttachmentEventWrite(const char* ProjectId,
    const char* SessionId, uint64 EventId, uint64 AgentRunId,
    const char Ids[4][33], size_t Count)
{
    char EventPath[MDO_SESSION_PATH_CAPACITY];
    char LegacyPath[MDO_SESSION_PATH_CAPACITY];
    bool Exists;
    xfileinfo Info;
    if ( !MdoImageEventPath(EventPath, ProjectId, SessionId, EventId) ||
         !MdoImageRunPath(LegacyPath, ProjectId, SessionId, AgentRunId) )
        return false;
    if ( Count == 0u ) {
        if ( !MdoHomeExternalStat(LegacyPath, &Exists, &Info) ) return false;
        if ( !Exists ) return true;
    }
    return MdoImageRecordWrite(EventPath, AgentRunId, Ids, Count);
}

bool MdoSessionAttachmentEventRead(const char* ProjectId,
    const char* SessionId, uint64 EventId, uint64 AgentRunId,
    char Ids[4][33], size_t* Count)
{
    char Path[MDO_SESSION_PATH_CAPACITY];
    bool Exists;
    xfileinfo Info;
    if ( !MdoImageEventPath(Path, ProjectId, SessionId, EventId) ||
         !MdoHomeExternalStat(Path, &Exists, &Info) ) return false;
    return Exists ? MdoImageRecordRead(Path, AgentRunId, Ids, Count) :
        MdoSessionAttachmentRunRead(ProjectId, SessionId, AgentRunId,
            Ids, Count);
}
