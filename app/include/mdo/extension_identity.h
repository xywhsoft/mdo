#ifndef MDO_EXTENSION_IDENTITY_H
#define MDO_EXTENSION_IDENTITY_H
#include <stdio.h>
#include <string.h>

#define MDO_EXTENSION_TEXT_LIMIT (128u * 1024u)

/* A callable alias is independent of the physical directory identity. */
static inline bool MdoExtensionIdentifier(cstr Name,size_t Limit)
{
    size_t N=Name?strlen(Name):0;if(!N||N>Limit||Name[0]=='.'||Name[N-1]=='.'||strstr(Name,".."))return false;
    for(size_t I=0;I<N;I++){unsigned char C=Name[I];if(!((C>='a'&&C<='z')||(C>='A'&&C<='Z')||(C>='0'&&C<='9')||C=='-'||C=='_'||C=='.'))return false;}
    return true;
}
static inline bool MdoExtensionAlias(cstr Name){return MdoExtensionIdentifier(Name,64);}

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


#endif
