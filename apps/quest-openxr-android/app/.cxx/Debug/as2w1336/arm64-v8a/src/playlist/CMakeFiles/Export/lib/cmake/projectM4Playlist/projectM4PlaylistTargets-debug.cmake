#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "libprojectM::playlist" for configuration "Debug"
set_property(TARGET libprojectM::playlist APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(libprojectM::playlist PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libprojectM-4-playlistd.so"
  IMPORTED_SONAME_DEBUG "libprojectM-4-playlistd.so"
  )

list(APPEND _IMPORT_CHECK_TARGETS libprojectM::playlist )
list(APPEND _IMPORT_CHECK_FILES_FOR_libprojectM::playlist "${_IMPORT_PREFIX}/lib/libprojectM-4-playlistd.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
