#ifndef MDO_KNOWLEDGE_H
#define MDO_KNOWLEDGE_H
#include "home.h"
#include <xwork.h>
bool MdoKnowledgeAppendPrompt(char** Prompt);
bool MdoKnowledgeAppendPromptForWorkspace(char** Prompt,cstr Workspace);
xvalue* MdoKnowledgeReferencesForWorkspace(cstr Workspace);
bool MdoKnowledgeReferenceReady(cstr Id,bool ForModel,uint64* Budget,char Error[512]);
bool MdoKnowledgeConfigureFileTools(xwork_agent* Agent,xwork_error* Error);
/* Read-only, current checks; a missing optional root is healthy. */
xvalue* MdoKnowledgeDiagnostics(void);
#endif
