#ifndef MDO_BROWSER_CDP_WEBSOCKET_H
#define MDO_BROWSER_CDP_WEBSOCKET_H
#include "MdoBrowserCdpSocket.h"
#include <stdint.h>

/* Private native-worker transport. An ID is a bounded discovery target ID,
 * never a URL/path. This does not prove that the target belongs to a managed
 * WebView. The caller must bind ownership before issuing browser effects.
 * All I/O for a handle belongs to one worker; concurrent I/O returns BUSY
 * before touching the stream. close is thread-safe and wakes blocked I/O.
 * A send failure retires the handle: delivery may be uncertain, never replay.
 * Receive TIMEOUT retains partial data; total assembly has a separate deadline.
 * No handle is reused. At most eight opening/live/draining sessions exist. */
enum MdoCdpSocketCode mdoCdpWsOpen(const char* Id,size_t Size,int TimeoutMs,uint64_t* Handle);
/* Fixed browser namespace only, from checked discovery. Opening does not
 * establish WebView ownership or authorize target attachment/effects. */
enum MdoCdpSocketCode mdoCdpWsOpenBrowser(const char* Path,size_t Size,int TimeoutMs,uint64_t* Handle);
enum MdoCdpSocketCode mdoCdpWsSend(uint64_t Handle,const unsigned char* Data,size_t Size,int TimeoutMs);
MdoCdpDiscovery mdoCdpWsReceive(uint64_t Handle,int TimeoutMs);
void mdoCdpWsClose(uint64_t Handle);
#endif
