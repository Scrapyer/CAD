if(NOT DEFINED INSTALLED_HEADERS)
    message(FATAL_ERROR "INSTALLED_HEADERS is not defined")
endif()

set(required_headers
    Theme.h
)

foreach(header IN LISTS required_headers)
    set(header_index -1)
    foreach(installed_header IN LISTS INSTALLED_HEADERS)
        get_filename_component(installed_header_name "${installed_header}" NAME)
        if(installed_header_name STREQUAL "${header}")
            set(header_index 0)
            break()
        endif()
    endforeach()
    if(header_index EQUAL -1)
        message(FATAL_ERROR "${header} must be installed because it is used by public FERender headers")
    endif()
endforeach()
