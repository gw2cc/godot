cmake_minimum_required(VERSION 3.29)
project(godot VERSION 4.7 LANGUAGES NONE)

include(CTest)
set(CMAKE_INSTALL_LIBDIR "lib" CACHE PATH "Object code libraries")
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# ---- Platform ----
if(WIN32)
    set(GODOT_PLATFORM windows)
    set(GODOT_D3D12_ARG d3d12=yes)
else()
    set(GODOT_PLATFORM linuxbsd)
    set(GODOT_D3D12_ARG "")
endif()

# ---- Build target (editor vs export template) ----
if(BUILD_EXPORT_TEMPLATE)
    set(GODOT_TARGET template_release)
    set(GODOT_IS_EDITOR OFF)
else()
    set(GODOT_TARGET editor)
    set(GODOT_IS_EDITOR ON)
endif()

# ---- LTO ----
if(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    set(GODOT_LTO_ARG lto=full)
else()
    set(GODOT_LTO_ARG lto=none)
endif()

if(BUILD_TESTING)
    set(GODOT_TESTS_ARG yes)
else()
    set(GODOT_TESTS_ARG no)
endif()

# ---- Parallelism ----
include(ProcessorCount)
ProcessorCount(GODOT_CPU_COUNT)
if(GODOT_CPU_COUNT EQUAL 0)
    set(GODOT_CPU_COUNT 4)
endif()

set(GODOT_SCONS_COMMAND "uvx scons" CACHE STRING "Command used to invoke SCons")
separate_arguments(GODOT_SCONS_COMMAND_LIST NATIVE_COMMAND "${GODOT_SCONS_COMMAND}")

set(GODOT_SCONS_ARGS
    debug_symbols=yes
    debug_paths_relative=yes
    use_static_cpp=yes
    engine_update_check=no
    module_navigation_enabled=no
    disable_path_overrides=no
    deprecated=no
    tests=${GODOT_TESTS_ARG}
    platform=${GODOT_PLATFORM}
    windows_subsystem=console
    library_type=static_library
    progress=no
    optimize=speed_trace
    target=${GODOT_TARGET}
    ${GODOT_LTO_ARG}
)
if(GODOT_D3D12_ARG)
    list(APPEND GODOT_SCONS_ARGS ${GODOT_D3D12_ARG})
endif()

set(GODOT_LINK_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/godot-link.json")
execute_process(
    COMMAND ${GODOT_SCONS_COMMAND_LIST}
        --dry-run
        ${GODOT_SCONS_ARGS}
        link_manifest=${GODOT_LINK_MANIFEST}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE GODOT_MANIFEST_RESULT
    OUTPUT_VARIABLE GODOT_MANIFEST_OUTPUT
    ERROR_VARIABLE GODOT_MANIFEST_ERROR
)
if(NOT GODOT_MANIFEST_RESULT EQUAL 0 OR NOT EXISTS "${GODOT_LINK_MANIFEST}")
    message(FATAL_ERROR
        "Failed to generate Godot link manifest.\n"
        "${GODOT_MANIFEST_OUTPUT}${GODOT_MANIFEST_ERROR}"
    )
endif()

file(READ "${GODOT_LINK_MANIFEST}" GODOT_LINK_MANIFEST_JSON)

function(godot_read_manifest_array output_variable member)
    string(JSON item_count LENGTH "${GODOT_LINK_MANIFEST_JSON}" "${member}")
    set(items)
    if(item_count GREATER 0)
        math(EXPR last_index "${item_count} - 1")
        foreach(index RANGE 0 ${last_index})
            string(JSON item GET "${GODOT_LINK_MANIFEST_JSON}" "${member}" ${index})
            list(APPEND items "${item}")
        endforeach()
    endif()
    set(${output_variable} "${items}" PARENT_SCOPE)
endfunction()

godot_read_manifest_array(GODOT_LIBRARY_PATHS library_paths)
godot_read_manifest_array(GODOT_EXTERNAL_LIBRARIES external_libraries)
godot_read_manifest_array(GODOT_LIBRARY_RELATIVE_PATHS libraries)

set(GODOT_LIBRARIES)
set(GODOT_INSTALLED_LIBRARIES)
foreach(library_path IN LISTS GODOT_LIBRARY_RELATIVE_PATHS)
    if(IS_ABSOLUTE "${library_path}")
        list(APPEND GODOT_LIBRARIES "${library_path}")
    else()
        list(APPEND GODOT_LIBRARIES "${CMAKE_CURRENT_SOURCE_DIR}/${library_path}")
    endif()
    get_filename_component(library_name "${library_path}" NAME)
    list(APPEND GODOT_INSTALLED_LIBRARIES
        "$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/${library_name}"
    )
endforeach()

set(GODOT_EXTERNAL_LIBRARY_FILES)
set(GODOT_INSTALLED_EXTERNAL_LIBRARIES)
foreach(external_library IN LISTS GODOT_EXTERNAL_LIBRARIES)
    set(external_library_file "")
    if(IS_ABSOLUTE "${external_library}" AND EXISTS "${external_library}")
        set(external_library_file "${external_library}")
    else()
        foreach(library_path IN LISTS GODOT_LIBRARY_PATHS)
            if(EXISTS "${library_path}/${external_library}")
                set(external_library_file "${library_path}/${external_library}")
                break()
            endif()
        endforeach()
    endif()

    if(external_library_file)
        list(APPEND GODOT_EXTERNAL_LIBRARY_FILES "${external_library_file}")
        get_filename_component(external_library_name "${external_library_file}" NAME)
        list(APPEND GODOT_INSTALLED_EXTERNAL_LIBRARIES
            "$<INSTALL_PREFIX>/${CMAKE_INSTALL_LIBDIR}/${external_library_name}"
        )
    else()
        list(APPEND GODOT_INSTALLED_EXTERNAL_LIBRARIES "${external_library}")
    endif()
endforeach()

# ---- SCons build target ----
add_custom_target(godot_build
    COMMAND ${GODOT_SCONS_COMMAND_LIST}
        -j${GODOT_CPU_COUNT}
        ${GODOT_SCONS_ARGS}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    BYPRODUCTS ${GODOT_LIBRARIES}
    COMMENT "Building Godot (${GODOT_TARGET}) via SCons"
    VERBATIM
)

# ---- Interface target ----
add_library(godot INTERFACE)
add_library(godot::godot ALIAS godot)
add_dependencies(godot godot_build)

foreach(library_path IN LISTS GODOT_LIBRARY_PATHS)
    target_link_directories(godot INTERFACE
        "$<BUILD_INTERFACE:${library_path}>"
    )
endforeach()
target_link_libraries(godot INTERFACE
    $<BUILD_INTERFACE:${GODOT_LIBRARIES}>
    $<INSTALL_INTERFACE:${GODOT_INSTALLED_LIBRARIES}>
    $<BUILD_INTERFACE:${GODOT_EXTERNAL_LIBRARIES}>
    $<INSTALL_INTERFACE:${GODOT_INSTALLED_EXTERNAL_LIBRARIES}>
)

# Compile definitions mirroring what the vcpkg port config generates
target_compile_definitions(godot INTERFACE
    ACCESSKIT_DYNAMIC
    ACCESSKIT_ENABLED
    BROTLI_ENABLED
    CLIPPER2_ENABLED
    DISABLE_DEPRECATED
    EGL_ENABLED
    GLAD_ENABLED
    GLES3_ENABLED
    MINIZIP_ENABLED
    NDEBUG
    NO_EDITOR_SPLASH
    RD_ENABLED
    SDL_ENABLED
    THREADS_ENABLED
    TYPED_METHOD_BIND
    USE_VOLK
    VULKAN_ENABLED
    ZSTD_STATIC_LINKING_ONLY
)
if(WIN32)
    target_compile_definitions(godot INTERFACE
        _HAS_EXCEPTIONS=0
        _WIN32_WINNT=0x0601
        _WIN64
        D3D12_ENABLED
        NOMINMAX
        VK_USE_PLATFORM_WIN32_KHR
        WASAPI_ENABLED
        WIN32
        WINDOWS_ENABLED
        WINDOWS_SUBSYSTEM_CONSOLE
        WINMIDI_ENABLED
        WINVER=0x0601
    )
endif()
if(GODOT_IS_EDITOR)
    target_compile_definitions(godot INTERFACE TOOLS_ENABLED DEBUG_ENABLED)
endif()

# MSVC: ensure __cplusplus reflects the actual standard (required by typedefs.h)
target_compile_options(godot INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
)

# Include directories: engine root, platform headers, all thirdparty subdirs
file(GLOB _godot_thirdparty_dirs
    LIST_DIRECTORIES true
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/*"
)
target_include_directories(godot INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/platform/${GODOT_PLATFORM}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/godot>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/godot/platform/${GODOT_PLATFORM}>
)

foreach(thirdparty_dir IN LISTS _godot_thirdparty_dirs)
    if(IS_DIRECTORY "${thirdparty_dir}")
        get_filename_component(thirdparty_name "${thirdparty_dir}" NAME)
        target_include_directories(godot INTERFACE
            "$<BUILD_INTERFACE:${thirdparty_dir}>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/godot/thirdparty/${thirdparty_name}>"
        )
    endif()
endforeach()

# ---- Installation ----
set(GODOT_PACKAGE_DIR "share/godot")
set(GODOT_CONFIG_FILE "${CMAKE_CURRENT_BINARY_DIR}/godot-config.cmake")
set(GODOT_CONFIG_VERSION_FILE "${CMAKE_CURRENT_BINARY_DIR}/godot-config-version.cmake")

install(
    TARGETS godot
    EXPORT godot-targets
)

install(
    FILES
        ${GODOT_LIBRARIES}
        ${GODOT_EXTERNAL_LIBRARY_FILES}
    DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/godot"
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
        PATTERN "*.inc"
)

if(WIN32)
    install(CODE "
        file(GLOB_RECURSE godot_pdbs \"${CMAKE_CURRENT_SOURCE_DIR}/bin/*.pdb\")
        if(godot_pdbs)
            file(INSTALL \${godot_pdbs}
                DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}\"
            )
        endif()
    ")
endif()

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/godot-config.cmake.in"
    "${GODOT_CONFIG_FILE}"
    INSTALL_DESTINATION "${GODOT_PACKAGE_DIR}"
)

write_basic_package_version_file(
    "${GODOT_CONFIG_VERSION_FILE}"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

install(
    EXPORT godot-targets
    NAMESPACE godot::
    DESTINATION "${GODOT_PACKAGE_DIR}"
    FILE godot-targets.cmake
)

install(
    FILES
        "${GODOT_CONFIG_FILE}"
        "${GODOT_CONFIG_VERSION_FILE}"
    DESTINATION "${GODOT_PACKAGE_DIR}"
)

