package org.openxray;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.util.Log;

import androidx.core.content.FileProvider;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/**
 * One-tap diagnostic bundle.
 *
 * Everything that can explain a bad frame lives in a different place: the
 * engine writes its own log next to the game data, the frame watchdog writes
 * frame_trace.log into the game folder, the app writes openxray_app.log, and
 * native crashes only ever show up in logcat's crash buffer (never in our own
 * pid-filtered capture, because the process is already dead by then). Asking a
 * tester to find and attach six files by hand is how "no logs" happens.
 *
 * So: gather all of it, plus a summary, into one zip and hand it to the system
 * share sheet. Tails are capped in bytes so a long session produces a file that
 * can still go through Telegram or a mail attachment.
 */
public final class LogShare {

    private static final String TAG = "OpenXRay";
    private static final String AUTHORITY = "org.openxray.fileprovider";

    // Caps. The interesting part of a log is always its tail, and logcat from a
    // long gaming session runs to tens of megabytes.
    private static final long MAX_APP_LOG_BYTES = 2L * 1024 * 1024;
    private static final long MAX_LOGCAT_BYTES = 4L * 1024 * 1024;
    private static final long MAX_ENGINE_LOG_BYTES = 4L * 1024 * 1024;
    private static final long MAX_TEXT_BYTES = 1024L * 1024;

    private LogShare() {
    }

    /**
     * Builds the bundle. Safe to call off the main thread; nothing here touches
     * the UI. Returns null on failure (the caller toasts it).
     */
    public static File build(Context context, File logDir, String gameMode) {
        if (logDir == null || !logDir.isDirectory()) {
            return null;
        }

        AppLog.i(TAG, "Building diagnostic bundle from " + logDir.getAbsolutePath());
        AppLog.flush();

        String stamp = new SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(new Date());
        File zip = new File(logDir, "openxray-logs-" + safeName(gameMode) + "-" + stamp + ".zip");

        try {
            collect(context.getApplicationContext(), logDir, gameMode, zip);
        } catch (Throwable t) {
            // A half-written zip is worse than none: delete it so the next tap
            // does not offer the user a corrupt archive.
            if (zip.exists() && !zip.delete()) {
                Log.w(TAG, "Could not delete partial bundle " + zip);
            }
            AppLog.e(TAG, "Failed to build diagnostic bundle", t);
            return null;
        }

        AppLog.i(TAG, "Diagnostic bundle ready: " + zip.getAbsolutePath() + " (" + zip.length() / 1024 + " KB)");
        return zip;
    }

    /**
     * Opens the share sheet for a bundle. Must be called on the main thread.
     */
    public static void share(Activity activity, File zip) {
        if (activity == null || zip == null || !zip.isFile()) {
            return;
        }
        shareInternal(activity, zip);
    }

