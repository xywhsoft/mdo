package org.xserver.android;

import java.net.InetSocketAddress;
import java.net.Proxy;
import java.net.ProxySelector;
import java.net.URI;
import java.util.List;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.Future;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

/** Reads Android's default network proxy without changing network settings. */
public final class SystemProxy {
    // A stalled system PAC resolver must not create unbounded worker threads.
    private static final ThreadPoolExecutor WORKER = new ThreadPoolExecutor(
        1, 1, 30, TimeUnit.SECONDS, new ArrayBlockingQueue<Runnable>(1), task -> {
            Thread thread = new Thread(task, "xs-system-proxy");
            thread.setDaemon(true);
            return thread;
        });
    static { WORKER.allowCoreThreadTimeOut(true); }
    private SystemProxy() { }

    /** null means resolution failed; {"direct"} is an explicit direct route. */
    public static String[] resolve(String url, int timeoutMs) {
        if (url == null || url.length() > 16384 || timeoutMs < 1 || timeoutMs > 10000)
            return null;
        Future<String[]> future = null;
        try {
            final URI target = new URI(url);
            if (!("http".equalsIgnoreCase(target.getScheme()) ||
                  "https".equalsIgnoreCase(target.getScheme())) || target.getHost() == null)
                return null;
            future = WORKER.submit(() -> {
                ProxySelector selector = ProxySelector.getDefault();
                if (selector == null) return new String[] { "direct" };
                List<Proxy> choices = selector.select(target);
                if (choices == null || choices.isEmpty()) return null;
                Proxy proxy = choices.get(0);
                if (proxy == null) return null;
                if (proxy.type() == Proxy.Type.DIRECT) return new String[] { "direct" };
                if (!(proxy.address() instanceof InetSocketAddress)) return null;
                InetSocketAddress address = (InetSocketAddress) proxy.address();
                String host = address.getHostString();
                if (host == null || host.isEmpty() || host.length() > 255 ||
                    address.getPort() < 1 || address.getPort() > 65535) return null;
                return new String[] { proxy.type() == Proxy.Type.HTTP ? "http" : "socks5",
                    host, Integer.toString(address.getPort()) };
            });
            return future.get(timeoutMs, TimeUnit.MILLISECONDS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return null;
        } catch (Exception e) {
            // Exceptions can contain endpoint/PAC details. Never log them.
            return null;
        } finally {
            if (future != null && !future.isDone()) {
                future.cancel(true);
                WORKER.remove((Runnable) future);
            }
        }
    }
}
