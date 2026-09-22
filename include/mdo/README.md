# Public mdo ABI

`module.h` is the standalone, versioned C ABI for trusted in-process modules.
It depends only on standard C headers. Application-private headers remain under
`app/include/mdo`; no xs, xrt, xllm, xwork, or product-internal structure may
cross this public boundary.

The build copies this file byte-for-byte to
`app/generated/module-sdk/mdo/module.h`. Restricted TCC states expose that
generated directory only as a quoted include root, so a module uses
`#include "mdo/module.h"` and always compiles against the ABI packed into the
same executable.
