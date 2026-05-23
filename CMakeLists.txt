# ============================================================
# CMake 构建配置文件
# 项目名称：FEModelViewer
# 描述：基于 Qt6.8 + OpenGL 的有限元模型查看器
# ============================================================

cmake_minimum_required(VERSION 3.16)

project(FEModelViewer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS: keep deployment target explicit so third-party dylib compatibility
# checks (e.g. Vulkan loader) are deterministic across generators.
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0" CACHE STRING "Minimum macOS deployment target" FORCE)
endif()

# 启用 Qt 的自动 MOC
set(CMAKE_AUTOMOC ON)

# ── Qt 路径提示（可通过命令行覆盖：-DCMAKE_PREFIX_PATH=...）──
# 优先使用用户传入的 CMAKE_PREFIX_PATH，否则尝试常见安装位置
if(NOT CMAKE_PREFIX_PATH)
    if(WIN32)
        # Windows：根据编译器选择对应的 Qt 套件
        if(MSVC)
            set(_qt_kits
                "C:/Qt/6.8.3/msvc2022_64"
                "C:/Qt/6.8.2/msvc2022_64"
                "C:/Qt/6.8.1/msvc2022_64"
                "C:/Qt/6.8.0/msvc2022_64"
                "C:/Qt/6.8.3/msvc2019_64"
                "C:/Qt/6.8.2/msvc2019_64"
                "C:/Qt/6.8.1/msvc2019_64"
                "C:/Qt/6.8.0/msvc2019_64"
                "D:/Qt/6.8.3/msvc2022_64"
                "D:/Qt/6.8.2/msvc2022_64"
                "D:/Qt/6.8.1/msvc2022_64"
                "D:/Qt/6.8.0/msvc2022_64"
                "D:/Qt/6.8.3/msvc2019_64"
                "D:/Qt/6.8.2/msvc2019_64"
                "D:/Qt/6.8.1/msvc2019_64"
                "D:/Qt/6.8.0/msvc2019_64"
            )
        else()
            set(_qt_kits
                "C:/Qt/6.8.3/mingw_64"
                "C:/Qt/6.8.2/mingw_64"
                "C:/Qt/6.8.1/mingw_64"
                "C:/Qt/6.8.0/mingw_64"
                "D:/Qt/6.8.3/mingw_64"
                "D:/Qt/6.8.2/mingw_64"
                "D:/Qt/6.8.1/mingw_64"
                "D:/Qt/6.8.0/mingw_64"
            )
        endif()
        foreach(_qt_hint IN LISTS _qt_kits)
            if(EXISTS "${_qt_hint}")
                set(CMAKE_PREFIX_PATH "${_qt_hint}")
                break()
            endif()
        endforeach()
    elseif(APPLE)
        # macOS：Homebrew（Apple Silicon 和 Intel）
        foreach(_qt_hint
            "$ENV{HOME}/Qt/6.8.3/macos"
            "$ENV{HOME}/Qt/6.8.2/macos"
            "$ENV{HOME}/Qt/6.8.1/macos"
            "$ENV{HOME}/Qt/6.8.0/macos"
            "/opt/homebrew/opt/qt"
            "/usr/local/opt/qt"
            "/opt/homebrew/opt/qt@6"
            "/usr/local/opt/qt@6"
        )
            if(EXISTS "${_qt_hint}")
                set(CMAKE_PREFIX_PATH "${_qt_hint}")
                break()
            endif()
        endforeach()
    endif()
endif()

# ── 依赖库查找 ──

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
if(NOT APPLE)
    find_package(OpenGL REQUIRED)
endif()

option(FEMODELVIEWER_ENABLE_VULKAN_RHI "Build the optional Vulkan RHI backend when a Vulkan SDK is available" ON)
option(FEMODELVIEWER_ENABLE_METAL_RHI "Build the optional Metal RHI backend on macOS" ON)
option(FEMODELVIEWER_ENABLE_OPENCASCADE_IMPORT "Build optional STEP/IGES CAD import support with OpenCASCADE" ON)
set(FERENDER_VULKAN_SHADER_TARGET_ENV "vulkan1.4" CACHE STRING
    "glslc --target-env value used when compiling FERender Vulkan shaders")
set(FERENDER_HAS_VULKAN_RHI OFF)
set(FERENDER_HAS_VULKAN_TRIANGLE_PIPELINE OFF)
set(FERENDER_HAS_METAL_RHI OFF)
set(FERENDER_HAS_OPENCASCADE_IMPORT OFF)
if(FEMODELVIEWER_ENABLE_VULKAN_RHI)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        set(_ferender_enable_vulkan_rhi ON)
        if(APPLE AND EXISTS "${Vulkan_LIBRARY}")
            execute_process(
                COMMAND /usr/bin/otool -l "${Vulkan_LIBRARY}"
                OUTPUT_VARIABLE _ferender_vulkan_otool
                ERROR_QUIET
            )
            string(REGEX MATCH "minos ([0-9]+\\.[0-9]+)" _ferender_vulkan_minos_match "${_ferender_vulkan_otool}")
            if(CMAKE_MATCH_1)
                set(_ferender_vulkan_loader_minos "${CMAKE_MATCH_1}")
                if(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS _ferender_vulkan_loader_minos)
                    set(_ferender_enable_vulkan_rhi OFF)
                    message(WARNING
                        "Vulkan loader '${Vulkan_LIBRARY}' requires macOS ${_ferender_vulkan_loader_minos}, "
                        "but CMAKE_OSX_DEPLOYMENT_TARGET is ${CMAKE_OSX_DEPLOYMENT_TARGET}. "
                        "Disabling Vulkan RHI to avoid linker warnings and runtime incompatibility. "
                        "Increase CMAKE_OSX_DEPLOYMENT_TARGET or use a Vulkan loader built for an older macOS target."
                    )
                endif()
            endif()
        endif()

        if(_ferender_enable_vulkan_rhi)
            set(FERENDER_HAS_VULKAN_RHI ON)
            message(STATUS "Vulkan RHI enabled: ${Vulkan_LIBRARY}")
        endif()
    else()
        message(STATUS "Vulkan RHI disabled: Vulkan SDK was not found")
    endif()
endif()

if(APPLE AND FEMODELVIEWER_ENABLE_METAL_RHI)
    find_library(METAL_FRAMEWORK Metal)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    find_library(APPKIT_FRAMEWORK AppKit)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore)
    if(METAL_FRAMEWORK AND FOUNDATION_FRAMEWORK AND APPKIT_FRAMEWORK AND QUARTZCORE_FRAMEWORK)
        set(FERENDER_HAS_METAL_RHI ON)
        message(STATUS "Metal RHI enabled: ${METAL_FRAMEWORK}")
    else()
        message(STATUS "Metal RHI disabled: Metal/Foundation/AppKit/QuartzCore framework was not found")
    endif()
