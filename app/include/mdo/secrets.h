#ifndef MDO_SECRETS_H
#define MDO_SECRETS_H

#include <xsbase.h>

bool MdoSecretReferenceSyntaxValid(xstrview Reference);

/* Resolves env: and Home-relative file: references. keychain: and prompt:
 * remain valid configuration syntax but require a platform/UI resolver and
 * therefore fail closed here. The caller owns the returned value. */
bool MdoSecretResolve(xstrview Reference, size_t Limit, char** ppValue);

/* Securely clears and releases a resolved value, then stores NULL. */
void MdoSecretRelease(char** ppValue);

#endif
