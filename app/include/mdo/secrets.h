#ifndef MDO_SECRETS_H
#define MDO_SECRETS_H

#include <xsbase.h>

bool MdoSecretReferenceSyntaxValid(xstrview Reference);

#define MDO_SECRET_VAULT_REFERENCE_CAPACITY 71u
/* A new immutable, device-sealed credential. Never overwrites an existing key.
 * Call Discard only when the enclosing config transaction did not commit. */
bool MdoSecretStore(cstr Value, char Reference[MDO_SECRET_VAULT_REFERENCE_CAPACITY]);
bool MdoSecretDiscard(cstr Reference);

/* Resolves vault:, env: and Home-relative file: references. keychain: and prompt:
 * remain valid configuration syntax but require a platform/UI resolver and
 * therefore fail closed here. The caller owns the returned value. */
bool MdoSecretResolve(xstrview Reference, size_t Limit, char** ppValue);

/* Securely clears and releases a resolved value, then stores NULL. */
void MdoSecretRelease(char** ppValue);

#endif