elseif(FEMODELVIEWER_ENABLE_METAL_RHI)
    message(STATUS "Metal RHI disabled: Metal is only available on Apple platforms")
endif()

if(FEMODELVIEWER_ENABLE_OPENCASCADE_IMPORT)
    set(_occ_vendor_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencascade/lib/cmake/opencascade")
    if(EXISTS "${_occ_vendor_dir}/OpenCASCADEConfig.cmake")
        set(OpenCASCADE_DIR "${_occ_vendor_dir}" CACHE PATH "OpenCASCADE CMake package directory" FORCE)
    elseif(NOT OpenCASCADE_DIR OR NOT EXISTS "${OpenCASCADE_DIR}/OpenCASCADEConfig.cmake")
        foreach(_occ_hint
            "$ENV{OpenCASCADE_DIR}"
            "/opt/homebrew/opt/opencascade/lib/cmake/opencascade"
            "/usr/local/opt/opencascade/lib/cmake/opencascade"
            "C:/OpenCASCADE/lib/cmake/opencascade"
            "D:/OpenCASCADE/lib/cmake/opencascade"
        )
            if(EXISTS "${_occ_hint}/OpenCASCADEConfig.cmake")
                set(OpenCASCADE_DIR "${_occ_hint}" CACHE PATH "OpenCASCADE CMake package directory" FORCE)
                break()
            endif()
        endforeach()
    endif()
    find_package(OpenCASCADE CONFIG QUIET COMPONENTS
        FoundationClasses
        ModelingData
        ModelingAlgorithms
        DataExchange
    )
    if(OpenCASCADE_FOUND OR (OpenCASCADE_INCLUDE_DIR AND EXISTS "${OpenCASCADE_INCLUDE_DIR}/STEPControl_Reader.hxx"))
        set(FERENDER_HAS_OPENCASCADE_IMPORT ON)
        message(STATUS "OpenCASCADE import enabled: ${OpenCASCADE_VERSION} (${OpenCASCADE_DIR})")
    else()
        message(STATUS "OpenCASCADE import disabled: OpenCASCADE was not found")
    endif()
endif()

# MSVC：源文件以 UTF-8 编码解析，避免中文字符串报语法错误
if(MSVC)
    add_compile_options(/utf-8)
endif()

# macOS：抑制 OpenGL 弃用警告（macOS 10.14+ 将 OpenGL 标为 deprecated）
if(APPLE)
    add_compile_definitions(GL_SILENCE_DEPRECATION)
endif()

# GLM：header-only。优先使用本地 third_party，避免构建时依赖 GitHub 网络。
# 支持两种布局：
#   third_party/glm/glm.hpp        （只放 glm 头目录）
#   third_party/glm/glm/glm.hpp    （完整 GLM 仓库）
set(GLM_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(GLM_LOCAL_REPO_DIR "${GLM_THIRD_PARTY_DIR}/glm")
if(EXISTS "${GLM_THIRD_PARTY_DIR}/glm/glm.hpp")
    set(glm_SOURCE_DIR "${GLM_THIRD_PARTY_DIR}")
elseif(EXISTS "${GLM_LOCAL_REPO_DIR}/glm/glm.hpp")
    set(glm_SOURCE_DIR "${GLM_LOCAL_REPO_DIR}")
else()
    include(FetchContent)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        0.9.9.8
        GIT_SHALLOW    TRUE
    )
    # GLM 0.9.9.8 的 CMakeLists 仍声明了很老的最低版本；
    # 新版 CMake 需要显式允许该兼容策略区间。
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(glm)
endif()

# ── RENDER 模块（可独立安装的共享库）──

set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/source)

set(RENDER_INCLUDE_DIRS
    ${SOURCE_DIR}/app/platform/macos
    ${SOURCE_DIR}/app/window
    ${SOURCE_DIR}/common
    ${SOURCE_DIR}/data
    ${SOURCE_DIR}/convert
    ${SOURCE_DIR}/io
    ${SOURCE_DIR}/post
    ${SOURCE_DIR}/rhi
    ${SOURCE_DIR}/rhi/OpenGL
    ${SOURCE_DIR}/rhi/Metal
    ${SOURCE_DIR}/rhi/Vulkan
    ${SOURCE_DIR}/render
)

# qt6_wrap_cpp 使用目录 include 属性生成 moc 命令；这里同步模块头文件路径。
include_directories(
    ${SOURCE_DIR}/app
    ${SOURCE_DIR}/app/ui
    ${RENDER_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR}
)

