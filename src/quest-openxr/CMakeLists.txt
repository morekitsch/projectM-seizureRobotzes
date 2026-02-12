if(NOT CMAKE_SYSTEM_NAME STREQUAL Android)
    message(FATAL_ERROR "ENABLE_QUEST_VR_APP is only supported when targeting Android (Quest).")
endif()

if(DEFINED ENV{OPENXR_SDK})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{OPENXR_SDK}")
endif()

find_package(OpenXR REQUIRED)

set(_ndk_root "${CMAKE_ANDROID_NDK}")
if(NOT _ndk_root)
    set(_ndk_root "$ENV{ANDROID_NDK_ROOT}")
endif()
if(NOT _ndk_root)
    set(_ndk_root "$ENV{ANDROID_NDK_HOME}")
endif()
if(NOT _ndk_root)
    message(FATAL_ERROR "Could not locate Android NDK path for android_native_app_glue.")
endif()

set(_native_app_glue_source "${_ndk_root}/sources/android/native_app_glue/android_native_app_glue.c")
set(_native_app_glue_include "${_ndk_root}/sources/android/native_app_glue")
if(NOT EXISTS "${_native_app_glue_source}")
    message(FATAL_ERROR "android_native_app_glue.c was not found at ${_native_app_glue_source}")
endif()

add_library(projectm_quest_openxr SHARED
        main.cpp
        "${_native_app_glue_source}"
        )

set(_openxr_target "")
if(TARGET OpenXR::openxr_loader)
    set(_openxr_target OpenXR::openxr_loader)
elseif(TARGET OpenXR::openxr)
    set(_openxr_target OpenXR::openxr)
endif()

find_library(ANDROID_LIBRARY android REQUIRED)
find_library(LOG_LIBRARY log REQUIRED)
find_library(EGL_LIBRARY EGL REQUIRED)
find_library(GLESV3_LIBRARY GLESv3 REQUIRED)

if(_openxr_target)
    target_link_libraries(projectm_quest_openxr
            PRIVATE
            libprojectM::projectM
            "${_openxr_target}"
            ${ANDROID_LIBRARY}
            ${LOG_LIBRARY}
            ${EGL_LIBRARY}
            ${GLESV3_LIBRARY}
            )
else()
    if(NOT OPENXR_LIBRARIES)
        message(FATAL_ERROR "OpenXR found, but no OpenXR library target or OPENXR_LIBRARIES variable is available.")
    endif()

    set(_openxr_include_dirs "")
    if(DEFINED OPENXR_INCLUDE_DIRS)
        list(APPEND _openxr_include_dirs ${OPENXR_INCLUDE_DIRS})
    endif()
    if(DEFINED OPENXR_INCLUDE_DIR)
        list(APPEND _openxr_include_dirs ${OPENXR_INCLUDE_DIR})
    endif()
    if(DEFINED OpenXR_INCLUDE_DIRS)
        list(APPEND _openxr_include_dirs ${OpenXR_INCLUDE_DIRS})
    endif()
    if(DEFINED OpenXR_INCLUDE_DIR)
        list(APPEND _openxr_include_dirs ${OpenXR_INCLUDE_DIR})
    endif()
    if(_openxr_include_dirs)
        target_include_directories(projectm_quest_openxr PRIVATE ${_openxr_include_dirs})
    endif()

    target_link_libraries(projectm_quest_openxr
            PRIVATE
            libprojectM::projectM
            ${OPENXR_LIBRARIES}
            ${ANDROID_LIBRARY}
            ${LOG_LIBRARY}
            ${EGL_LIBRARY}
            ${GLESV3_LIBRARY}
            )
endif()

target_include_directories(projectm_quest_openxr
        PRIVATE
        "${_native_app_glue_include}"
        "${PROJECTM_SOURCE_DIR}/src/api/include"
        "${PROJECTM_SOURCE_DIR}/vendor"
        )

target_compile_features(projectm_quest_openxr PRIVATE cxx_std_14)
target_compile_definitions(projectm_quest_openxr
        PRIVATE
        XR_USE_PLATFORM_ANDROID
        XR_USE_GRAPHICS_API_OPENGL_ES
        )

set_target_properties(projectm_quest_openxr PROPERTIES
        OUTPUT_NAME "projectm_quest_openxr"
        FOLDER "apps"
        )
