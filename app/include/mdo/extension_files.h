#ifndef MDO_EXTENSION_FILES_H
#define MDO_EXTENSION_FILES_H

#include <stdio.h>
#include <string.h>
#include "home.h"

#define MDO_EXTENSION_TEXT_LIMIT (128u * 1024u)

/* Shared file conventions only; each resource keeps its existing runtime. */
static inline bool MdoExtensionIdValid(cstr Id)
{
    size_t i, Length = Id != NULL ? strlen(Id) : 0u;
    if (Length == 0u || Length > 64u || Id[0] == '.' || Id[Length - 1u] == '.')
        return false;
    for (i = 0u; i < Length; ++i) {
        unsigned char c = (unsigned char)Id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_' || c == '.')) return false;
        if (c == '.' && i != 0u && Id[i - 1u] == '.') return false;
    }
    /* Windows reserves the basename even when it has an extension. */
    {
    char Base[65];
    size_t BaseLength = strcspn(Id, ".");
    memcpy(Base, Id, BaseLength); Base[BaseLength] = '\0';
    if (strcmp(Base, "con") == 0 || strcmp(Base, "prn") == 0 ||
        strcmp(Base, "aux") == 0 || strcmp(Base, "nul") == 0 ||
        (BaseLength == 4u && (strncmp(Base, "com", 3u) == 0 ||
         strncmp(Base, "lpt", 3u) == 0) && Base[3] >= '1' && Base[3] <= '9'))
        return false;
    }
    return true;
}

static inline char* MdoExtensionRead(cstr Path, bool External, size_t Limit,
    bool* Missing)
{
    xfile File = External ? MdoHomeOpenRead(Path) : MdoResourceOpenRead(Path);
    xfileinfo Info;
    char* Text;
    if (Missing != NULL) *Missing = false;
    if (File == NULL) {
        const xerror* Error = xrtGetError();
        if (Error != NULL && xrtErrorKind(Error) == XERR_NOT_FOUND) {
            if (Missing != NULL) *Missing = true;
            xrtClearError();
        }
        return NULL;
    }
    memset(&Info, 0, sizeof(Info));
    if (!xrtFileStat(File, &Info) || Info.Type != XFILE_TYPE_FILE ||
        (Info.Available & XFILE_INFO_SIZE) == 0u || Info.Size > Limit) { (void)xrtClose(File); return NULL; }
    Text = (char*)xrtMalloc((size_t)Info.Size + 1u);
    if (Text == NULL) { (void)xrtClose(File); return NULL; }
    {
        bool Read = xrtReadFull(File, Text, (size_t)Info.Size, NULL);
        bool Closed = xrtClose(File);
        if (!Read || !Closed) { xrtFree(Text); return NULL; }
    }
    Text[Info.Size] = '\0';
    if (memchr(Text, 0, (size_t)Info.Size) != NULL ||
        !xrtUtf8Valid(xrtStrViewN(Text, (size_t)Info.Size), NULL)) {
        xrtFree(Text); return NULL;
    }
    return Text;
}

static inline bool MdoExtensionHashBytes(const void* Bytes,size_t Length,char Output[65])
{
    static const char Hex[] = "0123456789abcdef";
    unsigned char Digest[32];
    size_t i;
    if (Bytes == NULL || !xrtSha256(Bytes, Length, Digest)) return false;
    for (i = 0u; i < sizeof(Digest); ++i) {
        Output[2u * i] = Hex[Digest[i] >> 4u];
        Output[2u * i + 1u] = Hex[Digest[i] & 15u];
    }
    Output[64] = '\0'; return true;
}

static inline bool MdoExtensionHash(cstr Text,char Output[65])
{
    return Text != NULL && MdoExtensionHashBytes(Text,strlen(Text),Output);
}

static inline bool MdoExtensionEnabled(cstr Kind, cstr Id, bool* Enabled)
{
    bool Missing = false;
    char* Text = MdoExtensionRead("config/extensions.json", true, 64u * 1024u, &Missing);
    char Key[96];
    xvalue* Config;
    const xvalue* Disabled;
    bool Value = false, Ok;
    *Enabled = true;
    if (Text == NULL) return Missing;
    Config = xrtJsonParse(xrtStrView(Text));
    xrtFree(Text);
    if (Config == NULL || xrtValueType(Config) != XVALUE_OBJECT) {
        xrtValueRelease(Config); return false;
    }
    snprintf(Key, sizeof(Key), "%s:%s", Kind, Id);
    Disabled = xrtValueObjectGet(Config, XRT_STR_LITERAL("disabled"));
    Ok = Disabled == NULL || xrtValueType(Disabled) == XVALUE_OBJECT;
    if (Ok && Disabled != NULL) {
        const xvalue* Item = xrtValueObjectGet(Disabled, xrtStrView(Key));
        Ok = Item == NULL || xrtValueGetBool(Item, &Value);
    }
    if (Ok) *Enabled = !Value;
    xrtValueRelease(Config);
    return Ok;
}

#endif
