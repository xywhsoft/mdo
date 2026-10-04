#ifndef MDO_ASKS_H
#define MDO_ASKS_H

#include <xsbase.h>
#include <xwork.h>

#include "sessions.h"

#define MDO_ASK_PENDING_MAX 16u
#define MDO_ASK_OPTIONS_MAX 8u
#define MDO_ASK_QUESTION_CAPACITY 4097u
#define MDO_ASK_OPTION_CAPACITY 721u
#define MDO_ASK_ANSWER_CAPACITY 1025u

typedef struct MdoAskBinding MdoAskBinding;

typedef struct MdoAskInfo {
    uint64 Id;
    uint64 RunId;
    int64 CreatedAt;
    size_t OptionCount;
    char Question[MDO_ASK_QUESTION_CAPACITY];
    char Options[MDO_ASK_OPTIONS_MAX][MDO_ASK_OPTION_CAPACITY];
} MdoAskInfo;

bool MdoAskManagerInit(void);
void MdoAskManagerUnit(void);
bool MdoAskRegisterTool(xwork_agent* Agent, const char* ProjectId,
    const char* SessionId, MdoAskBinding** Binding, xwork_error* Error);
void MdoAskBindingDestroy(MdoAskBinding* Binding);
bool MdoAskList(const char* ProjectId, const char* SessionId,
    MdoAskInfo* Items, size_t Capacity, size_t* Count);
bool MdoAskAnswer(const char* ProjectId, const char* SessionId,
    uint64 Id, const char* Answer, xwork_error* Error);

/* Observer runs under the manager lock; enqueue only, never reenter. */
void MdoAskObserve(void (*Changed)(void*), void* Data);

#endif
