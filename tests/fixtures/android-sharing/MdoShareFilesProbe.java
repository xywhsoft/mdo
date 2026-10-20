package org.xserver.android;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.Arrays;

public class MdoShareFilesProbe {
    static void require(boolean value) { if (!value) throw new AssertionError(); }
    public static void main(String[] args) throws Exception {
        File root = new File(args[0]); root.mkdirs();
        require(MdoShareFiles.filename("../目录/小说.txt").equals("小说.txt"));
        require(MdoShareFiles.filename("\u0000?*").equals("___"));
        char[] longName = new char[300]; Arrays.fill(longName, '文');
        String name = MdoShareFiles.filename(new String(longName)+".txt");
        require(name.endsWith(".txt") && name.getBytes(StandardCharsets.UTF_8).length <= 180);
        File file = MdoShareFiles.child(root,"copy.bin");
        String hash = MdoShareFiles.copy(new ByteArrayInputStream("abc".getBytes(StandardCharsets.UTF_8)), file, 3);
        require(hash.equals("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
        try { MdoShareFiles.copy(new ByteArrayInputStream(new byte[1]),file,3); throw new AssertionError(); }
        catch (IOException expected) { require(file.length()==3); }
        File large = MdoShareFiles.child(root,"large.bin");
        try { MdoShareFiles.copy(new ByteArrayInputStream(new byte[11]),large,10); throw new AssertionError(); }
        catch (IOException expected) { require(!large.exists()); }
        File zero = MdoShareFiles.child(root,"zero.bin");
        MdoShareFiles.copy(new ByteArrayInputStream(new byte[0]),zero,0); require(zero.length()==0);
        MdoShareFiles.atomic(root,"ready.json","first"); MdoShareFiles.atomic(root,"ready.json","second");
        require(MdoShareFiles.text(new File(root,"ready.json"),6).equals("second"));
        try { MdoShareFiles.text(new File(root,"ready.json"),5); throw new AssertionError(); } catch (IOException expected) { }
        try { MdoShareFiles.child(root,"../escape"); throw new AssertionError(); } catch (IOException expected) { }
        Path link = new File(root,"link").toPath(); Files.createSymbolicLink(link,file.toPath());
        try { MdoShareFiles.child(root,"link"); throw new AssertionError(); } catch (IOException expected) { }
        Files.delete(link);
        Thread.currentThread().interrupt();
        try { MdoShareFiles.copy(new ByteArrayInputStream(new byte[1]),large,1); throw new AssertionError(); }
        catch (IOException expected) { require(!large.exists()); } finally { Thread.interrupted(); }
        require(MdoShareFiles.bytes(root)==9);
        MdoShareFiles.remove(root); require(!root.exists());
        System.out.println("Android share files PASS: bounded streams, hashes, names, atomic publication, cancellation and symlinks");
    }
}
