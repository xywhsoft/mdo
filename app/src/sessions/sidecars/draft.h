#ifndef MDO_SIDECAR_DRAFT_H
#define MDO_SIDECAR_DRAFT_H

#include "profile.h"
#define MDO_DRAFT_TEXT_MAX 65536u
#define MDO_DRAFT_FILE_MAX (256u * 1024u)
#define MDO_DRAFT_SUBMISSIONS_MAX 20u
#define MDO_DRAFT_SUBMISSIONS_TEXT_MAX (192u * 1024u)

typedef enum MdoDraftSubmissionState {
    MDO_DRAFT_PREPARED,
    MDO_DRAFT_POSTING,
    MDO_DRAFT_REJECTED
} MdoDraftSubmissionState;

typedef struct MdoDraftSubmission {
    char Id[33];
    char Text[MDO_DRAFT_TEXT_MAX + 1u];
    size_t TextSize;
    char Attachments[4][33];
    size_t AttachmentCount;
    bool Interrupt;
    MdoDraftSubmissionState State;
    MdoComposerProfile Profile;
} MdoDraftSubmission;

typedef enum MdoDraftNewTaskPhase {
    MDO_DRAFT_TASK_CREATING,
    MDO_DRAFT_TASK_COPYING,
    MDO_DRAFT_TASK_REJECTED
} MdoDraftNewTaskPhase;

typedef struct MdoDraftNewTask {
    char ProjectId[MDO_PROJECT_ID_CAPACITY];
    char SessionId[MDO_SESSION_ID_CAPACITY];
    char Title[MDO_SESSION_TITLE_CAPACITY];
    char AgentId[MDO_SESSION_IDENTITY_CAPACITY];
    char ModelId[MDO_SESSION_IDENTITY_CAPACITY];
    char ReasoningEffort[MDO_SESSION_REASONING_CAPACITY];
    char PermissionProfile[MDO_SESSION_REASONING_CAPACITY];
    MdoDraftNewTaskPhase Phase;
} MdoDraftNewTask;

typedef struct MdoDraft {
    uint64 Revision;
    bool RunAdmissionUncertain;
    char Text[MDO_DRAFT_TEXT_MAX + 1u];
    size_t TextSize;
    char Attachments[4][33];
    size_t AttachmentCount;
    MdoComposerProfile ComposerProfile;
    MdoDraftSubmission* Submissions[MDO_DRAFT_SUBMISSIONS_MAX];
    size_t SubmissionCount;
    MdoDraftNewTask NewTask;
    bool HasNewTask;
} MdoDraft;

/* An initialized result owns its submission allocations. Unit before reuse;
 * Parse zeroes the result and releases every partial allocation on failure. */
typedef enum MdoDraftScope { MDO_DRAFT_SESSION, MDO_DRAFT_GLOBAL, MDO_DRAFT_PROJECT } MdoDraftScope;

void MdoDraftUnit(MdoDraft* Draft);
bool MdoDraftParse(xstrview Json, MdoDraftScope Scope, MdoDraft* Draft);
bool MdoDraftCaptureId(xstrview View, char* Output,
    size_t Capacity);
bool MdoDraftUInt(const xvalue* Object, cstr Name, uint64* Output);
bool MdoDraftBool(const xvalue* Object, cstr Name, bool* Output);
bool MdoDraftTextView(const xvalue* Object, cstr Name,
    xstrview* Text);
bool MdoDraftProjectProfileRead(const xvalue* Value, MdoComposerProfile* Profile);
bool MdoDraftNewTaskRead(const xvalue* Value,
    MdoDraftNewTask* Task, bool* Present);
bool MdoDraftSubmissionRead(const xvalue* Value,
    MdoDraftSubmission* Submission, bool Legacy, bool AllowRejected,
    bool AllowProfile);
bool MdoDraftSubmissionsRead(const xvalue* Value,
    MdoDraft* Draft, bool Legacy, bool AllowRejected, bool AllowProfile);

#endif
