cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

project(projectm_quest_openxr_native LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

set(_openxr_roots "")
if(DEFINED OPENXR_SDK AND NOT OPENXR_SDK STREQUAL "")
    list(APPEND _openxr_roots "${OPENXR_SDK}")
endif()
if(DEFINED ENV{OPENXR_SDK} AND NOT "$ENV{OPENXR_SDK}" STREQUAL "")
    list(APPEND _openxr_roots "$ENV{OPENXR_SDK}")
endif()

set(_openxr_package_dirs "")
foreach(_openxr_root IN LISTS _openxr_roots)
    list(APPEND _openxr_package_dirs
            "${_openxr_root}"
            "${_openxr_root}/lib/cmake/OpenXR"
            "${_openxr_root}/lib/cmake/openxr"
            "${_openxr_root}/build/native"
    )
endforeach()

list(REMOVE_DUPLICATES _openxr_package_dirs)
foreach(_openxr_dir IN LISTS _openxr_package_dirs)
    if(EXISTS "${_openxr_dir}")
        list(APPEND CMAKE_PREFIX_PATH "${_openxr_dir}")
        if((NOT DEFINED OpenXR_DIR OR OpenXR_DIR STREQUAL "" OR OpenXR_DIR STREQUAL "OpenXR_DIR-NOTFOUND")
                AND (EXISTS "${_openxr_dir}/OpenXRConfig.cmake" OR EXISTS "${_openxr_dir}/openxr-config.cmake"))
            set(OpenXR_DIR "${_openxr_dir}")
        endif()
    endif()
endforeach()

find_package(OpenXR REQUIRED CONFIG)

set(_projectm4_roots "")
if(DEFINED PROJECTM4_SDK AND NOT PROJECTM4_SDK STREQUAL "")
    list(APPEND _projectm4_roots "${PROJECTM4_SDK}")
endif()
if(DEFINED ENV{PROJECTM4_SDK} AND NOT "$ENV{PROJECTM4_SDK}" STREQUAL "")
    list(APPEND _projectm4_roots "$ENV{PROJECTM4_SDK}")
endif()
foreach(_projectm4_root IN LISTS _projectm4_roots)
    if(EXISTS "${_projectm4_root}")
        list(APPEND CMAKE_PREFIX_PATH "${_projectm4_root}")
    endif()
endforeach()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PROJECTM4 QUIET projectM-4)
endif()

set(PROJECTM4_INCLUDE_DIR "")
foreach(_projectm4_root IN LISTS _projectm4_roots)
    if(EXISTS "${_projectm4_root}/include/projectM-4/projectM.h")
        set(PROJECTM4_INCLUDE_DIR "${_projectm4_root}/include")
        break()
    endif()
endforeach()

if(NOT PROJECTM4_INCLUDE_DIR)
    find_path(PROJECTM4_INCLUDE_DIR
            NAMES projectM-4/projectM.h
            HINTS ${_projectm4_roots}
            PATH_SUFFIXES include)
endif()

if(NOT PROJECTM4_INCLUDE_DIR AND PROJECTM4_INCLUDEDIR)
    find_path(PROJECTM4_INCLUDE_DIR
            NAMES projectM-4/projectM.h
            HINTS "${PROJECTM4_INCLUDEDIR}")
endif()

if(NOT PROJECTM4_INCLUDE_DIR)
    message(FATAL_ERROR "Could not find projectM-4 headers. Set PROJECTM4_SDK to a prefix containing include/projectM-4/projectM.h.")
endif()

set(PROJECTM4_LIBRARY "")
foreach(_projectm4_root IN LISTS _projectm4_roots)
    foreach(_projectm4_lib_candidate
            "${_projectm4_root}/lib/arm64-v8a/libprojectM-4.so"
            "${_projectm4_root}/lib/libprojectM-4.so"
            "${_projectm4_root}/lib64/libprojectM-4.so"
            "${_projectm4_root}/lib/arm64-v8a/libprojectM-4.so.4"
            "${_projectm4_root}/lib/libprojectM-4.so.4"
            "${_projectm4_root}/lib64/libprojectM-4.so.4")
        if(EXISTS "${_projectm4_lib_candidate}")
            set(PROJECTM4_LIBRARY "${_projectm4_lib_candidate}")
            break()
        endif()
    endforeach()
    if(PROJECTM4_LIBRARY)
        break()
    endif()
endforeach()

if(NOT PROJECTM4_LIBRARY)
    find_library(PROJECTM4_LIBRARY
            NAMES projectM-4 libprojectM-4
            HINTS ${_projectm4_roots}
            PATH_SUFFIXES lib lib64 lib/arm64-v8a)
endif()

if(NOT PROJECTM4_LIBRARY AND PROJECTM4_LIBRARY_DIRS)
    find_library(PROJECTM4_LIBRARY
            NAMES projectM-4 libprojectM-4
            HINTS ${PROJECTM4_LIBRARY_DIRS})
endif()

if(NOT PROJECTM4_LIBRARY)
    message(FATAL_ERROR "Could not find libprojectM-4 shared library. Set PROJECTM4_SDK to a prefix containing libprojectM-4.so.")
endif()

add_library(projectM4_external SHARED IMPORTED GLOBAL)
set_target_properties(projectM4_external PROPERTIES
        IMPORTED_LOCATION "${PROJECTM4_LIBRARY}")

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

target_include_directories(projectm_quest_openxr
        PRIVATE
        "${PROJECTM4_INCLUDE_DIR}"
        )

target_link_libraries(projectm_quest_openxr
        PRIVATE
        projectM4_external
        ${ANDROID_LIBRARY}
        ${LOG_LIBRARY}
        ${EGL_LIBRARY}
        ${GLESV3_LIBRARY}
        )

if(_openxr_target)
    target_link_libraries(projectm_quest_openxr PRIVATE "${_openxr_target}")
else()
    if(NOT OPENXR_LIBRARIES)
        message(FATAL_ERROR "OpenXR found, but no OpenXR library target or OPENXR_LIBRARIES variable is available.")
    endif()
    target_link_libraries(projectm_quest_openxr PRIVATE ${OPENXR_LIBRARIES})

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
endif()

target_include_directories(projectm_quest_openxr
        PRIVATE
        "${_native_app_glue_include}"
        "${CMAKE_CURRENT_LIST_DIR}/third_party"
        )

target_compile_definitions(projectm_quest_openxr
        PRIVATE
        HAVE_PROJECTM=1
        XR_USE_PLATFORM_ANDROID
        XR_USE_GRAPHICS_API_OPENGL_ES
        )

set_target_properties(projectm_quest_openxr PROPERTIES
        OUTPUT_NAME "projectm_quest_openxr"
        DEBUG_POSTFIX ""
        )
