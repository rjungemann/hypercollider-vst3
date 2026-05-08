#pragma once

#include "wasm_host.h"
#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class HCLangEngine {
public:
    struct Config {
        uint32_t sampleRate   { 48000 };
        uint32_t maxBlockSize { 512   };
        bool     isEffect     { false }; // true -> 2-in/2-out
    };

    // Lifecycle
    bool init(const Config& cfg);
    void shutdown();

    // Called on the audio thread each block.
    // inputL/R may be nullptr for the instrument variant.
    void render(const float* inputL, const float* inputR,
                float* outputL, float* outputR,
                uint32_t nframes);

    // Called from the UI / main thread.
    void evaluate(std::string_view code);
    void stop();

    // Log access (thread-safe)
    bool pollLogLine(std::string& out);
    void postLogLine(const std::string& line, bool isError = false);

    const std::string& currentCode() const { return m_currentCode; }
    void setCode(std::string_view code) { 
        std::lock_guard<std::mutex> lock(m_mutex); 
        m_currentCode = code; 
    }

    const Config& getConfig() const { return m_config; }

    // MIDI event handling
    struct MidiEvent {
        uint8_t status;  // MIDI status byte (e.g., 0x90 = note on, 0x80 = note off)
        uint8_t channel; // MIDI channel (0-15)
        uint8_t note;    // Note number (0-127)
        uint8_t velocity; // Velocity (0-127)
    };
    void postMidiEvent(const MidiEvent& event);

    // Status
    enum class Status { Booting, Ready, Error };
    Status getStatus() const { return m_status; }

private:
    WasmHost m_lang;   // hclang WASI module
    WasmHost m_synth;  // hcsynth WASI module

    // background lang thread
    std::thread m_langThread;
    std::atomic<bool> m_langRunning { false };

    // Log queue
    struct LogEntry {
        std::string text;
        bool isError;
    };
    std::queue<LogEntry> m_logQueue;
    mutable std::mutex m_logMutex;

    std::string m_currentCode;
    Config m_config;
    std::mutex m_mutex;
    std::atomic<Status> m_status { Status::Booting };

    // MIDI event queue (UI/main thread -> lang thread)
    std::queue<MidiEvent> m_midiQueue;
    mutable std::mutex m_midiMutex;
};
