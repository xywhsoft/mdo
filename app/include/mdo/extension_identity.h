#ifndef MDO_EXTENSION_IDENTITY_H
#define MDO_EXTENSION_IDENTITY_H
#include <stdio.h>
#include <string.h>

#define MDO_EXTENSION_TEXT_LIMIT (128u * 1024u)
#define MDO_EXTENSION_REF_BYTES 160u
#define MDO_EXTENSION_RELATIONS 128u

/* Logical identity is not a directory name, callable alias or store XID. */
static inline cstr MdoExtensionRefPrefix(cstr Kind)
{
    static const char* Kinds[]={"agents","subagents","tools","skills","mcp","external-tools","commands","plugins","knowledge"};
    static const char* Prefixes[]={"agent","subagent","tool","skill","mcp","external-tool","command","plugin","knowledge"};
    for(size_t I=0;Kind&&I<9;I++)if(!strcmp(Kind,Kinds[I]))return Prefixes[I];
    return NULL;
}
static inline cstr MdoExtensionRefKind(cstr Ref)
{
    static const char* Kinds[]={"agents","subagents","tools","skills","mcp","external-tools","commands","plugins","knowledge"};
    if(!Ref||strlen(Ref)>MDO_EXTENSION_REF_BYTES)return NULL;
    const char* Colon=strchr(Ref,':');if(!Colon)return NULL;
    const char* Name=Colon+1;size_t N=strlen(Name);
    if(!N||N>128||Name[0]=='.'||Name[N-1]=='.'||strstr(Name,".."))return NULL;
    for(size_t I=0;I<N;I++){unsigned char C=Name[I];if(!((C>='a'&&C<='z')||(C>='0'&&C<='9')||C=='.'||C=='_'||C=='-'))return NULL;}
    for(size_t I=0;I<9;I++){cstr Prefix=MdoExtensionRefPrefix(Kinds[I]);size_t P=strlen(Prefix);if((size_t)(Colon-Ref)==P&&!memcmp(Ref,Prefix,P))return Kinds[I];}
    return NULL;
}
static inline bool MdoExtensionRefValid(cstr Ref){return MdoExtensionRefKind(Ref)!=NULL;}

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