set(RENDER_HEADERS
    ${SOURCE_DIR}/render/Camera.h
    ${SOURCE_DIR}/render/Geometry.h
    ${SOURCE_DIR}/render/GLWidget.h
    ${SOURCE_DIR}/app/window/RenderViewport.h
    ${SOURCE_DIR}/render/FERenderData.h
    ${SOURCE_DIR}/render/FEPickResult.h
    ${SOURCE_DIR}/data/FEModel.h
    ${SOURCE_DIR}/data/FEField.h
    ${SOURCE_DIR}/data/FENode.h
    ${SOURCE_DIR}/data/FEElement.h
    ${SOURCE_DIR}/data/FEGroup.h
    ${SOURCE_DIR}/data/FEResultData.h
    ${SOURCE_DIR}/convert/FEMeshConverter.h
    ${SOURCE_DIR}/io/FEParser.h
    ${SOURCE_DIR}/rhi/RenderBackend.h
    ${SOURCE_DIR}/rhi/RenderBackendFactory.h
    ${SOURCE_DIR}/rhi/RenderSettings.h
    ${SOURCE_DIR}/post/FEResultMapper.h
    ${SOURCE_DIR}/post/FEResultRepository.h
    ${SOURCE_DIR}/post/FEDeformation.h
    ${SOURCE_DIR}/post/FEAnimationController.h
    ${SOURCE_DIR}/post/FEProbe.h
    ${SOURCE_DIR}/post/FEPostFilter.h
    ${SOURCE_DIR}/post/FEIsoSurface.h
    ${SOURCE_DIR}/common/Theme.h
)

set(RENDER_SOURCES
    ${SOURCE_DIR}/render/Camera.cpp
    ${SOURCE_DIR}/render/ColorBarOverlay.cpp
    ${SOURCE_DIR}/render/Geometry.cpp
    ${SOURCE_DIR}/render/GLWidget.cpp
    ${SOURCE_DIR}/render/GLWidgetInteraction.cpp
    ${SOURCE_DIR}/render/GLWidgetOverlays.cpp
    ${SOURCE_DIR}/render/GLWidgetPicking.cpp
    ${SOURCE_DIR}/render/GLWidgetRender.cpp
    ${SOURCE_DIR}/render/GLWidgetSelection.cpp
    ${SOURCE_DIR}/render/GLWidgetVisibility.cpp
    ${SOURCE_DIR}/app/window/RenderViewport.cpp
    ${SOURCE_DIR}/rhi/RenderBackendFactory.cpp
    ${SOURCE_DIR}/rhi/RenderSettings.cpp
    ${SOURCE_DIR}/rhi/OpenGL/OpenGLRenderBackend.cpp
    ${SOURCE_DIR}/data/FEModel.cpp
    ${SOURCE_DIR}/data/FEField.cpp
    ${SOURCE_DIR}/convert/FEMeshConverter.cpp
    ${SOURCE_DIR}/io/FEParser_inp.cpp
    ${SOURCE_DIR}/io/FEParser_bdf.cpp
    ${SOURCE_DIR}/io/FEParser_stl.cpp
    ${SOURCE_DIR}/io/FEParser_occ.cpp
    ${SOURCE_DIR}/io/FEParser_op2.cpp
    ${SOURCE_DIR}/io/FEParser_op2results.cpp
    ${SOURCE_DIR}/io/FEParser_unv.cpp
    ${SOURCE_DIR}/post/FEResultMapper.cpp
    ${SOURCE_DIR}/post/FEResultRepository.cpp
    ${SOURCE_DIR}/post/FEDeformation.cpp
    ${SOURCE_DIR}/post/FEAnimationController.cpp
    ${SOURCE_DIR}/post/FEProbe.cpp
    ${SOURCE_DIR}/post/FEPostFilter.cpp
    ${SOURCE_DIR}/post/FEIsoSurface.cpp
)

if(FERENDER_HAS_METAL_RHI)
    list(APPEND RENDER_SOURCES
        ${SOURCE_DIR}/app/window/MetalViewport.cpp
        ${SOURCE_DIR}/rhi/Metal/MetalAttachmentResourceBuilder.mm
        ${SOURCE_DIR}/rhi/Metal/MetalBufferResource.mm
        ${SOURCE_DIR}/rhi/Metal/MetalClearFramePass.mm
        ${SOURCE_DIR}/rhi/Metal/MetalDepthStencilFactory.mm
        ${SOURCE_DIR}/rhi/Metal/MetalDeviceFactory.mm
        ${SOURCE_DIR}/rhi/Metal/MetalDrawableFrameSubmitter.mm
        ${SOURCE_DIR}/app/platform/macos/MacOSMetalLayerHost.mm
        ${SOURCE_DIR}/rhi/Metal/MetalLineUpload.cpp
        ${SOURCE_DIR}/rhi/Metal/MetalMeshFramePass.mm
        ${SOURCE_DIR}/rhi/Metal/MetalMeshFramePassBuilder.cpp
        ${SOURCE_DIR}/rhi/Metal/MetalMeshScalarUpdater.mm
        ${SOURCE_DIR}/rhi/Metal/MetalMeshUploadBuilder.cpp
        ${SOURCE_DIR}/rhi/Metal/MetalMeshUploader.cpp
        ${SOURCE_DIR}/rhi/Metal/MetalObjectResource.mm
        ${SOURCE_DIR}/rhi/Metal/MetalPipelineFactory.mm
        ${SOURCE_DIR}/rhi/Metal/MetalPickPass.mm
        ${SOURCE_DIR}/rhi/Metal/MetalPickPassBuilder.cpp
        ${SOURCE_DIR}/rhi/Metal/MetalPickUtils.mm
        ${SOURCE_DIR}/rhi/Metal/MetalRenderPassFactory.mm
        ${SOURCE_DIR}/rhi/Metal/MetalShaderSources.cpp
        ${SOURCE_DIR}/rhi/Metal/MetalStateResource.mm
        ${SOURCE_DIR}/rhi/Metal/MetalSurfaceUploadBuilder.cpp
        ${SOURCE_DIR}/rhi/Metal/MetalSurfaceUploader.cpp
        ${SOURCE_DIR}/rhi/Metal/MetalTextureResource.mm
        ${SOURCE_DIR}/rhi/Metal/MetalUniformUtils.cpp
        ${SOURCE_DIR}/rhi/Metal/MetalRenderBackend.mm
    )
