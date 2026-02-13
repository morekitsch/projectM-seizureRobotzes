#include <android/asset_manager.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <jni.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <projectM-4/projectM.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/system_properties.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#include <deque>

namespace {

constexpr char kLogTag[] = "projectM-QuestXR";

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, kLogTag, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, kLogTag, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kLogTag, __VA_ARGS__)

constexpr float kNearZ = 0.05f;
constexpr float kFarZ = 100.0f;
constexpr uint32_t kProjectMWidth = 2048;
constexpr uint32_t kProjectMHeight = 1024;
constexpr uint32_t kPcmFramesPerPush = 512;
constexpr float kAudioSampleRate = 48000.0f;
constexpr float kAudioCarrierFrequency = 220.0f;
constexpr float kAudioBeatFrequency = 1.9f;
constexpr float kPi = 3.14159265358979323846f;
constexpr double kPresetSwitchSeconds = 20.0;
constexpr double kPresetScanIntervalSeconds = 10.0;
constexpr double kAudioFallbackDelaySeconds = 3.0;
constexpr size_t kMaxQueuedAudioFrames = 48000 * 2;
constexpr float kHudDistance = 0.72f;
constexpr float kHudDistanceHandTracking = 0.55f;
constexpr float kHudVerticalOffset = -0.27f;
constexpr float kHudVerticalOffsetHandTracking = -0.08f;
constexpr float kHudWidth = 0.68f;
constexpr float kHudHeight = 0.36f;
constexpr double kHudVisibleOnStartSeconds = 8.0;
constexpr double kHudVisibleAfterInteractionSeconds = 6.0;
constexpr double kHudVisibleAfterStatusChangeSeconds = 3.0;
constexpr double kHudInputFeedbackSeconds = 1.4;
constexpr float kTriggerPressThreshold = 0.75f;
constexpr double kHandModeSwitchToHandDebounceSeconds = 0.08;
constexpr double kHandModeSwitchToControllerDebounceSeconds = 0.16;
constexpr float kHudTouchHoverDistance = 0.030f;
constexpr float kHudTouchActivationDistance = 0.010f;
constexpr float kHudTouchReleaseDistance = 0.018f;
constexpr float kHudTouchMaxPenetration = 0.015f;
constexpr float kHudTouchReleaseMaxPenetration = 0.028f;
constexpr float kHudTouchForwardOffset = 0.007f;
constexpr float kHudFlashPeak = 1.35f;
constexpr double kRuntimePropertyPollIntervalSeconds = 1.0;
constexpr double kPerfGraceAfterPresetSwitchSeconds = 4.0;
constexpr float kDefaultPerfAutoSkipMinFps = 42.0f;
constexpr double kDefaultPerfAutoSkipHoldSeconds = 2.0;
constexpr double kDefaultPerfAutoSkipCooldownSeconds = 8.0;
constexpr int kDefaultMeshWidth = 64;
constexpr int kDefaultMeshHeight = 48;
constexpr int kHudTextTextureWidth = 1024;
constexpr int kHudTextTextureHeight = 512;
constexpr int kHudGlyphWidth = 5;
constexpr int kHudGlyphHeight = 7;
constexpr int kHudStatusScale = 2;
constexpr int kHudDetailScale = 2;
constexpr int kHudActionScale = 4;
constexpr int kHudInputScale = 3;
constexpr int kHudTriggerScale = 3;

struct HudRect {
    float minU;
    float maxU;
    float minV;
    float maxV;
};

constexpr HudRect kHudRectPrevPreset{0.07f, 0.46f, 0.60f, 0.82f};
constexpr HudRect kHudRectNextPreset{0.54f, 0.93f, 0.60f, 0.82f};
constexpr HudRect kHudRectTogglePlay{0.07f, 0.46f, 0.30f, 0.52f};
constexpr HudRect kHudRectNextTrack{0.54f, 0.93f, 0.30f, 0.52f};
constexpr HudRect kHudRectPack{0.07f, 0.33f, 0.08f, 0.24f};
constexpr HudRect kHudRectCenter{0.37f, 0.63f, 0.08f, 0.24f};
constexpr HudRect kHudRectProjection{0.67f, 0.93f, 0.08f, 0.24f};

enum class HandSide : uint8_t {
    Left = 0,
    Right = 1,
};

enum class HudButtonId : uint8_t {
    None = 0,
    PrevPreset = 1,
    NextPreset = 2,
    TogglePlay = 3,
    NextTrack = 4,
    OptionalPack = 5,
    CycleAudio = 6,
    ToggleProjection = 7,
};

enum class HudPointerMode : uint8_t {
    None = 0,
    Ray = 1,
    Touch = 2,
};

struct HandBone {
    XrHandJointEXT from;
    XrHandJointEXT to;
};

constexpr std::array<HandBone, 24> kHandBones{{
    {XR_HAND_JOINT_WRIST_EXT, XR_HAND_JOINT_PALM_EXT},

    {XR_HAND_JOINT_PALM_EXT, XR_HAND_JOINT_THUMB_METACARPAL_EXT},
    {XR_HAND_JOINT_THUMB_METACARPAL_EXT, XR_HAND_JOINT_THUMB_PROXIMAL_EXT},
    {XR_HAND_JOINT_THUMB_PROXIMAL_EXT, XR_HAND_JOINT_THUMB_DISTAL_EXT},
    {XR_HAND_JOINT_THUMB_DISTAL_EXT, XR_HAND_JOINT_THUMB_TIP_EXT},

    {XR_HAND_JOINT_PALM_EXT, XR_HAND_JOINT_INDEX_METACARPAL_EXT},
    {XR_HAND_JOINT_INDEX_METACARPAL_EXT, XR_HAND_JOINT_INDEX_PROXIMAL_EXT},
    {XR_HAND_JOINT_INDEX_PROXIMAL_EXT, XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT},
    {XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT, XR_HAND_JOINT_INDEX_DISTAL_EXT},
    {XR_HAND_JOINT_INDEX_DISTAL_EXT, XR_HAND_JOINT_INDEX_TIP_EXT},

    {XR_HAND_JOINT_PALM_EXT, XR_HAND_JOINT_MIDDLE_METACARPAL_EXT},
    {XR_HAND_JOINT_MIDDLE_METACARPAL_EXT, XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT},
    {XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT, XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT},
    {XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT, XR_HAND_JOINT_MIDDLE_DISTAL_EXT},
    {XR_HAND_JOINT_MIDDLE_DISTAL_EXT, XR_HAND_JOINT_MIDDLE_TIP_EXT},

    {XR_HAND_JOINT_PALM_EXT, XR_HAND_JOINT_RING_METACARPAL_EXT},
    {XR_HAND_JOINT_RING_METACARPAL_EXT, XR_HAND_JOINT_RING_PROXIMAL_EXT},
    {XR_HAND_JOINT_RING_PROXIMAL_EXT, XR_HAND_JOINT_RING_INTERMEDIATE_EXT},
    {XR_HAND_JOINT_RING_INTERMEDIATE_EXT, XR_HAND_JOINT_RING_DISTAL_EXT},
    {XR_HAND_JOINT_RING_DISTAL_EXT, XR_HAND_JOINT_RING_TIP_EXT},

    {XR_HAND_JOINT_PALM_EXT, XR_HAND_JOINT_LITTLE_METACARPAL_EXT},
    {XR_HAND_JOINT_LITTLE_METACARPAL_EXT, XR_HAND_JOINT_LITTLE_PROXIMAL_EXT},
    {XR_HAND_JOINT_LITTLE_PROXIMAL_EXT, XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT},
    {XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT, XR_HAND_JOINT_LITTLE_DISTAL_EXT},
}};

constexpr std::array<XrHandJointEXT, 2> kHandHighlightJoints{
    XR_HAND_JOINT_INDEX_TIP_EXT,
    XR_HAND_JOINT_THUMB_TIP_EXT,
};

constexpr char kFallbackPreset[] =
    "[preset00]\n"
    "fDecay=0.98\n"
    "zoom=0.99\n"
    "rot=0.01*sin(time*0.5)\n"
    "warp=0.05\n"
    "wave_mode=7\n"
    "wave_r=1\n"
    "wave_g=0.6\n"
    "wave_b=0.2\n"
    "wave_a=1\n"
    "ob_size=0\n"
    "ib_size=0\n"
    "per_frame_1=zoom=1.0+0.03*sin(time*0.33);\n"
    "per_frame_2=wave_x=0.5+0.25*sin(time*0.71);\n"
    "per_frame_3=wave_y=0.5+0.2*cos(time*0.47);\n";

enum class ProjectionMode {
    FullSphere = 0,
    FrontDome = 1,
};

struct SphereVertex {
    float x;
    float y;
    float z;
};

struct XrSwapchainBundle {
    XrSwapchain handle{XR_NULL_HANDLE};
    int32_t width{0};
    int32_t height{0};
    std::vector<XrSwapchainImageOpenGLESKHR> images;
};

struct HandJointRenderState {
    bool isActive{false};
    std::array<glm::vec3, XR_HAND_JOINT_COUNT_EXT> positions{};
    std::array<uint8_t, XR_HAND_JOINT_COUNT_EXT> tracked{};
};

struct HandModeDebounceState {
    bool initialized{false};
    bool rawHandTracking{false};
    bool debouncedHandTracking{false};
    double rawStateSinceSeconds{0.0};
};

enum class AudioMode : int {
    Synthetic = 0,
    GlobalCapture = 1,
    MediaFallback = 2,
    Microphone = 3,
};

std::mutex g_uiMutex;
AudioMode g_audioMode = AudioMode::Synthetic;
bool g_mediaPlaying = false;
std::string g_mediaLabel = "none";

std::mutex g_audioMutex;
std::deque<float> g_audioQueueInterleavedStereo;

void EnqueueAudioFrames(const float* samplesInterleavedStereo, size_t frameCount) {
    if (samplesInterleavedStereo == nullptr || frameCount == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_audioMutex);

    const size_t sampleCount = frameCount * 2;
    if (sampleCount == 0) {
        return;
    }

    const size_t currentFrameCount = g_audioQueueInterleavedStereo.size() / 2;
    if (currentFrameCount + frameCount > kMaxQueuedAudioFrames) {
        const size_t overflowFrames = (currentFrameCount + frameCount) - kMaxQueuedAudioFrames;
        const size_t overflowSamples = overflowFrames * 2;
        for (size_t i = 0; i < overflowSamples && !g_audioQueueInterleavedStereo.empty(); ++i) {
            g_audioQueueInterleavedStereo.pop_front();
        }
    }

    for (size_t i = 0; i < sampleCount; ++i) {
        g_audioQueueInterleavedStereo.push_back(samplesInterleavedStereo[i]);
    }
}

size_t DequeueAudioFrames(float* outputInterleavedStereo, size_t maxFrames) {
    if (outputInterleavedStereo == nullptr || maxFrames == 0) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_audioMutex);
    const size_t availableFrames = g_audioQueueInterleavedStereo.size() / 2;
    const size_t framesToPop = std::min(maxFrames, availableFrames);
    const size_t samplesToPop = framesToPop * 2;

    for (size_t i = 0; i < samplesToPop; ++i) {
        outputInterleavedStereo[i] = g_audioQueueInterleavedStereo.front();
        g_audioQueueInterleavedStereo.pop_front();
    }

    return framesToPop;
}

bool EnsureDirectory(const std::string& path) {
    if (path.empty() || path == "/") {
        return true;
    }

    struct stat st {};
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }

    const auto slash = path.find_last_of('/');
    if (slash != std::string::npos && slash > 0) {
        if (!EnsureDirectory(path.substr(0, slash))) {
            return false;
        }
    }

    if (mkdir(path.c_str(), 0755) == 0) {
        return true;
    }

    if (errno == EEXIST) {
        return true;
    }

    LOGE("mkdir failed for %s: %d", path.c_str(), errno);
    return false;
}

bool CopyAssetFile(AAssetManager* manager, const std::string& assetPath, const std::string& outputPath) {
    AAsset* asset = AAssetManager_open(manager, assetPath.c_str(), AASSET_MODE_STREAMING);
    if (!asset) {
        LOGW("Could not open asset: %s", assetPath.c_str());
        return false;
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(AAsset_getLength(asset)));
    if (!buffer.empty()) {
        const int64_t read = AAsset_read(asset, buffer.data(), static_cast<int>(buffer.size()));
        if (read < 0 || static_cast<size_t>(read) != buffer.size()) {
            LOGE("Failed to read asset: %s", assetPath.c_str());
            AAsset_close(asset);
            return false;
        }
    }
    AAsset_close(asset);

    const auto slash = outputPath.find_last_of('/');
    if (slash != std::string::npos && !EnsureDirectory(outputPath.substr(0, slash))) {
        return false;
    }

    std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        LOGE("Failed to open output path: %s", outputPath.c_str());
        return false;
    }

    if (!buffer.empty()) {
        out.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    }

    return static_cast<bool>(out);
}

void CopyAssetDirectoryFlat(AAssetManager* manager, const std::string& assetDir, const std::string& outputDir) {
    AAssetDir* dir = AAssetManager_openDir(manager, assetDir.c_str());
    if (!dir) {
        return;
    }

    EnsureDirectory(outputDir);

    const char* filename = nullptr;
    while ((filename = AAssetDir_getNextFileName(dir)) != nullptr) {
        const std::string source = assetDir + "/" + filename;
        const std::string target = outputDir + "/" + filename;
        CopyAssetFile(manager, source, target);
    }

    AAssetDir_close(dir);
}

std::vector<std::string> CollectPresetFiles(const std::string& path) {
    std::vector<std::string> files;
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return files;
    }

    for (dirent* entry = readdir(dir); entry != nullptr; entry = readdir(dir)) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        std::string filename(entry->d_name);
        if (filename.size() < 5 || filename.substr(filename.size() - 5) != ".milk") {
            continue;
        }

        files.push_back(path + "/" + filename);
    }

    closedir(dir);
    std::sort(files.begin(), files.end());
    return files;
}

uint32_t CompileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_TRUE) {
        return shader;
    }

    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::vector<char> log(static_cast<size_t>(std::max(logLength, 1)));
    glGetShaderInfoLog(shader, logLength, nullptr, log.data());
    LOGE("Shader compile failed: %s", log.data());

    glDeleteShader(shader);
    return 0;
}

uint32_t LinkProgram(uint32_t vertexShader, uint32_t fragmentShader) {
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_TRUE) {
        return program;
    }

    GLint logLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
    std::vector<char> log(static_cast<size_t>(std::max(logLength, 1)));
    glGetProgramInfoLog(program, logLength, nullptr, log.data());
    LOGE("Program link failed: %s", log.data());

    glDeleteProgram(program);
    return 0;
}

glm::mat4 BuildProjectionMatrix(const XrFovf& fov, float nearZ, float farZ) {
    const float tanLeft = std::tan(fov.angleLeft);
    const float tanRight = std::tan(fov.angleRight);
    const float tanDown = std::tan(fov.angleDown);
    const float tanUp = std::tan(fov.angleUp);

    const float tanWidth = tanRight - tanLeft;
    const float tanHeight = tanUp - tanDown;

    glm::mat4 projection(0.0f);
    projection[0][0] = 2.0f / tanWidth;
    projection[1][1] = 2.0f / tanHeight;
    projection[2][0] = (tanRight + tanLeft) / tanWidth;
    projection[2][1] = (tanUp + tanDown) / tanHeight;
    projection[2][2] = -(farZ + nearZ) / (farZ - nearZ);
    projection[2][3] = -1.0f;
    projection[3][2] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
    return projection;
}

glm::mat4 BuildViewMatrix(const XrPosef& pose) {
    const glm::vec3 position(
        pose.position.x,
        pose.position.y,
        pose.position.z);
    const glm::quat orientation(
        pose.orientation.w,
        pose.orientation.x,
        pose.orientation.y,
        pose.orientation.z);

    const glm::mat4 world = glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(orientation);
    return glm::inverse(world);
}

std::string BasenamePath(const std::string& path) {
    if (path.empty()) {
        return std::string();
    }

    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos || slash + 1 >= path.size()) {
        return path;
    }
    return path.substr(slash + 1);
}

std::string StripExtension(std::string value) {
    const size_t dot = value.find_last_of('.');
    if (dot != std::string::npos) {
        value.erase(dot);
    }
    return value;
}

void ReplaceAll(std::string& value, const std::string& needle, const std::string& replacement) {
    if (needle.empty()) {
        return;
    }

    size_t start = 0;
    while ((start = value.find(needle, start)) != std::string::npos) {
        value.replace(start, needle.size(), replacement);
        start += replacement.size();
    }
}

std::string SanitizeHudText(const std::string& raw, size_t maxChars) {
    std::string normalized;
    normalized.reserve(raw.size());

    bool lastWasSpace = false;
    for (const unsigned char ch : raw) {
        char out = static_cast<char>(ch);
        if (out == '\n' || out == '\r' || out == '\t') {
            out = ' ';
        }
        if (out < 32 || out > 126) {
            out = '?';
        }

        if (out == ' ') {
            if (lastWasSpace) {
                continue;
            }
            lastWasSpace = true;
        } else {
            lastWasSpace = false;
        }

        normalized.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(out))));
    }

    while (!normalized.empty() && normalized.front() == ' ') {
        normalized.erase(normalized.begin());
    }
    while (!normalized.empty() && normalized.back() == ' ') {
        normalized.pop_back();
    }

    if (normalized.empty()) {
        normalized = "NONE";
    }

    if (normalized.size() > maxChars) {
        if (maxChars <= 3) {
            normalized.resize(maxChars);
        } else {
            normalized.resize(maxChars - 3);
            normalized += "...";
        }
    }

    return normalized;
}

std::string TrimAscii(const std::string& text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(start, end - start);
}

bool ReadSystemProperty(const char* key, std::string& valueOut) {
    if (key == nullptr || *key == '\0') {
        return false;
    }

    char buffer[PROP_VALUE_MAX] = {};
    const int len = __system_property_get(key, buffer);
    if (len <= 0) {
        return false;
    }

    valueOut.assign(buffer, static_cast<size_t>(len));
    return true;
}

bool ParseBoolText(const std::string& text, bool& valueOut) {
    std::string normalized = TrimAscii(text);
    std::transform(normalized.begin(),
                   normalized.end(),
                   normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        valueOut = true;
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        valueOut = false;
        return true;
    }

    return false;
}

