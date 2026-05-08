cmake_minimum_required(VERSION 3.25)

project(HyperCollider VERSION 0.1.0 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Git QUIET)

set(HC_PLUGIN_GIT_HASH "nogit")
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --short=12 HEAD
        OUTPUT_VARIABLE HC_PLUGIN_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" status --porcelain
        OUTPUT_VARIABLE HC_PLUGIN_GIT_STATUS
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(HC_PLUGIN_GIT_STATUS)
        string(APPEND HC_PLUGIN_GIT_HASH "-dirty")
    endif()
endif()

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

string(TIMESTAMP HC_PLUGIN_BUILD_TIMESTAMP_UTC "%Y-%m-%dT%H:%M:%SZ" UTC)
set(HC_PLUGIN_BUILD_HASH
    "${HC_PLUGIN_GIT_HASH}-${CMAKE_BUILD_TYPE}")
message(STATUS "HyperCollider build hash: ${HC_PLUGIN_BUILD_HASH}")

# Options
option(HC_EMBED_WASM    "Embed hclang.wasm + hcsynth.wasm into the binary" OFF)
option(BUILD_INSTRUMENT "Build instrument variant"  ON)
option(BUILD_EFFECT     "Build effect variant"      OFF)
option(BUILD_CLAP       "Build CLAP variants"       OFF)
option(BUILD_IMGUI_UI   "Build Dear ImGui UI"       OFF)
option(BUILD_STANDALONE "Build standalone test app" OFF)

# WASM paths (for development)
set(HC_LANG_WASM_PATH  "" CACHE STRING "Path to hclang.wasm")
set(HC_SYNTH_WASM_PATH "" CACHE STRING "Path to hcsynth.wasm")
set(HC_CLASSLIB_PACK   "" CACHE STRING "Path to hclang_classlib.pack")

# Include cmake helpers
include(cmake/EmbedWasm.cmake)

# Setup WASM embedding if enabled
if(HC_EMBED_WASM)
    message(STATUS "WASM embedding enabled")
    
    # Generate embedded WASM blobs
    # These will be created from the paths specified in the environment or CMake variables
    if(HC_LANG_WASM_PATH AND EXISTS "${HC_LANG_WASM_PATH}")
        embed_wasm(hypercollider_core "${HC_LANG_WASM_PATH}" hclang_wasm_blob)
        message(STATUS "Embedding hclang.wasm from ${HC_LANG_WASM_PATH}")
    else()
        message(WARNING "HC_LANG_WASM_PATH not set or file not found - hclang.wasm will not be embedded")
    endif()
    
    if(HC_SYNTH_WASM_PATH AND EXISTS "${HC_SYNTH_WASM_PATH}")
        embed_wasm(hypercollider_core "${HC_SYNTH_WASM_PATH}" hcsynth_wasm_blob)
        message(STATUS "Embedding hcsynth.wasm from ${HC_SYNTH_WASM_PATH}")
    else()
        message(WARNING "HC_SYNTH_WASM_PATH not set or file not found - hcsynth.wasm will not be embedded")
    endif()
    
    # Add compile definition for embedded mode
    target_compile_definitions(hypercollider_core PRIVATE HC_EMBEDDED_WASM=1)
endif()

# Fetch dependencies
include(FetchContent)

# WAMR
FetchContent_Declare(
    wamr
    GIT_REPOSITORY https://github.com/bytecodealliance/wasm-micro-runtime.git
    GIT_TAG WAMR-2.4.3
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(wamr)

# Set WAMR build options
set(WAMR_ROOT_DIR "${wamr_SOURCE_DIR}")

# Platform-specific WAMR setup
if(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(WAMR_BUILD_TARGET "AARCH64")
    else()
        set(WAMR_BUILD_TARGET "X86_64")
    endif()
    set(WAMR_BUILD_PLATFORM "darwin")
elseif(WIN32)
    set(WAMR_BUILD_TARGET "X86_64")
    set(WAMR_BUILD_PLATFORM "windows")
else()
    set(WAMR_BUILD_TARGET "X86_64")
    set(WAMR_BUILD_PLATFORM "linux")
endif()

set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 1)
set(WAMR_BUILD_LIBC_WASI 1)
set(WAMR_BUILD_LIBC_BUILTIN 1)
set(WAMR_BUILD_SHARED_MEMORY 0)

# Include WAMR runtime lib cmake to get the sources
include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

# VST3 SDK
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_AUV2_BUILDS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG master
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    GIT_SUBMODULES_RECURSE TRUE
)
FetchContent_MakeAvailable(vst3sdk)
smtg_enable_vst3_sdk()