endif()

if(FERENDER_HAS_VULKAN_RHI)
    list(APPEND RENDER_SOURCES
        ${SOURCE_DIR}/rhi/Vulkan/VulkanBufferResource.cpp
        ${SOURCE_DIR}/rhi/Vulkan/VulkanClearFrameRenderer.cpp
        ${SOURCE_DIR}/rhi/Vulkan/VulkanCommandResource.cpp
        ${SOURCE_DIR}/rhi/Vulkan/VulkanContext.cpp
        ${SOURCE_DIR}/rhi/Vulkan/VulkanDepthResource.cpp
        ${SOURCE_DIR}/rhi/Vulkan/VulkanDescriptorResource.cpp
        ${SOURCE_DIR}/rhi/Vulkan/VulkanDescriptorSetLayoutResource.cpp
        ${SOURCE_DIR}/rhi/Vulkan/VulkanDevice.cpp
        ${SOURCE_DIR}/rhi/Vulkan/VulkanFramebufferResource.cpp
        ${SOURCE_DIR}/rhi/Vulkan/VulkanFramePipelines.cpp
        ${SOURCE_DIR}/rhi/Vulkan/VulkanMeshBufferResources.cpp
        ${SOURCE_DIR}/rhi/Vulkan/VulkanMeshFramePass.cpp
        ${SOURCE_DIR}/rhi/Vulkan/VulkanPickPass.cpp
        ${SOURCE_DIR}/rhi/Vulkan/VulkanPickResources.cpp
        ${SOURCE_DIR}/rhi/Vulkan/VulkanPipelineResource.cpp
        ${SOURCE_DIR}/rhi/Vulkan/VulkanRenderPassResource.cpp
        ${SOURCE_DIR}/rhi/Vulkan/VulkanRenderBackend.cpp
        ${SOURCE_DIR}/rhi/Vulkan/VulkanStagingUploadContext.cpp
        ${SOURCE_DIR}/rhi/Vulkan/VulkanSurface.cpp
        ${SOURCE_DIR}/rhi/Vulkan/VulkanSwapchain.cpp
        ${SOURCE_DIR}/rhi/Vulkan/VulkanSwapchainFrameResources.cpp
    )
    if(APPLE)
        list(APPEND RENDER_SOURCES
            ${SOURCE_DIR}/rhi/Vulkan/VulkanMacOSSurfaceFactory.mm
            ${SOURCE_DIR}/app/window/VulkanViewport.cpp
        )
    endif()
endif()

# Shader 资源文件
qt6_add_resources(RENDER_QRC shaders.qrc)

add_library(FERender SHARED ${RENDER_SOURCES} ${RENDER_HEADERS} ${RENDER_QRC})

include(GenerateExportHeader)
generate_export_header(FERender
    EXPORT_MACRO_NAME FERENDER_EXPORT
    EXPORT_FILE_NAME  ${CMAKE_CURRENT_BINARY_DIR}/ferender_export.h
)

target_include_directories(FERender
    PUBLIC
        $<BUILD_INTERFACE:${SOURCE_DIR}/common>
        $<BUILD_INTERFACE:${SOURCE_DIR}/data>
        $<BUILD_INTERFACE:${SOURCE_DIR}/convert>
        $<BUILD_INTERFACE:${SOURCE_DIR}/io>
        $<BUILD_INTERFACE:${SOURCE_DIR}/post>
        $<BUILD_INTERFACE:${SOURCE_DIR}/rhi>
        $<BUILD_INTERFACE:${SOURCE_DIR}/rhi/OpenGL>
        $<BUILD_INTERFACE:${SOURCE_DIR}/rhi/Vulkan>
        $<BUILD_INTERFACE:${SOURCE_DIR}/render>
        $<BUILD_INTERFACE:${SOURCE_DIR}/app/window>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include/FERender>
)

target_link_libraries(FERender PUBLIC
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
)

if(NOT APPLE)
    target_link_libraries(FERender PUBLIC OpenGL::GL)
endif()

if(FERENDER_HAS_OPENCASCADE_IMPORT)
    target_compile_definitions(FERender PRIVATE FERENDER_HAS_OPENCASCADE_IMPORT=1)
    target_include_directories(FERender PRIVATE ${OpenCASCADE_INCLUDE_DIR})
    target_link_directories(FERender PRIVATE ${OpenCASCADE_LIBRARY_DIR})
    target_link_libraries(FERender PRIVATE
        ${OpenCASCADE_FoundationClasses_LIBRARIES}
        ${OpenCASCADE_ModelingData_LIBRARIES}
        ${OpenCASCADE_ModelingAlgorithms_LIBRARIES}
        ${OpenCASCADE_DataExchange_LIBRARIES}
    )
endif()

