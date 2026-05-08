# HCPlugin build commands

# Default: build instrument variant
build:
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j$(nproc)

# Build with development WASM paths
dev:
    cmake -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DHC_LANG_WASM_PATH=$HC_LANG_WASM \
        -DHC_SYNTH_WASM_PATH=$HC_SYNTH_WASM \
        -DHC_CLASSLIB_PACK=$HC_CLASSLIB_PACK
    cmake --build build -j$(nproc)

# Build release with embedded WASM
release:
    cmake -B build_release -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DHC_EMBED_WASM=ON \
        -DHC_LANG_WASM_PATH=$HC_LANG_WASM \
        -DHC_SYNTH_WASM_PATH=$HC_SYNTH_WASM \
        -DHC_CLASSLIB_PACK=$HC_CLASSLIB_PACK
    cmake --build build_release -j$(nproc)

# Install VST3
install:
    cmake --install build --component vst3_instrument

# Clean
clean:
    rm -rf build build_release