bool ParseFloatText(const std::string& text, float& valueOut) {
    const std::string trimmed = TrimAscii(text);
    if (trimmed.empty()) {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const float parsed = std::strtof(trimmed.c_str(), &end);
    if (end == trimmed.c_str() || errno == ERANGE) {
        return false;
    }
    while (end != nullptr && *end != '\0') {
        if (std::isspace(static_cast<unsigned char>(*end)) == 0) {
            return false;
        }
        ++end;
    }

    valueOut = parsed;
    return true;
}

bool ParseIntPairText(const std::string& text, int& firstOut, int& secondOut) {
    int first = 0;
    int second = 0;
    if (std::sscanf(text.c_str(), "%d x %d", &first, &second) == 2 ||
        std::sscanf(text.c_str(), "%dX%d", &first, &second) == 2 ||
        std::sscanf(text.c_str(), "%d,%d", &first, &second) == 2) {
        firstOut = first;
        secondOut = second;
        return true;
    }
    return false;
}

using GlyphRows = std::array<uint8_t, kHudGlyphHeight>;

const GlyphRows& HudGlyphRows(char c) {
    static const GlyphRows kSpace{{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
    static const GlyphRows kUnknown{{0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}};
    static const GlyphRows kA{{0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
    static const GlyphRows kB{{0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}};
    static const GlyphRows kC{{0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}};
    static const GlyphRows kD{{0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}};
    static const GlyphRows kE{{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}};
    static const GlyphRows kF{{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}};
    static const GlyphRows kG{{0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}};
    static const GlyphRows kH{{0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}};
    static const GlyphRows kI{{0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}};
    static const GlyphRows kJ{{0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E}};
    static const GlyphRows kK{{0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}};
    static const GlyphRows kL{{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}};
    static const GlyphRows kM{{0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}};
    static const GlyphRows kN{{0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}};
    static const GlyphRows kO{{0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};
    static const GlyphRows kP{{0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}};
    static const GlyphRows kQ{{0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}};
    static const GlyphRows kR{{0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}};
    static const GlyphRows kS{{0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}};
    static const GlyphRows kT{{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}};
    static const GlyphRows kU{{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};
    static const GlyphRows kV{{0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}};
    static const GlyphRows kW{{0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}};
    static const GlyphRows kX{{0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}};
    static const GlyphRows kY{{0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}};
    static const GlyphRows kZ{{0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}};
    static const GlyphRows k0{{0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}};
    static const GlyphRows k1{{0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}};
    static const GlyphRows k2{{0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}};
    static const GlyphRows k3{{0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}};
    static const GlyphRows k4{{0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}};
    static const GlyphRows k5{{0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}};
    static const GlyphRows k6{{0x07, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}};
    static const GlyphRows k7{{0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}};
    static const GlyphRows k8{{0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}};
    static const GlyphRows k9{{0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x1C}};
    static const GlyphRows kColon{{0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00}};
    static const GlyphRows kDot{{0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06}};
    static const GlyphRows kDash{{0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}};
    static const GlyphRows kUnderscore{{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}};
    static const GlyphRows kSlash{{0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00}};
    static const GlyphRows kOpenParen{{0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}};
    static const GlyphRows kCloseParen{{0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}};
    static const GlyphRows kHash{{0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}};
    static const GlyphRows kPlus{{0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}};
    static const GlyphRows kEqual{{0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00, 0x00}};

    switch (c) {
        case 'A': return kA;
        case 'B': return kB;
        case 'C': return kC;
        case 'D': return kD;
        case 'E': return kE;
        case 'F': return kF;
        case 'G': return kG;
        case 'H': return kH;
        case 'I': return kI;
        case 'J': return kJ;
        case 'K': return kK;
        case 'L': return kL;
        case 'M': return kM;
        case 'N': return kN;
        case 'O': return kO;
        case 'P': return kP;
        case 'Q': return kQ;
        case 'R': return kR;
        case 'S': return kS;
        case 'T': return kT;
        case 'U': return kU;
        case 'V': return kV;
        case 'W': return kW;
        case 'X': return kX;
        case 'Y': return kY;
        case 'Z': return kZ;
        case '0': return k0;
        case '1': return k1;
        case '2': return k2;
        case '3': return k3;
        case '4': return k4;
        case '5': return k5;
        case '6': return k6;
        case '7': return k7;
        case '8': return k8;
        case '9': return k9;
        case ' ': return kSpace;
        case ':': return kColon;
        case '.': return kDot;
        case '-': return kDash;
        case '_': return kUnderscore;
        case '/': return kSlash;
        case '(': return kOpenParen;
        case ')': return kCloseParen;
        case '#': return kHash;
        case '+': return kPlus;
        case '=': return kEqual;
        default: return kUnknown;
    }
}

void SetHudPixel(std::vector<uint8_t>& texture, int x, int yTop, uint8_t alpha) {
    if (x < 0 || x >= kHudTextTextureWidth || yTop < 0 || yTop >= kHudTextTextureHeight) {
        return;
    }

    const int yBottom = (kHudTextTextureHeight - 1) - yTop;
    const size_t index = static_cast<size_t>(yBottom * kHudTextTextureWidth + x);
    texture[index] = std::max(texture[index], alpha);
}

void DrawHudGlyph(std::vector<uint8_t>& texture, int xTopLeft, int yTopLeft, int scale, char c, uint8_t alpha) {
    if (scale <= 0) {
        return;
    }

    const GlyphRows& rows = HudGlyphRows(c);
    for (int row = 0; row < kHudGlyphHeight; ++row) {
        for (int col = 0; col < kHudGlyphWidth; ++col) {
            const int bit = kHudGlyphWidth - 1 - col;
            if (((rows[static_cast<size_t>(row)] >> bit) & 0x01U) == 0U) {
                continue;
            }

            for (int dy = 0; dy < scale; ++dy) {
                for (int dx = 0; dx < scale; ++dx) {
                    SetHudPixel(texture, xTopLeft + col * scale + dx, yTopLeft + row * scale + dy, alpha);
                }
            }
        }
    }
}

int MeasureHudTextWidth(const std::string& text, int scale) {
    if (text.empty() || scale <= 0) {
        return 0;
    }
    const int advance = (kHudGlyphWidth + 1) * scale;
    return static_cast<int>(text.size()) * advance - scale;
}

std::string FitHudTextToWidth(const std::string& text, int scale, int maxPixelWidth) {
    if (text.empty() || scale <= 0 || maxPixelWidth <= 0) {
        return std::string();
    }
    if (MeasureHudTextWidth(text, scale) <= maxPixelWidth) {
        return text;
    }

    const std::string ellipsis = "...";
    const int ellipsisWidth = MeasureHudTextWidth(ellipsis, scale);
    if (ellipsisWidth > maxPixelWidth) {
        return std::string();
    }

    std::string trimmed = text;
    while (!trimmed.empty() && MeasureHudTextWidth(trimmed, scale) + ellipsisWidth > maxPixelWidth) {
        trimmed.pop_back();
    }

    if (trimmed.empty()) {
        return ellipsis;
    }
    return trimmed + ellipsis;
}

void DrawHudText(std::vector<uint8_t>& texture, int xTopLeft, int yTopLeft, int scale, const std::string& text, uint8_t alpha) {
    if (text.empty() || scale <= 0) {
        return;
    }

    const int advance = (kHudGlyphWidth + 1) * scale;
    int cursor = xTopLeft;
    for (const char c : text) {
        DrawHudGlyph(texture, cursor, yTopLeft, scale, c, alpha);
        cursor += advance;
    }
}

class QuestVisualizerApp {
public:
    explicit QuestVisualizerApp(android_app* app)
        : app_(app) {
        app_->userData = this;
        app_->onAppCmd = HandleAppCommand;
        startTime_ = std::chrono::steady_clock::now();
    }

    ~QuestVisualizerApp() {
        Shutdown();
    }

    void Run() {
        if (!Initialize()) {
            LOGE("Initialization failed.");
            return;
        }

        while (!exitRenderLoop_ && app_->destroyRequested == 0) {
            ProcessAndroidEvents();
            PollOpenXrEvents();

            if (sessionRunning_) {
                RenderFrame();
            }
        }

        LOGI("Render loop exited.");
    }

private:
    static void HandleAppCommand(android_app* app, int32_t cmd) {
        auto* self = static_cast<QuestVisualizerApp*>(app->userData);
        if (self) {
            self->OnAppCommand(cmd);
        }
    }

    void OnAppCommand(int32_t cmd) {
        switch (cmd) {
            case APP_CMD_RESUME:
                resumed_ = true;
                LOGI("APP_CMD_RESUME");
                break;
            case APP_CMD_PAUSE:
                resumed_ = false;
                LOGI("APP_CMD_PAUSE");
                break;
            case APP_CMD_INIT_WINDOW:
                hasWindow_ = true;
                LOGI("APP_CMD_INIT_WINDOW");
                break;
            case APP_CMD_TERM_WINDOW:
                hasWindow_ = false;
                LOGI("APP_CMD_TERM_WINDOW");
                break;
            case APP_CMD_DESTROY:
                exitRenderLoop_ = true;
                LOGI("APP_CMD_DESTROY");
                break;
            default:
                break;
        }
    }

    void ProcessAndroidEvents() {
        int events = 0;
        android_poll_source* source = nullptr;
        int timeoutMillis = sessionRunning_ ? 0 : -1;

        while (true) {
            const int pollResult = ALooper_pollOnce(
                timeoutMillis, nullptr, &events, reinterpret_cast<void**>(&source));
            if (pollResult < 0) {
                break;
            }

            if (source) {
                source->process(app_, source);
            }

            if (app_->destroyRequested != 0) {
                exitRenderLoop_ = true;
                break;
            }

            // Drain any queued events without blocking after the first wake.
            timeoutMillis = 0;
        }
    }

    JNIEnv* GetJniEnv(bool& didAttach) const {
        didAttach = false;
        if (app_ == nullptr || app_->activity == nullptr || app_->activity->vm == nullptr) {
            return nullptr;
        }

        JNIEnv* env = nullptr;
        const jint getEnvResult = app_->activity->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (getEnvResult == JNI_OK) {
            return env;
        }
        if (getEnvResult != JNI_EDETACHED) {
            return nullptr;
        }

        if (app_->activity->vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return nullptr;
        }

        didAttach = true;
        return env;
    }

    void CallJavaControlMethod(const char* methodName) {
        if (methodName == nullptr || *methodName == '\0') {
            return;
        }

        bool didAttach = false;
        JNIEnv* env = GetJniEnv(didAttach);
        if (!env) {
            return;
        }

        jclass activityClass = env->GetObjectClass(app_->activity->clazz);
        if (!activityClass) {
            if (didAttach) {
                app_->activity->vm->DetachCurrentThread();
            }
            return;
        }

        jmethodID method = env->GetMethodID(activityClass, methodName, "()V");
        if (!method) {
            env->DeleteLocalRef(activityClass);
            if (didAttach) {
                app_->activity->vm->DetachCurrentThread();
            }
            return;
        }

        env->CallVoidMethod(app_->activity->clazz, method);
        env->DeleteLocalRef(activityClass);

        if (didAttach) {
            app_->activity->vm->DetachCurrentThread();
        }
    }

    bool InitializeInputActions() {
        XrActionSetCreateInfo actionSetInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
        std::strncpy(actionSetInfo.actionSetName, "projectm_controls", XR_MAX_ACTION_SET_NAME_SIZE - 1);
        std::strncpy(actionSetInfo.localizedActionSetName, "projectM Controls", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
        actionSetInfo.priority = 0;

        if (XR_FAILED(xrCreateActionSet(xrInstance_, &actionSetInfo, &actionSet_))) {
            LOGE("xrCreateActionSet failed.");
            return false;
        }

        if (XR_FAILED(xrStringToPath(xrInstance_, "/user/hand/left", &leftHandPath_)) ||
            XR_FAILED(xrStringToPath(xrInstance_, "/user/hand/right", &rightHandPath_))) {
            LOGE("Failed to create subaction hand paths.");
            return false;
        }
        const XrPath subactionPaths[] = {leftHandPath_, rightHandPath_};

        auto createAction = [&](XrAction& actionOut,
                                XrActionType actionType,
                                const char* name,
                                const char* localized) -> bool {
            XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
            actionInfo.actionType = actionType;
            std::strncpy(actionInfo.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
            std::strncpy(actionInfo.localizedActionName, localized, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
            actionInfo.countSubactionPaths = 2;
            actionInfo.subactionPaths = subactionPaths;
            if (XR_FAILED(xrCreateAction(actionSet_, &actionInfo, &actionOut))) {
                LOGE("xrCreateAction failed for %s", name);
                return false;
            }
            return true;
        };

        if (!createAction(actionNextPreset_, XR_ACTION_TYPE_BOOLEAN_INPUT, "next_preset", "Next Preset")) return false;
        if (!createAction(actionPrevPreset_, XR_ACTION_TYPE_BOOLEAN_INPUT, "prev_preset", "Previous Preset")) return false;
        if (!createAction(actionTogglePlay_, XR_ACTION_TYPE_BOOLEAN_INPUT, "toggle_play", "Toggle Play Pause")) return false;
        if (!createAction(actionNextTrack_, XR_ACTION_TYPE_BOOLEAN_INPUT, "next_track", "Next Track")) return false;
        if (!createAction(actionPrevTrack_, XR_ACTION_TYPE_BOOLEAN_INPUT, "prev_track", "Previous Track")) return false;
        if (!createAction(actionCycleAudioInput_, XR_ACTION_TYPE_BOOLEAN_INPUT, "cycle_audio_input", "Cycle Audio Input")) return false;
        if (!createAction(actionToggleProjection_, XR_ACTION_TYPE_FLOAT_INPUT, "toggle_projection", "Toggle Projection")) return false;
        if (!createAction(actionOptionalPack_, XR_ACTION_TYPE_FLOAT_INPUT, "optional_pack", "Optional Preset Pack")) return false;
        if (!createAction(actionAimPose_, XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim Pose")) return false;

        auto bind = [&](std::vector<XrActionSuggestedBinding>& bindings, XrAction action, const char* path) {
            XrPath xrPath = XR_NULL_PATH;
            if (XR_SUCCEEDED(xrStringToPath(xrInstance_, path, &xrPath)) && xrPath != XR_NULL_PATH) {
                bindings.push_back({action, xrPath});
            }
        };

        auto suggestBindings = [&](const char* profilePath, const std::vector<XrActionSuggestedBinding>& bindings) -> bool {
            if (bindings.empty()) {
                return false;
            }

            XrPath interactionProfile = XR_NULL_PATH;
            if (XR_FAILED(xrStringToPath(xrInstance_, profilePath, &interactionProfile)) ||
                interactionProfile == XR_NULL_PATH) {
                return false;
            }
            XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
            suggested.interactionProfile = interactionProfile;
            suggested.suggestedBindings = bindings.data();
            suggested.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
            const XrResult result = xrSuggestInteractionProfileBindings(xrInstance_, &suggested);
            if (XR_FAILED(result)) {
                LOGW("xrSuggestInteractionProfileBindings failed for %s: %d",
                     profilePath,
                     static_cast<int>(result));
                return false;
            }
            LOGI("Suggested %u input bindings for %s.",
                 static_cast<unsigned>(bindings.size()),
                 profilePath);
            return true;
        };

        std::vector<XrActionSuggestedBinding> controllerBindings;
        bind(controllerBindings, actionNextPreset_, "/user/hand/right/input/a/click");
        bind(controllerBindings, actionPrevPreset_, "/user/hand/left/input/x/click");
        bind(controllerBindings, actionTogglePlay_, "/user/hand/left/input/y/click");
        bind(controllerBindings, actionNextTrack_, "/user/hand/right/input/b/click");
        bind(controllerBindings, actionPrevTrack_, "/user/hand/left/input/thumbstick/click");
        bind(controllerBindings, actionCycleAudioInput_, "/user/hand/right/input/thumbstick/click");
        bind(controllerBindings, actionToggleProjection_, "/user/hand/right/input/trigger/value");
        bind(controllerBindings, actionOptionalPack_, "/user/hand/left/input/trigger/value");
        bind(controllerBindings, actionAimPose_, "/user/hand/left/input/aim/pose");
        bind(controllerBindings, actionAimPose_, "/user/hand/right/input/aim/pose");

        bool suggestedAnyControllerProfile = false;
        suggestedAnyControllerProfile |= suggestBindings("/interaction_profiles/meta/touch_controller_plus", controllerBindings);
        suggestedAnyControllerProfile |= suggestBindings("/interaction_profiles/meta/touch_controller_pro", controllerBindings);
        suggestedAnyControllerProfile |= suggestBindings("/interaction_profiles/oculus/touch_controller", controllerBindings);
        if (!suggestedAnyControllerProfile) {
            LOGW("No controller profile binding suggestions succeeded; controller input may be unavailable.");
        }

        std::vector<XrActionSuggestedBinding> handBindings;
        bind(handBindings, actionToggleProjection_, "/user/hand/right/input/pinch_ext/value");
        bind(handBindings, actionOptionalPack_, "/user/hand/left/input/pinch_ext/value");
        bind(handBindings, actionAimPose_, "/user/hand/left/input/aim/pose");
        bind(handBindings, actionAimPose_, "/user/hand/right/input/aim/pose");
        const bool suggestedHandProfile = suggestBindings("/interaction_profiles/ext/hand_interaction_ext", handBindings);
        if (!suggestedHandProfile) {
            LOGW("Hand interaction profile binding suggestion failed; hand pinches may be unavailable.");
        }
        auto cacheProfilePath = [&](const char* profilePath, XrPath& profileOut) {
            if (XR_FAILED(xrStringToPath(xrInstance_, profilePath, &profileOut))) {
                profileOut = XR_NULL_PATH;
            }
        };
        cacheProfilePath("/interaction_profiles/meta/touch_controller_plus", controllerPlusProfilePath_);
        cacheProfilePath("/interaction_profiles/meta/touch_controller_pro", controllerProProfilePath_);
        cacheProfilePath("/interaction_profiles/oculus/touch_controller", controllerTouchProfilePath_);
        cacheProfilePath("/interaction_profiles/ext/hand_interaction_ext", handInteractionProfilePath_);

        XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        attachInfo.countActionSets = 1;
        attachInfo.actionSets = &actionSet_;
        if (XR_FAILED(xrAttachSessionActionSets(xrSession_, &attachInfo))) {
            LOGE("xrAttachSessionActionSets failed.");
            return false;
        }

        auto createAimSpace = [&](XrPath subactionPath, XrSpace& spaceOut, const char* label) {
            XrActionSpaceCreateInfo actionSpaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
            actionSpaceInfo.action = actionAimPose_;
            actionSpaceInfo.subactionPath = subactionPath;
            actionSpaceInfo.poseInActionSpace.orientation.w = 1.0f;
            if (XR_FAILED(xrCreateActionSpace(xrSession_, &actionSpaceInfo, &spaceOut))) {
                LOGW("xrCreateActionSpace failed for %s aim pose.", label);
                spaceOut = XR_NULL_HANDLE;
            }
        };
        createAimSpace(leftHandPath_, leftAimSpace_, "left");
        createAimSpace(rightHandPath_, rightAimSpace_, "right");

        return true;
    }

    bool Initialize() {
        if (!InitializeEgl()) {
            return false;
        }
        if (!InitializeOpenXr()) {
            return false;
        }
        if (!InitializeInputActions()) {
            return false;
        }
        if (!InitializeScene()) {
            return false;
        }
        if (!InitializeProjectM()) {
            return false;
        }
        return true;
    }

    bool InitializeEgl() {
        eglDisplay_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (eglDisplay_ == EGL_NO_DISPLAY) {
            LOGE("eglGetDisplay failed.");
            return false;
        }

        if (eglInitialize(eglDisplay_, nullptr, nullptr) != EGL_TRUE) {
            LOGE("eglInitialize failed.");
            return false;
        }

        const EGLint configAttributes[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_NONE,
        };

        EGLint numConfigs = 0;
        if (eglChooseConfig(eglDisplay_, configAttributes, &eglConfig_, 1, &numConfigs) != EGL_TRUE ||
            numConfigs == 0) {
            LOGE("eglChooseConfig failed.");
            return false;
        }

        const EGLint contextAttributes[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE,
        };
        eglContext_ = eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT, contextAttributes);
        if (eglContext_ == EGL_NO_CONTEXT) {
            LOGE("eglCreateContext failed.");
            return false;
        }

        const EGLint pbufferAttributes[] = {
            EGL_WIDTH, 16,
            EGL_HEIGHT, 16,
            EGL_NONE,
        };
        eglSurface_ = eglCreatePbufferSurface(eglDisplay_, eglConfig_, pbufferAttributes);
        if (eglSurface_ == EGL_NO_SURFACE) {
            LOGE("eglCreatePbufferSurface failed.");
            return false;
        }

        if (eglMakeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_) != EGL_TRUE) {
            LOGE("eglMakeCurrent failed.");
            return false;
        }

        LOGI("EGL ready. Renderer: %s", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
        return true;
    }

    bool InitializeOpenXr() {
        handTrackingExtensionEnabled_ = false;
        handTrackingReady_ = false;

        PFN_xrInitializeLoaderKHR initializeLoader = nullptr;
        xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                              reinterpret_cast<PFN_xrVoidFunction*>(&initializeLoader));
        if (initializeLoader) {
            XrLoaderInitInfoAndroidKHR loaderInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
            loaderInfo.applicationVM = app_->activity->vm;
            loaderInfo.applicationContext = app_->activity->clazz;
            const XrResult loaderResult = initializeLoader(
                reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loaderInfo));
            if (XR_FAILED(loaderResult)) {
                LOGE("xrInitializeLoaderKHR failed: %d", loaderResult);
                return false;
            }
        }

        std::vector<const char*> requiredExtensions = {
            XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
            XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
        };

        uint32_t extensionCount = 0;
        if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr))) {
            LOGE("xrEnumerateInstanceExtensionProperties count failed.");
            return false;
        }

        std::vector<XrExtensionProperties> extensionProperties(extensionCount);
        for (auto& ext : extensionProperties) {
            ext.type = XR_TYPE_EXTENSION_PROPERTIES;
        }

        if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount,
                                                             extensionProperties.data()))) {
            LOGE("xrEnumerateInstanceExtensionProperties list failed.");
            return false;
        }

        auto hasInstanceExtension = [&](const char* extensionName) {
            return std::any_of(extensionProperties.begin(),
                               extensionProperties.end(),
                               [extensionName](const XrExtensionProperties& ext) {
                                   return std::strcmp(ext.extensionName, extensionName) == 0;
                               });
        };

        for (const char* required : requiredExtensions) {
            const bool found = hasInstanceExtension(required);
            if (!found) {
                LOGE("Required OpenXR extension missing: %s", required);
                return false;
            }
        }

        if (hasInstanceExtension("XR_EXT_hand_interaction")) {
            requiredExtensions.push_back("XR_EXT_hand_interaction");
            LOGI("Enabling XR_EXT_hand_interaction for hand gesture input.");
        } else {
            LOGW("XR_EXT_hand_interaction not reported by runtime; hand gesture input may be unavailable.");
        }
        if (hasInstanceExtension(XR_EXT_HAND_TRACKING_EXTENSION_NAME)) {
            requiredExtensions.push_back(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
            handTrackingExtensionEnabled_ = true;
            LOGI("Enabling XR_EXT_hand_tracking for tracked hand-joint rendering.");
        } else {
            LOGW("XR_EXT_hand_tracking not reported by runtime; tracked hand-joint rendering unavailable.");
        }

        XrInstanceCreateInfoAndroidKHR androidInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
        androidInfo.applicationVM = app_->activity->vm;
        androidInfo.applicationActivity = app_->activity->clazz;

        XrInstanceCreateInfo instanceInfo{XR_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.next = &androidInfo;
        std::strncpy(instanceInfo.applicationInfo.applicationName, "projectM Quest XR",
                     XR_MAX_APPLICATION_NAME_SIZE - 1);
        std::strncpy(instanceInfo.applicationInfo.engineName, "projectM",
                     XR_MAX_ENGINE_NAME_SIZE - 1);
        instanceInfo.applicationInfo.applicationVersion = 1;
        instanceInfo.applicationInfo.engineVersion = 1;
        instanceInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
        instanceInfo.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size());
        instanceInfo.enabledExtensionNames = requiredExtensions.data();

        if (XR_FAILED(xrCreateInstance(&instanceInfo, &xrInstance_))) {
            LOGE("xrCreateInstance failed.");
            return false;
        }

        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        if (XR_FAILED(xrGetSystem(xrInstance_, &systemInfo, &xrSystemId_))) {
            LOGE("xrGetSystem failed.");
            return false;
        }

        PFN_xrGetOpenGLESGraphicsRequirementsKHR getGraphicsRequirements = nullptr;
        if (XR_FAILED(xrGetInstanceProcAddr(
                xrInstance_, "xrGetOpenGLESGraphicsRequirementsKHR",
                reinterpret_cast<PFN_xrVoidFunction*>(&getGraphicsRequirements))) ||
            getGraphicsRequirements == nullptr) {
            LOGE("Failed to get xrGetOpenGLESGraphicsRequirementsKHR.");
            return false;
        }

        XrGraphicsRequirementsOpenGLESKHR graphicsRequirements{
            XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
        if (XR_FAILED(getGraphicsRequirements(xrInstance_, xrSystemId_, &graphicsRequirements))) {
            LOGE("xrGetOpenGLESGraphicsRequirementsKHR failed.");
            return false;
        }
        (void)graphicsRequirements;

        XrGraphicsBindingOpenGLESAndroidKHR graphicsBinding{
            XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
        graphicsBinding.display = eglDisplay_;
        graphicsBinding.config = eglConfig_;
        graphicsBinding.context = eglContext_;

        XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
        sessionInfo.next = &graphicsBinding;
        sessionInfo.systemId = xrSystemId_;
        if (XR_FAILED(xrCreateSession(xrInstance_, &sessionInfo, &xrSession_))) {
            LOGE("xrCreateSession failed.");
            return false;
        }

        XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
        spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        XrResult spaceResult = xrCreateReferenceSpace(xrSession_, &spaceInfo, &xrAppSpace_);
        if (XR_FAILED(spaceResult)) {
            spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
            spaceResult = xrCreateReferenceSpace(xrSession_, &spaceInfo, &xrAppSpace_);
        }
        if (XR_FAILED(spaceResult)) {
            LOGE("xrCreateReferenceSpace failed.");
            return false;
        }

        if (handTrackingExtensionEnabled_ && !InitializeHandTrackers()) {
            LOGW("OpenXR hand trackers unavailable; continuing without rendered hand joints.");
            handTrackingReady_ = false;
        }

        uint32_t viewCount = 0;
        if (XR_FAILED(xrEnumerateViewConfigurationViews(
                xrInstance_, xrSystemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                0, &viewCount, nullptr))) {
            LOGE("xrEnumerateViewConfigurationViews count failed.");
            return false;
        }

        viewConfigs_.resize(viewCount);
        for (auto& config : viewConfigs_) {
            config = {};
            config.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
        }
        if (XR_FAILED(xrEnumerateViewConfigurationViews(
                xrInstance_, xrSystemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                viewCount, &viewCount, viewConfigs_.data()))) {
            LOGE("xrEnumerateViewConfigurationViews failed.");
            return false;
        }

        xrViews_.resize(viewCount);
        for (auto& view : xrViews_) {
            view = {};
            view.type = XR_TYPE_VIEW;
        }

        if (!CreateSwapchains()) {
            return false;
        }

        glGenFramebuffers(1, &swapchainFramebuffer_);
        if (swapchainFramebuffer_ == 0) {
            LOGE("Failed to create swapchain framebuffer.");
            return false;
        }

        LOGI("OpenXR initialized. Views: %u", static_cast<unsigned>(viewCount));
        return true;
    }

    bool CreateSwapchains() {
        uint32_t formatCount = 0;
        if (XR_FAILED(xrEnumerateSwapchainFormats(xrSession_, 0, &formatCount, nullptr))) {
            LOGE("xrEnumerateSwapchainFormats count failed.");
            return false;
        }

        std::vector<int64_t> formats(formatCount);
        if (XR_FAILED(xrEnumerateSwapchainFormats(xrSession_, formatCount, &formatCount, formats.data()))) {
            LOGE("xrEnumerateSwapchainFormats list failed.");
            return false;
        }

        int64_t chosenFormat = 0;
        const int64_t preferredFormats[] = {GL_SRGB8_ALPHA8, GL_RGBA8};
        for (const auto preferred : preferredFormats) {
            if (std::find(formats.begin(), formats.end(), preferred) != formats.end()) {
                chosenFormat = preferred;
                break;
            }
        }
        if (chosenFormat == 0 && !formats.empty()) {
            chosenFormat = formats.front();
        }
        if (chosenFormat == 0) {
            LOGE("No OpenXR swapchain format available.");
            return false;
        }

        swapchains_.resize(viewConfigs_.size());

        for (size_t i = 0; i < viewConfigs_.size(); ++i) {
            XrSwapchainCreateInfo createInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
            createInfo.arraySize = 1;
            createInfo.mipCount = 1;
            createInfo.faceCount = 1;
            createInfo.format = chosenFormat;
            createInfo.width = static_cast<uint32_t>(viewConfigs_[i].recommendedImageRectWidth);
            createInfo.height = static_cast<uint32_t>(viewConfigs_[i].recommendedImageRectHeight);
            createInfo.sampleCount = viewConfigs_[i].recommendedSwapchainSampleCount;
            createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;

            if (XR_FAILED(xrCreateSwapchain(xrSession_, &createInfo, &swapchains_[i].handle))) {
                LOGE("xrCreateSwapchain failed for eye %zu", i);
                return false;
            }

            swapchains_[i].width = static_cast<int32_t>(createInfo.width);
            swapchains_[i].height = static_cast<int32_t>(createInfo.height);

            uint32_t imageCount = 0;
            if (XR_FAILED(xrEnumerateSwapchainImages(
                    swapchains_[i].handle, 0, &imageCount, nullptr))) {
                LOGE("xrEnumerateSwapchainImages count failed.");
                return false;
            }

            swapchains_[i].images.resize(imageCount);
            for (auto& image : swapchains_[i].images) {
                image = {};
                image.type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
            }
            if (XR_FAILED(xrEnumerateSwapchainImages(
                    swapchains_[i].handle,
                    imageCount,
                    &imageCount,
                    reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchains_[i].images.data())))) {
                LOGE("xrEnumerateSwapchainImages failed.");
                return false;
            }
        }

        return true;
    }

    bool InitializeScene() {
        static const char* kVertexShaderSource = R"(
            #version 300 es
            precision highp float;
            layout(location = 0) in vec3 aPosition;
            uniform mat4 uViewProjection;
            out vec3 vDirection;
            void main() {
                vDirection = aPosition;
                gl_Position = uViewProjection * vec4(aPosition, 1.0);
            }
        )";

        static const char* kFragmentShaderSource = R"(
            #version 300 es
            precision highp float;
            in vec3 vDirection;
            uniform sampler2D uProjectMTexture;
            uniform int uProjectionMode;
            out vec4 fragColor;

            const float PI = 3.14159265358979323846;

            void main() {
                vec3 dir = normalize(vDirection);

                // On Quest in this app's space, +Z is forward for the viewed content.
                // Dome mode should render the forward hemisphere and hide the rear half.
                if (uProjectionMode == 1 && dir.z < 0.0) {
                    fragColor = vec4(0.0, 0.0, 0.0, 1.0);
                    return;
                }

                // Place equirectangular seam on the rear hemisphere (behind the user).
                float u = atan(dir.x, dir.z) / (2.0 * PI) + 0.5;
                float v = asin(clamp(dir.y, -1.0, 1.0)) / PI + 0.5;
                vec2 uv = vec2(u, 1.0 - v);
                fragColor = texture(uProjectMTexture, uv);
            }
        )";

        const GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexShaderSource);
        const GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShaderSource);
        if (vs == 0 || fs == 0) {
            if (vs != 0) {
                glDeleteShader(vs);
            }
            if (fs != 0) {
                glDeleteShader(fs);
            }
            return false;
        }

        sceneProgram_ = LinkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (sceneProgram_ == 0) {
            return false;
        }

        uViewProjectionLoc_ = glGetUniformLocation(sceneProgram_, "uViewProjection");
        uTextureLoc_ = glGetUniformLocation(sceneProgram_, "uProjectMTexture");
        uProjectionModeLoc_ = glGetUniformLocation(sceneProgram_, "uProjectionMode");

        BuildSphereMesh();
        if (sphereVao_ == 0 || sphereIndexCount_ == 0) {
            return false;
        }

        char modeValue[PROP_VALUE_MAX] = {};
        const int propLen = __system_property_get("debug.projectm.quest.projection", modeValue);
        const char* mode = propLen > 0 ? modeValue : std::getenv("PROJECTM_QUEST_PROJECTION_MODE");
        if (mode && std::strcmp(mode, "dome") == 0) {
            projectionMode_ = ProjectionMode::FrontDome;
            LOGI("Projection mode: dome");
        } else {
            projectionMode_ = ProjectionMode::FullSphere;
            LOGI("Projection mode: full sphere");
        }

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        if (!InitializeHudOverlay()) {
            return false;
        }
        if (!InitializeHandOverlay()) {
            LOGW("Failed to initialize hand overlay renderer.");
        }
        return true;
    }

    bool InitializeHudOverlay() {
        static const char* kHudVertexShaderSource = R"(
            #version 300 es
            precision highp float;
            layout(location = 0) in vec2 aPosition;
            layout(location = 1) in vec2 aUv;
            uniform mat4 uHudMvp;
            out vec2 vUv;
            void main() {
                vUv = aUv;
                gl_Position = uHudMvp * vec4(aPosition, 0.0, 1.0);
            }
        )";

        static const char* kHudFragmentShaderSource = R"(
            #version 300 es
            precision mediump float;
            in vec2 vUv;
            uniform vec4 uFlashA;
            uniform vec4 uFlashB;
            uniform vec4 uFlashX;
            uniform vec4 uFlashY;
            uniform vec4 uFlashRT;
            uniform vec4 uFlashLT;
            uniform vec4 uFlashMenu;
            uniform vec4 uPointerLeft;
            uniform vec4 uPointerRight;
            uniform sampler2D uTextTexture;
            out vec4 fragColor;

            float rectMask(vec2 uv, vec2 minPt, vec2 maxPt, float feather) {
                vec2 inMin = smoothstep(minPt - vec2(feather), minPt + vec2(feather), uv);
                vec2 inMax = smoothstep(maxPt + vec2(feather), maxPt - vec2(feather), uv);
                return inMin.x * inMin.y * inMax.x * inMax.y;
            }

            vec3 blendRect(vec3 baseColor, vec2 uv, vec2 minPt, vec2 maxPt, vec3 color, float alpha) {
                float m = rectMask(uv, minPt, maxPt, 0.0035);
                return mix(baseColor, color, m * alpha);
            }

            float pointerMask(vec2 uv, vec2 center, float radius, float feather) {
                float dist = length(uv - center);
                return smoothstep(radius + feather, radius - feather, dist);
            }

            float ringMask(vec2 uv, vec2 center, float outerRadius, float innerRadius, float feather) {
                float outer = pointerMask(uv, center, outerRadius, feather);
                float inner = pointerMask(uv, center, innerRadius, feather);
                return clamp(outer - inner, 0.0, 1.0);
            }

            void main() {
                vec3 color = vec3(0.0);
                float alpha = rectMask(vUv, vec2(0.015, 0.02), vec2(0.985, 0.980), 0.0035) * 0.62;
                if (alpha <= 0.001) {
                    discard;
                }

                color = vec3(0.08, 0.08, 0.10);

                color = blendRect(color, vUv, vec2(0.07, 0.60), vec2(0.46, 0.82), vec3(0.14, 0.44, 0.87), 0.90 + uFlashX.x);
                color = blendRect(color, vUv, vec2(0.54, 0.60), vec2(0.93, 0.82), vec3(0.93, 0.34, 0.26), 0.90 + uFlashA.x);
                color = blendRect(color, vUv, vec2(0.07, 0.30), vec2(0.46, 0.52), vec3(0.18, 0.74, 0.38), 0.90 + uFlashY.x);
                color = blendRect(color, vUv, vec2(0.54, 0.30), vec2(0.93, 0.52), vec3(0.91, 0.82, 0.28), 0.90 + uFlashB.x);
                color = blendRect(color, vUv, vec2(0.07, 0.08), vec2(0.33, 0.24), vec3(0.58, 0.32, 0.86), 0.88 + uFlashLT.x);
                color = blendRect(color, vUv, vec2(0.37, 0.08), vec2(0.63, 0.24), vec3(0.90, 0.54, 0.20), 0.88 + uFlashMenu.x);
                color = blendRect(color, vUv, vec2(0.67, 0.08), vec2(0.93, 0.24), vec3(0.23, 0.72, 0.85), 0.88 + uFlashRT.x);

                float textMask = texture(uTextTexture, vUv).r;
                color = mix(color, vec3(0.97), clamp(textMask * 1.45, 0.0, 1.0));
                alpha = max(alpha, textMask);

                if (uPointerLeft.z > 0.5) {
                    float isTouch = step(1.5, uPointerLeft.w);
                    float isTouchActive = step(1.5, uPointerLeft.z);

                    float rayRing = ringMask(vUv, uPointerLeft.xy, 0.022, 0.013, 0.0035);
                    float rayCore = pointerMask(vUv, uPointerLeft.xy, 0.006, 0.0020);
                    float rayMask = clamp(rayRing + rayCore, 0.0, 1.0) * (1.0 - isTouch);
                    color = mix(color, vec3(0.30, 0.88, 0.92), rayMask * 0.90);
                    alpha = max(alpha, rayMask * 0.95);

                    float touchDot = pointerMask(vUv, uPointerLeft.xy, 0.0045, 0.0015) * isTouch;
                    float touchPress = pointerMask(vUv, uPointerLeft.xy, 0.0070, 0.0018) * isTouch * isTouchActive;
                    float touchMask = max(touchDot, touchPress);
                    color = mix(color, vec3(0.74, 0.96, 0.98), touchMask * (0.55 + 0.35 * isTouchActive));
                    alpha = max(alpha, touchMask * 0.85);
                }
                if (uPointerRight.z > 0.5) {
                    float isTouch = step(1.5, uPointerRight.w);
                    float isTouchActive = step(1.5, uPointerRight.z);

                    float rayRing = ringMask(vUv, uPointerRight.xy, 0.022, 0.013, 0.0035);
                    float rayCore = pointerMask(vUv, uPointerRight.xy, 0.006, 0.0020);
                    float rayMask = clamp(rayRing + rayCore, 0.0, 1.0) * (1.0 - isTouch);
                    color = mix(color, vec3(0.98, 0.72, 0.30), rayMask * 0.90);
                    alpha = max(alpha, rayMask * 0.95);

                    float touchDot = pointerMask(vUv, uPointerRight.xy, 0.0045, 0.0015) * isTouch;
                    float touchPress = pointerMask(vUv, uPointerRight.xy, 0.0070, 0.0018) * isTouch * isTouchActive;
                    float touchMask = max(touchDot, touchPress);
                    color = mix(color, vec3(1.00, 0.93, 0.75), touchMask * (0.55 + 0.35 * isTouchActive));
                    alpha = max(alpha, touchMask * 0.85);
                }

                fragColor = vec4(color, alpha);
            }
        )";

        const GLuint vs = CompileShader(GL_VERTEX_SHADER, kHudVertexShaderSource);
        const GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kHudFragmentShaderSource);
        if (vs == 0 || fs == 0) {
            if (vs != 0) {
                glDeleteShader(vs);
            }
            if (fs != 0) {
                glDeleteShader(fs);
            }
            return false;
        }

        hudProgram_ = LinkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (hudProgram_ == 0) {
            return false;
        }

        hudMvpLoc_ = glGetUniformLocation(hudProgram_, "uHudMvp");
        hudFlashALoc_ = glGetUniformLocation(hudProgram_, "uFlashA");
        hudFlashBLoc_ = glGetUniformLocation(hudProgram_, "uFlashB");
        hudFlashXLoc_ = glGetUniformLocation(hudProgram_, "uFlashX");
        hudFlashYLoc_ = glGetUniformLocation(hudProgram_, "uFlashY");
        hudFlashRtLoc_ = glGetUniformLocation(hudProgram_, "uFlashRT");
        hudFlashLtLoc_ = glGetUniformLocation(hudProgram_, "uFlashLT");
        hudFlashMenuLoc_ = glGetUniformLocation(hudProgram_, "uFlashMenu");
        hudPointerLeftLoc_ = glGetUniformLocation(hudProgram_, "uPointerLeft");
        hudPointerRightLoc_ = glGetUniformLocation(hudProgram_, "uPointerRight");
        hudTextSamplerLoc_ = glGetUniformLocation(hudProgram_, "uTextTexture");

        const float hudVertices[] = {
            -0.5f, -0.5f, 0.0f, 0.0f,
             0.5f, -0.5f, 1.0f, 0.0f,
             0.5f,  0.5f, 1.0f, 1.0f,
            -0.5f, -0.5f, 0.0f, 0.0f,
             0.5f,  0.5f, 1.0f, 1.0f,
            -0.5f,  0.5f, 0.0f, 1.0f,
        };

        glGenVertexArrays(1, &hudVao_);
        glGenBuffers(1, &hudVbo_);
        glBindVertexArray(hudVao_);
        glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(hudVertices), hudVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<void*>(sizeof(float) * 2));
        glBindVertexArray(0);

        glGenTextures(1, &hudTextTexture_);
        glBindTexture(GL_TEXTURE_2D, hudTextTexture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        hudTextPixels_.assign(static_cast<size_t>(kHudTextTextureWidth * kHudTextTextureHeight), 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_R8,
                     kHudTextTextureWidth,
                     kHudTextTextureHeight,
                     0,
                     GL_RED,
                     GL_UNSIGNED_BYTE,
                     hudTextPixels_.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        hudTextDirty_ = true;

        return true;
    }

    bool InitializeHandOverlay() {
        static const char* kHandVertexShaderSource = R"(
            #version 300 es
            precision highp float;
            layout(location = 0) in vec3 aPosition;
            uniform mat4 uViewProjection;
            uniform float uPointSize;
            void main() {
                gl_Position = uViewProjection * vec4(aPosition, 1.0);
                gl_PointSize = uPointSize;
            }
        )";

        static const char* kHandFragmentShaderSource = R"(
            #version 300 es
            precision mediump float;
            uniform vec4 uColor;
            out vec4 fragColor;
            void main() {
                fragColor = uColor;
            }
        )";

        const GLuint vs = CompileShader(GL_VERTEX_SHADER, kHandVertexShaderSource);
        const GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kHandFragmentShaderSource);
        if (vs == 0 || fs == 0) {
            if (vs != 0) {
                glDeleteShader(vs);
            }
            if (fs != 0) {
                glDeleteShader(fs);
            }
            return false;
        }

        handProgram_ = LinkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (handProgram_ == 0) {
            return false;
        }

        handViewProjectionLoc_ = glGetUniformLocation(handProgram_, "uViewProjection");
        handColorLoc_ = glGetUniformLocation(handProgram_, "uColor");
        handPointSizeLoc_ = glGetUniformLocation(handProgram_, "uPointSize");
        if (handViewProjectionLoc_ < 0 || handColorLoc_ < 0 || handPointSizeLoc_ < 0) {
            return false;
        }

        glGenVertexArrays(1, &handVao_);
        glGenBuffers(1, &handVbo_);
        if (handVao_ == 0 || handVbo_ == 0) {
            return false;
        }

        glBindVertexArray(handVao_);
        glBindBuffer(GL_ARRAY_BUFFER, handVbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3) * XR_HAND_JOINT_COUNT_EXT * 2, nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
        glBindVertexArray(0);
        return true;
    }

    bool InitializeHandTrackers() {
        if (!handTrackingExtensionEnabled_) {
            return false;
        }

        auto loadProc = [&](const char* procName, PFN_xrVoidFunction* procOut) -> bool {
            *procOut = nullptr;
            return XR_SUCCEEDED(
                xrGetInstanceProcAddr(xrInstance_, procName, procOut)) && *procOut != nullptr;
        };

        PFN_xrVoidFunction createProc = nullptr;
        PFN_xrVoidFunction destroyProc = nullptr;
        PFN_xrVoidFunction locateProc = nullptr;
        if (!loadProc("xrCreateHandTrackerEXT", &createProc) ||
            !loadProc("xrDestroyHandTrackerEXT", &destroyProc) ||
            !loadProc("xrLocateHandJointsEXT", &locateProc)) {
            LOGW("Failed to load XR_EXT_hand_tracking function pointers.");
            return false;
        }

        xrCreateHandTrackerEXT_ = reinterpret_cast<PFN_xrCreateHandTrackerEXT>(createProc);
        xrDestroyHandTrackerEXT_ = reinterpret_cast<PFN_xrDestroyHandTrackerEXT>(destroyProc);
        xrLocateHandJointsEXT_ = reinterpret_cast<PFN_xrLocateHandJointsEXT>(locateProc);

        XrHandTrackerCreateInfoEXT createInfo{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
        createInfo.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;

        createInfo.hand = XR_HAND_LEFT_EXT;
        if (XR_FAILED(xrCreateHandTrackerEXT_(xrSession_, &createInfo, &leftHandTracker_))) {
            leftHandTracker_ = XR_NULL_HANDLE;
            LOGW("xrCreateHandTrackerEXT failed for left hand.");
            return false;
        }

        createInfo.hand = XR_HAND_RIGHT_EXT;
        if (XR_FAILED(xrCreateHandTrackerEXT_(xrSession_, &createInfo, &rightHandTracker_))) {
            if (leftHandTracker_ != XR_NULL_HANDLE) {
                xrDestroyHandTrackerEXT_(leftHandTracker_);
                leftHandTracker_ = XR_NULL_HANDLE;
            }
            rightHandTracker_ = XR_NULL_HANDLE;
            LOGW("xrCreateHandTrackerEXT failed for right hand.");
            return false;
        }

        handTrackingReady_ = true;
        LOGI("OpenXR hand trackers initialized.");
        return true;
    }

    void ClearHandJointRenderState() {
        leftHandJointRender_ = {};
        rightHandJointRender_ = {};
    }

    void UpdateHandJointRenderState(XrTime displayTime) {
        ClearHandJointRenderState();
        if (!handTrackingReady_ ||
            xrLocateHandJointsEXT_ == nullptr ||
            xrAppSpace_ == XR_NULL_HANDLE) {
            return;
        }

        auto locateHand = [&](XrHandTrackerEXT tracker, HandJointRenderState& handOut) {
            if (tracker == XR_NULL_HANDLE) {
                return;
            }

            std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT> jointLocations{};

            XrHandJointLocationsEXT locations{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
            locations.jointCount = XR_HAND_JOINT_COUNT_EXT;
            locations.jointLocations = jointLocations.data();

            XrHandJointsLocateInfoEXT locateInfo{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
            locateInfo.baseSpace = xrAppSpace_;
            locateInfo.time = displayTime;

            if (XR_FAILED(xrLocateHandJointsEXT_(tracker, &locateInfo, &locations)) || !locations.isActive) {
                return;
            }

            handOut.isActive = true;
            for (uint32_t i = 0; i < XR_HAND_JOINT_COUNT_EXT; ++i) {
                const XrSpaceLocationFlags flags = jointLocations[i].locationFlags;
                const bool tracked = (flags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
                handOut.tracked[i] = tracked ? 1 : 0;
                if (tracked) {
                    handOut.positions[i] = glm::vec3(jointLocations[i].pose.position.x,
                                                     jointLocations[i].pose.position.y,
                                                     jointLocations[i].pose.position.z);
                }
            }
        };

        locateHand(leftHandTracker_, leftHandJointRender_);
        locateHand(rightHandTracker_, rightHandJointRender_);
    }

    void RenderHandJoints(const glm::mat4& viewProjection) {
        if (handProgram_ == 0 || handVao_ == 0 || handVbo_ == 0) {
            return;
        }
        if (!handTrackingReady_) {
            return;
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glUseProgram(handProgram_);
        glUniformMatrix4fv(handViewProjectionLoc_, 1, GL_FALSE, glm::value_ptr(viewProjection));
        glBindVertexArray(handVao_);
        glBindBuffer(GL_ARRAY_BUFFER, handVbo_);

        auto drawHand = [&](const HandJointRenderState& hand,
                            const glm::vec3& lineColor,
                            const glm::vec3& tipColor) {
            if (!hand.isActive) {
                return;
            }

            std::array<glm::vec3, kHandBones.size() * 2> lineVertices{};
            GLsizei lineVertexCount = 0;
            for (const HandBone& bone : kHandBones) {
                const int from = static_cast<int>(bone.from);
                const int to = static_cast<int>(bone.to);
                if (from < 0 || to < 0) {
                    continue;
                }
                if (from >= static_cast<int>(XR_HAND_JOINT_COUNT_EXT) ||
                    to >= static_cast<int>(XR_HAND_JOINT_COUNT_EXT)) {
                    continue;
                }
                if (hand.tracked[from] == 0 || hand.tracked[to] == 0) {
                    continue;
                }

                lineVertices[static_cast<size_t>(lineVertexCount++)] = hand.positions[static_cast<size_t>(from)];
                lineVertices[static_cast<size_t>(lineVertexCount++)] = hand.positions[static_cast<size_t>(to)];
            }

            if (lineVertexCount > 0) {
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(lineVertexCount) * sizeof(glm::vec3),
                             lineVertices.data(),
                             GL_DYNAMIC_DRAW);
                glUniform4f(handColorLoc_, lineColor.r, lineColor.g, lineColor.b, 0.88f);
                glUniform1f(handPointSizeLoc_, 1.0f);
                glDrawArrays(GL_LINES, 0, lineVertexCount);
            }

            std::array<glm::vec3, XR_HAND_JOINT_COUNT_EXT> jointVertices{};
            GLsizei jointVertexCount = 0;
            for (uint32_t i = 0; i < XR_HAND_JOINT_COUNT_EXT; ++i) {
                if (hand.tracked[i] == 0) {
                    continue;
                }
                jointVertices[static_cast<size_t>(jointVertexCount++)] = hand.positions[i];
            }

            if (jointVertexCount > 0) {
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(jointVertexCount) * sizeof(glm::vec3),
                             jointVertices.data(),
                             GL_DYNAMIC_DRAW);
                glUniform4f(handColorLoc_, lineColor.r, lineColor.g, lineColor.b, 0.65f);
                glUniform1f(handPointSizeLoc_, 6.0f);
                glDrawArrays(GL_POINTS, 0, jointVertexCount);
            }

            std::array<glm::vec3, kHandHighlightJoints.size()> tipVertices{};
            GLsizei tipVertexCount = 0;
            for (const XrHandJointEXT joint : kHandHighlightJoints) {
                const uint32_t index = static_cast<uint32_t>(joint);
                if (index >= XR_HAND_JOINT_COUNT_EXT || hand.tracked[index] == 0) {
                    continue;
                }
                tipVertices[static_cast<size_t>(tipVertexCount++)] = hand.positions[index];
            }

            if (tipVertexCount > 0) {
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(tipVertexCount) * sizeof(glm::vec3),
                             tipVertices.data(),
                             GL_DYNAMIC_DRAW);
                glUniform4f(handColorLoc_, tipColor.r, tipColor.g, tipColor.b, 0.95f);
                glUniform1f(handPointSizeLoc_, 11.0f);
                glDrawArrays(GL_POINTS, 0, tipVertexCount);
            }
        };

        drawHand(leftHandJointRender_, glm::vec3(0.70f, 0.93f, 0.98f), glm::vec3(0.92f, 0.99f, 1.00f));
        drawHand(rightHandJointRender_, glm::vec3(1.00f, 0.86f, 0.64f), glm::vec3(1.00f, 0.96f, 0.86f));

        glBindVertexArray(0);
    }

    void BuildSphereMesh() {
        constexpr uint32_t kStacks = 48;
        constexpr uint32_t kSlices = 96;
        constexpr float kRadius = 5.0f;

        std::vector<SphereVertex> vertices;
        vertices.reserve((kStacks + 1) * (kSlices + 1));

        for (uint32_t stack = 0; stack <= kStacks; ++stack) {
            const float v = static_cast<float>(stack) / static_cast<float>(kStacks);
            const float phi = v * kPi;
            const float y = std::cos(phi);
            const float r = std::sin(phi);

            for (uint32_t slice = 0; slice <= kSlices; ++slice) {
                const float u = static_cast<float>(slice) / static_cast<float>(kSlices);
                const float theta = u * kPi * 2.0f;
                const float x = r * std::sin(theta);
                const float z = -r * std::cos(theta);
                vertices.push_back({x * kRadius, y * kRadius, z * kRadius});
            }
        }

        std::vector<uint32_t> indices;
        indices.reserve(kStacks * kSlices * 6);

        for (uint32_t stack = 0; stack < kStacks; ++stack) {
            for (uint32_t slice = 0; slice < kSlices; ++slice) {
                const uint32_t a = stack * (kSlices + 1) + slice;
                const uint32_t b = a + kSlices + 1;

                indices.push_back(a);
                indices.push_back(b);
                indices.push_back(a + 1);

                indices.push_back(a + 1);
                indices.push_back(b);
                indices.push_back(b + 1);
            }
        }

        sphereIndexCount_ = static_cast<GLsizei>(indices.size());

        glGenVertexArrays(1, &sphereVao_);
        glGenBuffers(1, &sphereVbo_);
        glGenBuffers(1, &sphereIbo_);

        glBindVertexArray(sphereVao_);

        glBindBuffer(GL_ARRAY_BUFFER, sphereVbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices.size() * sizeof(SphereVertex)),
                     vertices.data(),
                     GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereIbo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                     indices.data(),
                     GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SphereVertex), nullptr);

        glBindVertexArray(0);
    }

    bool InitializeProjectM() {
        projectM_ = projectm_create();
        if (!projectM_) {
            LOGE("projectm_create failed. Ensure GLES context is current and compatible.");
            return false;
        }

        projectm_set_window_size(projectM_, kProjectMWidth, kProjectMHeight);
        meshWidth_ = kDefaultMeshWidth;
        meshHeight_ = kDefaultMeshHeight;
        projectm_set_mesh_size(projectM_, meshWidth_, meshHeight_);
        projectm_set_fps(projectM_, 72);
        projectm_set_hard_cut_enabled(projectM_, true);
        projectm_set_hard_cut_duration(projectM_, 15.0);
        projectm_set_hard_cut_sensitivity(projectM_, 1.4f);

        const std::string appDataPath(app_->activity->internalDataPath ? app_->activity->internalDataPath : "");
        const std::string presetOutputDir = appDataPath + "/presets";
        const std::string textureOutputDir = appDataPath + "/textures";
        presetDirectory_ = presetOutputDir;
        slowPresetFilePath_ = appDataPath.empty() ? std::string() : (appDataPath + "/slow_presets.txt");

        if (app_->activity->assetManager != nullptr) {
            CopyAssetDirectoryFlat(app_->activity->assetManager, "presets", presetOutputDir);
            CopyAssetDirectoryFlat(app_->activity->assetManager, "textures", textureOutputDir);
        }

        LoadSlowPresetList();
        presetFiles_ = CollectPresetFiles(presetOutputDir);
        if (!presetFiles_.empty()) {
            currentPresetIndex_ = 0;
            if (skipMarkedPresets_) {
                for (size_t i = 0; i < presetFiles_.size(); ++i) {
                    if (!IsPresetMarkedSlow(presetFiles_[i])) {
                        currentPresetIndex_ = i;
                        break;
                    }
                }
            }

            projectm_load_preset_file(projectM_, presetFiles_[currentPresetIndex_].c_str(), false);
            LOGI("Loaded first preset from assets: %s", presetFiles_[currentPresetIndex_].c_str());
            usingFallbackPreset_ = false;
            currentPresetLabel_ = BuildPresetDisplayLabel(presetFiles_[currentPresetIndex_]);
        } else {
            projectm_load_preset_data(projectM_, kFallbackPreset, false);
            LOGW("No preset assets found, using built-in fallback preset.");
            usingFallbackPreset_ = true;
            currentPresetLabel_ = "FALLBACK";
        }
        hudTextDirty_ = true;

        if (EnsureDirectory(textureOutputDir)) {
            const char* texturePath = textureOutputDir.c_str();
            projectm_set_texture_search_paths(projectM_, &texturePath, 1);
        }

        glGenTextures(1, &projectMTexture_);
        glBindTexture(GL_TEXTURE_2D, projectMTexture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA,
                     static_cast<GLsizei>(kProjectMWidth),
                     static_cast<GLsizei>(kProjectMHeight),
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     nullptr);

        glGenFramebuffers(1, &projectMFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, projectMFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, projectMTexture_, 0);

        const GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (fboStatus != GL_FRAMEBUFFER_COMPLETE) {
            LOGE("projectM framebuffer incomplete: 0x%x", fboStatus);
            return false;
        }

        return true;
    }

    bool IsPresetMarkedSlow(const std::string& presetPath) const {
        if (presetPath.empty()) {
            return false;
        }

        const std::string basename = BasenamePath(presetPath);
        for (const std::string& entry : slowPresets_) {
            if (entry == presetPath || entry == basename) {
                return true;
            }
        }
        return false;
    }

    bool FindPresetIndexRelative(int delta, bool skipMarked, size_t& outIndex) const {
        if (presetFiles_.empty()) {
            return false;
        }

        if (delta == 0) {
            outIndex = currentPresetIndex_;
            return true;
        }

        const int step = delta > 0 ? 1 : -1;
        const int64_t count = static_cast<int64_t>(presetFiles_.size());
        int64_t index = static_cast<int64_t>(currentPresetIndex_);

        for (int64_t attempt = 0; attempt < count; ++attempt) {
            index += step;
            index %= count;
            if (index < 0) {
                index += count;
            }

            const size_t candidate = static_cast<size_t>(index);
            if (skipMarked && IsPresetMarkedSlow(presetFiles_[candidate])) {
                continue;
            }

            outIndex = candidate;
            return true;
        }

        return false;
    }

    void PersistSlowPresetList() const {
        if (slowPresetFilePath_.empty()) {
            return;
        }

        std::ofstream out(slowPresetFilePath_, std::ios::trunc);
        if (!out) {
            LOGW("Could not write slow preset list: %s", slowPresetFilePath_.c_str());
            return;
        }

        for (const std::string& path : slowPresets_) {
            out << path << '\n';
        }
    }

    void LoadSlowPresetList() {
        slowPresets_.clear();
        if (slowPresetFilePath_.empty()) {
            return;
        }

        std::ifstream in(slowPresetFilePath_);
        if (!in) {
            return;
        }

        std::string line;
        while (std::getline(in, line)) {
            line = TrimAscii(line);
            if (line.empty() || line[0] == '#') {
                continue;
            }
            if (std::find(slowPresets_.begin(), slowPresets_.end(), line) == slowPresets_.end()) {
                slowPresets_.push_back(line);
            }
        }

        if (!slowPresets_.empty()) {
            LOGI("Loaded %zu marked slow presets.", slowPresets_.size());
        }
    }

    void ClearSlowPresetMarks() {
        slowPresets_.clear();
        PersistSlowPresetList();
        LOGI("Cleared marked slow presets.");
    }

    void MarkCurrentPresetSlow() {
        if (presetFiles_.empty() || currentPresetIndex_ >= presetFiles_.size()) {
            return;
        }

        const std::string& presetPath = presetFiles_[currentPresetIndex_];
        if (IsPresetMarkedSlow(presetPath)) {
            return;
        }

        slowPresets_.push_back(presetPath);
        PersistSlowPresetList();
        LOGW("Marked preset as slow: %s", presetPath.c_str());
    }

    void AddSyntheticAudioForFrame() {
        std::array<float, kPcmFramesPerPush * 2> samples{};

        const float carrierStep = (2.0f * kPi * kAudioCarrierFrequency) / kAudioSampleRate;
        const float beatStep = (2.0f * kPi * kAudioBeatFrequency) / kAudioSampleRate;

        for (uint32_t i = 0; i < kPcmFramesPerPush; ++i) {
            audioCarrierPhase_ += carrierStep;
            audioBeatPhase_ += beatStep;

            if (audioCarrierPhase_ > 2.0f * kPi) {
                audioCarrierPhase_ -= 2.0f * kPi;
            }
            if (audioBeatPhase_ > 2.0f * kPi) {
                audioBeatPhase_ -= 2.0f * kPi;
            }

            const float envelope = 0.25f + 0.35f * (0.5f + 0.5f * std::sin(audioBeatPhase_));
            const float sample = envelope * std::sin(audioCarrierPhase_);
            samples[2 * i] = sample;
            samples[2 * i + 1] = sample;
        }

        projectm_pcm_add_float(projectM_, samples.data(), kPcmFramesPerPush, PROJECTM_STEREO);
    }

    void AddAudioForFrame(double nowSeconds) {
        std::array<float, kPcmFramesPerPush * 2> queuedSamples{};
        const size_t queuedFrames = DequeueAudioFrames(queuedSamples.data(), kPcmFramesPerPush);
        if (queuedFrames > 0) {
            projectm_pcm_add_float(projectM_, queuedSamples.data(), static_cast<unsigned int>(queuedFrames), PROJECTM_STEREO);
            lastExternalAudioSeconds_ = nowSeconds;
            return;
        }

        if (nowSeconds - lastExternalAudioSeconds_ > kAudioFallbackDelaySeconds) {
            if (currentAudioMode_ != AudioMode::Synthetic || currentMediaPlaying_) {
                hudTextDirty_ = true;
            }
            AddSyntheticAudioForFrame();
            currentAudioMode_ = AudioMode::Synthetic;
            currentMediaPlaying_ = false;
        }
    }

    void RefreshPresetListIfNeeded(double nowSeconds) {
        if (nowSeconds - lastPresetScanSeconds_ < kPresetScanIntervalSeconds) {
            return;
        }
        lastPresetScanSeconds_ = nowSeconds;

        const std::vector<std::string> scanned = CollectPresetFiles(presetDirectory_);
        if (scanned.empty()) {
            return;
        }

        if (scanned == presetFiles_) {
            return;
        }

        std::string currentPresetPath;
        if (!presetFiles_.empty() && currentPresetIndex_ < presetFiles_.size()) {
            currentPresetPath = presetFiles_[currentPresetIndex_];
        }

        presetFiles_ = scanned;
        if (!currentPresetPath.empty()) {
            const auto it = std::find(presetFiles_.begin(), presetFiles_.end(), currentPresetPath);
            currentPresetIndex_ = it == presetFiles_.end()
                ? 0
                : static_cast<size_t>(std::distance(presetFiles_.begin(), it));
        } else {
            currentPresetIndex_ = 0;
        }

        if (skipMarkedPresets_ &&
            !presetFiles_.empty() &&
            currentPresetIndex_ < presetFiles_.size() &&
            IsPresetMarkedSlow(presetFiles_[currentPresetIndex_])) {
            size_t nextUnmarked = currentPresetIndex_;
            if (FindPresetIndexRelative(1, true, nextUnmarked)) {
                currentPresetIndex_ = nextUnmarked;
            }
        }

        if (usingFallbackPreset_) {
            projectm_load_preset_file(projectM_, presetFiles_[currentPresetIndex_].c_str(), false);
            usingFallbackPreset_ = false;
            currentPresetLabel_ = BuildPresetDisplayLabel(presetFiles_[currentPresetIndex_]);
            hudTextDirty_ = true;
        }

        if (!presetFiles_.empty() && currentPresetIndex_ < presetFiles_.size()) {
            const std::string updated = BuildPresetDisplayLabel(presetFiles_[currentPresetIndex_]);
            if (updated != currentPresetLabel_) {
                currentPresetLabel_ = updated;
                hudTextDirty_ = true;
            }
        }

        LOGI("Preset list updated (%zu presets).", presetFiles_.size());
    }

    void SwitchPresetRelative(int delta, bool smooth) {
        if (presetFiles_.empty() || projectM_ == nullptr) {
            return;
        }

        size_t nextIndex = currentPresetIndex_;
        const bool preferUnmarked = skipMarkedPresets_;
        if (!FindPresetIndexRelative(delta, preferUnmarked, nextIndex)) {
            if (!FindPresetIndexRelative(delta, false, nextIndex)) {
                return;
            }
        }

        currentPresetIndex_ = nextIndex;
        projectm_load_preset_file(projectM_, presetFiles_[currentPresetIndex_].c_str(), smooth);
        lastPresetSwitchSeconds_ = ElapsedSeconds();
        currentPresetLabel_ = BuildPresetDisplayLabel(presetFiles_[currentPresetIndex_]);
        hudTextDirty_ = true;
    }

    std::string BuildPresetDisplayLabel(const std::string& presetPath) const {
        std::string name = StripExtension(BasenamePath(presetPath));
        ReplaceAll(name, "__", " - ");
        ReplaceAll(name, "_", " ");
        return SanitizeHudText(name, 56);
    }

    std::string BuildTrackDisplayLabel(const std::string& rawLabel) const {
        std::string label = rawLabel;
        if (label.empty()) {
            label = "none";
        }

        if (label.rfind("http://", 0) == 0 || label.rfind("https://", 0) == 0) {
            const std::string name = BasenamePath(label);
            if (!name.empty() && name != label) {
                label = name;
            }
        } else if (label.find('/') != std::string::npos || label.find('\\') != std::string::npos) {
            label = BasenamePath(label);
        }

        return SanitizeHudText(label, 56);
    }

    std::string AudioModeLabel() const {
        switch (currentAudioMode_) {
            case AudioMode::Synthetic:
                return "SYNTHETIC";
            case AudioMode::GlobalCapture:
                return "GLOBAL CAPTURE";
            case AudioMode::MediaFallback:
                return "MEDIA FALLBACK";
            case AudioMode::Microphone:
                return "MICROPHONE";
            default:
                return "UNKNOWN";
        }
    }

    void DrawHudTextCentered(float minU,
                             float maxU,
                             float minV,
                             float maxV,
                             const std::string& text,
                             int scale,
                             uint8_t alpha = 255) {
        if (text.empty()) {
            return;
        }

        const int rectMinX = static_cast<int>(minU * static_cast<float>(kHudTextTextureWidth));
        const int rectMaxX = static_cast<int>(maxU * static_cast<float>(kHudTextTextureWidth));
        const int rectTop = static_cast<int>((1.0f - maxV) * static_cast<float>(kHudTextTextureHeight));
        const int rectBottom = static_cast<int>((1.0f - minV) * static_cast<float>(kHudTextTextureHeight));

        const int rectWidth = std::max(0, rectMaxX - rectMinX);
        const int horizontalPadding = std::max(2, scale);
        const int usableWidth = std::max(0, rectWidth - horizontalPadding * 2);
        const std::string fittedText = FitHudTextToWidth(text, scale, usableWidth);
        if (fittedText.empty()) {
            return;
        }

        const int textWidth = MeasureHudTextWidth(fittedText, scale);
        const int textHeight = kHudGlyphHeight * scale;

        const int x = rectMinX + horizontalPadding + std::max(0, (usableWidth - textWidth) / 2);
        const int y = rectTop + std::max(0, (rectBottom - rectTop - textHeight) / 2);
        DrawHudText(hudTextPixels_, x, y, scale, fittedText, alpha);
    }

    void RefreshHudTextTextureIfNeeded() {
        if (hudTextTexture_ == 0) {
            return;
        }

        const std::string audioLabel = SanitizeHudText(AudioModeLabel(), 18);
        const std::string projectionLabel =
            projectionMode_ == ProjectionMode::FrontDome ? "DOME" : "SPHERE";
        const std::string playbackLabel = currentMediaPlaying_ ? "PLAYING" : "PAUSED";
        const std::string presetLabel = SanitizeHudText(currentPresetLabel_, 56);
        const std::string trackLabel = BuildTrackDisplayLabel(currentMediaLabel_);
        const std::string centerInfoLabel = "TRACK: " + trackLabel;

        const bool changed =
            hudTextDirty_ ||
            hudRenderedAudioLabel_ != audioLabel ||
            hudRenderedProjectionLabel_ != projectionLabel ||
            hudRenderedPlaybackLabel_ != playbackLabel ||
            hudRenderedPresetLabel_ != presetLabel ||
            hudRenderedTrackLabel_ != trackLabel ||
            hudRenderedInputFeedbackLabel_ != centerInfoLabel;

        if (!changed) {
            return;
        }

        hudRenderedAudioLabel_ = audioLabel;
        hudRenderedProjectionLabel_ = projectionLabel;
        hudRenderedPlaybackLabel_ = playbackLabel;
        hudRenderedPresetLabel_ = presetLabel;
        hudRenderedTrackLabel_ = trackLabel;
        hudRenderedInputFeedbackLabel_ = centerInfoLabel;
        hudTextDirty_ = false;

        std::fill(hudTextPixels_.begin(), hudTextPixels_.end(), 0);
        DrawHudTextCentered(0.05f, 0.34f, 0.885f, 0.93f, "AUD " + hudRenderedAudioLabel_, kHudStatusScale);
        DrawHudTextCentered(0.36f, 0.64f, 0.885f, 0.93f, "PROJ " + hudRenderedProjectionLabel_, kHudStatusScale);
        DrawHudTextCentered(0.66f, 0.95f, 0.885f, 0.93f, "PLAY " + hudRenderedPlaybackLabel_, kHudStatusScale);
        DrawHudTextCentered(0.05f, 0.95f, 0.835f, 0.875f, "PRESET " + hudRenderedPresetLabel_, kHudDetailScale);

        DrawHudTextCentered(kHudRectPrevPreset.minU, kHudRectPrevPreset.maxU, kHudRectPrevPreset.minV, kHudRectPrevPreset.maxV, "PREV PRESET", kHudActionScale);
        DrawHudTextCentered(kHudRectNextPreset.minU, kHudRectNextPreset.maxU, kHudRectNextPreset.minV, kHudRectNextPreset.maxV, "NEXT PRESET", kHudActionScale);
        DrawHudTextCentered(kHudRectTogglePlay.minU, kHudRectTogglePlay.maxU, kHudRectTogglePlay.minV, kHudRectTogglePlay.maxV, "PLAY PAUSE", kHudActionScale);
        DrawHudTextCentered(kHudRectNextTrack.minU, kHudRectNextTrack.maxU, kHudRectNextTrack.minV, kHudRectNextTrack.maxV, "NEXT TRACK", kHudActionScale);
        DrawHudTextCentered(0.07f, 0.93f, 0.535f, 0.585f, hudRenderedInputFeedbackLabel_, kHudInputScale, 170);
        DrawHudTextCentered(0.07f, 0.93f, 0.245f, 0.295f, "DIRECT HAND TOUCH", kHudInputScale, 190);
        DrawHudTextCentered(kHudRectPack.minU, kHudRectPack.maxU, kHudRectPack.minV, kHudRectPack.maxV, "PACK", kHudTriggerScale);
        DrawHudTextCentered(kHudRectCenter.minU, kHudRectCenter.maxU, kHudRectCenter.minV, kHudRectCenter.maxV, "AUDIO MODE", kHudTriggerScale);
        DrawHudTextCentered(kHudRectProjection.minU, kHudRectProjection.maxU, kHudRectProjection.minV, kHudRectProjection.maxV, "PROJECTION", kHudTriggerScale);

        glBindTexture(GL_TEXTURE_2D, hudTextTexture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D,
                        0,
                        0,
                        0,
                        kHudTextTextureWidth,
                        kHudTextTextureHeight,
                        GL_RED,
                        GL_UNSIGNED_BYTE,
                        hudTextPixels_.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    bool GetActionPressed(XrAction action) const {
        if (action == XR_NULL_HANDLE) {
            return false;
        }

        XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.action = action;
        XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
        if (XR_FAILED(xrGetActionStateBoolean(xrSession_, &getInfo, &state))) {
            return false;
        }

        return state.isActive && state.changedSinceLastSync && state.currentState;
    }

    bool GetFloatActionPressed(XrAction action, float threshold, bool& wasPressedLastFrame) const {
        return GetFloatActionPressed(action, XR_NULL_PATH, threshold, wasPressedLastFrame);
    }

    bool GetFloatActionPressed(XrAction action, XrPath subactionPath, float threshold, bool& wasPressedLastFrame) const {
        if (action == XR_NULL_HANDLE) {
            wasPressedLastFrame = false;
            return false;
        }

        XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.action = action;
        getInfo.subactionPath = subactionPath;
        XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
        if (XR_FAILED(xrGetActionStateFloat(xrSession_, &getInfo, &state))) {
            wasPressedLastFrame = false;
            return false;
        }

        const bool isPressed = state.isActive && state.currentState >= threshold;
        const bool justPressed = isPressed && !wasPressedLastFrame;
        wasPressedLastFrame = isPressed;
        return justPressed;
    }

    struct HudPanelFrame {
        glm::vec3 position;
        glm::vec3 right;
        glm::vec3 up;
        glm::vec3 normal;
    };

    float EffectiveHudDistance() const {
        if (!hudHandTrackingActive_) {
            return hudDistance_;
        }
        return std::min(hudDistance_, kHudDistanceHandTracking);
    }

    float EffectiveHudVerticalOffset() const {
        if (!hudHandTrackingActive_) {
            return hudVerticalOffset_;
        }
        return std::max(hudVerticalOffset_, kHudVerticalOffsetHandTracking);
    }

    HudPanelFrame BuildHudPanelFrame(const XrPosef& headPose) const {
        const glm::vec3 headPosition(headPose.position.x, headPose.position.y, headPose.position.z);
        const glm::quat headOrientation(headPose.orientation.w,
                                        headPose.orientation.x,
                                        headPose.orientation.y,
                                        headPose.orientation.z);
        const float hudDistance = EffectiveHudDistance();
        const float hudVerticalOffset = EffectiveHudVerticalOffset();
        HudPanelFrame panel{};
        panel.position = headPosition + headOrientation * glm::vec3(0.0f, hudVerticalOffset, -hudDistance);
        panel.right = glm::normalize(headOrientation * glm::vec3(1.0f, 0.0f, 0.0f));
        panel.up = glm::normalize(headOrientation * glm::vec3(0.0f, 1.0f, 0.0f));
        panel.normal = glm::normalize(headOrientation * glm::vec3(0.0f, 0.0f, 1.0f));
        return panel;
    }

    bool IsAimPoseActive(XrPath handPath) const {
        if (actionAimPose_ == XR_NULL_HANDLE) {
            return false;
        }
        XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
        getInfo.action = actionAimPose_;
        getInfo.subactionPath = handPath;
        XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
        if (XR_FAILED(xrGetActionStatePose(xrSession_, &getInfo, &state))) {
            return false;
        }
        return state.isActive;
    }

    XrPath GetCurrentInteractionProfilePath(XrPath handPath) const {
        if (xrSession_ == XR_NULL_HANDLE || handPath == XR_NULL_PATH) {
            return XR_NULL_PATH;
        }
        XrInteractionProfileState profileState{XR_TYPE_INTERACTION_PROFILE_STATE};
        if (XR_FAILED(xrGetCurrentInteractionProfile(xrSession_, handPath, &profileState))) {
            return XR_NULL_PATH;
        }
        return profileState.interactionProfile;
    }

    bool IsControllerInteractionProfile(XrPath interactionProfilePath) const {
        if (interactionProfilePath == XR_NULL_PATH) {
            return false;
        }
        return (controllerPlusProfilePath_ != XR_NULL_PATH && interactionProfilePath == controllerPlusProfilePath_) ||
            (controllerProProfilePath_ != XR_NULL_PATH && interactionProfilePath == controllerProProfilePath_) ||
            (controllerTouchProfilePath_ != XR_NULL_PATH && interactionProfilePath == controllerTouchProfilePath_);
    }

    bool IsHandTrackingInputActive(XrPath handPath) const {
        const XrPath interactionProfilePath = GetCurrentInteractionProfilePath(handPath);
        if (interactionProfilePath == XR_NULL_PATH) {
            return false;
        }
        if (handInteractionProfilePath_ != XR_NULL_PATH &&
            interactionProfilePath == handInteractionProfilePath_) {
            return true;
        }

        // Some Quest runtime builds can expose a non-controller hand profile path.
        return !IsControllerInteractionProfile(interactionProfilePath);
    }

    void ResetHandModeDebounce() {
        leftHandModeDebounce_ = {};
        rightHandModeDebounce_ = {};
    }

    void ResetHudPointerAndTouchState() {
        hudPointerLeftVisible_ = false;
        hudPointerRightVisible_ = false;
        hudPointerLeftMode_ = HudPointerMode::None;
        hudPointerRightMode_ = HudPointerMode::None;
        hudTouchLeftActive_ = false;
        hudTouchRightActive_ = false;
        hudTouchLeftWasActive_ = false;
        hudTouchRightWasActive_ = false;
        hudTouchLeftLatched_ = false;
        hudTouchRightLatched_ = false;
    }

    bool DebounceHandTrackingMode(HandSide handSide, bool rawHandTracking, double nowSeconds) {
        HandModeDebounceState& state =
            handSide == HandSide::Left ? leftHandModeDebounce_ : rightHandModeDebounce_;
        if (!state.initialized) {
            state.initialized = true;
            state.rawHandTracking = rawHandTracking;
            state.debouncedHandTracking = rawHandTracking;
            state.rawStateSinceSeconds = nowSeconds;
            return state.debouncedHandTracking;
        }

        if (state.rawHandTracking != rawHandTracking) {
            state.rawHandTracking = rawHandTracking;
            state.rawStateSinceSeconds = nowSeconds;
        }

        if (state.debouncedHandTracking != state.rawHandTracking) {
            const double debounceSeconds = state.rawHandTracking
                ? kHandModeSwitchToHandDebounceSeconds
                : kHandModeSwitchToControllerDebounceSeconds;
            if (nowSeconds - state.rawStateSinceSeconds >= debounceSeconds) {
                state.debouncedHandTracking = state.rawHandTracking;
            }
        }

        return state.debouncedHandTracking;
    }

    bool LocateAimPoseForHand(HandSide handSide, XrTime displayTime, XrPosef& poseOut) const {
        const XrSpace handSpace = handSide == HandSide::Left ? leftAimSpace_ : rightAimSpace_;
        const XrPath handPath = handSide == HandSide::Left ? leftHandPath_ : rightHandPath_;
        if (handSpace == XR_NULL_HANDLE || handPath == XR_NULL_PATH || xrAppSpace_ == XR_NULL_HANDLE) {
            return false;
        }
        if (!IsAimPoseActive(handPath)) {
            return false;
        }

        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        if (XR_FAILED(xrLocateSpace(handSpace, xrAppSpace_, displayTime, &location))) {
            return false;
        }
        constexpr XrSpaceLocationFlags kRequiredFlags =
            XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if ((location.locationFlags & kRequiredFlags) != kRequiredFlags) {
            return false;
        }

        poseOut = location.pose;
        return true;
    }

    XrPosef BuildCenterHeadPose(uint32_t viewCount) const {
        XrPosef pose{};
        pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
        pose.position = {0.0f, 0.0f, 0.0f};
        if (viewCount == 0) {
            return pose;
        }
        if (viewCount == 1) {
            return xrViews_[0].pose;
        }

        const XrPosef& leftPose = xrViews_[0].pose;
        const XrPosef& rightPose = xrViews_[1].pose;
        const glm::vec3 leftPosition(leftPose.position.x, leftPose.position.y, leftPose.position.z);
        const glm::vec3 rightPosition(rightPose.position.x, rightPose.position.y, rightPose.position.z);
        const glm::vec3 centerPosition = (leftPosition + rightPosition) * 0.5f;

        glm::quat leftOrientation(leftPose.orientation.w,
                                  leftPose.orientation.x,
                                  leftPose.orientation.y,
                                  leftPose.orientation.z);
        glm::quat rightOrientation(rightPose.orientation.w,
                                   rightPose.orientation.x,
                                   rightPose.orientation.y,
                                   rightPose.orientation.z);
        if (glm::dot(leftOrientation, rightOrientation) < 0.0f) {
            rightOrientation = -rightOrientation;
        }

        glm::quat centerOrientation = leftOrientation + rightOrientation;
        if (glm::length(centerOrientation) < 1.0e-5f) {
            centerOrientation = leftOrientation;
        } else {
            centerOrientation = glm::normalize(centerOrientation);
        }

        pose.orientation = {centerOrientation.x, centerOrientation.y, centerOrientation.z, centerOrientation.w};
        pose.position = {centerPosition.x, centerPosition.y, centerPosition.z};
        return pose;
    }

    bool RaycastHudPanel(const HudPanelFrame& panel, const XrPosef& aimPose, glm::vec2& uvOut) const {
        const glm::vec3 origin(aimPose.position.x, aimPose.position.y, aimPose.position.z);
        const glm::quat orientation(aimPose.orientation.w,
                                    aimPose.orientation.x,
                                    aimPose.orientation.y,
                                    aimPose.orientation.z);
        const glm::vec3 direction = glm::normalize(orientation * glm::vec3(0.0f, 0.0f, -1.0f));

        const float denom = glm::dot(direction, panel.normal);
        if (std::fabs(denom) < 1.0e-5f) {
            return false;
        }

        const float t = glm::dot(panel.position - origin, panel.normal) / denom;
        if (t <= 0.0f) {
            return false;
        }

        const glm::vec3 hitPoint = origin + direction * t;
        const glm::vec3 local = hitPoint - panel.position;
        const float localX = glm::dot(local, panel.right) / hudWidth_;
        const float localY = glm::dot(local, panel.up) / hudHeight_;
        const float u = localX + 0.5f;
        const float v = localY + 0.5f;
        if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
            return false;
        }

        uvOut = glm::vec2(u, v);
        return true;
    }

    bool LocateHandTouchPoint(HandSide handSide, glm::vec3& pointOut) const {
        const HandJointRenderState& handState =
            handSide == HandSide::Left ? leftHandJointRender_ : rightHandJointRender_;
        if (!handState.isActive) {
            return false;
        }

        const uint32_t indexTip = static_cast<uint32_t>(XR_HAND_JOINT_INDEX_TIP_EXT);
        const uint32_t indexDistal = static_cast<uint32_t>(XR_HAND_JOINT_INDEX_DISTAL_EXT);
        if (indexTip >= XR_HAND_JOINT_COUNT_EXT || handState.tracked[indexTip] == 0) {
            return false;
        }

        glm::vec3 touchPoint = handState.positions[indexTip];
        if (indexDistal < XR_HAND_JOINT_COUNT_EXT && handState.tracked[indexDistal] != 0) {
            const glm::vec3 distal = handState.positions[indexDistal];
            const glm::vec3 tipDirection = touchPoint - distal;
            const float tipDirectionLength = glm::length(tipDirection);
            if (tipDirectionLength > 1.0e-5f) {
                touchPoint += (tipDirection / tipDirectionLength) * kHudTouchForwardOffset;
            }
        }

        pointOut = touchPoint;
        return true;
    }

    bool LocateHudTouchPoint(const HudPanelFrame& panel,
                             const glm::vec3& touchPoint,
                             glm::vec2& uvOut,
                             float& signedDistanceOut) const {
        const glm::vec3 local = touchPoint - panel.position;
        const float u = glm::dot(local, panel.right) / hudWidth_ + 0.5f;
        const float v = glm::dot(local, panel.up) / hudHeight_ + 0.5f;
        if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
            return false;
        }

        signedDistanceOut = glm::dot(local, panel.normal);
        uvOut = glm::vec2(u, v);
        return true;
    }

    bool UvInRect(const glm::vec2& uv, const HudRect& rect) const {
        return uv.x >= rect.minU && uv.x <= rect.maxU && uv.y >= rect.minV && uv.y <= rect.maxV;
    }

    HudButtonId ResolveHudButton(const glm::vec2& uv) const {
        if (UvInRect(uv, kHudRectPrevPreset)) {
            return HudButtonId::PrevPreset;
        }
        if (UvInRect(uv, kHudRectNextPreset)) {
            return HudButtonId::NextPreset;
        }
        if (UvInRect(uv, kHudRectTogglePlay)) {
            return HudButtonId::TogglePlay;
        }
        if (UvInRect(uv, kHudRectNextTrack)) {
            return HudButtonId::NextTrack;
        }
        if (UvInRect(uv, kHudRectPack)) {
            return HudButtonId::OptionalPack;
        }
        if (UvInRect(uv, kHudRectCenter)) {
            return HudButtonId::CycleAudio;
        }
        if (UvInRect(uv, kHudRectProjection)) {
            return HudButtonId::ToggleProjection;
        }
        return HudButtonId::None;
    }

    void ExecuteHudButton(HudButtonId button, double nowSeconds) {
        switch (button) {
            case HudButtonId::PrevPreset:
                SwitchPresetRelative(-1, true);
                hudFlashX_ = kHudFlashPeak;
                SetHudInputFeedback(nowSeconds, "UI PREV PRESET");
                break;

            case HudButtonId::NextPreset:
                SwitchPresetRelative(+1, true);
                hudFlashA_ = kHudFlashPeak;
                SetHudInputFeedback(nowSeconds, "UI NEXT PRESET");
                break;

            case HudButtonId::TogglePlay:
                CallJavaControlMethod("onNativeTogglePlayback");
                hudFlashY_ = kHudFlashPeak;
                SetHudInputFeedback(nowSeconds, "UI PLAY PAUSE");
                break;

            case HudButtonId::NextTrack:
                CallJavaControlMethod("onNativeNextTrack");
                hudFlashB_ = kHudFlashPeak;
                SetHudInputFeedback(nowSeconds, "UI NEXT TRACK");
                break;

            case HudButtonId::OptionalPack:
                CallJavaControlMethod("onNativeRequestOptionalCreamPack");
                lastPresetScanSeconds_ = nowSeconds - kPresetScanIntervalSeconds;
                hudFlashLt_ = kHudFlashPeak;
                SetHudInputFeedback(nowSeconds, "UI REQUEST PACK");
                break;

            case HudButtonId::CycleAudio:
                CallJavaControlMethod("onNativeCycleAudioInput");
                hudFlashMenu_ = kHudFlashPeak;
                SetHudInputFeedback(nowSeconds, "UI AUDIO INPUT");
                break;

            case HudButtonId::ToggleProjection:
                projectionMode_ = projectionMode_ == ProjectionMode::FullSphere
                    ? ProjectionMode::FrontDome
                    : ProjectionMode::FullSphere;
                hudFlashRt_ = kHudFlashPeak;
                hudTextDirty_ = true;
                SetHudInputFeedback(nowSeconds,
                                    projectionMode_ == ProjectionMode::FrontDome
                                        ? "UI PROJECTION DOME"
                                        : "UI PROJECTION SPHERE");
                break;

            case HudButtonId::None:
            default:
                return;
        }

        ExtendHudVisibility(nowSeconds, kHudVisibleAfterInteractionSeconds);
    }

    void UpdateHudPointerState(XrTime displayTime,
                               const XrPosef& headPose,
                               bool leftHandInteractionActive,
                               bool rightHandInteractionActive) {
        hudPointerLeftVisible_ = false;
        hudPointerRightVisible_ = false;
        hudPointerLeftMode_ = HudPointerMode::None;
        hudPointerRightMode_ = HudPointerMode::None;
        hudTouchLeftActive_ = false;
        hudTouchRightActive_ = false;

        if (!hudEnabled_) {
            hudTouchLeftLatched_ = false;
            hudTouchRightLatched_ = false;
            return;
        }

        const HudPanelFrame panel = BuildHudPanelFrame(headPose);
        auto updateHandPointer = [&](HandSide handSide, bool handInteractionActive) {
            bool* pointerVisible = handSide == HandSide::Left ? &hudPointerLeftVisible_ : &hudPointerRightVisible_;
            glm::vec2* pointerUv = handSide == HandSide::Left ? &hudPointerLeftUv_ : &hudPointerRightUv_;
            HudPointerMode* pointerMode = handSide == HandSide::Left ? &hudPointerLeftMode_ : &hudPointerRightMode_;
            bool* touchActive = handSide == HandSide::Left ? &hudTouchLeftActive_ : &hudTouchRightActive_;
            bool* touchLatched = handSide == HandSide::Left ? &hudTouchLeftLatched_ : &hudTouchRightLatched_;

            if (handInteractionActive) {
                glm::vec3 touchPoint(0.0f);
                glm::vec2 touchUv(0.0f);
                float touchDistance = 0.0f;
                if (LocateHandTouchPoint(handSide, touchPoint) &&
                    LocateHudTouchPoint(panel, touchPoint, touchUv, touchDistance)) {
                    const bool touchHover =
                        touchDistance <= kHudTouchHoverDistance && touchDistance >= -kHudTouchMaxPenetration;
                    const bool touchAcquire =
                        touchDistance <= kHudTouchActivationDistance && touchDistance >= -kHudTouchMaxPenetration;
                    const bool touchRelease =
                        touchDistance <= kHudTouchReleaseDistance &&
                        touchDistance >= -kHudTouchReleaseMaxPenetration;
                    *touchActive = *touchLatched ? touchRelease : touchAcquire;
                    *touchLatched = *touchActive;
                    if (touchHover || *touchActive) {
                        *pointerVisible = true;
                        *pointerUv = touchUv;
                        *pointerMode = HudPointerMode::Touch;
                    }
                } else {
                    *touchLatched = false;
                }
                return;
            }

            *touchLatched = false;
            XrPosef aimPose{};
            if (!LocateAimPoseForHand(handSide, displayTime, aimPose)) {
                return;
            }

            if (!(*pointerVisible)) {
                glm::vec2 rayUv(0.0f);
                if (RaycastHudPanel(panel, aimPose, rayUv)) {
                    *pointerVisible = true;
                    *pointerUv = rayUv;
                    *pointerMode = HudPointerMode::Ray;
                }
            }
        };

        updateHandPointer(HandSide::Left, leftHandInteractionActive);
        updateHandPointer(HandSide::Right, rightHandInteractionActive);
    }

    bool ConsumeHudDirectTouchPress(double nowSeconds, HandSide handSide) {
        if (!hudEnabled_) {
            return false;
        }

        const bool touchActive = handSide == HandSide::Left ? hudTouchLeftActive_ : hudTouchRightActive_;
        bool& touchWasActive = handSide == HandSide::Left ? hudTouchLeftWasActive_ : hudTouchRightWasActive_;
        if (!touchActive) {
            touchWasActive = false;
            return false;
        }

        const bool touchJustPressed = !touchWasActive;
        touchWasActive = true;
        if (!touchJustPressed) {
            return false;
        }

        if (nowSeconds > hudVisibleUntilSeconds_) {
            SetHudInputFeedback(nowSeconds, "MENU SHOWN");
            ExtendHudVisibility(nowSeconds, kHudVisibleAfterInteractionSeconds);
            return true;
        }

        const bool pointerVisible = handSide == HandSide::Left ? hudPointerLeftVisible_ : hudPointerRightVisible_;
        const HudPointerMode pointerMode = handSide == HandSide::Left ? hudPointerLeftMode_ : hudPointerRightMode_;
        if (!pointerVisible || pointerMode != HudPointerMode::Touch) {
            return false;
        }

        const glm::vec2 pointerUv = handSide == HandSide::Left ? hudPointerLeftUv_ : hudPointerRightUv_;
        const HudButtonId button = ResolveHudButton(pointerUv);
        if (button == HudButtonId::None) {
            return false;
        }

        ExecuteHudButton(button, nowSeconds);
        return true;
    }

    bool ConsumeHudPointerPress(double nowSeconds, HandSide handSide, bool allowMenuWake) {
        if (!hudEnabled_) {
            return false;
        }

        if (nowSeconds > hudVisibleUntilSeconds_) {
            if (!allowMenuWake) {
                return false;
            }
            SetHudInputFeedback(nowSeconds, "MENU SHOWN");
            ExtendHudVisibility(nowSeconds, kHudVisibleAfterInteractionSeconds);
            return true;
        }

        const bool pointerVisible = handSide == HandSide::Left ? hudPointerLeftVisible_ : hudPointerRightVisible_;
        if (!pointerVisible) {
            return false;
        }
        const glm::vec2 pointerUv = handSide == HandSide::Left ? hudPointerLeftUv_ : hudPointerRightUv_;
        const HudButtonId button = ResolveHudButton(pointerUv);
        if (button == HudButtonId::None) {
            return false;
        }

        ExecuteHudButton(button, nowSeconds);
        return true;
    }

    void ExtendHudVisibility(double nowSeconds, double durationSeconds) {
        hudVisibleUntilSeconds_ = std::max(hudVisibleUntilSeconds_, nowSeconds + durationSeconds);
    }

    void PollRuntimeDebugProperties(double nowSeconds) {
        if (nowSeconds - lastRuntimePropertyPollSeconds_ < kRuntimePropertyPollIntervalSeconds) {
            return;
        }
        lastRuntimePropertyPollSeconds_ = nowSeconds;

        auto readBoolProperty = [](const char* key, bool defaultValue) {
            std::string text;
            bool parsed = defaultValue;
            if (ReadSystemProperty(key, text)) {
                bool propValue = defaultValue;
                if (ParseBoolText(text, propValue)) {
                    parsed = propValue;
                }
            }
            return parsed;
        };

        auto readFloatProperty = [](const char* key, float defaultValue) {
            std::string text;
            float parsed = defaultValue;
            if (ReadSystemProperty(key, text)) {
                float propValue = defaultValue;
                if (ParseFloatText(text, propValue)) {
                    parsed = propValue;
                }
            }
            return parsed;
        };

        const bool hudEnabled = readBoolProperty("debug.projectm.quest.hud.enabled", true);
        const float hudDistance = std::clamp(readFloatProperty("debug.projectm.quest.hud.distance", kHudDistance), 0.40f, 3.0f);
        const float hudVerticalOffset = std::clamp(readFloatProperty("debug.projectm.quest.hud.v_offset", kHudVerticalOffset), -1.2f, 1.2f);
        const float hudScale = std::clamp(readFloatProperty("debug.projectm.quest.hud.scale", 1.0f), 0.50f, 2.0f);

        const bool perfAutoSkip = readBoolProperty("debug.projectm.quest.perf.auto_skip", true);
        const bool skipMarked = readBoolProperty("debug.projectm.quest.perf.skip_marked", true);
        const float perfMinFps = std::clamp(readFloatProperty("debug.projectm.quest.perf.min_fps", kDefaultPerfAutoSkipMinFps), 15.0f, 90.0f);
        const float perfHold = std::clamp(readFloatProperty("debug.projectm.quest.perf.bad_seconds",
                                                            static_cast<float>(kDefaultPerfAutoSkipHoldSeconds)),
                                          0.3f,
                                          10.0f);
        const float perfCooldown = std::clamp(readFloatProperty("debug.projectm.quest.perf.cooldown_seconds",
                                                                static_cast<float>(kDefaultPerfAutoSkipCooldownSeconds)),
                                              1.0f,
                                              60.0f);

        const float newHudWidth = kHudWidth * hudScale;
        const float newHudHeight = kHudHeight * hudScale;

        bool hudChanged = false;
        if (hudEnabled_ != hudEnabled) {
            hudEnabled_ = hudEnabled;
            hudChanged = true;
        }
        if (std::fabs(hudDistance_ - hudDistance) > 0.0005f) {
            hudDistance_ = hudDistance;
            hudChanged = true;
        }
        if (std::fabs(hudVerticalOffset_ - hudVerticalOffset) > 0.0005f) {
            hudVerticalOffset_ = hudVerticalOffset;
            hudChanged = true;
        }
        if (std::fabs(hudWidth_ - newHudWidth) > 0.0005f) {
            hudWidth_ = newHudWidth;
            hudChanged = true;
        }
        if (std::fabs(hudHeight_ - newHudHeight) > 0.0005f) {
            hudHeight_ = newHudHeight;
            hudChanged = true;
        }

        if (hudChanged) {
            ExtendHudVisibility(nowSeconds, kHudVisibleAfterStatusChangeSeconds);
            LOGI("HUD tuning updated: enabled=%d distance=%.2f vOffset=%.2f scale=%.2f",
                 hudEnabled_ ? 1 : 0,
                 hudDistance_,
                 hudVerticalOffset_,
                 hudScale);
        }

        perfAutoSkipEnabled_ = perfAutoSkip;
        skipMarkedPresets_ = skipMarked;
        perfAutoSkipMinFps_ = perfMinFps;
        perfAutoSkipHoldSeconds_ = static_cast<double>(perfHold);
        perfAutoSkipCooldownSeconds_ = static_cast<double>(perfCooldown);

        std::string meshText;
        int parsedMeshWidth = kDefaultMeshWidth;
        int parsedMeshHeight = kDefaultMeshHeight;
        if (ReadSystemProperty("debug.projectm.quest.perf.mesh", meshText) &&
            ParseIntPairText(meshText, parsedMeshWidth, parsedMeshHeight)) {
            parsedMeshWidth = std::clamp(parsedMeshWidth, 16, 128);
            parsedMeshHeight = std::clamp(parsedMeshHeight, 12, 128);
        } else {
            parsedMeshWidth = kDefaultMeshWidth;
            parsedMeshHeight = kDefaultMeshHeight;
        }

        if ((meshWidth_ != parsedMeshWidth || meshHeight_ != parsedMeshHeight) && projectM_ != nullptr) {
            meshWidth_ = parsedMeshWidth;
            meshHeight_ = parsedMeshHeight;
            projectm_set_mesh_size(projectM_, meshWidth_, meshHeight_);
            LOGI("projectM mesh size set to %d x %d", meshWidth_, meshHeight_);
            hudInputFeedbackLabel_ = "QUALITY MESH UPDATED";
            hudInputFeedbackUntilSeconds_ = nowSeconds + kHudInputFeedbackSeconds;
            hudTextDirty_ = true;
            ExtendHudVisibility(nowSeconds, kHudVisibleAfterStatusChangeSeconds);
        }

        const bool clearMarkedRequest = readBoolProperty("debug.projectm.quest.perf.clear_marked", false);
        if (clearMarkedRequest && !clearMarkedLatch_) {
            ClearSlowPresetMarks();
            clearMarkedLatch_ = true;
            hudInputFeedbackLabel_ = "CLEARED SLOW PRESET MARKS";
            hudInputFeedbackUntilSeconds_ = nowSeconds + kHudInputFeedbackSeconds;
            hudTextDirty_ = true;
            ExtendHudVisibility(nowSeconds, kHudVisibleAfterStatusChangeSeconds);
        }
        if (!clearMarkedRequest) {
            clearMarkedLatch_ = false;
        }
    }

    void SetHudInputFeedback(double nowSeconds, const std::string& feedbackLabel) {
        hudInputFeedbackLabel_ = feedbackLabel;
        hudInputFeedbackUntilSeconds_ = nowSeconds + kHudInputFeedbackSeconds;
        hudTextDirty_ = true;
    }

    void UpdatePerformanceAutoSkip(double nowSeconds, float deltaSeconds) {
        if (deltaSeconds <= 0.0f) {
            return;
        }

        const double clampedDelta = std::clamp(static_cast<double>(deltaSeconds), 1.0 / 240.0, 0.5);
        if (smoothedFrameSeconds_ <= 0.0) {
            smoothedFrameSeconds_ = clampedDelta;
        } else {
            smoothedFrameSeconds_ = smoothedFrameSeconds_ * 0.92 + clampedDelta * 0.08;
        }

        if (!perfAutoSkipEnabled_ || presetFiles_.size() <= 1 || usingFallbackPreset_) {
            lowFpsSinceSeconds_ = -1.0;
            return;
        }

        if (nowSeconds - lastPresetSwitchSeconds_ < kPerfGraceAfterPresetSwitchSeconds ||
            nowSeconds - lastAutoSkipSeconds_ < perfAutoSkipCooldownSeconds_) {
            lowFpsSinceSeconds_ = -1.0;
            return;
        }

        const double smoothedFps = 1.0 / std::max(smoothedFrameSeconds_, 1e-4);
        if (smoothedFps >= static_cast<double>(perfAutoSkipMinFps_)) {
            lowFpsSinceSeconds_ = -1.0;
            return;
        }

        if (lowFpsSinceSeconds_ < 0.0) {
            lowFpsSinceSeconds_ = nowSeconds;
            return;
        }

        if (nowSeconds - lowFpsSinceSeconds_ < perfAutoSkipHoldSeconds_) {
            return;
        }

        const std::string slowPresetLabel = currentPresetLabel_;
        MarkCurrentPresetSlow();
        lastAutoSkipSeconds_ = nowSeconds;
        lowFpsSinceSeconds_ = -1.0;

        SetHudInputFeedback(nowSeconds, "AUTO-SKIP SLOW PRESET");
        ExtendHudVisibility(nowSeconds, kHudVisibleAfterInteractionSeconds);
        SwitchPresetRelative(+1, true);
        LOGW("Auto-skipped slow preset %s (smoothed FPS %.1f < %.1f)",
             slowPresetLabel.c_str(),
             smoothedFps,
             static_cast<double>(perfAutoSkipMinFps_));
    }

    void PollInputActions(double nowSeconds, XrTime displayTime, const XrPosef& headPose) {
        if (!sessionRunning_ || actionSet_ == XR_NULL_HANDLE) {
            hudHandTrackingActive_ = false;
            ResetHandModeDebounce();
            ResetHudPointerAndTouchState();
            ClearHandJointRenderState();
            return;
        }

        XrActiveActionSet activeSet{};
        activeSet.actionSet = actionSet_;

        XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
        syncInfo.countActiveActionSets = 1;
        syncInfo.activeActionSets = &activeSet;
        if (XR_FAILED(xrSyncActions(xrSession_, &syncInfo))) {
            rightTriggerPressed_ = false;
            leftTriggerPressed_ = false;
            hudHandTrackingActive_ = false;
            ResetHandModeDebounce();
            ResetHudPointerAndTouchState();
            ClearHandJointRenderState();
            return;
        }

        const bool leftHandTrackingRaw = IsHandTrackingInputActive(leftHandPath_);
        const bool rightHandTrackingRaw = IsHandTrackingInputActive(rightHandPath_);
        const bool leftHandTrackingActive =
            DebounceHandTrackingMode(HandSide::Left, leftHandTrackingRaw, nowSeconds);
        const bool rightHandTrackingActive =
            DebounceHandTrackingMode(HandSide::Right, rightHandTrackingRaw, nowSeconds);
        hudHandTrackingActive_ = leftHandTrackingActive || rightHandTrackingActive;
        UpdateHudPointerState(displayTime, headPose, leftHandTrackingActive, rightHandTrackingActive);

        bool handledInput = false;
        handledInput |= ConsumeHudDirectTouchPress(nowSeconds, HandSide::Left);
        handledInput |= ConsumeHudDirectTouchPress(nowSeconds, HandSide::Right);

        if (GetActionPressed(actionNextPreset_)) {
            SwitchPresetRelative(+1, true);
            hudFlashA_ = kHudFlashPeak;
            SetHudInputFeedback(nowSeconds, "A NEXT PRESET");
            handledInput = true;
        }
        if (GetActionPressed(actionPrevPreset_)) {
            SwitchPresetRelative(-1, true);
            hudFlashX_ = kHudFlashPeak;
            SetHudInputFeedback(nowSeconds, "X PREV PRESET");
            handledInput = true;
        }
        if (GetActionPressed(actionTogglePlay_)) {
            CallJavaControlMethod("onNativeTogglePlayback");
            hudFlashY_ = kHudFlashPeak;
            SetHudInputFeedback(nowSeconds, "Y PLAY PAUSE");
            handledInput = true;
        }
        if (GetActionPressed(actionNextTrack_)) {
            CallJavaControlMethod("onNativeNextTrack");
            hudFlashB_ = kHudFlashPeak;
            SetHudInputFeedback(nowSeconds, "B NEXT TRACK");
            handledInput = true;
        }
        if (GetActionPressed(actionPrevTrack_)) {
            CallJavaControlMethod("onNativePreviousTrack");
            hudFlashMenu_ = kHudFlashPeak;
            SetHudInputFeedback(nowSeconds, "L3 PREV TRACK");
            handledInput = true;
        }
        if (GetActionPressed(actionCycleAudioInput_)) {
            CallJavaControlMethod("onNativeCycleAudioInput");
            hudFlashMenu_ = kHudFlashPeak;
            SetHudInputFeedback(nowSeconds, "R3 AUDIO INPUT");
            handledInput = true;
        }

        if (rightHandTrackingActive) {
            rightTriggerPressed_ = false;
        } else if (GetFloatActionPressed(actionToggleProjection_,
                                         rightHandPath_,
                                         kTriggerPressThreshold,
                                         rightTriggerPressed_)) {
            const bool consumedByHud = ConsumeHudPointerPress(
                nowSeconds,
                HandSide::Right,
                true);
            if (!consumedByHud) {
                projectionMode_ = projectionMode_ == ProjectionMode::FullSphere
                    ? ProjectionMode::FrontDome
                    : ProjectionMode::FullSphere;
                hudFlashRt_ = kHudFlashPeak;
                hudTextDirty_ = true;
                SetHudInputFeedback(nowSeconds,
                                    projectionMode_ == ProjectionMode::FrontDome
                                        ? "RT PROJECTION DOME"
                                        : "RT PROJECTION SPHERE");
            }
            handledInput = true;
        }
        if (leftHandTrackingActive) {
            leftTriggerPressed_ = false;
        } else if (GetFloatActionPressed(actionOptionalPack_,
                                         leftHandPath_,
                                         kTriggerPressThreshold,
                                         leftTriggerPressed_)) {
            const bool consumedByHud = ConsumeHudPointerPress(
                nowSeconds,
                HandSide::Left,
                true);
            if (!consumedByHud) {
                CallJavaControlMethod("onNativeRequestOptionalCreamPack");
                lastPresetScanSeconds_ = nowSeconds - kPresetScanIntervalSeconds;
                hudFlashLt_ = kHudFlashPeak;
                SetHudInputFeedback(nowSeconds, "LT REQUEST PACK");
            }
            handledInput = true;
        }

        if (handledInput) {
            ExtendHudVisibility(nowSeconds, kHudVisibleAfterInteractionSeconds);
        }
    }

    void UpdateUiStateFromJava(double nowSeconds) {
        std::lock_guard<std::mutex> lock(g_uiMutex);
        const std::string mediaLabel = g_mediaLabel;
        if (currentAudioMode_ != g_audioMode ||
            currentMediaPlaying_ != g_mediaPlaying ||
            currentMediaLabel_ != mediaLabel) {
            hudTextDirty_ = true;
            ExtendHudVisibility(nowSeconds, kHudVisibleAfterStatusChangeSeconds);
        }
        currentAudioMode_ = g_audioMode;
        currentMediaPlaying_ = g_mediaPlaying;
        currentMediaLabel_ = mediaLabel;
    }

    void AdvanceHudFlash(float deltaSeconds) {
        const float decay = std::max(deltaSeconds * 2.2f, 0.01f);
        auto decayValue = [decay](float& value) {
            value = std::max(0.0f, value - decay);
        };
        decayValue(hudFlashA_);
        decayValue(hudFlashB_);
        decayValue(hudFlashX_);
        decayValue(hudFlashY_);
        decayValue(hudFlashRt_);
        decayValue(hudFlashLt_);
        decayValue(hudFlashMenu_);
    }

    void RenderHud(const glm::mat4& projection, const glm::mat4& view, const XrPosef& pose, double nowSeconds) {
        if (hudProgram_ == 0 || hudVao_ == 0) {
            return;
        }
        if (!hudEnabled_) {
            return;
        }
        if (nowSeconds > hudVisibleUntilSeconds_) {
            return;
        }

        const glm::vec3 basePosition(pose.position.x, pose.position.y, pose.position.z);
        const glm::quat baseOrientation(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z);
        const glm::vec3 panelOffset = baseOrientation * glm::vec3(0.0f, EffectiveHudVerticalOffset(), -EffectiveHudDistance());
        const glm::vec3 panelPosition = basePosition + panelOffset;

        glm::mat4 model = glm::translate(glm::mat4(1.0f), panelPosition) * glm::mat4_cast(baseOrientation);
        model = glm::scale(model, glm::vec3(hudWidth_, hudHeight_, 1.0f));
        const glm::mat4 mvp = projection * view * model;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);

        glUseProgram(hudProgram_);
        RefreshHudTextTextureIfNeeded();
        glUniformMatrix4fv(hudMvpLoc_, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniform4f(hudFlashALoc_, hudFlashA_, 0.0f, 0.0f, 0.0f);
        glUniform4f(hudFlashBLoc_, hudFlashB_, 0.0f, 0.0f, 0.0f);
        glUniform4f(hudFlashXLoc_, hudFlashX_, 0.0f, 0.0f, 0.0f);
        glUniform4f(hudFlashYLoc_, hudFlashY_, 0.0f, 0.0f, 0.0f);
        glUniform4f(hudFlashRtLoc_, hudFlashRt_, 0.0f, 0.0f, 0.0f);
        glUniform4f(hudFlashLtLoc_, hudFlashLt_, 0.0f, 0.0f, 0.0f);
        glUniform4f(hudFlashMenuLoc_, hudFlashMenu_, 0.0f, 0.0f, 0.0f);
        const bool leftTouchMode = hudPointerLeftMode_ == HudPointerMode::Touch;
        const bool rightTouchMode = hudPointerRightMode_ == HudPointerMode::Touch;
        const float leftPointerState = !hudPointerLeftVisible_
            ? 0.0f
            : (leftTouchMode && hudTouchLeftActive_ ? 2.0f : 1.0f);
        const float rightPointerState = !hudPointerRightVisible_
            ? 0.0f
            : (rightTouchMode && hudTouchRightActive_ ? 2.0f : 1.0f);
        glUniform4f(hudPointerLeftLoc_,
                    hudPointerLeftUv_.x,
                    hudPointerLeftUv_.y,
                    leftPointerState,
                    static_cast<float>(static_cast<int>(hudPointerLeftMode_)));
        glUniform4f(hudPointerRightLoc_,
                    hudPointerRightUv_.x,
                    hudPointerRightUv_.y,
                    rightPointerState,
                    static_cast<float>(static_cast<int>(hudPointerRightMode_)));
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, hudTextTexture_);
        glUniform1i(hudTextSamplerLoc_, 1);
        glActiveTexture(GL_TEXTURE0);

        glBindVertexArray(hudVao_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
    }

    void RenderProjectMFrame(double nowSeconds, float deltaSeconds) {
        if (!projectM_) {
            return;
        }

        AddAudioForFrame(nowSeconds);
        if (deltaSeconds > 0.0001f) {
            projectm_set_fps(projectM_, static_cast<int32_t>(1.0f / deltaSeconds));
        }

        RefreshPresetListIfNeeded(nowSeconds);

        if (presetFiles_.size() > 1 && nowSeconds - lastPresetSwitchSeconds_ > kPresetSwitchSeconds) {
            SwitchPresetRelative(+1, true);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, projectMFbo_);
        glViewport(0, 0, static_cast<GLsizei>(kProjectMWidth), static_cast<GLsizei>(kProjectMHeight));
        projectm_opengl_render_frame_fbo(projectM_, projectMFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void PollOpenXrEvents() {
        if (xrInstance_ == XR_NULL_HANDLE) {
            return;
        }

        XrEventDataBuffer eventData{XR_TYPE_EVENT_DATA_BUFFER};
        while (xrPollEvent(xrInstance_, &eventData) == XR_SUCCESS) {
            switch (eventData.type) {
                case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                    LOGW("XR instance loss pending.");
                    exitRenderLoop_ = true;
                    break;

                case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                    const auto* stateChanged = reinterpret_cast<XrEventDataSessionStateChanged*>(&eventData);
                    xrSessionState_ = stateChanged->state;
                    HandleSessionStateChanged();
                    break;
                }

                default:
                    break;
            }

            eventData = {XR_TYPE_EVENT_DATA_BUFFER};
        }
    }

    void HandleSessionStateChanged() {
        switch (xrSessionState_) {
            case XR_SESSION_STATE_READY: {
                XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
                beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (XR_SUCCEEDED(xrBeginSession(xrSession_, &beginInfo))) {
                    sessionRunning_ = true;
                    lastFrameSeconds_ = ElapsedSeconds();
                    lastPresetSwitchSeconds_ = lastFrameSeconds_;
                    lowFpsSinceSeconds_ = -1.0;
                    lastAutoSkipSeconds_ = -1000.0;
                    smoothedFrameSeconds_ = 1.0 / 72.0;
                    lastRuntimePropertyPollSeconds_ = -1000.0;
                    rightTriggerPressed_ = false;
                    leftTriggerPressed_ = false;
                    ResetHudPointerAndTouchState();
                    hudHandTrackingActive_ = false;
                    ResetHandModeDebounce();
                    ClearHandJointRenderState();
                    ExtendHudVisibility(lastFrameSeconds_, kHudVisibleOnStartSeconds);
                    LOGI("XR session started.");
                } else {
                    LOGE("xrBeginSession failed.");
                    exitRenderLoop_ = true;
                }
                break;
            }

            case XR_SESSION_STATE_STOPPING:
                if (sessionRunning_) {
                    xrEndSession(xrSession_);
                    sessionRunning_ = false;
                    rightTriggerPressed_ = false;
                    leftTriggerPressed_ = false;
                    ResetHudPointerAndTouchState();
                    hudHandTrackingActive_ = false;
                    ResetHandModeDebounce();
                    ClearHandJointRenderState();
                    LOGI("XR session stopped.");
                }
                break;

            case XR_SESSION_STATE_EXITING:
            case XR_SESSION_STATE_LOSS_PENDING:
                exitRenderLoop_ = true;
                break;

            default:
                break;
        }
    }

    void RenderFrame() {
        XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState frameState{XR_TYPE_FRAME_STATE};
        if (XR_FAILED(xrWaitFrame(xrSession_, &waitInfo, &frameState))) {
            LOGE("xrWaitFrame failed.");
            exitRenderLoop_ = true;
            return;
        }

        XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
        if (XR_FAILED(xrBeginFrame(xrSession_, &beginInfo))) {
            LOGE("xrBeginFrame failed.");
            exitRenderLoop_ = true;
            return;
        }

        std::vector<XrCompositionLayerProjectionView> projectionViews;
        XrCompositionLayerProjection projectionLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        std::array<XrCompositionLayerBaseHeader*, 1> layers{};

        if (frameState.shouldRender && resumed_ && hasWindow_) {
            const double nowSeconds = ElapsedSeconds();
            const float deltaSeconds = static_cast<float>(nowSeconds - lastFrameSeconds_);
            lastFrameSeconds_ = nowSeconds;

            PollRuntimeDebugProperties(nowSeconds);
            UpdateUiStateFromJava(nowSeconds);
            AdvanceHudFlash(std::max(deltaSeconds, 0.0f));
            UpdatePerformanceAutoSkip(nowSeconds, std::max(deltaSeconds, 0.0f));
            RenderProjectMFrame(nowSeconds, deltaSeconds);

            XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
            locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            locateInfo.displayTime = frameState.predictedDisplayTime;
            locateInfo.space = xrAppSpace_;

            XrViewState viewState{XR_TYPE_VIEW_STATE};
            uint32_t viewCountOutput = 0;
            if (XR_FAILED(xrLocateViews(xrSession_, &locateInfo, &viewState,
                                        static_cast<uint32_t>(xrViews_.size()), &viewCountOutput,
                                        xrViews_.data()))) {
                LOGE("xrLocateViews failed.");
                exitRenderLoop_ = true;
            } else {
                XrPosef centerHeadPose{};
                if (viewCountOutput > 0) {
                    centerHeadPose = BuildCenterHeadPose(viewCountOutput);
                    UpdateHandJointRenderState(frameState.predictedDisplayTime);
                    PollInputActions(nowSeconds, frameState.predictedDisplayTime, centerHeadPose);
                } else {
                    hudHandTrackingActive_ = false;
                    ResetHudPointerAndTouchState();
                    ResetHandModeDebounce();
                    ClearHandJointRenderState();
                }
                projectionViews.clear();
                projectionViews.reserve(viewCountOutput);

                for (uint32_t viewIndex = 0; viewIndex < viewCountOutput; ++viewIndex) {
                    const auto& swapchain = swapchains_[viewIndex];
                    bool acquiredSwapchainImage = false;

                    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                    uint32_t imageIndex = 0;
                    if (XR_FAILED(xrAcquireSwapchainImage(swapchain.handle, &acquireInfo, &imageIndex))) {
                        LOGE("xrAcquireSwapchainImage failed.");
                        exitRenderLoop_ = true;
                        break;
                    }
                    acquiredSwapchainImage = true;

                    XrSwapchainImageWaitInfo waitImageInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                    waitImageInfo.timeout = XR_INFINITE_DURATION;
                    if (XR_FAILED(xrWaitSwapchainImage(swapchain.handle, &waitImageInfo))) {
                        LOGE("xrWaitSwapchainImage failed.");
                        if (acquiredSwapchainImage) {
                            XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                            xrReleaseSwapchainImage(swapchain.handle, &releaseInfo);
                        }
                        exitRenderLoop_ = true;
                        break;
                    }

                    const GLuint colorTexture = swapchain.images[imageIndex].image;
                    glBindFramebuffer(GL_FRAMEBUFFER, swapchainFramebuffer_);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                           colorTexture, 0);
                    glViewport(0, 0, swapchain.width, swapchain.height);
                    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                    glClear(GL_COLOR_BUFFER_BIT);

                    const glm::mat4 projection = BuildProjectionMatrix(xrViews_[viewIndex].fov, kNearZ, kFarZ);
                    const glm::mat4 view = BuildViewMatrix(xrViews_[viewIndex].pose);
                    const glm::mat4 viewProjection = projection * view;

                    glUseProgram(sceneProgram_);
                    glUniformMatrix4fv(uViewProjectionLoc_, 1, GL_FALSE, glm::value_ptr(viewProjection));
                    glUniform1i(uTextureLoc_, 0);
                    glUniform1i(uProjectionModeLoc_,
                                projectionMode_ == ProjectionMode::FrontDome ? 1 : 0);

                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, projectMTexture_);
                    glBindVertexArray(sphereVao_);
                    glDrawElements(GL_TRIANGLES, sphereIndexCount_, GL_UNSIGNED_INT, nullptr);
                    glBindVertexArray(0);

                    RenderHud(projection, view, centerHeadPose, nowSeconds);
                    RenderHandJoints(viewProjection);
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);

                    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                    xrReleaseSwapchainImage(swapchain.handle, &releaseInfo);

                    XrCompositionLayerProjectionView layerView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
                    layerView.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                    layerView.pose = xrViews_[viewIndex].pose;
                    layerView.fov = xrViews_[viewIndex].fov;
                    layerView.subImage.swapchain = swapchain.handle;
                    layerView.subImage.imageRect.offset = {0, 0};
                    layerView.subImage.imageRect.extent = {swapchain.width, swapchain.height};
                    layerView.subImage.imageArrayIndex = 0;
                    projectionViews.push_back(layerView);
                }

                if (!projectionViews.empty()) {
                    projectionLayer.space = xrAppSpace_;
                    projectionLayer.viewCount = static_cast<uint32_t>(projectionViews.size());
                    projectionLayer.views = projectionViews.data();
                    layers[0] = reinterpret_cast<XrCompositionLayerBaseHeader*>(&projectionLayer);
                }
            }
        }

        XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
        endInfo.displayTime = frameState.predictedDisplayTime;
        endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

        if (!projectionViews.empty()) {
            endInfo.layerCount = 1;
            endInfo.layers = layers.data();
        }

        if (XR_FAILED(xrEndFrame(xrSession_, &endInfo))) {
            LOGE("xrEndFrame failed.");
            exitRenderLoop_ = true;
        }
    }

    double ElapsedSeconds() const {
        const auto now = std::chrono::steady_clock::now();
        const std::chrono::duration<double> elapsed = now - startTime_;
        return elapsed.count();
    }

    void Shutdown() {
        if (projectM_) {
            projectm_destroy(projectM_);
            projectM_ = nullptr;
        }

        if (projectMFbo_ != 0) {
            glDeleteFramebuffers(1, &projectMFbo_);
            projectMFbo_ = 0;
        }
        if (projectMTexture_ != 0) {
            glDeleteTextures(1, &projectMTexture_);
            projectMTexture_ = 0;
        }

        if (sphereIbo_ != 0) {
            glDeleteBuffers(1, &sphereIbo_);
            sphereIbo_ = 0;
        }
        if (sphereVbo_ != 0) {
            glDeleteBuffers(1, &sphereVbo_);
            sphereVbo_ = 0;
        }
        if (sphereVao_ != 0) {
            glDeleteVertexArrays(1, &sphereVao_);
            sphereVao_ = 0;
        }
        if (sceneProgram_ != 0) {
            glDeleteProgram(sceneProgram_);
            sceneProgram_ = 0;
        }
        if (hudProgram_ != 0) {
            glDeleteProgram(hudProgram_);
            hudProgram_ = 0;
        }
        if (hudVbo_ != 0) {
            glDeleteBuffers(1, &hudVbo_);
            hudVbo_ = 0;
        }
        if (hudVao_ != 0) {
            glDeleteVertexArrays(1, &hudVao_);
            hudVao_ = 0;
        }
        if (hudTextTexture_ != 0) {
            glDeleteTextures(1, &hudTextTexture_);
            hudTextTexture_ = 0;
        }
        hudTextPixels_.clear();
        if (handVbo_ != 0) {
            glDeleteBuffers(1, &handVbo_);
            handVbo_ = 0;
        }
        if (handVao_ != 0) {
            glDeleteVertexArrays(1, &handVao_);
            handVao_ = 0;
        }
        if (handProgram_ != 0) {
            glDeleteProgram(handProgram_);
            handProgram_ = 0;
        }

        if (swapchainFramebuffer_ != 0) {
            glDeleteFramebuffers(1, &swapchainFramebuffer_);
            swapchainFramebuffer_ = 0;
        }

        for (auto& swapchain : swapchains_) {
            if (swapchain.handle != XR_NULL_HANDLE) {
                xrDestroySwapchain(swapchain.handle);
                swapchain.handle = XR_NULL_HANDLE;
            }
            swapchain.images.clear();
        }
        swapchains_.clear();

        if (leftHandTracker_ != XR_NULL_HANDLE && xrDestroyHandTrackerEXT_ != nullptr) {
            xrDestroyHandTrackerEXT_(leftHandTracker_);
            leftHandTracker_ = XR_NULL_HANDLE;
        }
        if (rightHandTracker_ != XR_NULL_HANDLE && xrDestroyHandTrackerEXT_ != nullptr) {
            xrDestroyHandTrackerEXT_(rightHandTracker_);
            rightHandTracker_ = XR_NULL_HANDLE;
        }

        if (leftAimSpace_ != XR_NULL_HANDLE) {
            xrDestroySpace(leftAimSpace_);
            leftAimSpace_ = XR_NULL_HANDLE;
        }
        if (rightAimSpace_ != XR_NULL_HANDLE) {
            xrDestroySpace(rightAimSpace_);
            rightAimSpace_ = XR_NULL_HANDLE;
        }
        if (xrAppSpace_ != XR_NULL_HANDLE) {
            xrDestroySpace(xrAppSpace_);
            xrAppSpace_ = XR_NULL_HANDLE;
        }

        if (xrSession_ != XR_NULL_HANDLE) {
            if (sessionRunning_) {
                xrEndSession(xrSession_);
                sessionRunning_ = false;
            }
            xrDestroySession(xrSession_);
            xrSession_ = XR_NULL_HANDLE;
        }

        if (actionNextPreset_ != XR_NULL_HANDLE) { xrDestroyAction(actionNextPreset_); actionNextPreset_ = XR_NULL_HANDLE; }
        if (actionPrevPreset_ != XR_NULL_HANDLE) { xrDestroyAction(actionPrevPreset_); actionPrevPreset_ = XR_NULL_HANDLE; }
        if (actionTogglePlay_ != XR_NULL_HANDLE) { xrDestroyAction(actionTogglePlay_); actionTogglePlay_ = XR_NULL_HANDLE; }
        if (actionNextTrack_ != XR_NULL_HANDLE) { xrDestroyAction(actionNextTrack_); actionNextTrack_ = XR_NULL_HANDLE; }
        if (actionPrevTrack_ != XR_NULL_HANDLE) { xrDestroyAction(actionPrevTrack_); actionPrevTrack_ = XR_NULL_HANDLE; }
        if (actionCycleAudioInput_ != XR_NULL_HANDLE) { xrDestroyAction(actionCycleAudioInput_); actionCycleAudioInput_ = XR_NULL_HANDLE; }
        if (actionToggleProjection_ != XR_NULL_HANDLE) { xrDestroyAction(actionToggleProjection_); actionToggleProjection_ = XR_NULL_HANDLE; }
        if (actionOptionalPack_ != XR_NULL_HANDLE) { xrDestroyAction(actionOptionalPack_); actionOptionalPack_ = XR_NULL_HANDLE; }
        if (actionAimPose_ != XR_NULL_HANDLE) { xrDestroyAction(actionAimPose_); actionAimPose_ = XR_NULL_HANDLE; }
        if (actionSet_ != XR_NULL_HANDLE) {
            xrDestroyActionSet(actionSet_);
            actionSet_ = XR_NULL_HANDLE;
        }
        leftHandPath_ = XR_NULL_PATH;
        rightHandPath_ = XR_NULL_PATH;
        controllerPlusProfilePath_ = XR_NULL_PATH;
        controllerProProfilePath_ = XR_NULL_PATH;
        controllerTouchProfilePath_ = XR_NULL_PATH;
        handInteractionProfilePath_ = XR_NULL_PATH;
        handTrackingExtensionEnabled_ = false;
        handTrackingReady_ = false;
        xrCreateHandTrackerEXT_ = nullptr;
        xrDestroyHandTrackerEXT_ = nullptr;
        xrLocateHandJointsEXT_ = nullptr;
        ResetHudPointerAndTouchState();
        hudHandTrackingActive_ = false;
        ResetHandModeDebounce();
        ClearHandJointRenderState();

        if (xrInstance_ != XR_NULL_HANDLE) {
            xrDestroyInstance(xrInstance_);
            xrInstance_ = XR_NULL_HANDLE;
        }

        if (eglDisplay_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (eglContext_ != EGL_NO_CONTEXT) {
                eglDestroyContext(eglDisplay_, eglContext_);
            }
            if (eglSurface_ != EGL_NO_SURFACE) {
                eglDestroySurface(eglDisplay_, eglSurface_);
            }
            eglTerminate(eglDisplay_);
        }

        eglDisplay_ = EGL_NO_DISPLAY;
        eglContext_ = EGL_NO_CONTEXT;
        eglSurface_ = EGL_NO_SURFACE;
        eglConfig_ = nullptr;
    }

