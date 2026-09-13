package org.f15se2.ex;

import java.io.*;
import java.nio.file.Files;
import java.util.*;
import java.util.zip.*;
import java.util.concurrent.*;

/** Runs without an Android emulator or copyrighted game data. */
public final class GameAssetInstallerTest {
    private static final Map<String, String> MANIFEST = Collections.singletonMap(
        "test.pic", "900150983cd24fb0d6963f7d28e17f72"); // MD5("abc")

    private static byte[] zip(String... pairs) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream output = new ZipOutputStream(bytes)) {
            for (int i = 0; i < pairs.length; i += 2) {
                output.putNextEntry(new ZipEntry(pairs[i]));
                output.write(pairs[i + 1].getBytes("UTF-8"));
                output.closeEntry();
            }
        }
        return bytes.toByteArray();
    }

    private static void install(File root, byte[] archive) throws IOException {
        GameAssetInstaller.install(new ByteArrayInputStream(archive), root, MANIFEST);
    }

    private static void reject(File root, byte[] archive) throws IOException {
        try {
            install(root, archive);
            throw new AssertionError("Invalid ZIP accepted");
        } catch (IOException expected) {
            GameAssetInstaller.validate(new File(root, "game"), MANIFEST);
        }
    }

    public static void main(String[] args) throws Exception {
        File root = new File(args[0]);
        Map<String, String> parsed = GameAssetInstaller.readManifest(
            new ByteArrayInputStream("900150983cd24fb0d6963f7d28e17f72 test.pic\n".getBytes("UTF-8")));
        if (!parsed.equals(MANIFEST)) throw new AssertionError("Manifest parser mismatch");
        install(root, zip("My game/TEST.PIC", "abc", "My game/program.exe", "not imported"));
        if (new File(root, "game/program.exe").exists()) throw new AssertionError("Unexpected file imported");
        File roster = new File(root, "game/HallFame");
        Files.write(roster.toPath(), "current pilot".getBytes("UTF-8"));
        install(root, zip("test.pic", "abc", "HallFame", "old pilot"));
        if (!new String(Files.readAllBytes(new File(root, "game/hallfame").toPath()), "UTF-8")
                .equals("current pilot")) throw new AssertionError("Roster overwritten");
        reject(root, zip("test.pic", "damaged"));
        reject(root, zip("other.pic", "abc"));
        reject(root, zip("test.pic", "abc", "other/TEST.PIC", "abc"));
        reject(root, zip("../test.pic", "abc"));
        reject(root, zip("/test.pic", "abc"));
        reject(root, zip("C:\\test.pic", "abc"));
        reject(root, zip("dir/../test.pic", "abc"));
        reject(root, new byte[] {1, 2, 3});
        try {
            GameAssetInstaller.install(new ByteArrayInputStream(zip("test.pic", "abc")), root, MANIFEST, 2);
            throw new AssertionError("Expansion limit ignored");
        } catch (IOException expected) {
            GameAssetInstaller.validate(new File(root, "game"), MANIFEST);
        }
        // Simulate process death after the old installation was renamed.
        File game = new File(root, "game");
        File parked = new File(root, "parked");
        if (!game.renameTo(parked)) throw new AssertionError("Fixture rename failed");
        GameAssetInstaller.recover(root);
        GameAssetInstaller.validate(game, MANIFEST);
        // A recreated launcher must wait while the previous installer owns
        // staging, even if recovery is requested by a different worker.
        ExecutorService workers = Executors.newFixedThreadPool(2);
        CountDownLatch reading = new CountDownLatch(1);
        CountDownLatch release = new CountDownLatch(1);
        CountDownLatch recovering = new CountDownLatch(1);
        byte[] archive = zip("test.pic", "abc");
        InputStream blocked = new FilterInputStream(new ByteArrayInputStream(archive)) {
            @Override
            public int read(byte[] bytes, int offset, int length) throws IOException {
                reading.countDown();
                try {
                    if (!release.await(5, TimeUnit.SECONDS)) throw new IOException("Test timeout");
                } catch (InterruptedException error) {
                    Thread.currentThread().interrupt();
                    throw new IOException(error);
                }
                return super.read(bytes, offset, length);
            }
        };
        try {
            Future<?> importing = workers.submit(() -> {
                GameAssetInstaller.install(blocked, root, MANIFEST);
                return null;
            });
            if (!reading.await(5, TimeUnit.SECONDS)) throw new AssertionError("Import did not start");
            Future<?> recovery = workers.submit(() -> {
                recovering.countDown();
                GameAssetInstaller.recover(root);
                return null;
            });
            if (!recovering.await(5, TimeUnit.SECONDS)) throw new AssertionError("Recovery did not start");
            try {
                recovery.get(100, TimeUnit.MILLISECONDS);
                throw new AssertionError("Recovery overlapped an active import");
            } catch (TimeoutException expected) { /* Waiting for the install lock. */ }
            release.countDown();
            importing.get(5, TimeUnit.SECONDS);
            recovery.get(5, TimeUnit.SECONDS);
            GameAssetInstaller.validate(game, MANIFEST);
        } finally {
            release.countDown();
            workers.shutdownNow();
        }
        System.out.println("Asset importer tests passed");
    }
}
