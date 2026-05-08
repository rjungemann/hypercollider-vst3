#include "hclang_engine.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>

// Exported functions from hclang.wasm
static const char* kHcWasmEvalString = "hc_wasm_eval_string";
static const char* kHcWasmEvalBootSequence = "hc_wasm_eval_boot_sequence";
static const char* kHcWasmRender = "hc_wasm_render";
static const char* kHcWasmOscDispatch = "hc_wasm_osc_dispatch";

bool HCLangEngine::init(const Config& cfg) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = cfg;
    m_status = Status::Booting;

    // Initialize WAMR
    if (!m_lang.init()) {
        postLogLine("Failed to init WAMR for lang", true);
        m_status = Status::Error;
        return false;
    }
    if (!m_synth.init()) {
        postLogLine("Failed to init WAMR for synth", true);
        m_status = Status::Error;
        return false;
    }

    // Load WASM modules
    bool langLoaded = false;
    bool synthLoaded = false;

#if defined(HC_EMBEDDED_WASM)
    // Use embedded blobs
    if (!m_lang.load_embedded("hclang_wasm_blob", "hclang")) {
        postLogLine(std::string("Failed to load embedded hclang: ") + m_lang.get_error(), true);
    } else {
        langLoaded = true;
    }
    
    if (!m_synth.load_embedded("hcsynth_wasm_blob", "hcsynth")) {
        postLogLine(std::string("Failed to load embedded hcsynth: ") + m_synth.get_error(), true);
    } else {
        synthLoaded = true;
    }
#else
    // Load from file paths
    const char* langPath = std::getenv("HC_LANG_WASM");
    const char* synthPath = std::getenv("HC_SYNTH_WASM");

    if (!langPath) langPath = "hclang.wasm";
    if (!synthPath) synthPath = "hcsynth.wasm";

    if (!m_lang.load_file(langPath, "hclang")) {
        postLogLine(std::string("Failed to load hclang.wasm: ") + m_lang.get_error(), true);
    } else {
        langLoaded = true;
    }

    if (!m_synth.load_file(synthPath, "hcsynth")) {
        postLogLine(std::string("Failed to load hcsynth.wasm: ") + m_synth.get_error(), true);
    } else {
        synthLoaded = true;
    }