    private static void collect(Context context, File logDir, String gameMode, File zip) throws IOException {
        // Resolve every source before writing anything. The summary lists the
        // bundle contents, so it has to be written *after* the inventory is
        // known -- but it also has to be the *first* entry in the zip so it is
        // the first thing anyone opens. Resolving up front satisfies both.
        File engineLog = AppLog.findEngineLog(logDir);
        File engineBkp = (engineLog != null)
                ? new File(engineLog.getParentFile(), engineLog.getName().replaceAll("\\.log$", ".bkp"))
                : null;
        File frameTrace = findFrameTrace(logDir);
        File userLtx = findUserLtx(logDir);

        // The two logcat dumps are the slow part, so get them going first.
        String crashBuffer = runLogcat("-b", "crash", "-d", "-v", "threadtime", "-t", "300");
        String systemTail = runLogcat("-d", "-v", "threadtime", "-t", "1500");

        // (entry name, source file, byte cap) in bundle order.
        List<String[]> planned = new ArrayList<>();
        planned.add(new String[]{"01_openxray_app.log", str(new File(logDir, "openxray_app.log")), str(MAX_APP_LOG_BYTES)});
        planned.add(new String[]{"02_openxray_app_previous.log", str(new File(logDir, "openxray_app.log.bkp")), str(MAX_APP_LOG_BYTES)});
        planned.add(new String[]{"03_openxray_logcat.log", str(new File(logDir, "openxray_logcat.log")), str(MAX_LOGCAT_BYTES)});
        planned.add(new String[]{"04_openxray_logcat_previous.log", str(new File(logDir, "openxray_logcat.log.bkp")), str(MAX_LOGCAT_BYTES)});
        planned.add(new String[]{"05_engine.log", str(engineLog), str(MAX_ENGINE_LOG_BYTES)});
        planned.add(new String[]{"06_engine_previous.log", str(engineBkp), str(MAX_ENGINE_LOG_BYTES)});
        planned.add(new String[]{"07_frame_trace.log", str(frameTrace), str(MAX_TEXT_BYTES)});
        planned.add(new String[]{"10_user.ltx", str(userLtx), str(MAX_TEXT_BYTES)});
        planned.add(new String[]{"11_fsgame.ltx", str(new File(logDir, "fsgame.ltx")), str(MAX_TEXT_BYTES)});

        List<String> inventory = new ArrayList<>();
        inventory.add(String.format(Locale.US, "%-34s %s", "00_summary.txt", "(this file)"));
        inventory.add(String.format(Locale.US, "%-34s %s", "08_logcat_crash_buffer.txt", describe(crashBuffer.length())));
        inventory.add(String.format(Locale.US, "%-34s %s", "09_logcat_system_tail.txt", describe(systemTail.length())));
        for (String[] p : planned) {
            inventory.add(String.format(Locale.US, "%-34s %s", p[0], describeFile(new File(p[1]), Long.parseLong(p[2]))));
        }

        try (OutputStream fos = new FileOutputStream(zip);
             ZipOutputStream zos = new ZipOutputStream(fos)) {

            // --- the summary first: if a tester only skims one file, it is this one
            addText(zos, "00_summary.txt",
                    buildSummary(context, logDir, gameMode, engineLog, new ArrayList<>(inventory)));

            for (String[] p : planned) {
                addTail(zos, p[0], new File(p[1]), Long.parseLong(p[2]));
            }

            // --- native crash buffer. A pid-filtered logcat can never contain
            //     this: the process is gone by the time it is written.
            addText(zos, "08_logcat_crash_buffer.txt", crashBuffer);

            // --- unfiltered logcat tail: ANRs and ActivityManager kills are
            //     reported against the system, not our pid, so they are invisible
            //     to the pid-filtered capture in openxray_logcat.log.
            addText(zos, "09_logcat_system_tail.txt", systemTail);
        }
    }

    private static String str(File f) {
        return f == null ? "" : f.getAbsolutePath();
    }

    private static String str(long v) {
        return Long.toString(v);
    }

    private static String describe(int bytes) {
        return String.format(Locale.US, "%,d bytes", bytes);
    }

    private static String describeFile(File f, long cap) {
        if (f == null || !f.isFile() || f.length() == 0) {
            return "MISSING";
        }
        if (f.length() > cap) {
            return String.format(Locale.US, "%,d bytes (bundled last %,d KB)",
                    f.length(), cap / 1024);
        }
        return String.format(Locale.US, "%,d bytes", f.length());
    }

