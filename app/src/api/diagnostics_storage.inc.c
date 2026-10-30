/* Storage safety state joins the same current warning snapshot. */
#include "../../include/mdo/diagnostics.h"
#include "../../include/mdo/home.h"

static bool MdoApiStorageWarnings(xvalue* Items, const MdoHomeSnapshot* Home)
{
    if ( Home->Persistence == MDO_PERSISTENCE_EPHEMERAL &&
         !MdoDiagnosticWarning(Items, "storage", "temporary_storage", "", Home->Path,
            Home->Message[0] ? Home->Message : "This device is using temporary storage; changes will not survive exit.",
            "Use a writable portable Home before continuing work that must be saved.", "") ) return false;
    if ( Home->RestartRequired &&
         !MdoDiagnosticWarning(Items, "storage", "restart_required", "", Home->Path,
            Home->Message[0] ? Home->Message : "Storage consistency checks require a restart before further changes.",
            "Close and restart mdo, then check again. Existing data is retained.", "") ) return false;
    return true;
}
