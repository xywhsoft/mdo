#include <string.h>

#include "../../include/mdo/home.h"
#include "../../include/mdo/secrets.h"

#define MDO_SECRET_ERROR_DOMAIN "mdo.secrets"
#define MDO_SECRET_REFERENCE_LIMIT 2048u

typedef enum MdoSecretError {
    MDO_SECRET_ERROR_ARGUMENT = 1,
    MDO_SECRET_ERROR_UNAVAILABLE,
    MDO_SECRET_ERROR_STORAGE,
    MDO_SECRET_ERROR_LIMIT
} MdoSecretError;

static void MdoSecretSetError(xerrkind Kind, MdoSecretError Code,
    cstr Message)
{
    xerror* pError = xrtErrorCreate(Kind, MDO_SECRET_ERROR_DOMAIN,
        (int32)Code, Message);
    if ( pError != NULL ) xrtSetErrorTake(pError);
}

static bool MdoSecretBytesContainZero(const void* Data, size_t Size)
{
    const unsigned char* pBytes = (const unsigned char*)Data;
    size_t i;
    for ( i = 0u; i < Size; ++i )
        if ( pBytes[i] == 0u ) return true;
    return false;
}

static bool MdoSecretPrefix(xstrview Reference, cstr Prefix)
{
    size_t Length = strlen(Prefix);
    return Reference.Data != NULL && Reference.Size > Length &&
        memcmp(Reference.Data, Prefix, Length) == 0;
}

bool MdoSecretReferenceSyntaxValid(xstrview Reference)
{
    if ( Reference.Size == 0u ||
         Reference.Size > MDO_SECRET_REFERENCE_LIMIT ||
         MdoSecretBytesContainZero(Reference.Data, Reference.Size) )
        return false;
    return MdoSecretPrefix(Reference, "env:") ||
        MdoSecretPrefix(Reference, "file:") ||
        MdoSecretPrefix(Reference, "keychain:") ||
        MdoSecretPrefix(Reference, "prompt:");
}

static bool MdoSecretPortablePath(cstr Path)
{
    const unsigned char* p = (const unsigned char*)Path;
    const unsigned char* pSegment;
    if ( p == NULL || p[0] == 0u || p[0] == '/' || p[0] == '\\' )
        return false;
    pSegment = p;
    for ( ; ; ++p ) {
        if ( *p == '\\' || *p == ':' || (*p != 0u && *p < 0x20u) )
            return false;
        if ( *p == '/' || *p == 0u ) {
            size_t Size = (size_t)(p - pSegment);
            if ( Size == 0u || (Size == 1u && pSegment[0] == '.') ||
                 (Size == 2u && pSegment[0] == '.' &&
                  pSegment[1] == '.') ) return false;
            if ( *p == 0u ) return true;
            pSegment = p + 1;
        }
    }
}

void MdoSecretRelease(char** ppValue)
{
    char* Value;
    if ( ppValue == NULL || *ppValue == NULL ) return;
    Value = *ppValue;
    *ppValue = NULL;
    xrtSecureZero(Value, strlen(Value));
    xrtFree(Value);
}

