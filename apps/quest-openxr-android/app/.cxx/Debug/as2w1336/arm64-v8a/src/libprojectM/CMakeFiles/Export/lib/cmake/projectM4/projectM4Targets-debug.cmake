#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "libprojectM::projectM" for configuration "Debug"
set_property(TARGET libprojectM::projectM APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(libprojectM::projectM PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libprojectM-4d.so"
  IMPORTED_SONAME_DEBUG "libprojectM-4d.so"
  )

list(APPEND _IMPORT_CHECK_TARGETS libprojectM::projectM )
list(APPEND _IMPORT_CHECK_FILES_FOR_libprojectM::projectM "${_IMPORT_PREFIX}/lib/libprojectM-4d.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
