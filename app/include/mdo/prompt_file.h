#ifndef MDO_PROMPT_FILE_H
#define MDO_PROMPT_FILE_H

#include <stdio.h>
#include <string.h>
#include "extension_files.h"

/* Bounded Markdown frontmatter reader. No YAML evaluation, aliases or tags.
 * Unknown metadata is preserved in the original file, not interpreted. */
static inline xvalue* MdoPromptScalar(xstrview Text)
{
    xvalue* Value;
    Text = xrtStrTrim(Text);
    if (Text.Size == 0u) return xrtValueString(Text);
    if (Text.Data[0] == '"' || Text.Data[0] == '[' || Text.Data[0] == '{') {
        Value = xrtJsonParse(Text);
        if (Value != NULL) return Value;
        xrtClearError();
        if (Text.Data[0] == '"') return NULL;
    }
    if (Text.Data[0] == '\'') {
        char* Copy;
        size_t i, n = 0u;
        if (Text.Size < 2u || Text.Data[Text.Size - 1u] != '\'') return NULL;
        Copy = (char*)xrtMalloc(Text.Size);
        if (Copy == NULL) return NULL;
        for (i = 1u; i + 1u < Text.Size; ++i) {
            if (Text.Data[i] == '\'') {
                if (i + 2u >= Text.Size || Text.Data[i + 1u] != '\'') {
                    xrtFree(Copy); return NULL;
                }
                ++i;
            }
            Copy[n++] = Text.Data[i];
        }
        Value = xrtValueString(xrtStrViewN(Copy, n));
        xrtFree(Copy); return Value;
    }
    if (Text.Data[0] == '&' || Text.Data[0] == '*' || Text.Data[0] == '!')
        return NULL;
    return xrtValueString(Text);
}

