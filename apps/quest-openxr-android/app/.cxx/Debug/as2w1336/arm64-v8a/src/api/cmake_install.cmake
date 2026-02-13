# Install script for directory: /home/here/Documents/swirl/src/api

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "0")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/home/here/Android/Sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objdump")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xDevelx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/projectM-4" TYPE FILE FILES
    "/home/here/Documents/swirl/apps/quest-openxr-android/app/.cxx/Debug/as2w1336/arm64-v8a/src/api/include/projectM-4/projectM_export.h"
    "/home/here/Documents/swirl/apps/quest-openxr-android/app/.cxx/Debug/as2w1336/arm64-v8a/src/api/include/projectM-4/version.h"
    "/home/here/Documents/swirl/src/api/include/projectM-4/audio.h"
    "/home/here/Documents/swirl/src/api/include/projectM-4/callbacks.h"
    "/home/here/Documents/swirl/src/api/include/projectM-4/core.h"
    "/home/here/Documents/swirl/src/api/include/projectM-4/debug.h"
    "/home/here/Documents/swirl/src/api/include/projectM-4/logging.h"
    "/home/here/Documents/swirl/src/api/include/projectM-4/memory.h"
    "/home/here/Documents/swirl/src/api/include/projectM-4/parameters.h"
    "/home/here/Documents/swirl/src/api/include/projectM-4/projectM.h"
    "/home/here/Documents/swirl/src/api/include/projectM-4/render_opengl.h"
    "/home/here/Documents/swirl/src/api/include/projectM-4/touch.h"
    "/home/here/Documents/swirl/src/api/include/projectM-4/types.h"
    "/home/here/Documents/swirl/src/api/include/projectM-4/user_sprites.h"
    )
endif()