private:
    android_app* app_{nullptr};

    bool resumed_{false};
    bool hasWindow_{false};
    bool sessionRunning_{false};
    bool exitRenderLoop_{false};

    EGLDisplay eglDisplay_{EGL_NO_DISPLAY};
    EGLConfig eglConfig_{nullptr};
    EGLContext eglContext_{EGL_NO_CONTEXT};
    EGLSurface eglSurface_{EGL_NO_SURFACE};

    XrInstance xrInstance_{XR_NULL_HANDLE};
    XrSystemId xrSystemId_{XR_NULL_SYSTEM_ID};
    XrSession xrSession_{XR_NULL_HANDLE};
    XrSpace xrAppSpace_{XR_NULL_HANDLE};
    XrSessionState xrSessionState_{XR_SESSION_STATE_UNKNOWN};
    XrPath leftHandPath_{XR_NULL_PATH};
    XrPath rightHandPath_{XR_NULL_PATH};
    XrPath controllerPlusProfilePath_{XR_NULL_PATH};
    XrPath controllerProProfilePath_{XR_NULL_PATH};
    XrPath controllerTouchProfilePath_{XR_NULL_PATH};
    XrPath handInteractionProfilePath_{XR_NULL_PATH};
    bool handTrackingExtensionEnabled_{false};
    bool handTrackingReady_{false};
    PFN_xrCreateHandTrackerEXT xrCreateHandTrackerEXT_{nullptr};
    PFN_xrDestroyHandTrackerEXT xrDestroyHandTrackerEXT_{nullptr};
    PFN_xrLocateHandJointsEXT xrLocateHandJointsEXT_{nullptr};
    XrActionSet actionSet_{XR_NULL_HANDLE};
    XrAction actionNextPreset_{XR_NULL_HANDLE};
    XrAction actionPrevPreset_{XR_NULL_HANDLE};
    XrAction actionTogglePlay_{XR_NULL_HANDLE};
    XrAction actionNextTrack_{XR_NULL_HANDLE};
    XrAction actionPrevTrack_{XR_NULL_HANDLE};
    XrAction actionCycleAudioInput_{XR_NULL_HANDLE};
    XrAction actionToggleProjection_{XR_NULL_HANDLE};
    XrAction actionOptionalPack_{XR_NULL_HANDLE};
    XrAction actionAimPose_{XR_NULL_HANDLE};
    XrSpace leftAimSpace_{XR_NULL_HANDLE};
    XrSpace rightAimSpace_{XR_NULL_HANDLE};
    XrHandTrackerEXT leftHandTracker_{XR_NULL_HANDLE};
    XrHandTrackerEXT rightHandTracker_{XR_NULL_HANDLE};

    std::vector<XrViewConfigurationView> viewConfigs_;
    std::vector<XrView> xrViews_;
    std::vector<XrSwapchainBundle> swapchains_;

    GLuint swapchainFramebuffer_{0};

    GLuint sceneProgram_{0};
    GLint uViewProjectionLoc_{-1};
    GLint uTextureLoc_{-1};
    GLint uProjectionModeLoc_{-1};
    GLuint handProgram_{0};
    GLuint handVao_{0};
    GLuint handVbo_{0};
    GLint handViewProjectionLoc_{-1};
    GLint handColorLoc_{-1};
    GLint handPointSizeLoc_{-1};
    GLuint hudProgram_{0};
    GLuint hudVao_{0};
    GLuint hudVbo_{0};
    GLint hudMvpLoc_{-1};
    GLint hudFlashALoc_{-1};
    GLint hudFlashBLoc_{-1};
    GLint hudFlashXLoc_{-1};
    GLint hudFlashYLoc_{-1};
    GLint hudFlashRtLoc_{-1};
    GLint hudFlashLtLoc_{-1};
    GLint hudFlashMenuLoc_{-1};
    GLint hudPointerLeftLoc_{-1};
    GLint hudPointerRightLoc_{-1};
    GLint hudTextSamplerLoc_{-1};
    GLuint hudTextTexture_{0};
    std::vector<uint8_t> hudTextPixels_;

    GLuint sphereVao_{0};
    GLuint sphereVbo_{0};
    GLuint sphereIbo_{0};
    GLsizei sphereIndexCount_{0};

    projectm_handle projectM_{nullptr};
    GLuint projectMTexture_{0};
    GLuint projectMFbo_{0};

    std::vector<std::string> presetFiles_;
    size_t currentPresetIndex_{0};
    std::string presetDirectory_;
    bool usingFallbackPreset_{false};
    AudioMode currentAudioMode_{AudioMode::Synthetic};
    bool currentMediaPlaying_{false};
    std::string currentMediaLabel_{"none"};
    std::string currentPresetLabel_{"FALLBACK"};
    bool hudTextDirty_{true};
    std::string hudRenderedAudioLabel_;
    std::string hudRenderedProjectionLabel_;
    std::string hudRenderedPlaybackLabel_;
    std::string hudRenderedPresetLabel_;
    std::string hudRenderedTrackLabel_;
    std::string hudInputFeedbackLabel_{"READY"};
    std::string hudRenderedInputFeedbackLabel_;
    bool hudEnabled_{true};
    float hudDistance_{kHudDistance};
    float hudVerticalOffset_{kHudVerticalOffset};
    float hudWidth_{kHudWidth};
    float hudHeight_{kHudHeight};

    ProjectionMode projectionMode_{ProjectionMode::FullSphere};
    float hudFlashA_{0.0f};
    float hudFlashB_{0.0f};
    float hudFlashX_{0.0f};
    float hudFlashY_{0.0f};
    float hudFlashRt_{0.0f};
    float hudFlashLt_{0.0f};
    float hudFlashMenu_{0.0f};
    bool rightTriggerPressed_{false};
    bool leftTriggerPressed_{false};
    bool hudPointerLeftVisible_{false};
    bool hudPointerRightVisible_{false};
    HudPointerMode hudPointerLeftMode_{HudPointerMode::None};
    HudPointerMode hudPointerRightMode_{HudPointerMode::None};
    bool hudTouchLeftActive_{false};
    bool hudTouchRightActive_{false};
    bool hudTouchLeftWasActive_{false};
    bool hudTouchRightWasActive_{false};
    bool hudTouchLeftLatched_{false};
    bool hudTouchRightLatched_{false};
    bool hudHandTrackingActive_{false};
    HandModeDebounceState leftHandModeDebounce_{};
    HandModeDebounceState rightHandModeDebounce_{};
    HandJointRenderState leftHandJointRender_{};
    HandJointRenderState rightHandJointRender_{};
    glm::vec2 hudPointerLeftUv_{0.5f, 0.5f};
    glm::vec2 hudPointerRightUv_{0.5f, 0.5f};
    double hudVisibleUntilSeconds_{kHudVisibleOnStartSeconds};
    double hudInputFeedbackUntilSeconds_{0.0};

    float audioCarrierPhase_{0.0f};
    float audioBeatPhase_{0.0f};
    int meshWidth_{kDefaultMeshWidth};
    int meshHeight_{kDefaultMeshHeight};
    bool perfAutoSkipEnabled_{true};
    bool skipMarkedPresets_{true};
    float perfAutoSkipMinFps_{kDefaultPerfAutoSkipMinFps};
    double perfAutoSkipHoldSeconds_{kDefaultPerfAutoSkipHoldSeconds};
    double perfAutoSkipCooldownSeconds_{kDefaultPerfAutoSkipCooldownSeconds};
    double smoothedFrameSeconds_{1.0 / 72.0};
    double lowFpsSinceSeconds_{-1.0};
    double lastAutoSkipSeconds_{-1000.0};
    double lastRuntimePropertyPollSeconds_{-1000.0};
    bool clearMarkedLatch_{false};
    std::vector<std::string> slowPresets_;
    std::string slowPresetFilePath_;

    std::chrono::steady_clock::time_point startTime_{};
    double lastFrameSeconds_{0.0};
    double lastPresetSwitchSeconds_{0.0};
    double lastPresetScanSeconds_{0.0};
    double lastExternalAudioSeconds_{-1000.0};
};

} // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_projectm_questxr_QuestNativeActivity_nativePushAudioPcm(
    JNIEnv* env, jclass /*clazz*/, jfloatArray interleavedStereoSamples, jint frameCount) {
    if (env == nullptr || interleavedStereoSamples == nullptr || frameCount <= 0) {
        return;
    }

    const jsize sampleCount = env->GetArrayLength(interleavedStereoSamples);
    if (sampleCount < 2) {
        return;
    }

    const size_t maxFramesFromArray = static_cast<size_t>(sampleCount / 2);
    const size_t requestedFrames = static_cast<size_t>(frameCount);
    const size_t framesToCopy = std::min(maxFramesFromArray, requestedFrames);
    if (framesToCopy == 0) {
        return;
    }

    std::vector<float> samples(framesToCopy * 2);
    env->GetFloatArrayRegion(interleavedStereoSamples, 0, static_cast<jsize>(samples.size()), samples.data());
    EnqueueAudioFrames(samples.data(), framesToCopy);
}

extern "C" JNIEXPORT void JNICALL
Java_com_projectm_questxr_QuestNativeActivity_nativeUpdateUiState(
    JNIEnv* env, jclass /*clazz*/, jint audioMode, jboolean mediaPlaying, jstring mediaLabel) {
    std::lock_guard<std::mutex> lock(g_uiMutex);
    if (audioMode <= 0) {
        g_audioMode = AudioMode::Synthetic;
    } else if (audioMode == 1) {
        g_audioMode = AudioMode::GlobalCapture;
    } else if (audioMode == 2) {
        g_audioMode = AudioMode::MediaFallback;
    } else {
        g_audioMode = AudioMode::Microphone;
    }

    g_mediaPlaying = mediaPlaying == JNI_TRUE;
    g_mediaLabel = "none";
    if (env != nullptr && mediaLabel != nullptr) {
        const char* utf = env->GetStringUTFChars(mediaLabel, nullptr);
        if (utf != nullptr) {
            g_mediaLabel = utf;
            env->ReleaseStringUTFChars(mediaLabel, utf);
        }
    }
}

void android_main(android_app* app) {
    app_dummy();

    QuestVisualizerApp visualizer(app);
    visualizer.Run();
}