    // Kept separate from collect so the summary can be produced without an
    // Activity; the caller passes the one it has.
    private static String buildSummary(Context context, File logDir, String gameMode, File engineLog, List<String> inventory) {
        StringBuilder sb = new StringBuilder();
        String sep = "================================================================================";
        String line = "--------------------------------------------------------------------------------";

        sb.append(sep).append("\n");
        sb.append("OpenXRay Android diagnostic bundle\n");
        sb.append("Created:  ").append(new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(new Date())).append("\n");
        sb.append("Log dir:  ").append(logDir.getAbsolutePath()).append("\n");
        sb.append("Mode:     ").append(gameMode).append("\n");

        if (context != null) {
            try {
                PackageInfo pi = context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
                sb.append("App:      ").append(context.getPackageName())
                  .append(" v").append(pi.versionName)
                  .append(" (code ").append(pi.versionCode).append(")\n");
            } catch (Exception ignored) {
            }
        }

        sb.append(line).append("\n");
        sb.append("[DEVICE]\n");
        sb.append("Manufacturer:  ").append(Build.MANUFACTURER).append("\n");
        sb.append("Brand:         ").append(Build.BRAND).append("\n");
        sb.append("Model:         ").append(Build.MODEL).append("\n");
        sb.append("Device:        ").append(Build.DEVICE).append("\n");
        sb.append("Hardware:      ").append(Build.HARDWARE).append("\n");
        sb.append("Board:         ").append(Build.BOARD).append("\n");
        sb.append("SoC:           ").append(Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
                ? (Build.SOC_MANUFACTURER + " " + Build.SOC_MODEL) : "n/a (API < 31)").append("\n");
        sb.append("Android:       ").append(Build.VERSION.RELEASE)
          .append(" (API ").append(Build.VERSION.SDK_INT).append(")\n");
        sb.append("ABIs:          ").append(Arrays.toString(Build.SUPPORTED_ABIS)).append("\n");
        sb.append("CPU cores:     ").append(Runtime.getRuntime().availableProcessors()).append("\n");

        if (context != null) {
            try {
                android.app.ActivityManager am =
                        (android.app.ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
                if (am != null) {
                    android.app.ActivityManager.MemoryInfo mi = new android.app.ActivityManager.MemoryInfo();
                    am.getMemoryInfo(mi);
                    sb.append("Total RAM:     ").append(mi.totalMem / (1024 * 1024)).append(" MB\n");
                    sb.append("Free RAM:      ").append(mi.availMem / (1024 * 1024)).append(" MB\n");
                    sb.append("Low memory:    ").append(mi.lowMemory).append("\n");
                }
            } catch (Exception ignored) {
            }
        }

        sb.append(line).append("\n");
        sb.append("[NOTE FOR THE READER]\n");
        sb.append("A short description of what went wrong (FPS, black screen, crash, freeze)\n");
        sb.append("goes in 01_openxray_app.log, and the frame timings in 07_frame_trace.log.\n");

        // Pull the handful of lines that actually decide where the time goes out
        // of the engine log, so a report can be triaged without unzipping first.
        sb.append(line).append("\n");
        sb.append("[GPU / RENDERER CAPABILITIES]\n");
        if (engineLog != null) {
            String caps = grep(engineLog, 400, true,
                    "GL caps", "fp16", "OpenGL", "GL_VERSION", "GL_RENDERER", "GLSL",
                    "Adapter", "vendor", "Vendor", "HWDST", "shading", "SHADING");
            sb.append(caps == null || caps.isEmpty() ? "(not found in engine log)\n" : caps);
        } else {
            sb.append("(no engine log)\n");
        }

        sb.append(line).append("\n");
        sb.append("[NOTABLE LINES (errors / warnings)]\n");
        // The .bkp files matter as much as the current ones: the crash that made
        // the tester reopen the launcher is in the *previous* session's log, so
        // grepping only the live file routinely reports "(none)" for a crash
        // that very much happened.
        String[] APP = {"ERROR", "CRASH", "Exception", "error", "FATAL"};
        String[] ENGINE = {"!", "rror", "ailed", "xception", "ssert", "EGL"};
        StringBuilder notable = new StringBuilder();
        appendSection(notable, "app log (this session)",
                grep(new File(logDir, "openxray_app.log"), 200, false, APP));
        appendSection(notable, "app log (previous session)",
                grep(new File(logDir, "openxray_app.log.bkp"), 200, false, APP));
        appendSection(notable, "engine log (this session)",
                grep(engineLog, 200, false, ENGINE));
        if (engineLog != null) {
            File engineBkp = new File(engineLog.getParentFile(),
                    engineLog.getName().replaceAll("\\.log$", ".bkp"));
            appendSection(notable, "engine log (previous session)",
                    grep(engineBkp, 200, false, ENGINE));
        }
        sb.append(notable.length() == 0 ? "(none)\n" : notable);

        sb.append(line).append("\n");
        sb.append("[BUNDLE CONTENTS]\n");
        for (String s : inventory) {
            sb.append("  ").append(s).append("\n");
        }

        sb.append(line).append("\n");
        sb.append("[HOW TO REPRODUCE]\n");
        sb.append("Launch with the same game mode and graphics preset, then note the\n");
        sb.append("on-screen FPS in 07_frame_trace.log for the same area.\n");
        sb.append(sep).append("\n");
        return sb.toString();
    }

    // --- packaging helpers -------------------------------------------------

    private static void addTail(ZipOutputStream zos, String entryName, File file, long maxBytes) throws IOException {
        if (file == null || !file.isFile() || file.length() == 0) {
            return;
        }
        writeEntry(zos, entryName, readTailBytes(file, maxBytes));
    }

    private static void addText(ZipOutputStream zos, String entryName, String text) throws IOException {
        if (text == null || text.trim().isEmpty()) {
            return;
        }
        writeEntry(zos, entryName, text.getBytes(StandardCharsets.UTF_8));
    }

    private static void writeEntry(ZipOutputStream zos, String name, byte[] data) throws IOException {
        ZipEntry entry = new ZipEntry(name);
        entry.setTime(System.currentTimeMillis());
        zos.putNextEntry(entry);
        zos.write(data);
        zos.closeEntry();
    }

    /**
     * Reads at most maxBytes from the end of a file, dropping the first line if
     * it is a partial one. Reads bytes rather than lines on purpose: a 40 MB
     * logcat would take seconds to walk line by line on a phone.
     *
     * Uses a plain read loop instead of InputStream.readNBytes() because that
     * only exists from API 33 and this app runs on API 24.
     */
    private static byte[] readTailBytes(File file, long maxBytes) throws IOException {
        long len = file.length();
        int want = (int) Math.min(len, maxBytes);
        int prefix = 0;
        if (len > want) {
            // Starting mid-line would put a fragment at the top of the entry, so
            // replace the first byte we are about to keep with a newline.
            prefix = 1;
        }
        byte[] out = new byte[prefix + want];
        if (prefix == 1) {
            out[0] = '\n';
        }
        try (FileInputStream in = new FileInputStream(file)) {
            long skip = len - want;
            while (skip > 0) {
                long n = in.skip(skip);
                if (n <= 0) {
                    // skip() is allowed to make no progress; fall back to reads
                    // so a platform that behaves that way cannot loop forever.
                    if (in.read() < 0) {
                        return new byte[0];
                    }
                    skip--;
                    continue;
                }
                skip -= n;
            }
            int read = 0;
            while (read < want) {
                int n = in.read(out, prefix + read, want - read);
                if (n < 0) {
                    break;
                }
                read += n;
            }
            if (read < want) {
                out = Arrays.copyOf(out, prefix + read);
            }
        }
        return out;
    }

    /**
     * Lines containing any of the needles, keeping the last maxLines matches.
     *
     * matchCase exists so "!" is usable as a needle: engine warnings all start
     * with "! ", and matching a single character case-insensitively would match
     * every single line in the log.
     */
    private static String grep(File file, int maxLines, boolean matchCase, String... needles) {
        if (file == null || !file.isFile() || file.length() == 0) {
            return null;
        }
        StringBuilder sb = new StringBuilder();
        java.util.LinkedList<String> hits = new java.util.LinkedList<>();
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(new FileInputStream(file), StandardCharsets.UTF_8))) {
            String line;
            long scanned = 0;
            // Scan the whole file: capabilities are logged at engine startup (the
            // top of the log) while the errors worth reporting are at the end
            // (a crash), and this is the only pass that sees both.
            while ((line = reader.readLine()) != null) {
                scanned++;
                for (String n : needles) {
                    boolean hit = matchCase
                            ? line.contains(n)
                            : line.toLowerCase(Locale.US).contains(n.toLowerCase(Locale.US));
                    if (hit) {
                        hits.add(line);
                        // Keep the most recent maxLines hits: the tail is where a
                        // crash is, and the first matches are the capabilities.
                        if (hits.size() > maxLines) hits.removeFirst();
                        break;
                    }
                }
            }
        } catch (Exception e) {
            return "(grep failed: " + e.getMessage() + ")";
        }
        for (String s : hits) {
            sb.append(s).append('\n');
        }
        return sb.toString();
    }

    /**
     * Appends a labelled block, skipping empty ones. Used to fold the current
     * and previous session logs into one section: a crash that made the tester
     * reopen the launcher only exists in the .bkp, so both have to be read, and
     * a heading is what keeps them from reading as one undifferentiated wall.
     */
    private static void appendSection(StringBuilder out, String heading, String body) {
        if (body == null || body.isEmpty()) {
            return;
        }
        if (out.length() > 0) {
            out.append('\n');
        }
        out.append("--- ").append(heading).append(" ---\n").append(body);
    }

    /**
     * The frame watchdog writes into whatever directory the engine chdir'ed to,
     * which is the mode folder, but a mis-resolved -game_path can put it
     * elsewhere. Look one level down before giving up.
     */
    private static File findFrameTrace(File logDir) {
        File direct = new File(logDir, "frame_trace.log");
        if (direct.isFile()) {
            return direct;
        }
        File[] subs = logDir.listFiles(File::isDirectory);
        if (subs != null) {
            for (File sub : subs) {
                File candidate = new File(sub, "frame_trace.log");
                if (candidate.isFile()) {
                    return candidate;
                }
            }
        }
        return direct;
    }

    private static File findUserLtx(File logDir) {
        File[] candidates = {
                new File(new File(logDir, "_appdata_"), "user.ltx"),
                new File(logDir, "user.ltx"),
                new File(new File(logDir, "appdata"), "user.ltx"),
        };
        for (File c : candidates) {
            if (c.isFile()) {
                return c;
            }
        }
        return candidates[0];
    }

    private static String runLogcat(String... args) {
        List<String> cmd = new ArrayList<>();
        cmd.add("logcat");
        cmd.addAll(Arrays.asList(args));
        try {
            ProcessBuilder pb = new ProcessBuilder(cmd);
            Process p = pb.start();
            ByteArrayOutputStream bos = new ByteArrayOutputStream();
            try (InputStream in = p.getInputStream()) {
                byte[] buf = new byte[8192];
                int n;
                long total = 0;
                while ((n = in.read(buf)) > 0) {
                    // Same reason as readTailBytes: a full system buffer can be
                    // enormous, and the tail is the part that matters.
                    if (total + n > MAX_LOGCAT_BYTES) break;
                    bos.write(buf, 0, n);
                    total += n;
                }
            }
            p.waitFor();
            String s = new String(bos.toByteArray(), StandardCharsets.UTF_8).trim();
            return s.isEmpty() ? "(logcat returned nothing)" : s;
        } catch (Exception e) {
            return "(logcat unavailable: " + e.getMessage() + ")";
        }
    }

    // --- sharing -----------------------------------------------------------

    private static void shareInternal(Activity activity, File zip) {
        Uri uri;
        try {
            uri = FileProvider.getUriForFile(activity, AUTHORITY, zip);
        } catch (IllegalArgumentException e) {
            // FileProvider refuses paths outside its configured roots, which can
            // happen when the tester picked an unusual -game_path. A content URI
            // is mandatory from API 24 on, so there is no file:// fallback.
            AppLog.e(TAG, "Bundle is outside the shareable roots: " + zip.getAbsolutePath(), e);
            android.widget.Toast.makeText(activity,
                    "Bundle saved to " + zip.getAbsolutePath() + "\n(attach it from a file manager)",
                    android.widget.Toast.LENGTH_LONG).show();
            return;
        }

        Intent send = new Intent(Intent.ACTION_SEND);
        send.setType("application/zip");
        send.putExtra(Intent.EXTRA_STREAM, uri);
        send.putExtra(Intent.EXTRA_SUBJECT, "OpenXRay diagnostics");
        send.putExtra(Intent.EXTRA_TEXT, "OpenXRay diagnostic bundle.\n"
                + "The short description of the problem is at the top of 00_summary.txt.");
        // The receiving app gets a temporary grant to read this one file.
        send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);

        Intent chooser = Intent.createChooser(send, "Send OpenXRay logs");
        chooser.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try {
            activity.startActivity(chooser);
        } catch (ActivityNotFoundException e) {
            android.widget.Toast.makeText(activity,
                    "No app to send with. Bundle saved to " + zip.getAbsolutePath(),
                    android.widget.Toast.LENGTH_LONG).show();
        }
    }

    private static String safeName(String s) {
        if (s == null || s.isEmpty()) {
            return "logs";
        }
        return s.replaceAll("[^A-Za-z0-9_-]", "");
    }
}
