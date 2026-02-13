package com.projectm.questxr;

import android.Manifest;
import android.app.NativeActivity;
import android.content.pm.PackageManager;
import android.media.AudioFormat;
import android.media.AudioAttributes;
import android.media.AudioRecord;
import android.media.MediaPlayer;
import android.media.MediaRecorder;
import android.media.audiofx.AcousticEchoCanceler;
import android.media.audiofx.AutomaticGainControl;
import android.media.audiofx.NoiseSuppressor;
import android.media.audiofx.Visualizer;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
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
    private static final int AUDIO_MODE_SYNTHETIC = 0;
    private static final int AUDIO_MODE_GLOBAL_CAPTURE = 1;
    private static final int AUDIO_MODE_MEDIA_FALLBACK = 2;
    private static final int AUDIO_MODE_MICROPHONE = 3;
    private static final float VISUALIZER_GAIN_GLOBAL = 1.0f;
    private static final float VISUALIZER_GAIN_MEDIA = 1.2f;
    private static final float MICROPHONE_GAIN = 4.5f;
    private static final float MICROPHONE_TARGET_RMS = 0.16f;
    private static final float MICROPHONE_MIN_RMS_FOR_BOOST = 0.00025f;
    private static final float MICROPHONE_MAX_ADAPTIVE_GAIN = 80.0f;
    private static final float MICROPHONE_GAIN_ATTACK = 0.45f;
    private static final float MICROPHONE_GAIN_RELEASE = 0.12f;
    private static final float MICROPHONE_ENERGY_ACTIVE_RMS = 0.00012f;
    private static final long MICROPHONE_ONSET_COOLDOWN_MS = 140L;
    private static final long MICROPHONE_BEAT_FALLBACK_SILENCE_MS = 2200L;
    private static final float MICROPHONE_BEAT_FALLBACK_BPM = 118.0f;
    private static final float MICROPHONE_ONSET_FLOOR_MULTIPLIER = 1.8f;
    private static final float MICROPHONE_ONSET_FLOOR_OFFSET = 0.00003f;
    private static final float MICROPHONE_BEAT_INJECT_GAIN = 0.38f;
    private static final float MICROPHONE_BEAT_DECAY_PER_SAMPLE = 0.9935f;
    private static final float MICROPHONE_BEAT_KICK_HZ = 82.0f;
    private static final long MICROPHONE_LEVEL_LOG_INTERVAL_MS = 2000L;
    private static final float ACTIVE_WAVEFORM_RMS_THRESHOLD = 0.010f;

    static {
        System.loadLibrary("projectm_quest_openxr");
    }

    private final ExecutorService ioExecutor = Executors.newSingleThreadExecutor();
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    private Visualizer visualizer;
    private MediaPlayer mediaPlayer;
    private AudioRecord microphoneRecord;
    private Thread microphoneThread;
    private volatile boolean microphoneCaptureRunning;
    private volatile float microphoneAdaptiveGain = 1.0f;
    private volatile float microphoneNoiseFloor = 0.00008f;
    private volatile float microphoneBeatPulse = 0.0f;
    private volatile float microphoneBeatKickPhase = 0.0f;
    private volatile float microphoneFallbackBeatPhase = 0.0f;
    private long microphoneLastLevelLogUptimeMs = 0L;
    private long microphoneLastBeatUptimeMs = 0L;
    private long microphoneLastEnergyUptimeMs = 0L;
    private AutomaticGainControl microphoneAgc;
    private NoiseSuppressor microphoneNoiseSuppressor;
    private AcousticEchoCanceler microphoneEchoCanceler;
    private final List<String> mediaPlaylist = new ArrayList<>();
    private int mediaPlaylistIndex = -1;
    private int audioMode = AUDIO_MODE_SYNTHETIC;
    private int preferredAudioMode = AUDIO_MODE_GLOBAL_CAPTURE;
    private boolean mediaPlaying;
    private String currentMediaLabel = "none";
    private int activeVisualizerSessionId = Integer.MIN_VALUE;
    private long lastWaveformCaptureUptimeMs = 0L;
    private long lastWaveformEnergyUptimeMs = 0L;
    private int mediaCaptureExpectedSessionId = -1;
    private long mediaCaptureLastSwitchUptimeMs = 0L;
    private final Runnable mediaCaptureHealthCheckRunnable = this::checkMediaCaptureHealth;

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
        clearMediaCaptureHealthCheck();
        stopMicrophoneCapture();
        releaseVisualizer();
        releaseMediaPlayer();
        ioExecutor.shutdownNow();
        super.onDestroy();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == PERMISSION_REQUEST_CODE) {
            selectAudioInputMode(preferredAudioMode, false);
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

    public void onNativeCycleAudioInput() {
        runOnUiThread(this::cycleAudioInputInternal);
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
        selectAudioInputMode(preferredAudioMode, false);
    }

    private void cycleAudioInputInternal() {
        int[] modeOrder = {
                AUDIO_MODE_GLOBAL_CAPTURE,
                AUDIO_MODE_MEDIA_FALLBACK,
                AUDIO_MODE_MICROPHONE,
                AUDIO_MODE_SYNTHETIC
        };
        int currentIndex = 0;
        for (int i = 0; i < modeOrder.length; i++) {
            if (modeOrder[i] == preferredAudioMode) {
                currentIndex = i;
                break;
            }
        }
        int nextIndex = (currentIndex + 1) % modeOrder.length;
        preferredAudioMode = modeOrder[nextIndex];
        selectAudioInputMode(preferredAudioMode, true);
    }

    private void selectAudioInputMode(int requestedMode, boolean strict) {
        if (activateAudioMode(requestedMode)) {
            preferredAudioMode = requestedMode;
            return;
        }

        if (!strict) {
            int[] fallbackModes = {
                    AUDIO_MODE_GLOBAL_CAPTURE,
                    AUDIO_MODE_MEDIA_FALLBACK,
                    AUDIO_MODE_MICROPHONE
            };
            for (int mode : fallbackModes) {
                if (mode == requestedMode) {
                    continue;
                }
                if (activateAudioMode(mode)) {
                    preferredAudioMode = mode;
                    return;
                }
            }
        }

        setSyntheticMode(modeUnavailableLabel(requestedMode));
    }

    private boolean activateAudioMode(int mode) {
        clearMediaCaptureHealthCheck();
        stopMicrophoneCapture();
        releaseVisualizer();
        if (mode != AUDIO_MODE_MEDIA_FALLBACK) {
            releaseMediaPlayer();
        }

        switch (mode) {
            case AUDIO_MODE_GLOBAL_CAPTURE:
                return startGlobalOutputCaptureMode();
            case AUDIO_MODE_MEDIA_FALLBACK:
                return startMediaPlayerMode();
            case AUDIO_MODE_MICROPHONE:
                return startMicrophoneCaptureMode();
            case AUDIO_MODE_SYNTHETIC:
            default:
                setSyntheticMode("synthetic");
                return true;
        }
    }

    private boolean startGlobalOutputCaptureMode() {
        if (!attachVisualizerToSession(0, "global output")) {
            return false;
        }
        audioMode = AUDIO_MODE_GLOBAL_CAPTURE;
        mediaPlaying = false;
        currentMediaLabel = "system_mix";
        pushUiStateToNative();
        Log.i(TAG, "Using global output visualizer capture.");
        return true;
    }

    private boolean startMediaPlayerMode() {
        rebuildMediaPlaylist();
        if (mediaPlaylist.isEmpty()) {
            Log.w(TAG, "No media source found for media fallback mode.");
            return false;
        }

        if (mediaPlaylistIndex < 0 || mediaPlaylistIndex >= mediaPlaylist.size()) {
            mediaPlaylistIndex = 0;
        }
        return playMediaAtCurrentIndex();
    }

    private void togglePlaybackInternal() {
        if (preferredAudioMode != AUDIO_MODE_MEDIA_FALLBACK || mediaPlayer == null) {
            preferredAudioMode = AUDIO_MODE_MEDIA_FALLBACK;
            selectAudioInputMode(preferredAudioMode, true);
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
        if (preferredAudioMode != AUDIO_MODE_MEDIA_FALLBACK || mediaPlayer == null || mediaPlaylist.isEmpty()) {
            preferredAudioMode = AUDIO_MODE_MEDIA_FALLBACK;
            selectAudioInputMode(preferredAudioMode, true);
            return;
        }
        mediaPlaylistIndex = (mediaPlaylistIndex + 1) % mediaPlaylist.size();
        playMediaAtCurrentIndex();
    }

    private void playPreviousTrackInternal() {
        if (preferredAudioMode != AUDIO_MODE_MEDIA_FALLBACK || mediaPlayer == null || mediaPlaylist.isEmpty()) {
            preferredAudioMode = AUDIO_MODE_MEDIA_FALLBACK;
            selectAudioInputMode(preferredAudioMode, true);
            return;
        }
        mediaPlaylistIndex--;
        if (mediaPlaylistIndex < 0) {
            mediaPlaylistIndex = mediaPlaylist.size() - 1;
        }
        playMediaAtCurrentIndex();
    }

    private boolean playMediaAtCurrentIndex() {
        if (mediaPlaylist.isEmpty() || mediaPlaylistIndex < 0 || mediaPlaylistIndex >= mediaPlaylist.size()) {
            return false;
        }

        final String source = mediaPlaylist.get(mediaPlaylistIndex);

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
                    boolean captureAttached = attachVisualizerForMedia(mp.getAudioSessionId());
                    if (!captureAttached) {
                        Log.w(TAG, "Media playback started but no visualizer capture source is available.");
                    } else {
                        scheduleMediaCaptureHealthCheck(mp.getAudioSessionId());
                    }
                    mediaPlaying = true;
                    audioMode = AUDIO_MODE_MEDIA_FALLBACK;
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

            audioMode = AUDIO_MODE_MEDIA_FALLBACK;
            mediaPlaying = false;
            pushUiStateToNative();

            mediaPlayer.prepareAsync();
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Failed to initialize media fallback", t);
            releaseMediaPlayer();
            return false;
        }
    }

    private boolean attachVisualizerForMedia(int mediaSessionId) {
        // Prefer media-session capture for internal playback beat detection.
        if (attachVisualizerToSession(mediaSessionId, "media session " + mediaSessionId)) {
            return true;
        }
        return attachVisualizerToSession(0, "global mixer");
    }

    private boolean attachVisualizerToSession(int audioSessionId, String sourceLabel) {
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
            activeVisualizerSessionId = audioSessionId;
            Log.i(TAG, "Visualizer attached to " + sourceLabel + ".");
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Failed to attach visualizer to audio session " + audioSessionId, t);
            releaseVisualizer();
            return false;
        }
    }

    private boolean startMicrophoneCaptureMode() {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO)
                != PackageManager.PERMISSION_GRANTED) {
            Log.w(TAG, "Microphone mode requested but RECORD_AUDIO permission is missing.");
            return false;
        }

        releaseMicrophoneEffects();
        int[] sourcePriority = {
                MediaRecorder.AudioSource.CAMCORDER,
                MediaRecorder.AudioSource.UNPROCESSED,
                MediaRecorder.AudioSource.MIC,
                MediaRecorder.AudioSource.DEFAULT,
                MediaRecorder.AudioSource.VOICE_RECOGNITION
        };
        int[] channelPriority = {
                AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.CHANNEL_IN_STEREO
        };
        int[] sampleRates = {48000, 44100, 32000, 16000};
        for (int audioSource : sourcePriority) {
            for (int sampleRate : sampleRates) {
                for (int channelMask : channelPriority) {
                    int minBuffer = AudioRecord.getMinBufferSize(
                            sampleRate,
                            channelMask,
                            AudioFormat.ENCODING_PCM_16BIT
                    );
                    if (minBuffer <= 0) {
                        continue;
                    }

                    int channelCount = channelMask == AudioFormat.CHANNEL_IN_STEREO ? 2 : 1;
                    int bufferSize = Math.max(minBuffer, sampleRate / 6);
                    AudioRecord recorder = null;
                    try {
                        recorder = new AudioRecord(
                                audioSource,
                                sampleRate,
                                channelMask,
                                AudioFormat.ENCODING_PCM_16BIT,
                                bufferSize
                        );
                        if (recorder.getState() != AudioRecord.STATE_INITIALIZED) {
                            recorder.release();
                            continue;
                        }

                        recorder.startRecording();
                        if (recorder.getRecordingState() != AudioRecord.RECORDSTATE_RECORDING) {
                            recorder.release();
                            continue;
                        }
                    } catch (Throwable t) {
                        if (recorder != null) {
                            try {
                                recorder.release();
                            } catch (Throwable ignored) {
                            }
                        }
                        Log.w(TAG, "Could not start microphone capture source="
                                + audioSourceName(audioSource)
                                + " rate=" + sampleRate + " Hz channels=" + channelCount, t);
                        continue;
                    }

                    final AudioRecord activeRecorder = recorder;
                    microphoneRecord = activeRecorder;
                    configureMicrophoneBypassEffects(activeRecorder.getAudioSessionId());
                    microphoneAdaptiveGain = 1.0f;
                    microphoneNoiseFloor = MICROPHONE_MIN_RMS_FOR_BOOST;
                    microphoneBeatPulse = 0.0f;
                    microphoneBeatKickPhase = 0.0f;
                    microphoneFallbackBeatPhase = 0.0f;
                    microphoneLastLevelLogUptimeMs = 0L;
                    microphoneLastBeatUptimeMs = SystemClock.uptimeMillis();
                    microphoneLastEnergyUptimeMs = microphoneLastBeatUptimeMs;
                    microphoneCaptureRunning = true;
                    microphoneThread = new Thread(
                            () -> pumpMicrophoneAudio(activeRecorder, channelCount, sampleRate),
                            "projectm-mic-capture");
                    microphoneThread.start();

                    audioMode = AUDIO_MODE_MICROPHONE;
                    mediaPlaying = false;
                    currentMediaLabel = "mic_" + audioSourceName(audioSource).toLowerCase(Locale.ROOT)
                            + "_" + (channelCount == 2 ? "st" : "mono");
                    pushUiStateToNative();
                    Log.i(TAG, "Using microphone capture v3 source=" + audioSourceName(audioSource)
                            + " rate=" + sampleRate
                            + " Hz channels=" + channelCount
                            + " session=" + activeRecorder.getAudioSessionId() + ".");
                    return true;
                }
            }
        }

        return false;
    }

    private String audioSourceName(int audioSource) {
        switch (audioSource) {
            case MediaRecorder.AudioSource.MIC:
                return "MIC";
            case MediaRecorder.AudioSource.DEFAULT:
                return "DEFAULT";
            case MediaRecorder.AudioSource.CAMCORDER:
                return "CAMCORDER";
            case MediaRecorder.AudioSource.VOICE_RECOGNITION:
                return "VOICE_RECOGNITION";
            case MediaRecorder.AudioSource.UNPROCESSED:
                return "UNPROCESSED";
            default:
                return "SRC_" + audioSource;
        }
    }

    private void configureMicrophoneBypassEffects(int audioSessionId) {
        releaseMicrophoneEffects();
        if (audioSessionId <= 0) {
            return;
        }

        microphoneAgc = disableAutomaticGainControl(audioSessionId);
        microphoneNoiseSuppressor = disableNoiseSuppressor(audioSessionId);
        microphoneEchoCanceler = disableEchoCanceler(audioSessionId);
    }

    private AutomaticGainControl disableAutomaticGainControl(int audioSessionId) {
        if (!AutomaticGainControl.isAvailable()) {
            Log.i(TAG, "AGC unavailable for audio session " + audioSessionId + ".");
            return null;
        }

        try {
            AutomaticGainControl agc = AutomaticGainControl.create(audioSessionId);
            if (agc == null) {
                Log.w(TAG, "AGC instance is null for audio session " + audioSessionId + ".");
                return null;
            }
            agc.setEnabled(false);
            Log.i(TAG, "Disabled AGC for audio session " + audioSessionId + ".");
            return agc;
        } catch (Throwable t) {
            Log.w(TAG, "Failed disabling AGC for audio session " + audioSessionId + ".", t);
            return null;
        }
    }

    private NoiseSuppressor disableNoiseSuppressor(int audioSessionId) {
        if (!NoiseSuppressor.isAvailable()) {
            Log.i(TAG, "NoiseSuppressor unavailable for audio session " + audioSessionId + ".");
            return null;
        }

        try {
            NoiseSuppressor noiseSuppressor = NoiseSuppressor.create(audioSessionId);
            if (noiseSuppressor == null) {
                Log.w(TAG, "NoiseSuppressor instance is null for audio session " + audioSessionId + ".");
                return null;
            }
            noiseSuppressor.setEnabled(false);
            Log.i(TAG, "Disabled NoiseSuppressor for audio session " + audioSessionId + ".");
            return noiseSuppressor;
        } catch (Throwable t) {
            Log.w(TAG, "Failed disabling NoiseSuppressor for audio session " + audioSessionId + ".", t);
            return null;
        }
    }

    private AcousticEchoCanceler disableEchoCanceler(int audioSessionId) {
        if (!AcousticEchoCanceler.isAvailable()) {
            Log.i(TAG, "AcousticEchoCanceler unavailable for audio session " + audioSessionId + ".");
            return null;
        }

        try {
            AcousticEchoCanceler echoCanceler = AcousticEchoCanceler.create(audioSessionId);
            if (echoCanceler == null) {
                Log.w(TAG, "AcousticEchoCanceler instance is null for audio session " + audioSessionId + ".");
                return null;
            }
            echoCanceler.setEnabled(false);
            Log.i(TAG, "Disabled AcousticEchoCanceler for audio session " + audioSessionId + ".");
            return echoCanceler;
        } catch (Throwable t) {
            Log.w(TAG, "Failed disabling AcousticEchoCanceler for audio session " + audioSessionId + ".", t);
            return null;
        }
    }

    private void pumpMicrophoneAudio(AudioRecord recorder, int channelCount, int sampleRate) {
        int safeChannelCount = Math.max(1, channelCount);
        int safeSampleRate = Math.max(8000, sampleRate);
        short[] pcm = new short[1024 * safeChannelCount];
        float[] mono = new float[1024];

        while (microphoneCaptureRunning && recorder == microphoneRecord) {
            int samplesRead;
            try {
                samplesRead = recorder.read(pcm, 0, pcm.length);
            } catch (Throwable t) {
                Log.w(TAG, "Microphone read failed.", t);
                break;
            }

            if (samplesRead <= 0) {
                continue;
            }
            int framesRead = samplesRead / safeChannelCount;
            if (framesRead <= 0) {
                continue;
            }

            float sumSquares = 0.0f;
            float peak = 0.0f;
            for (int i = 0; i < framesRead; i++) {
                float mixed = 0.0f;
                int base = i * safeChannelCount;
                for (int c = 0; c < safeChannelCount; c++) {
                    float channelSample = pcm[base + c] / 32768.0f;
                    if (c == 0 || Math.abs(channelSample) > Math.abs(mixed)) {
                        mixed = channelSample;
                    }
                }
                mono[i] = mixed;
                sumSquares += mixed * mixed;
                float abs = Math.abs(mixed);
                if (abs > peak) {
                    peak = abs;
                }
            }

            float rms = (float) Math.sqrt(sumSquares / Math.max(1, framesRead));
            if (rms >= MICROPHONE_ENERGY_ACTIVE_RMS) {
                microphoneLastEnergyUptimeMs = SystemClock.uptimeMillis();
            }

            if (rms <= microphoneNoiseFloor * 1.2f) {
                microphoneNoiseFloor = microphoneNoiseFloor * 0.985f + rms * 0.015f;
            } else {
                microphoneNoiseFloor = microphoneNoiseFloor * 0.998f + rms * 0.002f;
            }
            if (microphoneNoiseFloor < MICROPHONE_MIN_RMS_FOR_BOOST * 0.25f) {
                microphoneNoiseFloor = MICROPHONE_MIN_RMS_FOR_BOOST * 0.25f;
            }

            long now = SystemClock.uptimeMillis();
            float onsetThreshold = microphoneNoiseFloor * MICROPHONE_ONSET_FLOOR_MULTIPLIER
                    + MICROPHONE_ONSET_FLOOR_OFFSET;
            boolean onset = rms >= onsetThreshold
                    && now - microphoneLastBeatUptimeMs >= MICROPHONE_ONSET_COOLDOWN_MS;
            if (onset) {
                microphoneBeatPulse = 1.0f;
                microphoneBeatKickPhase = 0.0f;
                microphoneLastBeatUptimeMs = now;
            }

            if (now - microphoneLastBeatUptimeMs >= MICROPHONE_BEAT_FALLBACK_SILENCE_MS
                    && now - microphoneLastEnergyUptimeMs <= 10000L) {
                float beatsPerSecond = MICROPHONE_BEAT_FALLBACK_BPM / 60.0f;
                microphoneFallbackBeatPhase += (framesRead / (float) safeSampleRate) * beatsPerSecond;
                while (microphoneFallbackBeatPhase >= 1.0f) {
                    microphoneFallbackBeatPhase -= 1.0f;
                    microphoneBeatPulse = 1.0f;
                    microphoneBeatKickPhase = 0.0f;
                    microphoneLastBeatUptimeMs = now;
                }
            }

            float desiredAdaptiveGain = 1.0f;
            if (rms >= MICROPHONE_MIN_RMS_FOR_BOOST) {
                desiredAdaptiveGain = MICROPHONE_TARGET_RMS / rms;
                if (desiredAdaptiveGain < 1.0f) {
                    desiredAdaptiveGain = 1.0f;
                } else if (desiredAdaptiveGain > MICROPHONE_MAX_ADAPTIVE_GAIN) {
                    desiredAdaptiveGain = MICROPHONE_MAX_ADAPTIVE_GAIN;
                }
            }

            float smoothing = desiredAdaptiveGain > microphoneAdaptiveGain
                    ? MICROPHONE_GAIN_ATTACK
                    : MICROPHONE_GAIN_RELEASE;
            microphoneAdaptiveGain += (desiredAdaptiveGain - microphoneAdaptiveGain) * smoothing;
            float totalGain = MICROPHONE_GAIN * microphoneAdaptiveGain;

            float[] stereo = new float[framesRead * 2];
            float beatPulse = microphoneBeatPulse;
            float beatKickPhase = microphoneBeatKickPhase;
            for (int i = 0; i < framesRead; i++) {
                float sample = mono[i] * totalGain;
                if (beatPulse > 0.0008f) {
                    float kick = (float) Math.sin(2.0 * Math.PI * MICROPHONE_BEAT_KICK_HZ * beatKickPhase);
                    sample += kick * beatPulse * MICROPHONE_BEAT_INJECT_GAIN;
                    beatKickPhase += 1.0f / safeSampleRate;
                    if (beatKickPhase >= 1.0f) {
                        beatKickPhase -= 1.0f;
                    }
                    beatPulse *= MICROPHONE_BEAT_DECAY_PER_SAMPLE;
                }
                if (sample > 1.0f) {
                    sample = 1.0f;
                } else if (sample < -1.0f) {
                    sample = -1.0f;
                }
                stereo[2 * i] = sample;
                stereo[2 * i + 1] = sample;
            }
            microphoneBeatPulse = beatPulse;
            microphoneBeatKickPhase = beatKickPhase;
            nativePushAudioPcm(stereo, framesRead);

            if (now - microphoneLastLevelLogUptimeMs >= MICROPHONE_LEVEL_LOG_INTERVAL_MS) {
                microphoneLastLevelLogUptimeMs = now;
                Log.i(TAG, String.format(
                        Locale.US,
                        "Mic level rms=%.5f peak=%.5f floor=%.5f thr=%.5f pulse=%.3f adaptive=%.2f total=%.2f channels=%d",
                        rms,
                        peak,
                        microphoneNoiseFloor,
                        onsetThreshold,
                        microphoneBeatPulse,
                        microphoneAdaptiveGain,
                        totalGain,
                        safeChannelCount));
            }
        }
    }

    private void stopMicrophoneCapture() {
        microphoneCaptureRunning = false;
        releaseMicrophoneEffects();

        AudioRecord recorder = microphoneRecord;
        microphoneRecord = null;
        if (recorder != null) {
            try {
                recorder.stop();
            } catch (Throwable ignored) {
            }
            try {
                recorder.release();
            } catch (Throwable ignored) {
            }
        }

        Thread thread = microphoneThread;
        microphoneThread = null;
        if (thread != null && thread != Thread.currentThread()) {
            try {
                thread.join(250);
            } catch (InterruptedException ignored) {
                Thread.currentThread().interrupt();
            }
        }
    }

    private void releaseMicrophoneEffects() {
        if (microphoneAgc != null) {
            try {
                microphoneAgc.release();
            } catch (Throwable ignored) {
            }
            microphoneAgc = null;
        }
        if (microphoneNoiseSuppressor != null) {
            try {
                microphoneNoiseSuppressor.release();
            } catch (Throwable ignored) {
            }
            microphoneNoiseSuppressor = null;
        }
        if (microphoneEchoCanceler != null) {
            try {
                microphoneEchoCanceler.release();
            } catch (Throwable ignored) {
            }
            microphoneEchoCanceler = null;
        }
    }

    private void setSyntheticMode(String label) {
        audioMode = AUDIO_MODE_SYNTHETIC;
        mediaPlaying = false;
        currentMediaLabel = label == null || label.isEmpty() ? "none" : label;
        pushUiStateToNative();
    }

    private String modeUnavailableLabel(int mode) {
        switch (mode) {
            case AUDIO_MODE_GLOBAL_CAPTURE:
                return "global_unavailable";
            case AUDIO_MODE_MEDIA_FALLBACK:
                return "media_unavailable";
            case AUDIO_MODE_MICROPHONE:
                return "mic_unavailable";
            case AUDIO_MODE_SYNTHETIC:
            default:
                return "synthetic";
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
        lastWaveformCaptureUptimeMs = SystemClock.uptimeMillis();
        float gain = audioMode == AUDIO_MODE_MEDIA_FALLBACK
                ? VISUALIZER_GAIN_MEDIA
                : VISUALIZER_GAIN_GLOBAL;

        int frames = waveform.length;
        float[] stereo = new float[frames * 2];
        float sumSquares = 0.0f;

        for (int i = 0; i < frames; i++) {
            float sample = (((waveform[i] & 0xFF) - 128.0f) / 128.0f) * gain;
            if (sample > 1.0f) {
                sample = 1.0f;
            } else if (sample < -1.0f) {
                sample = -1.0f;
            }
            sumSquares += sample * sample;
            stereo[2 * i] = sample;
            stereo[2 * i + 1] = sample;
        }

        float rms = (float) Math.sqrt(sumSquares / Math.max(1, frames));
        if (rms >= ACTIVE_WAVEFORM_RMS_THRESHOLD) {
            lastWaveformEnergyUptimeMs = lastWaveformCaptureUptimeMs;
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
        activeVisualizerSessionId = Integer.MIN_VALUE;
    }

    private void releaseMediaPlayer() {
        clearMediaCaptureHealthCheck();
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
    }

    private void scheduleMediaCaptureHealthCheck(int mediaSessionId) {
        clearMediaCaptureHealthCheck();
        mediaCaptureExpectedSessionId = mediaSessionId;
        lastWaveformCaptureUptimeMs = SystemClock.uptimeMillis();
        lastWaveformEnergyUptimeMs = lastWaveformCaptureUptimeMs;
        mediaCaptureLastSwitchUptimeMs = lastWaveformCaptureUptimeMs;
        mainHandler.postDelayed(mediaCaptureHealthCheckRunnable, 1800);
    }

    private void clearMediaCaptureHealthCheck() {
        mainHandler.removeCallbacks(mediaCaptureHealthCheckRunnable);
        mediaCaptureExpectedSessionId = -1;
        mediaCaptureLastSwitchUptimeMs = 0L;
    }

    private void checkMediaCaptureHealth() {
        if (preferredAudioMode != AUDIO_MODE_MEDIA_FALLBACK || mediaPlayer == null) {
            return;
        }

        long nowMs = SystemClock.uptimeMillis();
        long silentForMs = nowMs - lastWaveformEnergyUptimeMs;
        if (silentForMs < 1400) {
            mainHandler.postDelayed(mediaCaptureHealthCheckRunnable, 1200);
            return;
        }

        if (mediaCaptureExpectedSessionId >= 0 && nowMs - mediaCaptureLastSwitchUptimeMs >= 2200) {
            int retrySessionId = activeVisualizerSessionId == mediaCaptureExpectedSessionId
                    ? 0
                    : mediaCaptureExpectedSessionId;
            if (attachVisualizerToSession(
                    retrySessionId,
                    retrySessionId == 0 ? "global mixer retry" : "media session retry")) {
                mediaCaptureLastSwitchUptimeMs = nowMs;
                lastWaveformCaptureUptimeMs = nowMs;
                Log.w(TAG,
                        "Low-energy media waveform (silent for " + silentForMs
                                + " ms); switched capture session to " + retrySessionId + ".");
            }
        }
        mainHandler.postDelayed(mediaCaptureHealthCheckRunnable, 1500);
    }
}
