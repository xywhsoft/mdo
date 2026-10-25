#ifndef MDO_EXTENSION_FILES_H
#define MDO_EXTENSION_FILES_H

#include <stdio.h>
#include <string.h>
#include "home.h"

#include "extension_identity.h"

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
