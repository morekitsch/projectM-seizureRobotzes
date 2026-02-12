package com.projectm.questxr;

import android.Manifest;
import android.app.NativeActivity;
import android.content.pm.PackageManager;
import android.media.AudioAttributes;
import android.media.MediaPlayer;
import android.media.audiofx.Visualizer;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;

import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public class QuestNativeActivity extends NativeActivity {
    private static final String TAG = "projectM-QuestXR";
    private static final int PERMISSION_REQUEST_CODE = 1001;
    private static final String DEFAULT_PRESET_PACK = "presets-en-d";
    private static final String OPTIONAL_PRESET_PACK = "presets-cream-of-the-crop";

    static {
        System.loadLibrary("projectm_quest_openxr");
    }

    private final ExecutorService ioExecutor = Executors.newSingleThreadExecutor();

    private Visualizer visualizer;
    private MediaPlayer mediaPlayer;
    private final List<String> mediaPlaylist = new ArrayList<>();
    private int mediaPlaylistIndex = -1;
    private int audioMode = 0; // 0 synthetic, 1 global visualizer, 2 media fallback
    private boolean mediaPlaying;
    private String currentMediaLabel = "none";

    private static native void nativePushAudioPcm(float[] interleavedStereoSamples, int frameCount);
    private static native void nativeUpdateUiState(int audioMode, boolean mediaPlaying, String mediaLabel);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        pushUiStateToNative();
        requestRuntimePermissions();
        schedulePresetSync();
        startAudioCapturePipeline();
    }

    @Override
    protected void onDestroy() {
        releaseVisualizer();
        releaseMediaPlayer();
        ioExecutor.shutdownNow();
        super.onDestroy();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == PERMISSION_REQUEST_CODE) {
            startAudioCapturePipeline();
        }
    }

    private void pushUiStateToNative() {
        nativeUpdateUiState(audioMode, mediaPlaying, currentMediaLabel == null ? "none" : currentMediaLabel);
    }

    public void onNativeTogglePlayback() {
        runOnUiThread(this::togglePlaybackInternal);
    }

    public void onNativeNextTrack() {
        runOnUiThread(this::playNextTrackInternal);
    }

    public void onNativePreviousTrack() {
        runOnUiThread(this::playPreviousTrackInternal);
    }

    public void onNativeRequestOptionalCreamPack() {
        ioExecutor.execute(() -> {
            createOptionalPackFlag();
            syncPresetPacks();
        });
    }

    private void requestRuntimePermissions() {
        List<String> required = new ArrayList<>();
        required.add(Manifest.permission.RECORD_AUDIO);

        if (Build.VERSION.SDK_INT >= 33) {
            required.add(Manifest.permission.READ_MEDIA_AUDIO);
        } else {
            required.add(Manifest.permission.READ_EXTERNAL_STORAGE);
        }

        List<String> missing = new ArrayList<>();
        for (String permission : required) {
            if (ContextCompat.checkSelfPermission(this, permission) != PackageManager.PERMISSION_GRANTED) {
                missing.add(permission);
            }
        }

        if (!missing.isEmpty()) {
            ActivityCompat.requestPermissions(this, missing.toArray(new String[0]), PERMISSION_REQUEST_CODE);
        }
    }

    private void schedulePresetSync() {
        ioExecutor.execute(this::syncPresetPacks);
    }

    private void syncPresetPacks() {
        File presetDir = new File(getFilesDir(), "presets");
        if (!presetDir.exists() && !presetDir.mkdirs()) {
            Log.w(TAG, "Failed to create preset dir: " + presetDir.getAbsolutePath());
            return;
        }

        downloadPackIfNeeded(
                DEFAULT_PRESET_PACK,
                Arrays.asList(
                        "https://codeload.github.com/projectM-visualizer/presets-en-d/zip/refs/heads/master",
                        "https://codeload.github.com/projectM-visualizer/presets-en-d/zip/refs/heads/main"
                ),
                presetDir
        );

        if (isOptionalCreamDownloadEnabled()) {
            downloadPackIfNeeded(
                    OPTIONAL_PRESET_PACK,
                    Arrays.asList(
                            "https://codeload.github.com/projectM-visualizer/presets-cream-of-the-crop/zip/refs/heads/master",
                            "https://codeload.github.com/projectM-visualizer/presets-cream-of-the-crop/zip/refs/heads/main"
                    ),
                    presetDir
            );
        }
    }

    private boolean isOptionalCreamDownloadEnabled() {
        File internalFlag = new File(getFilesDir(), "enable_cream_download.flag");
        if (internalFlag.exists()) {
            return true;
        }

        File externalRoot = getExternalFilesDir(null);
        if (externalRoot != null) {
            File externalFlag = new File(externalRoot, "enable_cream_download.flag");
            return externalFlag.exists();
        }

        return false;
    }

    private void createOptionalPackFlag() {
        try {
            File internalFlag = new File(getFilesDir(), "enable_cream_download.flag");
            if (!internalFlag.exists()) {
                internalFlag.createNewFile();
            }
        } catch (IOException e) {
            Log.w(TAG, "Could not create internal optional-pack flag", e);
        }

        try {
            File externalRoot = getExternalFilesDir(null);
            if (externalRoot != null && !externalRoot.exists()) {
                externalRoot.mkdirs();
            }
            if (externalRoot != null) {
                File externalFlag = new File(externalRoot, "enable_cream_download.flag");
                if (!externalFlag.exists()) {
                    externalFlag.createNewFile();
                }
            }
        } catch (IOException e) {
            Log.w(TAG, "Could not create external optional-pack flag", e);
        }
    }

    private void downloadPackIfNeeded(String packName, List<String> candidates, File presetDir) {
        File marker = new File(presetDir, "." + packName + ".done");
        if (marker.exists()) {
            return;
        }

        for (String url : candidates) {
            try {
                int extracted = downloadAndExtractMilkFiles(packName, url, presetDir);
                if (extracted > 0) {
                    if (!marker.exists()) {
                        marker.createNewFile();
                    }
                    Log.i(TAG, "Downloaded preset pack " + packName + " (" + extracted + " presets)");
                    return;
                }
            } catch (Exception e) {
                Log.w(TAG, "Failed preset download attempt for " + packName + " from " + url, e);
            }
        }

        Log.w(TAG, "Could not download preset pack: " + packName);
    }

    private int downloadAndExtractMilkFiles(String packName, String url, File destinationDir) throws IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
        connection.setConnectTimeout(15000);
        connection.setReadTimeout(30000);
        connection.setInstanceFollowRedirects(true);
        connection.connect();

        int status = connection.getResponseCode();
        if (status != HttpURLConnection.HTTP_OK) {
            connection.disconnect();
            throw new IOException("Unexpected HTTP status: " + status);
        }

        File zipFile = File.createTempFile(packName + "-", ".zip", getCacheDir());
        try (InputStream in = new BufferedInputStream(connection.getInputStream());
             FileOutputStream out = new FileOutputStream(zipFile)) {
            byte[] buffer = new byte[16 * 1024];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
        } finally {
            connection.disconnect();
        }

        int extracted;
        try {
            extracted = extractMilkFilesFromZip(packName, zipFile, destinationDir);
        } finally {
            zipFile.delete();
        }

        return extracted;
    }

    private int extractMilkFilesFromZip(String packName, File zipFile, File destinationDir) throws IOException {
        int extractedCount = 0;

        try (ZipInputStream zis = new ZipInputStream(new BufferedInputStream(new FileInputStream(zipFile)))) {
            ZipEntry entry;
            byte[] buffer = new byte[16 * 1024];

            while ((entry = zis.getNextEntry()) != null) {
                if (entry.isDirectory()) {
                    zis.closeEntry();
                    continue;
                }

                String name = entry.getName();
                if (name == null || !name.toLowerCase(Locale.ROOT).endsWith(".milk")) {
                    zis.closeEntry();
                    continue;
                }

                String safeName = packName + "__" + name
                        .replace('\\', '_')
                        .replace('/', '_')
                        .replace(':', '_');

                File output = new File(destinationDir, safeName);
                try (BufferedOutputStream out = new BufferedOutputStream(new FileOutputStream(output))) {
                    int read;
                    while ((read = zis.read(buffer)) != -1) {
                        out.write(buffer, 0, read);
                    }
                }

                extractedCount++;
                zis.closeEntry();
            }
        }

        return extractedCount;
    }

    private void startAudioCapturePipeline() {
        if (visualizer != null || mediaPlayer != null) {
            return;
        }

        if (tryStartGlobalOutputCapture()) {
            return;
        }

        startMediaPlayerFallback();
    }

    private boolean tryStartGlobalOutputCapture() {
        try {
            releaseVisualizer();
            visualizer = new Visualizer(0);
            visualizer.setCaptureSize(Visualizer.getCaptureSizeRange()[1]);
            int captureRate = Math.max(Visualizer.getMaxCaptureRate() / 2, 10000);
            visualizer.setDataCaptureListener(
                    new Visualizer.OnDataCaptureListener() {
                        @Override
                        public void onWaveFormDataCapture(Visualizer visualizer, byte[] waveform, int samplingRate) {
                            pushWaveformToNative(waveform);
                        }

                        @Override
                        public void onFftDataCapture(Visualizer visualizer, byte[] fft, int samplingRate) {
                            // Waveform is enough for projectM PCM input.
                        }
                    },
                    captureRate,
                    true,
                    false
            );
            visualizer.setEnabled(true);
            audioMode = 1;
            mediaPlaying = false;
            currentMediaLabel = "system_mix";
            pushUiStateToNative();
            Log.i(TAG, "Using global output visualizer capture.");
            return true;
        } catch (Throwable t) {
            Log.w(TAG, "Global output capture unavailable, using media player fallback.", t);
            releaseVisualizer();
            return false;
        }
    }

    private void startMediaPlayerFallback() {
        rebuildMediaPlaylist();
        if (mediaPlaylist.isEmpty()) {
            Log.w(TAG, "No media source found. Native synthetic audio fallback remains active.");
            audioMode = 0;
            mediaPlaying = false;
            currentMediaLabel = "none";
            pushUiStateToNative();
            return;
        }

        mediaPlaylistIndex = 0;
        playMediaAtCurrentIndex();
    }

    private void togglePlaybackInternal() {
        if (mediaPlayer == null) {
            startMediaPlayerFallback();
            return;
        }

        try {
            if (mediaPlayer.isPlaying()) {
                mediaPlayer.pause();
                mediaPlaying = false;
            } else {
                mediaPlayer.start();
                mediaPlaying = true;
            }
            pushUiStateToNative();
        } catch (Throwable t) {
            Log.e(TAG, "Toggle playback failed", t);
        }
    }

    private void playNextTrackInternal() {
        if (mediaPlaylist.isEmpty()) {
            startMediaPlayerFallback();
            return;
        }
        mediaPlaylistIndex = (mediaPlaylistIndex + 1) % mediaPlaylist.size();
        playMediaAtCurrentIndex();
    }

    private void playPreviousTrackInternal() {
        if (mediaPlaylist.isEmpty()) {
            startMediaPlayerFallback();
            return;
        }
        mediaPlaylistIndex--;
        if (mediaPlaylistIndex < 0) {
            mediaPlaylistIndex = mediaPlaylist.size() - 1;
        }
        playMediaAtCurrentIndex();
    }

    private void playMediaAtCurrentIndex() {
        if (mediaPlaylist.isEmpty() || mediaPlaylistIndex < 0 || mediaPlaylistIndex >= mediaPlaylist.size()) {
            return;
        }

        String source = mediaPlaylist.get(mediaPlaylistIndex);

        try {
            releaseMediaPlayer();
            mediaPlayer = new MediaPlayer();
            mediaPlayer.setAudioAttributes(
                    new AudioAttributes.Builder()
                            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                            .setUsage(AudioAttributes.USAGE_MEDIA)
                            .build()
            );
            mediaPlayer.setLooping(false);

            mediaPlayer.setOnPreparedListener(mp -> {
                try {
                    mp.start();
                    attachVisualizerToSession(mp.getAudioSessionId());
                    mediaPlaying = true;
                    audioMode = 2;
                    currentMediaLabel = new File(source).getName();
                    pushUiStateToNative();
                    Log.i(TAG, "Media fallback started: " + source);
                } catch (Throwable t) {
                    Log.e(TAG, "Failed to start media fallback playback", t);
                }
            });

            mediaPlayer.setOnCompletionListener(mp -> playNextTrackInternal());

            mediaPlayer.setOnErrorListener((mp, what, extra) -> {
                Log.e(TAG, "MediaPlayer error what=" + what + " extra=" + extra);
                playNextTrackInternal();
                return true;
            });

            if (source.startsWith("http://") || source.startsWith("https://")) {
                mediaPlayer.setDataSource(source);
                currentMediaLabel = source;
            } else {
                File sourceFile = new File(source);
                mediaPlayer.setDataSource(this, Uri.fromFile(sourceFile));
                currentMediaLabel = sourceFile.getName();
            }

            audioMode = 2;
            mediaPlaying = false;
            pushUiStateToNative();

            mediaPlayer.prepareAsync();
        } catch (Throwable t) {
            Log.e(TAG, "Failed to initialize media fallback", t);
            releaseMediaPlayer();
            mediaPlaying = false;
            pushUiStateToNative();
        }
    }

    private void attachVisualizerToSession(int audioSessionId) {
        try {
            releaseVisualizer();
            visualizer = new Visualizer(audioSessionId);
            visualizer.setCaptureSize(Visualizer.getCaptureSizeRange()[1]);
            int captureRate = Math.max(Visualizer.getMaxCaptureRate() / 2, 10000);
            visualizer.setDataCaptureListener(
                    new Visualizer.OnDataCaptureListener() {
                        @Override
                        public void onWaveFormDataCapture(Visualizer visualizer, byte[] waveform, int samplingRate) {
                            pushWaveformToNative(waveform);
                        }

                        @Override
                        public void onFftDataCapture(Visualizer visualizer, byte[] fft, int samplingRate) {
                            // Waveform is enough for projectM PCM input.
                        }
                    },
                    captureRate,
                    true,
                    false
            );
            visualizer.setEnabled(true);
        } catch (Throwable t) {
            Log.e(TAG, "Failed to attach visualizer to audio session " + audioSessionId, t);
            releaseVisualizer();
        }
    }

    private void rebuildMediaPlaylist() {
        mediaPlaylist.clear();

        File sourceHint = new File(getFilesDir(), "media_source.txt");
        if (sourceHint.exists()) {
            try (BufferedReader reader = new BufferedReader(new FileReader(sourceHint))) {
                String line = reader.readLine();
                if (line != null) {
                    String source = line.trim();
                    if (!source.isEmpty()) {
                        if (source.startsWith("http://") || source.startsWith("https://")) {
                            mediaPlaylist.add(source);
                            return;
                        }
                        File sourceFile = new File(source);
                        if (sourceFile.exists()) {
                            mediaPlaylist.add(sourceFile.getAbsolutePath());
                            return;
                        }
                    }
                }
            } catch (IOException e) {
                Log.w(TAG, "Failed reading media source hint", e);
            }
        }

        List<String> discovered = new ArrayList<>();
        File appMusic = getExternalFilesDir(Environment.DIRECTORY_MUSIC);
        collectAudioFiles(appMusic, 3, discovered);

        File publicMusic = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MUSIC);
        collectAudioFiles(publicMusic, 4, discovered);
        Collections.sort(discovered);
        mediaPlaylist.addAll(discovered);
    }

    private void collectAudioFiles(File dir, int depthRemaining, List<String> out) {
        if (dir == null || out == null || depthRemaining < 0 || !dir.exists() || !dir.isDirectory()) {
            return;
        }

        File[] children = dir.listFiles();
        if (children == null) {
            return;
        }

        for (File child : children) {
            if (child.isFile() && isAudioExtension(child.getName())) {
                out.add(child.getAbsolutePath());
            }
        }

        for (File child : children) {
            if (child.isDirectory()) {
                collectAudioFiles(child, depthRemaining - 1, out);
            }
        }
    }

    private boolean isAudioExtension(String filename) {
        String lower = filename.toLowerCase(Locale.ROOT);
        return lower.endsWith(".mp3")
                || lower.endsWith(".m4a")
                || lower.endsWith(".aac")
                || lower.endsWith(".ogg")
                || lower.endsWith(".wav")
                || lower.endsWith(".flac");
    }

    private void pushWaveformToNative(byte[] waveform) {
        if (waveform == null || waveform.length == 0) {
            return;
        }

        int frames = waveform.length;
        float[] stereo = new float[frames * 2];

        for (int i = 0; i < frames; i++) {
            float sample = (((waveform[i] & 0xFF) - 128.0f) / 128.0f);
            stereo[2 * i] = sample;
            stereo[2 * i + 1] = sample;
        }

        nativePushAudioPcm(stereo, frames);
    }

    private void releaseVisualizer() {
        if (visualizer != null) {
            try {
                visualizer.setEnabled(false);
                visualizer.release();
            } catch (Throwable ignored) {
            }
            visualizer = null;
        }
        if (audioMode == 1) {
            audioMode = 0;
            pushUiStateToNative();
        }
    }

    private void releaseMediaPlayer() {
        if (mediaPlayer != null) {
            try {
                mediaPlayer.stop();
            } catch (Throwable ignored) {
            }
            try {
                mediaPlayer.release();
            } catch (Throwable ignored) {
            }
            mediaPlayer = null;
        }
        mediaPlaying = false;
        if (audioMode == 2) {
            audioMode = 0;
        }
        pushUiStateToNative();
    }
}