#endif

    if (!langLoaded || !synthLoaded) {
        m_status = Status::Error;
        return false;
    }

    // Setup WASI pre-opens for class library
    m_lang.add_wasi_dir(".", "/");
    m_synth.add_wasi_dir(".", "/");

    // Instantiate modules
    if (!m_lang.instantiate(131072, 64 * 1024 * 1024)) {
        postLogLine(std::string("Failed to instantiate hclang: ") + m_lang.get_error(), true);
        m_status = Status::Error;
        return false;
    }

    if (!m_synth.instantiate(131072, 64 * 1024 * 1024)) {
        postLogLine(std::string("Failed to instantiate hcsynth: ") + m_synth.get_error(), true);
        m_status = Status::Error;
        return false;
    }

    postLogLine("WASM modules loaded successfully");

    // Start the language thread
    m_langRunning = true;
    m_langThread = std::thread([this]() {
        postLogLine("Starting hclang boot sequence...");

        // Boot the class library
        wasm_function_inst_t bootFn = m_lang.lookup_function(kHcWasmEvalBootSequence);
        if (bootFn) {
            if (!wasm_runtime_call_wasm_v(m_lang.get_exec_env(), bootFn,
                                          0, nullptr, 0, nullptr)) {
                const char* exc = wasm_runtime_get_exception(m_lang.get_instance());
                postLogLine(std::string("Boot sequence failed: ") + (exc ? exc : "unknown"), true);
                m_status = Status::Error;
            } else {
                postLogLine("hclang boot sequence completed");
                m_status = Status::Ready;
            }
        } else {
            postLogLine("hc_wasm_eval_boot_sequence not found", true);
            m_status = Status::Error;
        }

        // For Phase 2: evaluate hardcoded SC code after boot
        const char* hardcodedCode = "{ SinOsc.ar(440, 0, 0.2) ! 2 }.play";
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_currentCode = hardcodedCode;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        evaluate(hardcodedCode);

        // Main loop: wait for commands (simple poll for Phase 2)
        while (m_langRunning) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    return true;
}

void HCLangEngine::shutdown() {
    m_langRunning = false;
    if (m_langThread.joinable()) {
        m_langThread.join();
    }
    m_lang.cleanup();
    m_synth.cleanup();
    m_status = Status::Error;
}

void HCLangEngine::render(const float* inputL, const float* inputR,
                         float* outputL, float* outputR,
                         uint32_t nframes) {
    // For Phase 2: call hcsynth render
    wasm_function_inst_t renderFn = m_synth.lookup_function(kHcWasmRender);
    if (!renderFn) {
        postLogLine("hc_wasm_render not found", true);
        std::memset(outputL, 0, nframes * sizeof(float));
        if (outputR) std::memset(outputR, 0, nframes * sizeof(float));
        return;
    }

    // Allocate buffer in WASM memory for output
    uint32_t bufferSize = nframes * sizeof(float) * 2; // stereo
    void* nativePtr = nullptr;
    uint32_t wasmPtr = m_synth.wasm_malloc(bufferSize, &nativePtr);

    if (!wasmPtr) {
        postLogLine("Failed to allocate WASM memory for output", true);
        std::memset(outputL, 0, nframes * sizeof(float));
        if (outputR) std::memset(outputR, 0, nframes * sizeof(float));
        return;
    }

    // Call render
    wasm_val_t args[1] = { wasm_val_t{ .of.i32 = static_cast<int32_t>(nframes) } };

    if (!wasm_runtime_call_wasm_v(m_synth.get_exec_env(), renderFn,
                                  0, nullptr, 1, args)) {
        const char* exc = wasm_runtime_get_exception(m_synth.get_instance());
        postLogLine(std::string("Render failed: ") + (exc ? exc : "unknown"), true);
        m_synth.wasm_free(wasmPtr);
        std::memset(outputL, 0, nframes * sizeof(float));
        if (outputR) std::memset(outputR, 0, nframes * sizeof(float));
        return;
    }

    // Copy output from WASM memory
    float* wasmOutput = static_cast<float*>(nativePtr);
    for (uint32_t i = 0; i < nframes; ++i) {
        outputL[i] = wasmOutput[i * 2];
        if (outputR) outputR[i] = wasmOutput[i * 2 + 1];
    }

    m_synth.wasm_free(wasmPtr);
}

void HCLangEngine::evaluate(std::string_view code) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_currentCode = code;
    }
    
    postLogLine(std::string("> ") + std::string(code));
    
    wasm_function_inst_t evalFn = m_lang.lookup_function(kHcWasmEvalString);
    if (!evalFn) {
        postLogLine("hc_wasm_eval_string not found", true);
        return;
    }

    // Allocate buffer for code in WASM memory
    uint32_t codeSize = static_cast<uint32_t>(code.size());
    void* nativeCodePtr = nullptr;
    uint32_t wasmCodePtr = m_lang.wasm_malloc(codeSize + 1, &nativeCodePtr);

    if (!wasmCodePtr) {
        postLogLine("Failed to allocate WASM memory for code", true);
        return;
    }

    // Copy code to WASM memory
    std::memcpy(nativeCodePtr, code.data(), codeSize);
    static_cast<char*>(nativeCodePtr)[codeSize] = '\0';

    // Call evaluate
    wasm_val_t args[2] = {
        wasm_val_t{ .of.i32 = static_cast<int32_t>(wasmCodePtr) },
        wasm_val_t{ .of.i32 = static_cast<int32_t>(codeSize) }
    };

    if (!wasm_runtime_call_wasm_v(m_lang.get_exec_env(), evalFn,
                                  0, nullptr, 2, args)) {
        const char* exc = wasm_runtime_get_exception(m_lang.get_instance());
        postLogLine(std::string("Eval failed: ") + (exc ? exc : "unknown"), true);
    } else {
        postLogLine(std::string("Evaluated: ") + std::string(code));
    }

    m_lang.wasm_free(wasmCodePtr);
}

void HCLangEngine::stop() {
    // Send stop message - for Phase 2, just evaluate a stop command
    postLogLine("Stopping all synths...");
    evaluate("Synth.allFree;");
}

bool HCLangEngine::pollLogLine(std::string& out) {
    std::lock_guard<std::mutex> lock(m_logMutex);
    if (m_logQueue.empty()) {
        return false;
    }
    out = m_logQueue.front().text;
    m_logQueue.pop();
    return true;
}

void HCLangEngine::postLogLine(const std::string& line, bool isError) {
    std::lock_guard<std::mutex> lock(m_logMutex);
    m_logQueue.push({line, isError});
}

void HCLangEngine::postMidiEvent(const MidiEvent& event) {
    std::lock_guard<std::mutex> lock(m_midiMutex);
    m_midiQueue.push(event);
    // TODO: Signal the lang thread to process MIDI events
    // For now, MIDI events are queued but not processed
}
