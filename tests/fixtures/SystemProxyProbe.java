package org.xserver.android;

import java.net.*;
import java.io.IOException;
import java.util.*;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicInteger;

public final class SystemProxyProbe {
    private static void check(boolean value) { if (!value) throw new AssertionError(); }
    private static void select(Proxy... choices) {
        ProxySelector.setDefault(new ProxySelector() {
            public List<Proxy> select(URI uri) { return Arrays.asList(choices); }
            public void connectFailed(URI uri, SocketAddress address, IOException failure) { }
        });
    }
    public static void main(String[] args) throws Exception {
        ProxySelector original = ProxySelector.getDefault();
        String url = "https://api.example.invalid/v1";
        CountDownLatch release = new CountDownLatch(1);
        try {
            select(Proxy.NO_PROXY);check(Arrays.equals(SystemProxy.resolve(url,1000),new String[]{"direct"}));
            select(new Proxy(Proxy.Type.HTTP,InetSocketAddress.createUnresolved("proxy.invalid",7890)));
            check(Arrays.equals(SystemProxy.resolve(url,1000),new String[]{"http","proxy.invalid","7890"}));
            select(new Proxy(Proxy.Type.SOCKS,InetSocketAddress.createUnresolved("::1",1080)));
            check(Arrays.equals(SystemProxy.resolve(url,1000),new String[]{"socks5","::1","1080"}));
            select();check(SystemProxy.resolve(url,1000)==null);
            select((Proxy)null);check(SystemProxy.resolve(url,1000)==null);
            ProxySelector.setDefault(null);check(SystemProxy.resolve(url,1000)[0].equals("direct"));
            check(SystemProxy.resolve("file:///tmp/a",1000)==null);
            check(SystemProxy.resolve(url,0)==null && SystemProxy.resolve(url,10001)==null);
            AtomicInteger calls = new AtomicInteger();
            ProxySelector.setDefault(new ProxySelector() {
                public List<Proxy> select(URI uri) {
                    calls.incrementAndGet();
                    for (;;) { try { release.await();break; } catch(InterruptedException ignored) { } }
                    return Arrays.asList(Proxy.NO_PROXY);
                }
                public void connectFailed(URI uri,SocketAddress address,IOException failure) { }
            });
            long start=System.nanoTime();
            for(int i=0;i<5;i++)check(SystemProxy.resolve(url,30)==null);
            check((System.nanoTime()-start)/1000000 < 2000 && calls.get()==1);
            release.countDown();select(Proxy.NO_PROXY);
            check(SystemProxy.resolve(url,1000)[0].equals("direct"));
            Thread.currentThread().interrupt();
            check(SystemProxy.resolve(url,1000)==null && Thread.currentThread().isInterrupted());
            Thread.interrupted();
            System.out.println("Android system proxy JVM probe: PASS");
        } finally { release.countDown();ProxySelector.setDefault(original); }
    }
}
