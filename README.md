# HCPlugin - HyperCollider VST3/CLAP Plugin

A VST3 and CLAP plugin that embeds the HyperCollider language (hclang) and synthesis engine (hcsynth) directly inside a plugin binary using WAMR.

## Status

This repository implements **Phases 1-2** of the HC_PLUGIN_PLAN.md:
- Phase 1: CMake scaffolding + WasmHost integration
- Phase 2: Audio rendering with hardcoded SC code

## Building

### Prerequisites

- CMake 3.25+
- Ninja
- C++17 compiler (clang, gcc, or MSVC)
- WAMR (fetched automatically via FetchContent)
- VST3 SDK (fetched automatically via FetchContent)

### Quick Start

```bash
# Clone this repo
cd hypercollider-vst3

# Set environment variables to point at your WASM builds
export HC_LANG_WASM=$(realpath ../hypercollider/build/wasi/lang/hclang/hclang.wasm)
export HC_SYNTH_WASM=$(realpath ../hypercollider/build/wasi/server/hcsynth/hcsynth.wasm)

# Configure and build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Install to system VST3 location
cmake --install build --component vst3_instrument
```

Or use `just`:
```bash
just dev
```

### Options

- `-DBUILD_INSTRUMENT=ON` - Build instrument variant (default: ON)
- `-DBUILD_EFFECT=ON` - Build effect variant (default: OFF)
- `-DBUILD_CLAP=ON` - Build CLAP plugin (default: OFF)
- `-DBUILD_IMGUI_UI=ON` - Build with Dear ImGui UI (default: OFF)
- `-DBUILD_STANDALONE=ON` - Build standalone test app (default: OFF)
- `-DHC_EMBED_WASM=ON` - Embed WASM blobs in binary (default: OFF)

## Project Structure

```
hypercollider-vst3/
  CMakeLists.txt          # Main build configuration
  Justfile                # Build commands
  README.md
  cmake/
    EmbedWasm.cmake       # CMake helper for WASM embedding
    EmbedWasmRun.cmake
  src/
    core/
      wasm_host.h/cpp     # WAMR wrapper
      hclang_engine.h/cpp  # HCLangEngine implementation
    plugin/
      processor.h/cpp     # VST3 AudioEffect
      controller.h/cpp     # VST3 EditController
      entry.cpp            # Plugin factory
      ids.h                # Plugin UIDs
      version.h           # Version info
```

## Phase 2: What Works

- VST3 instrument plugin loads in DAWs (Reaper, etc.)
- WAMR initializes and loads hclang.wasm + hcsynth.wasm from disk
- Hardcoded SC code `"{ SinOsc.ar(440, 0, 0.2) ! 2 }.play"` is evaluated
- Audio renders through hcsynth
- 440Hz sine tone should be audible

## Next Steps

- Phase 3: Add Dear ImGui UI (editor, log pane)
- Phase 4: Add preset management
- Phase 5: Add VST3 state serialization
- Phase 6: Add CLAP support
- Phase 7: Add WASM embedding for self-contained binary
- Phase 8: Add MIDI input support
- Phase 9: Add CI for cross-platform builds
