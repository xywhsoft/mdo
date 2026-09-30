#ifndef MDO_HOME_IMPORT_H
#define MDO_HOME_IMPORT_H

#include "home.h"

typedef struct MdoHomeImport MdoHomeImport;

/* Read-only eligibility check. Missing Home is available with PreserveCache
 * false. An already-mounted Home containing only its lock and data/cache/
 * webview2 is available with PreserveCache true. Cache contents are untouched;
 * unknown names, links, user data, another import or a frozen Home are refused. */
bool MdoHomeImportInspect(bool* Available, bool* PreserveCache);

/* Begin only in an eligible, mounted Home. Freeze ordinary Home mutations,
 * reserve the internal transaction directory, and return an owned anchored
 * staging root and native diagnostic path. The caller must close Stage before
 * End and consume Import before HomeUnit; no Home mutex is held while the
 * caller converts/validates its payload.
 * The existing Home process lease remains held throughout. */
MdoHomeImport* MdoHomeImportBegin(xroot* Stage, str* Path);

/* Consumes Import exactly once, after the staging root and all its files close.
 * Publish=true installs only the known top-level data roots, using anchored
 * no-replace moves and flushed immutable phase markers. Precommit failures
 * roll back; failed rollback/cleanup freezes writes until startup recovery.
 * Success freezes writes until restart so old managers cannot alter the new
 * batch. End(false) discards staging. Cache and the Home lock never move.
 * This guarantees process-interruption recovery, not power-loss durability. */
bool MdoHomeImportEnd(MdoHomeImport* Import, bool Publish);

#endif