static inline xvalue* MdoPromptParse(cstr Text, bool HeaderRequired,
    char* Error, size_t Capacity)
{
    const char* Line = Text;
    xvalue* Object = NULL;
    xvalue* Active = NULL; /* Borrowed list while reading block items. */
    char BlockKey[96] = {0};
    char* Block = NULL;
    size_t BlockSize = 0u, BlockIndent = 0u;
    bool Fold = false, IgnoredBlock = false, FoundEnd = false;
    if (Error != NULL && Capacity != 0u) Error[0] = '\0';
    if (Text == NULL || strlen(Text) > MDO_EXTENSION_TEXT_LIMIT ||
        !xrtUtf8Valid(xrtStrView(Text), NULL)) goto failed;
    if (strncmp(Line, "\xef\xbb\xbf", 3u) == 0) Line += 3;
    Object = xrtValueObject();
    if (Object == NULL) goto failed;
    if (strncmp(Line, "---\n", 4u) != 0 && strncmp(Line, "---\r\n", 5u) != 0) {
        if (HeaderRequired) goto failed;
        FoundEnd = true;
    } else {
        Line = strchr(Line, '\n') + 1;
        while (*Line != '\0') {
            const char* End = strchr(Line, '\n');
            xstrview Raw = xrtStrViewN(Line, End != NULL ? (size_t)(End - Line) : strlen(Line));
            xstrview Trimmed = xrtStrTrim(Raw);
            size_t Indent = 0u, Colon = 0u;
            while (Indent < Raw.Size && Raw.Data[Indent] == ' ') ++Indent;
            if (BlockKey[0] != '\0' && (Indent != 0u || Trimmed.Size == 0u)) {
                size_t Skip, Bytes;
                if (BlockIndent == 0u && Indent != 0u) BlockIndent = Indent;
                Skip = Indent < BlockIndent ? Indent : BlockIndent;
                Bytes = Raw.Size - Skip;
                if (Bytes != 0u && Raw.Data[Raw.Size - 1u] == '\r') --Bytes;
                if (BlockSize + Bytes + 2u > 64u * 1024u) goto failed;
                if (BlockSize != 0u) Block[BlockSize++] = Fold ? ' ' : '\n';
                memcpy(Block + BlockSize, Raw.Data + Skip, Bytes);
                BlockSize += Bytes; Block[BlockSize] = '\0';
                Line = End != NULL ? End + 1 : Line + Raw.Size; continue;
            }
            if (BlockKey[0] != '\0') {
                xvalue* Value = xrtValueString(xrtStrViewN(Block, BlockSize));
                bool Ok = Value != NULL && xrtValueObjectSet(Object, xrtStrView(BlockKey), Value);
                xrtValueRelease(Value); xrtFree(Block); Block = NULL;
                BlockKey[0] = '\0'; BlockSize = BlockIndent = 0u;
                if (!Ok) goto failed;
            }
            if (Trimmed.Size == 3u && memcmp(Trimmed.Data, "---", 3u) == 0 && Indent == 0u) {
                Line = End != NULL ? End + 1 : Line + Raw.Size; FoundEnd = true; break;
            }
            if (Trimmed.Size == 0u || Trimmed.Data[0] == '#') {
                Line = End != NULL ? End + 1 : Line + Raw.Size; continue;
            }
            if (Indent != 0u) {
                if (Active != NULL && Trimmed.Size >= 2u &&
                    Trimmed.Data[0] == '-' && Trimmed.Data[1] == ' ') {
                    xvalue* Item = MdoPromptScalar(xrtStrViewN(Trimmed.Data + 2, Trimmed.Size - 2u));
                    bool Ok = Item != NULL && xrtValueType(Item) == XVALUE_STRING &&
                        xrtValueArrayAppend(Active, Item);
                    xrtValueRelease(Item);
                    if (!Ok) goto failed;
                } else if (!IgnoredBlock) goto failed;
                Line = End != NULL ? End + 1 : Line + Raw.Size; continue;
            }
            Active = NULL; IgnoredBlock = false;
            while (Colon < Trimmed.Size && Trimmed.Data[Colon] != ':') ++Colon;
            if (Colon == 0u || Colon == Trimmed.Size || Colon >= 96u) goto failed;
            {
                xstrview Key = xrtStrTrim(xrtStrViewN(Trimmed.Data, Colon));
                xstrview Input = xrtStrTrim(xrtStrViewN(Trimmed.Data + Colon + 1u, Trimmed.Size - Colon - 1u));
                xvalue* Value;
                size_t i;
                for (i = 0u; i < Key.Size; ++i) {
                    unsigned char c = (unsigned char)Key.Data[i];
                    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_')) goto failed;
                }
                if (xrtValueObjectGet(Object, Key) != NULL ||
                    (Key.Size == 6u && memcmp(Key.Data, "prompt", 6u) == 0)) goto failed;
                if (Input.Size != 0u && (Input.Data[0] == '|' || Input.Data[0] == '>')) {
                    memcpy(BlockKey, Key.Data, Key.Size); BlockKey[Key.Size] = '\0';
                    Fold = Input.Data[0] == '>';
                    Block = (char*)xrtCalloc(64u * 1024u + 1u, 1u);
                    if (Block == NULL) goto failed;
                } else {
                    Value = Input.Size == 0u ? xrtValueArray() : MdoPromptScalar(Input);
                    if (Value == NULL || !xrtValueObjectSet(Object, Key, Value)) {
                        xrtValueRelease(Value); goto failed;
                    }
                    if (Input.Size == 0u) { Active = Value; IgnoredBlock = true; }
                    xrtValueRelease(Value);
                }
            }
            Line = End != NULL ? End + 1 : Line + Raw.Size;
        }
    }
    if (!FoundEnd || Block != NULL) goto failed;
    {
        xvalue* Body = xrtValueString(xrtStrView(Line));
        bool Ok = Body != NULL && xrtValueObjectSet(Object, XRT_STR_LITERAL("prompt"), Body);
        xrtValueRelease(Body);
        if (!Ok) goto failed;
    }
    return Object;
failed:
    xrtFree(Block); xrtValueRelease(Object);
    if (Error != NULL && Capacity != 0u)
        snprintf(Error, Capacity, "Invalid Markdown frontmatter, duplicate field, or text limit");
    return NULL;
}

static inline char* MdoPromptString(const xvalue* Object, cstr Key)
{
    const xvalue* Value = xrtValueObjectGet(Object, xrtStrView(Key));
    xstrview Text;
    return Value != NULL && xrtValueGetString(Value, &Text)
        ? xrtStrDupN(Text.Data, Text.Size) : NULL;
}

static inline bool MdoPromptTrue(const xvalue* Object, cstr Key)
{
    char* Text = MdoPromptString(Object, Key);
    bool Value = Text != NULL && strcmp(Text, "true") == 0;
    xrtFree(Text); return Value;
}

#endif
