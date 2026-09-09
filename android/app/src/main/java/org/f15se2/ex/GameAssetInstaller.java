package org.f15se2.ex;

import java.io.*;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.*;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Imports only the legacy files accepted by the native game's validator. */
final class GameAssetInstaller {
    private static final long MAX_BYTES = 128L * 1024 * 1024;
    private static final int MAX_ENTRIES = 4096;

    /** Reads the build-generated manifest, not an independently maintained list. */
    static Map<String, String> readManifest(InputStream input) throws IOException {
        Map<String, String> result = new LinkedHashMap<>();
        BufferedReader reader = new BufferedReader(new InputStreamReader(input, "UTF-8"));
        String line;
        while ((line = reader.readLine()) != null) {
            String[] fields = line.split(" ", 2);
            if (fields.length != 2 || !fields[0].matches("[0-9a-f]{32}") ||
                !fields[1].matches("[a-z0-9_.-]+") ||
                result.put(fields[1], fields[0]) != null)
                throw new IOException("Invalid asset manifest");
        }
        if (result.isEmpty()) throw new IOException("Empty asset manifest");
        return result;
    }

    /** Uses MD5 only for compatibility with the game's existing asset checks. */
    private static String checksum(File file) throws IOException {
        try {
            MessageDigest digest = MessageDigest.getInstance("MD5");
            byte[] buffer = new byte[16384];
            try (InputStream input = new FileInputStream(file)) {
                int count;
                while ((count = input.read(buffer)) != -1) digest.update(buffer, 0, count);
            }
            StringBuilder hex = new StringBuilder();
            for (byte value : digest.digest()) hex.append(String.format(Locale.ROOT, "%02x", value & 255));
            return hex.toString();
        } catch (NoSuchAlgorithmException error) {
            throw new IOException("MD5 is unavailable", error);
        }
    }

    /** Accepts DOS case-insensitivity, but not ambiguous duplicate names. */
    private static File find(File directory, String name) throws IOException {
        File found = null;
        File[] files = directory.listFiles();
        if (files != null) for (File file : files) {
            if (!file.getName().equalsIgnoreCase(name)) continue;
            if (found != null) throw new IOException("Duplicate file: " + name);
            found = file;
        }
        return found;
    }

    /** Validates an installation before allowing the native game to start. */
    static void validate(File directory, Map<String, String> manifest) throws IOException {
        for (Map.Entry<String, String> entry : manifest.entrySet()) {
            File file = find(directory, entry.getKey());
            if (file == null || !file.isFile()) throw new IOException("Missing file: " + entry.getKey());
            if (!checksum(file).equals(entry.getValue()))
                throw new IOException("Wrong game version or damaged file: " + entry.getKey());
        }
    }

    /** Removes only importer-owned staging directories; never follows symlinks. */
    private static void remove(File file) throws IOException {
        if (!file.exists()) return;
        if (!file.getCanonicalFile().equals(file.getAbsoluteFile()))
            throw new IOException("Unexpected link: " + file.getName());
        File[] children = file.listFiles();
        if (children != null) for (File child : children) remove(child);
        if (!file.delete()) throw new IOException("Cannot remove " + file.getName());
    }

    /** Restores the previous installation if a process died during the swap. */
    static void recover(File root) throws IOException {
        File game = new File(root, "game");
        File backup = new File(root, "game-import-backup");
        if (!game.exists() && backup.exists() && !backup.renameTo(game))
            throw new IOException("Cannot restore previous game files");
    }

    /** Stages and verifies the ZIP before replacing any installed game files. */
    static void install(InputStream source, File root, Map<String, String> manifest) throws IOException {
        install(source, root, manifest, MAX_BYTES);
    }

    /** The size-budget overload lets unit tests exercise decompression limits. */
    static void install(InputStream source, File root, Map<String, String> manifest,
                        long budget) throws IOException {
        recover(root);
        File staging = new File(root, "game-import-staging");
        File game = new File(root, "game");
        File backup = new File(root, "game-import-backup");
        remove(staging);
        if (!staging.mkdirs()) throw new IOException("Cannot create import directory");
        try {
            Set<String> seen = new HashSet<>();
            long total = 0;
            int entries = 0;
            byte[] buffer = new byte[16384];
            try (ZipInputStream zip = new ZipInputStream(source)) {
                ZipEntry entry;
                while ((entry = zip.getNextEntry()) != null) {
                    if (++entries > MAX_ENTRIES) throw new IOException("ZIP contains too many entries");
                    String path = entry.getName().replace('\\', '/');
                    if (path.startsWith("/") || path.indexOf(':') >= 0 || path.indexOf('\0') >= 0 ||
                        Arrays.asList(path.split("/")).contains(".."))
                        throw new IOException("Unsafe ZIP path");
                    String name = path.substring(path.lastIndexOf('/') + 1).toLowerCase(Locale.ROOT);
                    boolean wanted = !entry.isDirectory() &&
                        (manifest.containsKey(name) || name.equals("hallfame"));
                    if (wanted && !seen.add(name)) throw new IOException("Duplicate file: " + name);
                    // Flatten required DOS names; never extract archive paths or executables.
                    try (OutputStream output = wanted ? new FileOutputStream(new File(staging, name)) : null) {
                        int count;
                        while ((count = zip.read(buffer)) != -1) {
                            if (Thread.currentThread().isInterrupted()) throw new IOException("Import cancelled");
                            total += count;
                            if (total > budget) throw new IOException("ZIP expands beyond the import size limit");
                            if (output != null) output.write(buffer, 0, count);
                        }
                    }
                }
            }
            validate(staging, manifest);
            // HallFame is mutable state, not a checksummed asset. Reimport must
            // keep the current roster instead of restoring the ZIP's old copy.
            File roster = find(game, "hallfame");
            if (roster != null) {
                try (InputStream input = new FileInputStream(roster);
                     OutputStream output = new FileOutputStream(new File(staging, "hallfame"))) {
                    int count;
                    while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
                }
            }
            remove(backup);
            if (game.exists() && !game.renameTo(backup)) throw new IOException("Cannot back up game files");
            if (!staging.renameTo(game)) {
                recover(root);
                throw new IOException("Cannot activate imported game files");
            }
            // Leave the backup until the next successful import. Recovery also
            // works if Android kills the process between the two renames.
        } finally {
            remove(staging);
        }
    }
}
