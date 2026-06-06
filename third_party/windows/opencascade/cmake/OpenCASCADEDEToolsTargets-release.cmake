#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "TKExpress" for configuration "Release"
set_property(TARGET TKExpress APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(TKExpress PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/win64/vc14/lib/TKExpress.lib"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/win64/vc14/bin/TKExpress.dll"
  )

list(APPEND _cmake_import_check_targets TKExpress )
list(APPEND _cmake_import_check_files_for_TKExpress "${_IMPORT_PREFIX}/win64/vc14/lib/TKExpress.lib" "${_IMPORT_PREFIX}/win64/vc14/bin/TKExpress.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
