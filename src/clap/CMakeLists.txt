# CLAP plugin target - Phase 6

if(NOT BUILD_CLAP)
    return()
endif()

message(STATUS "Building CLAP plugin (Phase 6)")

# Set the plugin name
set(CLAP_PLUGIN_NAME hypercollider_clap)

# CLAP source files
set(CLAP_SOURCES
    clap_entry.cpp
    clap_plugin.h
    clap_plugin.cpp
    clap_audio_ports.h
    clap_audio_ports.cpp
    clap_params.h
    clap_params.cpp
    clap_state.h
    clap_state.cpp
)

# Create CLAP plugin shared library (module)
add_library(${CLAP_PLUGIN_NAME} MODULE ${CLAP_SOURCES})

# Enable C++17
set_target_properties(${CLAP_PLUGIN_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_VISIBILITY_PRESET hidden
)

# Include directories
target_include_directories(${CLAP_PLUGIN_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../
        ${CLAP_INCLUDE_DIRS}
        ${vst3sdk_SOURCE_DIR}
        ${nlohmann_json_SOURCE_DIR}/include
)

# Link libraries
target_link_libraries(${CLAP_PLUGIN_NAME}
    PRIVATE
        hypercollider_core
        ${CMAKE_DL_LIBS}
)

# macOS-specific settings
if(APPLE)
    target_link_libraries(${CLAP_PLUGIN_NAME} PRIVATE "-framework Foundation" "-framework CoreFoundation")
    set_target_properties(${CLAP_PLUGIN_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        BUNDLE_EXTENSION clap
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist
    )
endif()

# Set output directory and bundle name
set_target_properties(${CLAP_PLUGIN_NAME} PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/CLAP/${CMAKE_BUILD_TYPE}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/CLAP/${CMAKE_BUILD_TYPE}
    OUTPUT_NAME "HCPlugin"
)

# On macOS, rename .so to .clap
if(APPLE)
    set_target_properties(${CLAP_PLUGIN_NAME} PROPERTIES
        SUFFIX ".clap"
    )
endif()

# Install target
install(TARGETS ${CLAP_PLUGIN_NAME}
    COMPONENT clap
    LIBRARY DESTINATION .
    RUNTIME DESTINATION .
    BUNDLE DESTINATION .
)
