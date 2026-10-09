/* Compile/link evidence only. This verifies the existing compact mdo xrt
 * selection against the target libc/headers; main does not test its APIs. */
#define XRT_IMPLEMENTATION
#include "xrt.h"
int main(void) { return 0; }
