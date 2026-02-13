# Install script for directory: /home/here/Documents/swirl/src/playlist

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

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xRuntimex" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libprojectM-4-playlistd.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libprojectM-4-playlistd.so")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libprojectM-4-playlistd.so"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE SHARED_LIBRARY FILES "/home/here/Documents/swirl/apps/quest-openxr-android/app/build/intermediates/cxx/Debug/as2w1336/obj/arm64-v8a/libprojectM-4-playlistd.so")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libprojectM-4-playlistd.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libprojectM-4-playlistd.so")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/home/here/Android/Sdk/ndk/27.0.12077973/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libprojectM-4-playlistd.so")
    endif()
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xRuntimex" OR NOT CMAKE_INSTALL_COMPONENT)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xDevelx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/projectM-4" TYPE FILE FILES
    "/home/here/Documents/swirl/apps/quest-openxr-android/app/.cxx/Debug/as2w1336/arm64-v8a/src/playlist/include/projectM-4/projectM_playlist_export.h"
    "/home/here/Documents/swirl/src/playlist/api/projectM-4/playlist.h"
    "/home/here/Documents/swirl/src/playlist/api/projectM-4/playlist_callbacks.h"
    "/home/here/Documents/swirl/src/playlist/api/projectM-4/playlist_core.h"
    "/home/here/Documents/swirl/src/playlist/api/projectM-4/playlist_filter.h"
    "/home/here/Documents/swirl/src/playlist/api/projectM-4/playlist_items.h"
    "/home/here/Documents/swirl/src/playlist/api/projectM-4/playlist_memory.h"
    "/home/here/Documents/swirl/src/playlist/api/projectM-4/playlist_playback.h"
    "/home/here/Documents/swirl/src/playlist/api/projectM-4/playlist_types.h"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xDevelx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/projectM4Playlist" TYPE FILE FILES
    "/home/here/Documents/swirl/apps/quest-openxr-android/app/.cxx/Debug/as2w1336/arm64-v8a/src/playlist/libprojectMPlaylist/projectM4PlaylistConfigVersion.cmake"
    "/home/here/Documents/swirl/apps/quest-openxr-android/app/.cxx/Debug/as2w1336/arm64-v8a/src/playlist/libprojectMPlaylist/projectM4PlaylistConfig.cmake"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xDevelx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/projectM4Playlist/projectM4PlaylistTargets.cmake")
    file(DIFFERENT EXPORT_FILE_CHANGED FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/projectM4Playlist/projectM4PlaylistTargets.cmake"
         "/home/here/Documents/swirl/apps/quest-openxr-android/app/.cxx/Debug/as2w1336/arm64-v8a/src/playlist/CMakeFiles/Export/lib/cmake/projectM4Playlist/projectM4PlaylistTargets.cmake")
    if(EXPORT_FILE_CHANGED)
      file(GLOB OLD_CONFIG_FILES "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/projectM4Playlist/projectM4PlaylistTargets-*.cmake")
      if(OLD_CONFIG_FILES)
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/projectM4Playlist/projectM4PlaylistTargets.cmake\" will be replaced.  Removing files [${OLD_CONFIG_FILES}].")
        file(REMOVE ${OLD_CONFIG_FILES})
      endif()
    endif()
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/projectM4Playlist" TYPE FILE FILES "/home/here/Documents/swirl/apps/quest-openxr-android/app/.cxx/Debug/as2w1336/arm64-v8a/src/playlist/CMakeFiles/Export/lib/cmake/projectM4Playlist/projectM4PlaylistTargets.cmake")
  if("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/projectM4Playlist" TYPE FILE FILES "/home/here/Documents/swirl/apps/quest-openxr-android/app/.cxx/Debug/as2w1336/arm64-v8a/src/playlist/CMakeFiles/Export/lib/cmake/projectM4Playlist/projectM4PlaylistTargets-debug.cmake")
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xDevelx" OR NOT CMAKE_INSTALL_COMPONENT)
  if("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee]|[Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo]|[Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "/home/here/Documents/swirl/apps/quest-openxr-android/app/.cxx/Debug/as2w1336/arm64-v8a/src/playlist/projectM-4-playlist.pc")
  endif("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee]|[Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo]|[Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xDevelx" OR NOT CMAKE_INSTALL_COMPONENT)
  if("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "/home/here/Documents/swirl/apps/quest-openxr-android/app/.cxx/Debug/as2w1336/arm64-v8a/src/playlist/projectM-4-playlist-debug.pc")
  endif("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
endif()