# nlohmann_json for preset management
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# CLAP SDK (optional)
if(BUILD_CLAP)
    FetchContent_Declare(
        clap
        GIT_REPOSITORY https://github.com/free-audio/clap.git
        GIT_TAG main
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(clap)
    set(CLAP_INCLUDE_DIRS ${clap_SOURCE_DIR}/include)
    message(STATUS "CLAP SDK found at ${CLAP_INCLUDE_DIRS}")
endif()

# ImGui (optional)
if(BUILD_IMGUI_UI)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG master
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(imgui)
endif()

# WAMR include directories
include_directories(
    ${WAMR_ROOT_DIR}/core/iwasm/include
    ${WAMR_ROOT_DIR}/core/iwasm/interpreter
    ${WAMR_ROOT_DIR}/core/shared/platform/include
    ${WAMR_ROOT_DIR}/core/shared/utils/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Core library
set(HC_CORE_SOURCES
    src/core/wasm_host.h
    src/core/wasm_host.cpp
    src/core/hclang_engine.h
    src/core/hclang_engine.cpp
    src/core/preset_manager.h
    src/core/preset_manager.cpp
)

# Create core library
add_library(hypercollider_core STATIC ${HC_CORE_SOURCES})

target_include_directories(hypercollider_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${vst3sdk_SOURCE_DIR}
)

target_link_libraries(hypercollider_core
    PRIVATE
        vmlib
        nlohmann_json::nlohmann_json
)

# Platform-specific linking for WAMR
target_link_libraries(hypercollider_core PRIVATE pthread dl m)

if(APPLE)
    target_link_libraries(hypercollider_core PRIVATE "-framework Foundation")
endif()

# VST3 Plugin target
set(PLUGIN_SOURCES
    src/plugin/processor.h
    src/plugin/processor.cpp
    src/plugin/controller.h
    src/plugin/controller.cpp
    src/plugin/entry.cpp
    src/plugin/ids.h
    src/plugin/version.h
)

# Instrument variant
if(BUILD_INSTRUMENT)
    smtg_add_vst3plugin(hypercollider_instrument ${PLUGIN_SOURCES})

    target_include_directories(hypercollider_instrument
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${vst3sdk_SOURCE_DIR}
    )

    target_link_libraries(hypercollider_instrument
        PRIVATE
            hypercollider_core
            sdk
            nlohmann_json::nlohmann_json
    )

    target_compile_definitions(hypercollider_instrument PRIVATE HC_PLUGIN_IS_INSTRUMENT=1)

    smtg_target_configure_version_file(hypercollider_instrument)

    if(SMTG_MAC)
        smtg_target_set_bundle(hypercollider_instrument
            BUNDLE_IDENTIFIER "io.hypercollider.HCPlugin.Instrument"
            COMPANY_NAME "HyperCollider"
        )
    endif()

    install(TARGETS hypercollider_instrument
        COMPONENT vst3_instrument
        RUNTIME DESTINATION bin
        BUNDLE DESTINATION .
        LIBRARY DESTINATION lib
    )
endif()

# Effect variant
if(BUILD_EFFECT)
    smtg_add_vst3plugin(hypercollider_effect ${PLUGIN_SOURCES})

    target_include_directories(hypercollider_effect
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${vst3sdk_SOURCE_DIR}
    )

    target_link_libraries(hypercollider_effect
        PRIVATE
            hypercollider_core
            sdk
            nlohmann_json::nlohmann_json
    )

    target_compile_definitions(hypercollider_effect PRIVATE HC_PLUGIN_IS_EFFECT=1)

    smtg_target_configure_version_file(hypercollider_effect)

    if(SMTG_MAC)
        smtg_target_set_bundle(hypercollider_effect
            BUNDLE_IDENTIFIER "io.hypercollider.HCPlugin.Effect"
            COMPANY_NAME "HyperCollider"
        )
    endif()

    install(TARGETS hypercollider_effect
        COMPONENT vst3_effect
        RUNTIME DESTINATION bin
        BUNDLE DESTINATION .
        LIBRARY DESTINATION lib
    )
endif()

# CLAP target
if(BUILD_CLAP)
    add_subdirectory(src/clap)
endif()

# Standalone target
if(BUILD_STANDALONE)
    if(APPLE)
        add_executable(hypercollider_standalone MACOSX_BUNDLE
            src/standalone/main_mac.mm
        )
    elseif(WIN32)
        add_executable(hypercollider_standalone WIN32
            src/standalone/main_win.cpp
        )
    elseif(UNIX AND NOT APPLE)
        add_executable(hypercollider_standalone
            src/standalone/main_linux.cpp
        )
    endif()

    target_link_libraries(hypercollider_standalone
        PRIVATE
            hypercollider_core
            nlohmann_json::nlohmann_json
    )

    target_include_directories(hypercollider_standalone
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
endif()