static bool MdoSecretReadFile(cstr Path, size_t Limit, char** ppValue)
{
    xfile File = NULL;
    xfileinfo Info;
    char* Value = NULL;
    size_t Size;
    size_t Utf8Error = 0u;
    bool Ok = false;

    if ( !MdoSecretPortablePath(Path) ) {
        MdoSecretSetError(XERR_ARGUMENT, MDO_SECRET_ERROR_ARGUMENT,
            "file secret reference must be a portable Home-relative path");
        return false;
    }
    File = MdoHomeOpenRead(Path);
    if ( File == NULL ) {
        xrtClearError();
        MdoSecretSetError(XERR_NOT_FOUND, MDO_SECRET_ERROR_UNAVAILABLE,
            "file secret reference is unavailable");
        return false;
    }
    memset(&Info, 0, sizeof(Info));
    if ( !xrtFileStat(File, &Info) ||
         (Info.Available & XFILE_INFO_SIZE) == 0u ||
         Info.Size > Limit || Info.Size > SIZE_MAX - 1u ) {
        MdoSecretSetError(XERR_RANGE, MDO_SECRET_ERROR_LIMIT,
            "file secret exceeds its size limit");
        goto done;
    }
    Size = (size_t)Info.Size;
    Value = (char*)xrtMalloc(Size + 1u);
    if ( Value == NULL ) goto done;
    if ( Size != 0u && !xrtReadFull(File, Value, Size, NULL) ) {
        MdoSecretSetError(XERR_IO, MDO_SECRET_ERROR_STORAGE,
            "file secret cannot be read completely");
        goto done;
    }
    if ( Size != 0u && Value[Size - 1u] == '\n' ) {
        --Size;
        if ( Size != 0u && Value[Size - 1u] == '\r' ) --Size;
    }
    Value[Size] = '\0';
    if ( Size == 0u || MdoSecretBytesContainZero(Value, Size) ||
         !xrtUtf8Valid((xstrview){ Value, Size }, &Utf8Error) ) {
        MdoSecretSetError(XERR_ARGUMENT, MDO_SECRET_ERROR_ARGUMENT,
            "file secret must contain nonempty UTF-8 text without NUL bytes");
        goto done;
    }
    *ppValue = Value;
    Value = NULL;
    Ok = true;

done:
    if ( File != NULL && !xrtClose(File) ) Ok = false;
    if ( Value != NULL ) {
        xrtSecureZero(Value, (size_t)Info.Size);
        xrtFree(Value);
    }
    if ( !Ok ) MdoSecretRelease(ppValue);
    return Ok;
}

bool MdoSecretResolve(xstrview Reference, size_t Limit, char** ppValue)
{
    char* Text = NULL;
    char* Value = NULL;
    bool Ok = false;
    if ( ppValue == NULL || Limit == 0u ||
         !MdoSecretReferenceSyntaxValid(Reference) ) {
        MdoSecretSetError(XERR_ARGUMENT, MDO_SECRET_ERROR_ARGUMENT,
            "secret_ref is invalid");
        return false;
    }
    *ppValue = NULL;
    Text = xrtStrDupN(Reference.Data, Reference.Size);
    if ( Text == NULL ) return false;
    if ( strncmp(Text, "env:", 4u) == 0 ) {
        cstr Name = Text + 4u;
        if ( Name[0] == '\0' || strlen(Name) > 256u ||
             strchr(Name, '=') != NULL || !xrtEnvLookup(Name, &Value) ||
             Value == NULL || Value[0] == '\0' || strlen(Value) > Limit ) {
            xrtFree(Value);
            Value = NULL;
            MdoSecretSetError(XERR_NOT_FOUND, MDO_SECRET_ERROR_UNAVAILABLE,
                "environment secret reference is unavailable");
        } else {
            *ppValue = Value;
            Value = NULL;
            Ok = true;
        }
    } else if ( strncmp(Text, "file:", 5u) == 0 ) {
        Ok = MdoSecretReadFile(Text + 5u, Limit, ppValue);
    } else if ( strncmp(Text, "keychain:", 9u) == 0 ) {
        MdoSecretSetError(XERR_UNSUPPORTED, MDO_SECRET_ERROR_UNAVAILABLE,
            "keychain secret references are unavailable on this build");
    } else if ( strncmp(Text, "prompt:", 7u) == 0 ) {
        MdoSecretSetError(XERR_UNSUPPORTED, MDO_SECRET_ERROR_UNAVAILABLE,
            "prompt secret references require an interactive resolver");
    }
    xrtFree(Text);
    if ( Value != NULL ) {
        xrtSecureZero(Value, strlen(Value));
        xrtFree(Value);
    }
    return Ok;
}
