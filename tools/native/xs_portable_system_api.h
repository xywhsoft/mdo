/* Native queries also available to compact TCC builds, without OS headers.
 * Disk query accepts an existing directory, returns bytes available to this
 * process (including quotas), and clears the result on failure. */
XS_API uint64 xsCurrentProcessId(void);
XS_API bool xsDiskAvailableBytes(cstr Directory, uint64* Available);
