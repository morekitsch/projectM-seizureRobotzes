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
constexpr float kHudDistance = 1.15f;
constexpr float kHudVerticalOffset = -0.20f;
constexpr float kHudWidth = 0.62f;
constexpr float kHudHeight = 0.30f;

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

enum class AudioMode : int {
    Synthetic = 0,
    GlobalCapture = 1,
    MediaFallback = 2,
};

std::mutex g_uiMutex;
AudioMode g_audioMode = AudioMode::Synthetic;
bool g_mediaPlaying = false;

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

        while (ALooper_pollAll(sessionRunning_ ? 0 : -1, nullptr, &events,
                               reinterpret_cast<void**>(&source)) >= 0) {
            if (source) {
                source->process(app_, source);
            }

            if (app_->destroyRequested != 0) {
                exitRenderLoop_ = true;
                break;
            }
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

        auto createBooleanAction = [&](XrAction& actionOut, const char* name, const char* localized) -> bool {
            XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
            actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
            std::strncpy(actionInfo.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
            std::strncpy(actionInfo.localizedActionName, localized, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
            actionInfo.countSubactionPaths = 0;
            actionInfo.subactionPaths = nullptr;
            if (XR_FAILED(xrCreateAction(actionSet_, &actionInfo, &actionOut))) {
                LOGE("xrCreateAction failed for %s", name);
                return false;
            }
            return true;
        };

        if (!createBooleanAction(actionNextPreset_, "next_preset", "Next Preset")) return false;
        if (!createBooleanAction(actionPrevPreset_, "prev_preset", "Previous Preset")) return false;
        if (!createBooleanAction(actionTogglePlay_, "toggle_play", "Toggle Play Pause")) return false;
        if (!createBooleanAction(actionNextTrack_, "next_track", "Next Track")) return false;
        if (!createBooleanAction(actionPrevTrack_, "prev_track", "Previous Track")) return false;
        if (!createBooleanAction(actionToggleProjection_, "toggle_projection", "Toggle Projection")) return false;
        if (!createBooleanAction(actionOptionalPack_, "optional_pack", "Optional Preset Pack")) return false;

        std::vector<XrActionSuggestedBinding> bindings;

        auto bind = [&](XrAction action, const char* path) {
            XrPath xrPath = XR_NULL_PATH;
            if (XR_SUCCEEDED(xrStringToPath(xrInstance_, path, &xrPath))) {
                bindings.push_back({action, xrPath});
            }
        };

        bind(actionNextPreset_, "/user/hand/right/input/a/click");
        bind(actionPrevPreset_, "/user/hand/left/input/x/click");
        bind(actionTogglePlay_, "/user/hand/left/input/y/click");
        bind(actionNextTrack_, "/user/hand/right/input/b/click");
        bind(actionPrevTrack_, "/user/hand/left/input/menu/click");
        bind(actionToggleProjection_, "/user/hand/right/input/trigger/click");
        bind(actionOptionalPack_, "/user/hand/left/input/trigger/click");

        XrPath touchProfilePath = XR_NULL_PATH;
        xrStringToPath(xrInstance_, "/interaction_profiles/oculus/touch_controller", &touchProfilePath);
        if (touchProfilePath != XR_NULL_PATH && !bindings.empty()) {
            XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
            suggested.interactionProfile = touchProfilePath;
            suggested.suggestedBindings = bindings.data();
            suggested.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
            xrSuggestInteractionProfileBindings(xrInstance_, &suggested);
        }

        XrSessionActionSetsAttachInfo attachInfo{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        attachInfo.countActionSets = 1;
        attachInfo.actionSets = &actionSet_;
        if (XR_FAILED(xrAttachSessionActionSets(xrSession_, &attachInfo))) {
            LOGE("xrAttachSessionActionSets failed.");
            return false;
        }

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

        for (const char* required : requiredExtensions) {
            const bool found = std::any_of(extensionProperties.begin(), extensionProperties.end(),
                                           [required](const XrExtensionProperties& ext) {
                                               return std::strcmp(ext.extensionName, required) == 0;
                                           });
            if (!found) {
                LOGE("Required OpenXR extension missing: %s", required);
                return false;
            }
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

                if (uProjectionMode == 1 && dir.z > 0.0) {
                    fragColor = vec4(0.0, 0.0, 0.0, 1.0);
                    return;
                }

                float u = atan(dir.x, -dir.z) / (2.0 * PI) + 0.5;
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

        return InitializeHudOverlay();
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
            uniform vec4 uStatus;
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

            void main() {
                vec3 color = vec3(0.0);
                float alpha = rectMask(vUv, vec2(0.02, 0.04), vec2(0.98, 0.96), 0.004) * 0.62;
                if (alpha <= 0.001) {
                    discard;
                }

                color = vec3(0.08, 0.08, 0.10);

                color = blendRect(color, vUv, vec2(0.07, 0.59), vec2(0.46, 0.84), vec3(0.14, 0.44, 0.87), 0.90 + uFlashX.x);
                color = blendRect(color, vUv, vec2(0.54, 0.59), vec2(0.93, 0.84), vec3(0.93, 0.34, 0.26), 0.90 + uFlashA.x);
                color = blendRect(color, vUv, vec2(0.07, 0.30), vec2(0.46, 0.55), vec3(0.18, 0.74, 0.38), 0.90 + uFlashY.x);
                color = blendRect(color, vUv, vec2(0.54, 0.30), vec2(0.93, 0.55), vec3(0.91, 0.82, 0.28), 0.90 + uFlashB.x);
                color = blendRect(color, vUv, vec2(0.07, 0.08), vec2(0.46, 0.24), vec3(0.58, 0.32, 0.86), 0.88 + uFlashLT.x);
                color = blendRect(color, vUv, vec2(0.54, 0.08), vec2(0.93, 0.24), vec3(0.23, 0.72, 0.85), 0.88 + uFlashRT.x);

                // Audio mode dots: synthetic, global, media
                vec3 inactive = vec3(0.22, 0.22, 0.24);
                vec3 synthColor = vec3(0.95, 0.65, 0.25);
                vec3 globalColor = vec3(0.22, 0.86, 0.48);
                vec3 mediaColor = vec3(0.24, 0.62, 0.92);
                float audioMode = uStatus.x;
                float projMode = uStatus.y;
                float playing = uStatus.z;

                color = blendRect(color, vUv, vec2(0.10, 0.88), vec2(0.17, 0.93), mix(inactive, synthColor, step(audioMode, 0.5)), 1.0);
                color = blendRect(color, vUv, vec2(0.19, 0.88), vec2(0.26, 0.93), mix(inactive, globalColor, step(0.5, audioMode) * (1.0 - step(1.5, audioMode))), 1.0);
                color = blendRect(color, vUv, vec2(0.28, 0.88), vec2(0.35, 0.93), mix(inactive, mediaColor, step(1.5, audioMode)), 1.0);

                // Projection indicator
                vec3 sphereColor = vec3(0.26, 0.72, 0.90);
                vec3 domeColor = vec3(0.76, 0.34, 0.90);
                color = blendRect(color, vUv, vec2(0.56, 0.88), vec2(0.68, 0.93), mix(sphereColor, domeColor, projMode), 1.0);

                // Playback indicator
                vec3 playColor = mix(vec3(0.56, 0.20, 0.20), vec3(0.26, 0.82, 0.38), step(0.5, playing));
                color = blendRect(color, vUv, vec2(0.73, 0.88), vec2(0.89, 0.93), playColor, 1.0);

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
        hudStatusLoc_ = glGetUniformLocation(hudProgram_, "uStatus");

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

        return true;
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
        projectm_set_mesh_size(projectM_, 64, 48);
        projectm_set_fps(projectM_, 72);
        projectm_set_hard_cut_enabled(projectM_, true);
        projectm_set_hard_cut_duration(projectM_, 15.0);
        projectm_set_hard_cut_sensitivity(projectM_, 1.4f);

        const std::string appDataPath(app_->activity->internalDataPath ? app_->activity->internalDataPath : "");
        const std::string presetOutputDir = appDataPath + "/presets";
        const std::string textureOutputDir = appDataPath + "/textures";
        presetDirectory_ = presetOutputDir;

        if (app_->activity->assetManager != nullptr) {
            CopyAssetDirectoryFlat(app_->activity->assetManager, "presets", presetOutputDir);
            CopyAssetDirectoryFlat(app_->activity->assetManager, "textures", textureOutputDir);
        }

        presetFiles_ = CollectPresetFiles(presetOutputDir);
        if (!presetFiles_.empty()) {
            projectm_load_preset_file(projectM_, presetFiles_.front().c_str(), false);
            LOGI("Loaded first preset from assets: %s", presetFiles_.front().c_str());
            usingFallbackPreset_ = false;
        } else {
            projectm_load_preset_data(projectM_, kFallbackPreset, false);
            LOGW("No preset assets found, using built-in fallback preset.");
            usingFallbackPreset_ = true;
        }

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

        if (usingFallbackPreset_) {
            projectm_load_preset_file(projectM_, presetFiles_.front().c_str(), false);
            usingFallbackPreset_ = false;
            currentPresetIndex_ = 0;
        }

        LOGI("Preset list updated (%zu presets).", presetFiles_.size());
    }

    void SwitchPresetRelative(int delta, bool smooth) {
        if (presetFiles_.empty() || projectM_ == nullptr) {
            return;
        }

        const int64_t count = static_cast<int64_t>(presetFiles_.size());
        int64_t next = static_cast<int64_t>(currentPresetIndex_) + delta;
        next %= count;
        if (next < 0) {
            next += count;
        }

        currentPresetIndex_ = static_cast<size_t>(next);
        projectm_load_preset_file(projectM_, presetFiles_[currentPresetIndex_].c_str(), smooth);
        lastPresetSwitchSeconds_ = ElapsedSeconds();
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

    void PollInputActions(double nowSeconds) {
        if (!sessionRunning_ || actionSet_ == XR_NULL_HANDLE) {
            return;
        }

        XrActiveActionSet activeSet{};
        activeSet.actionSet = actionSet_;

        XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
        syncInfo.countActiveActionSets = 1;
        syncInfo.activeActionSets = &activeSet;
        if (XR_FAILED(xrSyncActions(xrSession_, &syncInfo))) {
            return;
        }

        if (GetActionPressed(actionNextPreset_)) {
            SwitchPresetRelative(+1, true);
            hudFlashA_ = 1.0f;
        }
        if (GetActionPressed(actionPrevPreset_)) {
            SwitchPresetRelative(-1, true);
            hudFlashX_ = 1.0f;
        }
        if (GetActionPressed(actionTogglePlay_)) {
            CallJavaControlMethod("onNativeTogglePlayback");
            hudFlashY_ = 1.0f;
        }
        if (GetActionPressed(actionNextTrack_)) {
            CallJavaControlMethod("onNativeNextTrack");
            hudFlashB_ = 1.0f;
        }
        if (GetActionPressed(actionPrevTrack_)) {
            CallJavaControlMethod("onNativePreviousTrack");
            hudFlashX_ = std::max(hudFlashX_, 0.6f);
        }
        if (GetActionPressed(actionToggleProjection_)) {
            projectionMode_ = projectionMode_ == ProjectionMode::FullSphere
                ? ProjectionMode::FrontDome
                : ProjectionMode::FullSphere;
            hudFlashRt_ = 1.0f;
        }
        if (GetActionPressed(actionOptionalPack_)) {
            CallJavaControlMethod("onNativeRequestOptionalCreamPack");
            lastPresetScanSeconds_ = nowSeconds - kPresetScanIntervalSeconds;
            hudFlashLt_ = 1.0f;
        }
    }

    void UpdateUiStateFromJava() {
        std::lock_guard<std::mutex> lock(g_uiMutex);
        currentAudioMode_ = g_audioMode;
        currentMediaPlaying_ = g_mediaPlaying;
    }

    void AdvanceHudFlash(float deltaSeconds) {
        const float decay = std::max(deltaSeconds * 2.8f, 0.01f);
        auto decayValue = [decay](float& value) {
            value = std::max(0.0f, value - decay);
        };
        decayValue(hudFlashA_);
        decayValue(hudFlashB_);
        decayValue(hudFlashX_);
        decayValue(hudFlashY_);
        decayValue(hudFlashRt_);
        decayValue(hudFlashLt_);
    }

    void RenderHud(const glm::mat4& projection, const glm::mat4& view, const XrPosef& pose) {
        if (hudProgram_ == 0 || hudVao_ == 0) {
            return;
        }

        const glm::vec3 basePosition(pose.position.x, pose.position.y, pose.position.z);
        const glm::quat baseOrientation(pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z);
        const glm::vec3 panelOffset = baseOrientation * glm::vec3(0.0f, kHudVerticalOffset, -kHudDistance);
        const glm::vec3 panelPosition = basePosition + panelOffset;

        glm::mat4 model = glm::translate(glm::mat4(1.0f), panelPosition) * glm::mat4_cast(baseOrientation);
        model = glm::scale(model, glm::vec3(kHudWidth, kHudHeight, 1.0f));
        const glm::mat4 mvp = projection * view * model;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);

        glUseProgram(hudProgram_);
        glUniformMatrix4fv(hudMvpLoc_, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniform4f(hudFlashALoc_, hudFlashA_, 0.0f, 0.0f, 0.0f);
        glUniform4f(hudFlashBLoc_, hudFlashB_, 0.0f, 0.0f, 0.0f);
        glUniform4f(hudFlashXLoc_, hudFlashX_, 0.0f, 0.0f, 0.0f);
        glUniform4f(hudFlashYLoc_, hudFlashY_, 0.0f, 0.0f, 0.0f);
        glUniform4f(hudFlashRtLoc_, hudFlashRt_, 0.0f, 0.0f, 0.0f);
        glUniform4f(hudFlashLtLoc_, hudFlashLt_, 0.0f, 0.0f, 0.0f);
        glUniform4f(hudStatusLoc_,
                    static_cast<float>(static_cast<int>(currentAudioMode_)),
                    projectionMode_ == ProjectionMode::FrontDome ? 1.0f : 0.0f,
                    currentMediaPlaying_ ? 1.0f : 0.0f,
                    0.0f);

        glBindVertexArray(hudVao_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
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
            currentPresetIndex_ = (currentPresetIndex_ + 1) % presetFiles_.size();
            projectm_load_preset_file(projectM_, presetFiles_[currentPresetIndex_].c_str(), true);
            lastPresetSwitchSeconds_ = nowSeconds;
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

            PollInputActions(nowSeconds);
            UpdateUiStateFromJava();
            AdvanceHudFlash(std::max(deltaSeconds, 0.0f));
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

                    RenderHud(projection, view, xrViews_[viewIndex].pose);
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
        if (actionToggleProjection_ != XR_NULL_HANDLE) { xrDestroyAction(actionToggleProjection_); actionToggleProjection_ = XR_NULL_HANDLE; }
        if (actionOptionalPack_ != XR_NULL_HANDLE) { xrDestroyAction(actionOptionalPack_); actionOptionalPack_ = XR_NULL_HANDLE; }
        if (actionSet_ != XR_NULL_HANDLE) {
            xrDestroyActionSet(actionSet_);
            actionSet_ = XR_NULL_HANDLE;
        }

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
    XrActionSet actionSet_{XR_NULL_HANDLE};
    XrAction actionNextPreset_{XR_NULL_HANDLE};
    XrAction actionPrevPreset_{XR_NULL_HANDLE};
    XrAction actionTogglePlay_{XR_NULL_HANDLE};
    XrAction actionNextTrack_{XR_NULL_HANDLE};
    XrAction actionPrevTrack_{XR_NULL_HANDLE};
    XrAction actionToggleProjection_{XR_NULL_HANDLE};
    XrAction actionOptionalPack_{XR_NULL_HANDLE};

    std::vector<XrViewConfigurationView> viewConfigs_;
    std::vector<XrView> xrViews_;
    std::vector<XrSwapchainBundle> swapchains_;

    GLuint swapchainFramebuffer_{0};

    GLuint sceneProgram_{0};
    GLint uViewProjectionLoc_{-1};
    GLint uTextureLoc_{-1};
    GLint uProjectionModeLoc_{-1};
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
    GLint hudStatusLoc_{-1};

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

    ProjectionMode projectionMode_{ProjectionMode::FullSphere};
    float hudFlashA_{0.0f};
    float hudFlashB_{0.0f};
    float hudFlashX_{0.0f};
    float hudFlashY_{0.0f};
    float hudFlashRt_{0.0f};
    float hudFlashLt_{0.0f};

    float audioCarrierPhase_{0.0f};
    float audioBeatPhase_{0.0f};

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
    } else {
        g_audioMode = AudioMode::MediaFallback;
    }

    g_mediaPlaying = mediaPlaying == JNI_TRUE;
    (void)env;
    (void)mediaLabel;
}

void android_main(android_app* app) {
    app_dummy();

    QuestVisualizerApp visualizer(app);
    visualizer.Run();
}
