#include <string.h>
#include "mdo/sdk.h"

/* Count Unicode scalar values, rejecting malformed UTF-8 before publishing
 * a successful result to the session plan projection. */
static bool TodoTextValid(const char* Text)
{
    size_t Count = 0u;
    const unsigned char* P = (const unsigned char*)Text;
    if ( P == NULL || *P == 0u ) return false;
    while ( *P != 0u ) {
        unsigned char Lead = *P++;
        uint32_t Code;
        size_t More;
        if ( Lead < 0x80u ) { Code = Lead; More = 0u; }
        else if ( Lead >= 0xc2u && Lead <= 0xdfu ) { Code = Lead & 0x1fu; More = 1u; }
        else if ( Lead >= 0xe0u && Lead <= 0xefu ) { Code = Lead & 0x0fu; More = 2u; }
        else if ( Lead >= 0xf0u && Lead <= 0xf4u ) { Code = Lead & 0x07u; More = 3u; }
        else return false;
        for ( size_t I = 0u; I < More; ++I ) {
            if ( (*P & 0xc0u) != 0x80u ) return false;
            Code = (Code << 6u) | (*P++ & 0x3fu);
        }
        if ( (More == 2u && Code < 0x800u) ||
             (More == 3u && Code < 0x10000u) ||
             (Code >= 0xd800u && Code <= 0xdfffu) || Code > 0x10ffffu ||
             ++Count > 240u ) return false;
    }
    return true;
}

static bool TodoArgumentsValid(const mdo_values_v1* V, const xvalue* Root)
{
    const xvalue* Items = V->Get(Root, "items");
    if ( V->Kind(Root) != MDO_VALUE_OBJECT || V->Count(Root) != 1u ||
         V->Kind(Items) != MDO_VALUE_ARRAY || V->Count(Items) > 24u ) return false;
    for ( size_t I = 0u; I < V->Count(Items); ++I ) {
        const xvalue* Item = V->At(Items, I);
        bool Done;
        if ( V->Kind(Item) != MDO_VALUE_OBJECT || V->Count(Item) != 2u ||
             !TodoTextValid(V->Text(V->Get(Item, "text"))) ||
             !V->Boolean(V->Get(Item, "done"), &Done) ) return false;
    }
    return true;
}

static mdo_result TodoExecute(void* UserData,
    const mdo_tool_context_v1* Context, const char* ArgumentsJson,
    mdo_result_writer_v1* Writer, char* ErrorMessage, size_t ErrorCapacity)
{
    const mdo_values_v1* V;
    xvalue* Root = NULL;
    size_t Size = 0u;
    bool Valid, Written;
    (void)UserData; (void)ErrorMessage; (void)ErrorCapacity;
    if ( Context == NULL || Context->Application == NULL ||
         Context->Application->Api == NULL ||
         (V = Context->Application->Api->Values) == NULL ||
         Writer == NULL || Writer->Write == NULL ||
         Writer->SetSuccess == NULL ) return MDO_RESULT_ERROR;
    if ( ArgumentsJson != NULL )
        while ( Size <= 12288u && ArgumentsJson[Size] != '\0' ) ++Size;
    if ( Size > 0u && Size <= 12288u ) Root = V->Parse(ArgumentsJson);
    Valid = Root != NULL && TodoArgumentsValid(V, Root);
    V->Release(Root);
    if ( Valid ) Written = Writer->Write(Writer->Context, ArgumentsJson, Size);
    else {
        const char* Correction = "Invalid todo arguments. Send {\"items\":[{\"text\":\"Task\",\"done\":false}]} with the complete plan (at most 24 items, each text 1-240 characters, done a boolean, no extra fields; maximum JSON size 12288 bytes). Use {\"items\":[]} to clear it.";
        Written = Writer->Write(Writer->Context, Correction, strlen(Correction));
    }
    /* Invalid input is a correctable tool failure, never a successful malformed
     * plan that could make session persistence abort the Agent run. */
    return Written && Writer->SetSuccess(Writer->Context, Valid) ?
        MDO_RESULT_OK : MDO_RESULT_LIMIT;
}

static const mdo_tool_v1 TODO_TOOL = {
    MDO_V1_HEADER(mdo_tool_v1),
    .Id = "todo",
    .Name = "Update plan",
    .Description = "Maintain a concise plan for multi-step work and update progress as tasks finish. Send the complete list on each update; an empty list clears it.",
    .ParametersJson = "{\"type\":\"object\",\"properties\":{\"items\":{\"type\":\"array\",\"maxItems\":24,\"items\":{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":240},\"done\":{\"type\":\"boolean\"}},\"required\":[\"text\",\"done\"],\"additionalProperties\":false}}},\"required\":[\"items\"],\"additionalProperties\":false}",
    .Effects = MDO_TOOL_EFFECT_READ,
    .Flags = MDO_TOOL_STRICT,
    .SerialGroup = "todo",
    .MaxResultBytes = 12288u,
    .Execute = TodoExecute
};

static mdo_result TodoRegister(const mdo_host_services_v1* Host,
    const mdo_registrar_v1* Registrar, void** ModuleData,
    char* ErrorMessage, size_t ErrorCapacity)
{
    (void)Host; (void)ModuleData;
    if ( Registrar == NULL || Registrar->AddTool == NULL ) return MDO_RESULT_ERROR;
    return Registrar->AddTool(Registrar->Context, &TODO_TOOL, ErrorMessage, ErrorCapacity);
}

static const mdo_module_v1 TODO_MODULE = {
    MDO_V1_HEADER(mdo_module_v1),
    .Id = "todo",
    .Name = "Conversation Plan",
    .Description = "Maintain the conversation task plan.",
    .Version = "1.0.0",
    .Register = TodoRegister
};

MDO_EXPORT const mdo_module_v1* mdoModuleEntry(void)
{
    return &TODO_MODULE;
}