if(FERENDER_HAS_METAL_RHI)
    target_compile_definitions(FERender PRIVATE FERENDER_HAS_METAL_RHI=1)
    target_link_libraries(FERender PRIVATE
        ${METAL_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
        ${APPKIT_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
    )
endif()

if(FERENDER_HAS_VULKAN_RHI)
    target_compile_definitions(FERender PRIVATE FERENDER_HAS_VULKAN_RHI=1)
    target_link_libraries(FERender PRIVATE Vulkan::Vulkan)
    if(Vulkan_GLSLC_EXECUTABLE)
        set(FERENDER_HAS_VULKAN_TRIANGLE_PIPELINE ON)
        set(VULKAN_SHADER_SOURCE_DIR "${SOURCE_DIR}/rhi/Vulkan/shaders")
        set(VULKAN_SHADER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/vulkan_spirv")
        set(VULKAN_BACKGROUND_VERT_SPV "${VULKAN_SHADER_BINARY_DIR}/vulkan_background.vert.spv")
        set(VULKAN_BACKGROUND_FRAG_SPV "${VULKAN_SHADER_BINARY_DIR}/vulkan_background.frag.spv")
        set(VULKAN_TRIANGLE_VERT_SPV "${VULKAN_SHADER_BINARY_DIR}/vulkan_triangle.vert.spv")
        set(VULKAN_TRIANGLE_FRAG_SPV "${VULKAN_SHADER_BINARY_DIR}/vulkan_triangle.frag.spv")
        set(VULKAN_MESH_VERT_SPV "${VULKAN_SHADER_BINARY_DIR}/vulkan_mesh.vert.spv")
        set(VULKAN_MESH_FRAG_SPV "${VULKAN_SHADER_BINARY_DIR}/vulkan_mesh.frag.spv")
        set(VULKAN_ISO_VERT_SPV "${VULKAN_SHADER_BINARY_DIR}/vulkan_iso.vert.spv")
        set(VULKAN_ISO_FRAG_SPV "${VULKAN_SHADER_BINARY_DIR}/vulkan_iso.frag.spv")
        set(VULKAN_LINE_VERT_SPV "${VULKAN_SHADER_BINARY_DIR}/vulkan_line.vert.spv")
        set(VULKAN_LINE_FRAG_SPV "${VULKAN_SHADER_BINARY_DIR}/vulkan_line.frag.spv")
        set(VULKAN_PICK_VERT_SPV "${VULKAN_SHADER_BINARY_DIR}/vulkan_pick.vert.spv")
        set(VULKAN_PICK_FRAG_SPV "${VULKAN_SHADER_BINARY_DIR}/vulkan_pick.frag.spv")

        add_custom_command(
            OUTPUT "${VULKAN_BACKGROUND_VERT_SPV}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKAN_SHADER_BINARY_DIR}"
            COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "--target-env=${FERENDER_VULKAN_SHADER_TARGET_ENV}"
                    "${VULKAN_SHADER_SOURCE_DIR}/vulkan_background.vert"
                    -o "${VULKAN_BACKGROUND_VERT_SPV}"
            DEPENDS "${VULKAN_SHADER_SOURCE_DIR}/vulkan_background.vert"
            VERBATIM
        )
        add_custom_command(
            OUTPUT "${VULKAN_BACKGROUND_FRAG_SPV}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKAN_SHADER_BINARY_DIR}"
            COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "--target-env=${FERENDER_VULKAN_SHADER_TARGET_ENV}"
                    "${VULKAN_SHADER_SOURCE_DIR}/vulkan_background.frag"
                    -o "${VULKAN_BACKGROUND_FRAG_SPV}"
            DEPENDS "${VULKAN_SHADER_SOURCE_DIR}/vulkan_background.frag"
            VERBATIM
        )
        add_custom_command(
            OUTPUT "${VULKAN_TRIANGLE_VERT_SPV}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKAN_SHADER_BINARY_DIR}"
            COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "--target-env=${FERENDER_VULKAN_SHADER_TARGET_ENV}"
                    "${VULKAN_SHADER_SOURCE_DIR}/vulkan_triangle.vert"
                    -o "${VULKAN_TRIANGLE_VERT_SPV}"
            DEPENDS "${VULKAN_SHADER_SOURCE_DIR}/vulkan_triangle.vert"
            VERBATIM
        )
        add_custom_command(
            OUTPUT "${VULKAN_TRIANGLE_FRAG_SPV}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKAN_SHADER_BINARY_DIR}"
            COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "--target-env=${FERENDER_VULKAN_SHADER_TARGET_ENV}"
                    "${VULKAN_SHADER_SOURCE_DIR}/vulkan_triangle.frag"
                    -o "${VULKAN_TRIANGLE_FRAG_SPV}"
            DEPENDS "${VULKAN_SHADER_SOURCE_DIR}/vulkan_triangle.frag"
            VERBATIM
        )
        add_custom_command(
            OUTPUT "${VULKAN_MESH_VERT_SPV}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKAN_SHADER_BINARY_DIR}"
            COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "--target-env=${FERENDER_VULKAN_SHADER_TARGET_ENV}"
                    "${VULKAN_SHADER_SOURCE_DIR}/vulkan_mesh.vert"
                    -o "${VULKAN_MESH_VERT_SPV}"
            DEPENDS "${VULKAN_SHADER_SOURCE_DIR}/vulkan_mesh.vert"
            VERBATIM
        )
        add_custom_command(
            OUTPUT "${VULKAN_MESH_FRAG_SPV}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKAN_SHADER_BINARY_DIR}"
            COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "--target-env=${FERENDER_VULKAN_SHADER_TARGET_ENV}"
                    "${VULKAN_SHADER_SOURCE_DIR}/vulkan_mesh.frag"
                    -o "${VULKAN_MESH_FRAG_SPV}"
            DEPENDS "${VULKAN_SHADER_SOURCE_DIR}/vulkan_mesh.frag"
            VERBATIM
        )
        add_custom_command(
            OUTPUT "${VULKAN_ISO_VERT_SPV}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKAN_SHADER_BINARY_DIR}"
            COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "--target-env=${FERENDER_VULKAN_SHADER_TARGET_ENV}"
                    "${VULKAN_SHADER_SOURCE_DIR}/vulkan_iso.vert"
                    -o "${VULKAN_ISO_VERT_SPV}"
            DEPENDS "${VULKAN_SHADER_SOURCE_DIR}/vulkan_iso.vert"
            VERBATIM
        )
        add_custom_command(
            OUTPUT "${VULKAN_ISO_FRAG_SPV}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKAN_SHADER_BINARY_DIR}"
            COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "--target-env=${FERENDER_VULKAN_SHADER_TARGET_ENV}"
                    "${VULKAN_SHADER_SOURCE_DIR}/vulkan_iso.frag"
                    -o "${VULKAN_ISO_FRAG_SPV}"
            DEPENDS "${VULKAN_SHADER_SOURCE_DIR}/vulkan_iso.frag"
            VERBATIM
        )
        add_custom_command(
            OUTPUT "${VULKAN_LINE_VERT_SPV}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKAN_SHADER_BINARY_DIR}"
            COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "--target-env=${FERENDER_VULKAN_SHADER_TARGET_ENV}"
                    "${VULKAN_SHADER_SOURCE_DIR}/vulkan_line.vert"
                    -o "${VULKAN_LINE_VERT_SPV}"
            DEPENDS "${VULKAN_SHADER_SOURCE_DIR}/vulkan_line.vert"
            VERBATIM
        )
        add_custom_command(
            OUTPUT "${VULKAN_LINE_FRAG_SPV}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKAN_SHADER_BINARY_DIR}"
            COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "--target-env=${FERENDER_VULKAN_SHADER_TARGET_ENV}"
                    "${VULKAN_SHADER_SOURCE_DIR}/vulkan_line.frag"
                    -o "${VULKAN_LINE_FRAG_SPV}"
            DEPENDS "${VULKAN_SHADER_SOURCE_DIR}/vulkan_line.frag"
            VERBATIM
        )
        add_custom_command(
            OUTPUT "${VULKAN_PICK_VERT_SPV}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKAN_SHADER_BINARY_DIR}"
            COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "--target-env=${FERENDER_VULKAN_SHADER_TARGET_ENV}"
                    "${VULKAN_SHADER_SOURCE_DIR}/vulkan_pick.vert"
                    -o "${VULKAN_PICK_VERT_SPV}"
            DEPENDS "${VULKAN_SHADER_SOURCE_DIR}/vulkan_pick.vert"
            VERBATIM
        )
        add_custom_command(
            OUTPUT "${VULKAN_PICK_FRAG_SPV}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VULKAN_SHADER_BINARY_DIR}"
            COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "--target-env=${FERENDER_VULKAN_SHADER_TARGET_ENV}"
                    "${VULKAN_SHADER_SOURCE_DIR}/vulkan_pick.frag"
                    -o "${VULKAN_PICK_FRAG_SPV}"
            DEPENDS "${VULKAN_SHADER_SOURCE_DIR}/vulkan_pick.frag"
            VERBATIM
        )
        add_custom_target(FERenderVulkanShaders
            DEPENDS
                "${VULKAN_BACKGROUND_VERT_SPV}"
                "${VULKAN_BACKGROUND_FRAG_SPV}"
                "${VULKAN_TRIANGLE_VERT_SPV}"
                "${VULKAN_TRIANGLE_FRAG_SPV}"
                "${VULKAN_MESH_VERT_SPV}"
                "${VULKAN_MESH_FRAG_SPV}"
                "${VULKAN_ISO_VERT_SPV}"
                "${VULKAN_ISO_FRAG_SPV}"
                "${VULKAN_LINE_VERT_SPV}"
                "${VULKAN_LINE_FRAG_SPV}"
                "${VULKAN_PICK_VERT_SPV}"
                "${VULKAN_PICK_FRAG_SPV}"
        )
        add_dependencies(FERender FERenderVulkanShaders)
        target_compile_definitions(FERender PRIVATE
            FERENDER_HAS_VULKAN_TRIANGLE_PIPELINE=1
            FERENDER_VULKAN_SHADER_DIR="${VULKAN_SHADER_BINARY_DIR}"
        )
    else()
        message(STATUS "Vulkan triangle pipeline disabled: glslc was not found")
    endif()
    if(APPLE)
        target_compile_definitions(FERender PRIVATE FERENDER_HAS_MACOS_VULKAN_SURFACE=1)
        target_link_libraries(FERender PRIVATE "-framework AppKit" "-framework QuartzCore")
    endif()
endif()

# GLM 是 header-only 库，直接添加 include 路径即可（避免 export set 问题）
target_include_directories(FERender SYSTEM PUBLIC
    $<BUILD_INTERFACE:${glm_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# ── GUI 模块（应用程序）──

# 手动 MOC 应用程序自己的头文件，避免 AUTOMOC 重复 moc FERender 的 Q_OBJECT 类
qt6_wrap_cpp(VIEWER_MOC_SRCS
    ${SOURCE_DIR}/app/ui/MainWindow.h
    ${SOURCE_DIR}/app/ui/FEModelPanel.h
    ${SOURCE_DIR}/app/ui/PartsPanel.h
    ${SOURCE_DIR}/app/ui/ResultPanel.h
    ${SOURCE_DIR}/app/ui/ViewportContextMenu.h
    ${SOURCE_DIR}/app/ui/ControlPanel.h
    ${SOURCE_DIR}/app/ui/MonitorPanel.h
)

qt6_add_resources(VIEWER_QRC icons.qrc)

add_executable(${PROJECT_NAME}
    ${SOURCE_DIR}/app/main.cpp
    ${SOURCE_DIR}/app/ui/MainWindow.cpp
    ${SOURCE_DIR}/app/ui/FEModelPanel.cpp
    ${SOURCE_DIR}/app/ui/PartsPanel.cpp
    ${SOURCE_DIR}/app/ui/ResultPanel.cpp
    ${SOURCE_DIR}/app/ui/ViewportContextMenu.cpp
    ${SOURCE_DIR}/app/ui/ControlPanel.cpp
    ${SOURCE_DIR}/app/ui/MonitorPanel.cpp
    ${VIEWER_MOC_SRCS}
    ${VIEWER_QRC}
)

set_target_properties(${PROJECT_NAME} PROPERTIES AUTOMOC OFF)

# macOS：打包为 .app bundle
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "FEModelViewer"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    )
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE FERender)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${SOURCE_DIR}/app
    ${SOURCE_DIR}/app/ui
    ${RENDER_INCLUDE_DIRS}
)

# ── 安装规则 ──

include(GNUInstallDirs)

# 安装库文件
install(TARGETS FERender
    EXPORT FERenderTargets
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# 安装可执行文件
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE  DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# 安装头文件
install(FILES ${RENDER_HEADERS}
    "${CMAKE_CURRENT_BINARY_DIR}/ferender_export.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/FERender
)

# 安装 GLM 头文件（header-only，随库一起分发）
install(DIRECTORY ${glm_SOURCE_DIR}/glm
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# 导出 CMake Target，其他项目可以 find_package(FERender) 使用
install(EXPORT FERenderTargets
    FILE FERenderTargets.cmake
    NAMESPACE FERender::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FERender
)

# 生成 FERenderConfig.cmake
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FERenderConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/FERenderConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FERender
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/FERenderConfig.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FERender
)

# ── 测试 ──

include(CTest)

if(BUILD_TESTING)
    add_executable(test_result_mapper
        tests/test_result_mapper.cpp
    )
    target_link_libraries(test_result_mapper PRIVATE FERender)
    add_test(NAME result_mapper
        COMMAND test_result_mapper
    )

    add_executable(test_result_repository
        tests/test_result_repository.cpp
    )
    target_link_libraries(test_result_repository PRIVATE FERender)
    add_test(NAME result_repository
        COMMAND test_result_repository
    )

    add_executable(test_deformation
        tests/test_deformation.cpp
    )
    target_link_libraries(test_deformation PRIVATE FERender)
    add_test(NAME deformation
        COMMAND test_deformation
    )

    add_executable(test_probe
        tests/test_probe.cpp
    )
    target_link_libraries(test_probe PRIVATE FERender)
    add_test(NAME probe
        COMMAND test_probe
    )

    add_executable(test_screen_space_picking
        tests/test_screen_space_picking.cpp
    )
    target_link_libraries(test_screen_space_picking PRIVATE FERender)
    add_test(NAME screen_space_picking
        COMMAND test_screen_space_picking
    )

    add_executable(test_post_filter
        tests/test_post_filter.cpp
    )
    target_link_libraries(test_post_filter PRIVATE FERender)
    add_test(NAME post_filter
        COMMAND test_post_filter
    )

    add_executable(test_post_state
        tests/test_post_state.cpp
    )
    target_link_libraries(test_post_state PRIVATE FERender)
    add_test(NAME post_state
        COMMAND test_post_state
    )

    add_executable(test_stl_parser
        tests/test_stl_parser.cpp
    )
    target_link_libraries(test_stl_parser PRIVATE FERender Qt6::Core)
    add_test(NAME stl_parser
        COMMAND test_stl_parser
    )

    add_executable(test_import_path_state
        tests/test_import_path_state.cpp
    )
    target_include_directories(test_import_path_state PRIVATE
        ${SOURCE_DIR}/common
    )
    target_link_libraries(test_import_path_state PRIVATE Qt6::Core)
    add_test(NAME import_path_state
        COMMAND test_import_path_state
    )

    add_executable(test_iso_cube_data
        tests/test_iso_cube_data.cpp
    )
    target_link_libraries(test_iso_cube_data PRIVATE FERender)
    add_test(NAME iso_cube_data
        COMMAND test_iso_cube_data
    )
    set_tests_properties(iso_cube_data PROPERTIES
        ENVIRONMENT "FERENDER_TEST_DATA_DIR=${CMAKE_CURRENT_SOURCE_DIR}/examples/data"
    )

    add_executable(test_threshold_row_data
        tests/test_threshold_row_data.cpp
    )
    target_link_libraries(test_threshold_row_data PRIVATE FERender)
    add_test(NAME threshold_row_data
        COMMAND test_threshold_row_data
    )
    set_tests_properties(threshold_row_data PROPERTIES
        ENVIRONMENT "FERENDER_TEST_DATA_DIR=${CMAKE_CURRENT_SOURCE_DIR}/examples/data"
    )

    add_executable(test_render_backend_factory
        tests/test_render_backend_factory.cpp
    )
    target_include_directories(test_render_backend_factory PRIVATE
        ${SOURCE_DIR}/rhi
    )
    target_link_libraries(test_render_backend_factory PRIVATE FERender)
    if(FERENDER_HAS_VULKAN_RHI)
        target_compile_definitions(test_render_backend_factory PRIVATE FERENDER_HAS_VULKAN_RHI=1)
        target_link_libraries(test_render_backend_factory PRIVATE Vulkan::Vulkan)
        if(APPLE)
            target_compile_definitions(test_render_backend_factory PRIVATE FERENDER_HAS_MACOS_VULKAN_SURFACE=1)
        endif()
    endif()
    if(FERENDER_HAS_METAL_RHI)
        target_compile_definitions(test_render_backend_factory PRIVATE FERENDER_HAS_METAL_RHI=1)
    endif()
    add_test(NAME render_backend_factory
        COMMAND test_render_backend_factory
    )

    add_executable(test_render_settings
        tests/test_render_settings.cpp
    )
    target_include_directories(test_render_settings PRIVATE
        ${SOURCE_DIR}/rhi
    )
    target_link_libraries(test_render_settings PRIVATE FERender Qt6::Core)
    if(FERENDER_HAS_METAL_RHI)
        target_compile_definitions(test_render_settings PRIVATE FERENDER_HAS_METAL_RHI=1)
    endif()
    add_test(NAME render_settings
        COMMAND test_render_settings
    )

    if(APPLE)
        add_executable(test_render_viewport_state
            tests/test_render_viewport_state.cpp
        )
        target_link_libraries(test_render_viewport_state PRIVATE FERender Qt6::Widgets)
        add_test(NAME render_viewport_state
            COMMAND test_render_viewport_state
        )
    endif()

    if(FERENDER_HAS_VULKAN_RHI AND APPLE)
        add_executable(test_macos_vulkan_surface
            tests/test_macos_vulkan_surface.cpp
        )
        target_include_directories(test_macos_vulkan_surface PRIVATE
            ${SOURCE_DIR}/rhi
        )
        target_compile_definitions(test_macos_vulkan_surface PRIVATE
            FERENDER_HAS_VULKAN_RHI=1
            FERENDER_HAS_MACOS_VULKAN_SURFACE=1
        )
        if(FERENDER_HAS_VULKAN_TRIANGLE_PIPELINE)
            target_compile_definitions(test_macos_vulkan_surface PRIVATE
                FERENDER_HAS_VULKAN_TRIANGLE_PIPELINE=1
            )
        endif()
        target_link_libraries(test_macos_vulkan_surface PRIVATE
            FERender Vulkan::Vulkan Qt6::Gui
        )
        add_test(NAME macos_vulkan_surface
            COMMAND test_macos_vulkan_surface
        )
        set_tests_properties(render_backend_factory PROPERTIES
            DEPENDS macos_vulkan_surface
        )
        if(TARGET test_render_viewport_state)
            # 先跑底层 Vulkan surface smoke test，再跑更宽的 Qt/RHI 视口集成测试。
            set_tests_properties(render_viewport_state PROPERTIES
                DEPENDS macos_vulkan_surface
            )
        endif()
    endif()

    if(FERENDER_HAS_METAL_RHI AND APPLE)
        add_executable(test_macos_metal_layer
            tests/test_macos_metal_layer.cpp
        )
        target_include_directories(test_macos_metal_layer PRIVATE
            ${SOURCE_DIR}/app/platform/macos
            ${SOURCE_DIR}/rhi
            ${SOURCE_DIR}/rhi/Metal
        )
        target_compile_definitions(test_macos_metal_layer PRIVATE FERENDER_HAS_METAL_RHI=1)
        target_link_libraries(test_macos_metal_layer PRIVATE
            FERender Qt6::Gui
        )
        add_test(NAME macos_metal_layer
            COMMAND test_macos_metal_layer
        )
    endif()

    qt6_wrap_cpp(TEST_RESULT_PANEL_MOC_SRCS ${SOURCE_DIR}/app/ui/ResultPanel.h)
    add_executable(test_result_panel
        tests/test_result_panel.cpp
        ${SOURCE_DIR}/app/ui/ResultPanel.cpp
        ${TEST_RESULT_PANEL_MOC_SRCS}
    )
    set_target_properties(test_result_panel PROPERTIES AUTOMOC OFF)
    target_include_directories(test_result_panel PRIVATE
        ${SOURCE_DIR}/app
        ${SOURCE_DIR}/app/ui
        ${RENDER_INCLUDE_DIRS}
    )
    target_link_libraries(test_result_panel PRIVATE FERender Qt6::Core Qt6::Gui Qt6::Widgets)
    add_test(NAME result_panel
        COMMAND test_result_panel
    )

    qt6_wrap_cpp(TEST_PARTS_PANEL_MOC_SRCS ${SOURCE_DIR}/app/ui/PartsPanel.h)
    add_executable(test_parts_panel
        tests/test_parts_panel.cpp
        ${SOURCE_DIR}/app/ui/PartsPanel.cpp
        ${TEST_PARTS_PANEL_MOC_SRCS}
    )
    set_target_properties(test_parts_panel PROPERTIES AUTOMOC OFF)
    target_include_directories(test_parts_panel PRIVATE
        ${SOURCE_DIR}/app
        ${SOURCE_DIR}/app/ui
        ${RENDER_INCLUDE_DIRS}
    )
    target_link_libraries(test_parts_panel PRIVATE FERender Qt6::Core Qt6::Gui Qt6::Widgets)
    add_test(NAME parts_panel
        COMMAND test_parts_panel
    )

    qt6_wrap_cpp(TEST_VIEWPORT_CONTEXT_MENU_MOC_SRCS ${SOURCE_DIR}/app/ui/ViewportContextMenu.h)
    add_executable(test_viewport_context_menu
        tests/test_viewport_context_menu.cpp
        ${SOURCE_DIR}/app/ui/ViewportContextMenu.cpp
        ${TEST_VIEWPORT_CONTEXT_MENU_MOC_SRCS}
    )
    set_target_properties(test_viewport_context_menu PROPERTIES AUTOMOC OFF)
    target_include_directories(test_viewport_context_menu PRIVATE
        ${SOURCE_DIR}/app
        ${SOURCE_DIR}/app/ui
        ${RENDER_INCLUDE_DIRS}
    )
    target_link_libraries(test_viewport_context_menu PRIVATE FERender Qt6::Core Qt6::Gui Qt6::Widgets)
    add_test(NAME viewport_context_menu
        COMMAND test_viewport_context_menu
    )

    add_test(NAME ferender_public_headers
        COMMAND ${CMAKE_COMMAND}
            "-DINSTALLED_HEADERS=${RENDER_HEADERS}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_public_headers.cmake"
    )

    add_test(NAME fe_model_viewer_parse_missing_file_is_headless
        COMMAND ${CMAKE_COMMAND}
            "-DVIEWER=$<TARGET_FILE:${PROJECT_NAME}>"
            "-DMISSING_FILE=${CMAKE_CURRENT_BINARY_DIR}/missing.op2"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_parse_missing_file_test.cmake"
    )

    add_test(NAME cli_parse_mode_layering
        COMMAND ${CMAKE_COMMAND}
            "-DSOURCE_FILE=${SOURCE_DIR}/app/main.cpp"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_parse_mode_layering.cmake"
    )

    add_test(NAME installed_simple_viewer_example
        COMMAND ${CMAKE_COMMAND}
            "-DPROJECT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DPROJECT_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            "-DINSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/example-install"
            "-DEXAMPLE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/examples/simple_viewer"
            "-DEXAMPLE_QT_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
            "-DEXAMPLE_CMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_installed_example.cmake"
    )
endif()
