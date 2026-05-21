cmake_minimum_required(VERSION 3.16)

project(FERenderSimpleViewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
find_package(FERender REQUIRED CONFIG)

add_executable(simple_viewer
    main.cpp
)

target_link_libraries(simple_viewer PRIVATE
    FERender::FERender
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
)

if(APPLE)
    set(_ferender_location "")
    foreach(_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        get_target_property(_candidate FERender::FERender "IMPORTED_LOCATION_${_config}")
        if(_candidate)
            set(_ferender_location "${_candidate}")
            break()
        endif()
    endforeach()

    if(NOT _ferender_location)
        get_target_property(_ferender_location FERender::FERender IMPORTED_LOCATION)
    endif()

    if(_ferender_location)
        get_filename_component(_ferender_lib_dir "${_ferender_location}" DIRECTORY)
        set_target_properties(simple_viewer PROPERTIES
            BUILD_RPATH "${_ferender_lib_dir}"
        )
    endif()
endif()
