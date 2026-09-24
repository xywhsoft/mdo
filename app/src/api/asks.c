#include <stdio.h>
#include <string.h>

#include "internal.h"
#include "../../include/mdo/asks.h"

static bool MdoApiAskIdText(xstrview View, char* Output, size_t Capacity)
{
    size_t Index;
    if ( View.Size == 0u || View.Size >= Capacity || View.Data[0] == '.' )
        return false;
    for ( Index = 0u; Index < View.Size; ++Index ) {
        unsigned char Byte = (unsigned char)View.Data[Index];
        if ( (Byte >= 'a' && Byte <= 'z') ||
             (Byte >= 'A' && Byte <= 'Z') ||
             (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' ||
             Byte == '.' ) continue;
        return false;
    }
    memcpy(Output, View.Data, View.Size);
    Output[View.Size] = '\0';
    return true;
}

static bool MdoApiAskNumber(xstrview Text, uint64* Number)
{
    uint64 Result = 0u;
    size_t Index;
    if ( Text.Size == 0u ) return false;
    for ( Index = 0u; Index < Text.Size; ++Index ) {
        uint64 Digit;
        if ( Text.Data[Index] < '0' || Text.Data[Index] > '9' )
            return false;
        Digit = (uint64)(Text.Data[Index] - '0');
        if ( Result > (UINT64_MAX - Digit) / 10u ) return false;
        Result = Result * 10u + Digit;
    }
    *Number = Result;
    return Result != 0u;
}

static bool MdoApiAskSession(MdoApiContext* Context,
    char ProjectId[MDO_PROJECT_ID_CAPACITY],
    char SessionId[MDO_SESSION_ID_CAPACITY])
{
    MdoSession* Session;
    xwork_error Error;
    if ( Context->ParamCount < 2u ||
         !MdoApiAskIdText(Context->Params[0], ProjectId,
            MDO_PROJECT_ID_CAPACITY) ||
         !MdoApiAskIdText(Context->Params[1], SessionId,
            MDO_SESSION_ID_CAPACITY) ) return false;
    memset(&Error, 0, sizeof(Error));
    Session = MdoSessionLoad(ProjectId, SessionId, &Error);
    if ( Session == NULL ) return false;
    MdoSessionRelease(Session);
    return true;
}

bool MdoApiAsksRoute(MdoApiContext* Context)
{
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    MdoAskInfo* Pending;
    xvalue* Data = NULL;
    xvalue* Items = NULL;
    size_t Total = 0u;
    size_t Index;
    bool Ok;
    if ( Context->ParamCount != 2u ||
         !MdoApiAskSession(Context, ProjectId, SessionId) )
        return MdoApiReplyError(Context, 404u, "session_not_found",
            "The requested session does not exist", NULL);
    Pending = (MdoAskInfo*)xrtCalloc(MDO_ASK_PENDING_MAX,
        sizeof(*Pending));
    if ( Pending == NULL ) goto unavailable;
    if ( !MdoAskList(ProjectId, SessionId, Pending,
            MDO_ASK_PENDING_MAX, &Total) ) {
        xrtFree(Pending);
        goto unavailable;
    }
    Data = xrtValueObject();
    Items = xrtValueArray();
    Ok = Data != NULL && Items != NULL;
    for ( Index = 0u; Ok && Index < Total; ++Index ) {
        const MdoAskInfo* Source = &Pending[Index];
        xvalue* Item = xrtValueObject();
        xvalue* Options = xrtValueArray();
        size_t OptionIndex;
        Ok = Item != NULL && Options != NULL &&
            MdoApiValueSetUInt(Item, "id", Source->Id) &&
            MdoApiValueSetUInt(Item, "run_id", Source->RunId) &&
            MdoApiValueSetInt(Item, "created_at", Source->CreatedAt) &&
            MdoApiValueSetString(Item, "question", Source->Question);
        for ( OptionIndex = 0u; Ok &&
              OptionIndex < Source->OptionCount; ++OptionIndex )
            Ok = MdoApiValueAppendString(Options,
                Source->Options[OptionIndex]);
        if ( Ok ) Ok = MdoApiValueSetTake(Item, "options", &Options) &&
            MdoApiValueAppendTake(Items, &Item);
        xrtValueRelease(Options);
        xrtValueRelease(Item);
    }
    if ( Ok ) Ok = MdoApiValueSetUInt(Data, "total", Total) &&
        MdoApiValueSetTake(Data, "items", &Items);
    xrtValueRelease(Items);
    xrtFree(Pending);
    if ( !Ok ) {
        xrtValueRelease(Data);
        goto unavailable;
    }
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
unavailable:
    return MdoApiReplyError(Context, 503u, "asks_unavailable",
        "Pending user questions could not be read", NULL);
}

bool MdoApiAskRoute(MdoApiContext* Context)
{
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    char Answer[MDO_ASK_ANSWER_CAPACITY];
    MdoApiJsonBody Body;
    MdoApiBodyStatus Status;
    const xvalue* AnswerValue;
    xstrview AnswerText;
    xwork_error Error;
    xvalue* Data;
    uint64 Id;
    if ( Context->ParamCount != 3u ||
         !MdoApiAskNumber(Context->Params[2], &Id) )
        return MdoApiReplyError(Context, 400u, "invalid_ask_id",
            "The question ID must be a nonzero decimal integer", NULL);
    if ( !MdoApiAskSession(Context, ProjectId, SessionId) )
        return MdoApiReplyError(Context, 404u, "session_not_found",
            "The requested session does not exist", NULL);
    Status = MdoApiJsonBodyRead(Context, &Body);
    if ( Status != MDO_API_BODY_OK )
        return MdoApiReplyBodyError(Context, Status);
    AnswerValue = xrtValueType(Body.Value) == XVALUE_OBJECT ?
        xrtValueObjectGet(Body.Value, XRT_STR_LITERAL("answer")) : NULL;
    if ( AnswerValue == NULL || xrtValueCount(Body.Value) != 1u ||
         xrtValueType(AnswerValue) != XVALUE_STRING ||
         !xrtValueGetString(AnswerValue, &AnswerText) ||
         AnswerText.Size == 0u ||
         AnswerText.Size >= sizeof(Answer) ||
         memchr(AnswerText.Data, 0, AnswerText.Size) != NULL ||
         !xrtUtf8Valid(AnswerText, NULL) ) {
        MdoApiJsonBodyUnit(&Body);
        return MdoApiReplyError(Context, 422u, "ask_answer_invalid",
            "The answer must be 1 to 1024 UTF-8 bytes", NULL);
    }
    memcpy(Answer, AnswerText.Data, AnswerText.Size);
    Answer[AnswerText.Size] = '\0';
    MdoApiJsonBodyUnit(&Body);
    memset(&Error, 0, sizeof(Error));
    if ( !MdoAskAnswer(ProjectId, SessionId, Id, Answer, &Error) )
        return MdoApiReplyError(Context,
            Error.eCode == XWORK_ERROR_POLICY ? 404u : 503u,
            Error.eCode == XWORK_ERROR_POLICY ? "ask_not_found" :
                "asks_unavailable",
            Error.eCode == XWORK_ERROR_POLICY ?
                "The question is no longer pending" :
                "The answer could not be applied", NULL);
    Data = xrtValueObject();
    if ( Data == NULL || !MdoApiValueSetUInt(Data, "id", Id) ||
         !MdoApiValueSetString(Data, "answer", Answer) ) {
        xrtValueRelease(Data);
        return MdoApiReplyError(Context, 500u, "ask_result_unavailable",
            "The answer was applied but its result could not be created", NULL);
    }
    return MdoApiReplySuccessTake(Context, 200u, Data, NULL);
}
