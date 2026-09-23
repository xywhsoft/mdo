#ifndef MDO_OPERATIONS_H
#define MDO_OPERATIONS_H

#include <xsbase.h>

#define MDO_OPERATION_ID_CAPACITY 48u
#define MDO_OPERATION_MESSAGE_CAPACITY 256u

typedef enum MdoOperationKind {
    MDO_OPERATION_MODULE_RELOAD = 0,
    MDO_OPERATION_MCP_REFRESH
} MdoOperationKind;

typedef enum MdoOperationState {
    MDO_OPERATION_PENDING = 0,
    MDO_OPERATION_RUNNING,
    MDO_OPERATION_SUCCEEDED,
    MDO_OPERATION_FAILED,
    MDO_OPERATION_CANCELLED
} MdoOperationState;

typedef struct MdoOperationInfo {
    uint32 Size;
    uint64 Sequence;
    MdoOperationKind Kind;
    MdoOperationState State;
    xtime CreatedAt;
    xtime StartedAt;
    xtime EndedAt;
    bool CancelRequested;
    uint64 Generation;
    uint64 AuxiliaryGeneration;
    uint64 CompletedCount;
    size_t ItemCount;
    size_t SecondaryCount;
    size_t TertiaryCount;
    size_t DiagnosticCount;
    uint32 RuntimeState;
    bool Enabled;
    bool Connected;
    bool ToolsDiscovered;
    char Id[MDO_OPERATION_ID_CAPACITY];
    char Target[128];
    char Message[MDO_OPERATION_MESSAGE_CAPACITY];
} MdoOperationInfo;

/* The operation manager is process-scoped and thread-safe. It retains a
 * bounded recent history; returned info structures are caller-owned copies. */
bool MdoOperationManagerInit(void);
void MdoOperationManagerUnit(void);
bool MdoOperationStartModuleReload(MdoOperationInfo* Info);
bool MdoOperationStartMcpRefresh(const char* ServerId, MdoOperationInfo* Info);
bool MdoOperationGet(const char* Id, MdoOperationInfo* Info);
size_t MdoOperationList(MdoOperationInfo* Items, size_t Capacity);
bool MdoOperationCancel(const char* Id, MdoOperationInfo* Info);

#endif
