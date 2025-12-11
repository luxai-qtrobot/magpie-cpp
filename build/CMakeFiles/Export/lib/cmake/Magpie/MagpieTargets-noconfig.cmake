#----------------------------------------------------------------
# Generated CMake target import file.
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "magpie::magpie_core" for configuration ""
set_property(TARGET magpie::magpie_core APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(magpie::magpie_core PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "CXX"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libmagpie_core.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS magpie::magpie_core )
list(APPEND _IMPORT_CHECK_FILES_FOR_magpie::magpie_core "${_IMPORT_PREFIX}/lib/libmagpie_core.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
