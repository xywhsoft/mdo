#ifndef MDO_MODELS_H
#define MDO_MODELS_H

#include <xsbase.h>
#include <xllm.h>

typedef struct MdoModelCatalog MdoModelCatalog;

typedef enum MdoModelProtocol {
    MDO_MODEL_PROTOCOL_OPENAI_CHAT_COMPLETIONS = 1,
    MDO_MODEL_PROTOCOL_OPENAI_RESPONSES,
    MDO_MODEL_PROTOCOL_ANTHROPIC_MESSAGES
} MdoModelProtocol;

typedef uint32 MdoModelProtocolFlags;
#define MDO_MODEL_PROTOCOL_FLAG_CHAT_COMPLETIONS (1u << 0)
#define MDO_MODEL_PROTOCOL_FLAG_RESPONSES        (1u << 1)
#define MDO_MODEL_PROTOCOL_FLAG_ANTHROPIC        (1u << 2)

typedef uint32 MdoModelAttachmentFlags;
#define MDO_MODEL_ATTACHMENT_IMAGE (1u << 0)
#define MDO_MODEL_ATTACHMENT_AUDIO (1u << 1)
#define MDO_MODEL_ATTACHMENT_FILE  (1u << 2)

typedef struct MdoProviderInfo {
    uint32 Size;
    uint64 Generation;
    bool Builtin;
    bool Editable;
    bool Removable;
    bool VerifyPeer;
    bool HasCredentialReference;
    const char* Id;
    const char* Name;
    uint32 TimeoutMilliseconds;
    MdoModelProtocolFlags Protocols;
    const char* ChatCompletionsEndpoint;
    const char* ResponsesEndpoint;
    const char* AnthropicMessagesEndpoint;
} MdoProviderInfo;

typedef struct MdoModelInfo {
    uint32 Size;
    uint64 Generation;
    bool Builtin;
    bool Free;
    bool Editable;
    bool Removable;
    const char* Id;
    const char* Name;
    const char* ProviderId;
    const char* WireModel;
    MdoModelProtocolFlags Protocols;
    MdoModelProtocol DefaultProtocol;
    xllm_capability_flags Capabilities;
    xllm_window_mode WindowMode;
    uint64 ContextWindowTokens;
    uint64 MaxInputTokens;
    uint32 MaxOutputTokens;
    uint32 OutputReserveTokens;
    uint32 SummaryTokens;
    const char* const* ReasoningEfforts;
    size_t ReasoningEffortCount;
    const char* DefaultReasoningEffort;
    MdoModelAttachmentFlags Attachments;
} MdoModelInfo;

bool MdoModelManagerInit(void);
void MdoModelManagerUnit(void);
bool MdoModelManagerReload(void);
uint64 MdoModelManagerGeneration(void);

MdoModelCatalog* MdoModelCatalogSnapshot(void);
MdoModelCatalog* MdoModelCatalogRef(MdoModelCatalog* pCatalog);
void MdoModelCatalogRelease(MdoModelCatalog* pCatalog);
size_t MdoModelCatalogProviderCount(const MdoModelCatalog* pCatalog);
size_t MdoModelCatalogModelCount(const MdoModelCatalog* pCatalog);
bool MdoModelCatalogProviderAt(const MdoModelCatalog* pCatalog,
    size_t Index, MdoProviderInfo* pInfo);
bool MdoModelCatalogProviderFind(const MdoModelCatalog* pCatalog,
    const char* ProviderId, MdoProviderInfo* pInfo);
bool MdoModelCatalogModelAt(const MdoModelCatalog* pCatalog,
    size_t Index, MdoModelInfo* pInfo);
bool MdoModelCatalogModelFind(const MdoModelCatalog* pCatalog,
    const char* ModelId, MdoModelInfo* pInfo);
bool MdoModelCatalogDefault(const MdoModelCatalog* pCatalog,
    MdoModelInfo* pInfo);

/* Builds a non-secret xllm profile for a selected wire protocol. All string
 * pointers remain borrowed from the retained catalog snapshot. */
bool MdoModelCatalogProfile(const MdoModelCatalog* pCatalog,
    const char* ModelId, MdoModelProtocol Protocol,
    xllm_model_profile* pProfile, xllm_error* pError);

const char* MdoModelProtocolName(MdoModelProtocol Protocol);
xllm_provider MdoModelProtocolProvider(MdoModelProtocol Protocol);

#endif
