# HyperCollider VST3 / CLAP Plugin Plan

**Date**: 2026-05-08  
**Target platforms**: macOS arm64/x86_64, Windows x86_64, Linux x86_64  
**Plugin formats**: VST3 + CLAP (same shared core, thin wrappers)  
**Engine**: WAMR-embedded hclang + hcsynth (from `native/`)

---

## Overview

`hcsynth_plugin` embeds the WAMR-hosted hclang language engine and hcsynth audio
engine directly inside a standard plugin binary. No separate process or server is
required. The user sees a code editor and preset browser that mirrors the browser
IDE (`src/platform/test/sc_ide.html`).

Two plugin variants share one CMake target tree:

| Variant | VST3 category | CLAP category | Audio I/O |
|---|---|---|---|
| **Instrument** | `kInstrumentSynth` | `CLAP_PLUGIN_FEATURE_INSTRUMENT` | 0 in / 2 out |
| **Effect** | `kFx` | `CLAP_PLUGIN_FEATURE_AUDIO_EFFECT` | 2 in / 2 out |

SC code determines which variant makes sense — `Synth("kick")` works in both, but
`FreeVerb.ar(in)` only makes sense in the effect variant where the host feeds audio
into the input buses.

---

## Repository layout

The plugin lives in a new sibling directory `hcsynth-vst3/` (parallel to
`flashkick-vst3/`, `chiaro-vst3/`, `combover-vst3/`):

```
hcsynth-vst3/
  CMakeLists.txt
  Justfile
  README.md
  presets/
    factory/
      Init.json           ← { name, code, synthDefName, params }
      SineTone.json
      FMBass.json
  src/
    core/
      hclang_engine.h     ← WasmHost wrapper for hclang + hcsynth
      hclang_engine.cpp
      preset_manager.h    ← load/save/list JSON presets
      preset_manager.cpp
      plugin_state.h      ← current code text, active preset name
      socket_logger.h     ← UDP debug log (port 9999, matches combover)
    plugin/               ← VST3 wrappers
      processor.h / .cpp  ← AudioEffect or SingleComponentEffect
      controller.h / .cpp ← EditController
      entry.cpp
      ids.h
      version.h
    clap/                 ← CLAP wrappers
      clap_plugin.h / .cpp
      clap_process.h / .cpp
      clap_state.h / .cpp
      clap_params.h / .cpp
      clap_param_access.h / .cpp
      clap_gui.h / .cpp
      clap_gui_cocoa.mm
      clap_gui_win32.cpp
      CMakeLists.txt
      Info.plist
    ui/
      editor_core.h / .cpp   ← Dear ImGui UI (platform-independent draw logic)
      imgui_editor.h
      imgui_editor_mac.mm    ← NSView-hosted ImGui context (macOS)
      imgui_editor_win32.cpp ← HWND-hosted ImGui context (Windows)
      imgui_editor_linux.cpp ← X11/XCB-hosted ImGui context (Linux)
      imgui_style.h          ← dark theme matching the browser IDE palette
      imgui_fonts.h          ← embedded DM Sans + Share Tech Mono + Material Icons
      syntax_highlight.h     ← SC/SCSCM syntax highlight table for ImGui
    fonts/
      DmSans-*.ttf + *_ttf.h
      ShareTechMono-Regular.ttf + *_ttf.h
      MaterialIcons-Regular.ttf + *_ttf.h
      FiraCode-Regular.ttf + *_ttf.h   ← monospace for the editor
      embed_fonts.py
    standalone/
      main_mac.mm
      main_win.cpp
      main_linux.cpp
      runtime.h / .cpp      ← miniaudio real-time audio for standalone testing
      midi_input.h / .cpp
      debug_server.h / .cpp
  tests/
    test_preset_roundtrip.cpp
    test_hclang_engine.cpp
    smoke_test.py
  docs/
    HC_PLUGIN_PLAN.md       ← (link back to hypercollider repo)
```

The plugin reuses `native/hclang_host/wasm_host.{h,cpp}` and
`native/cmake/EmbedWasm.cmake` directly from the hypercollider repo (via a CMake
`add_subdirectory` or by copying the two files into `src/core/`).

---

## Architecture

### Threading model

```
┌───────────────────────────────────────────────────────────────┐
│ Plugin process                                                │
│                                                               │
│  ┌──────────────────┐        OSC/UDP loopback (127.0.0.1)    │
│  │  hclang thread   │◄──────────────────────────────────────┐│
│  │  (background)    │  hc_wasm_eval_string                  ││
│  │  WasmHost lang   │──→ sends /d_recv, /s_new, /n_set OSC ─┤│
│  └──────────────────┘                                        ││
│                                                               ││
│  ┌──────────────────┐                                        ││
│  │  hcsynth thread  │◄──────────────────────────────────────┘│
│  │  (audio thread)  │  hc_wasm_render(nframes)                │
│  │  WasmHost synth  │──→ float* output buf → host            │
│  └──────────────────┘                                        │
│                                                               │
│  ┌──────────────────┐                                        │
│  │  UI thread       │  Dear ImGui (editor + log + presets)   │
│  └──────────────────┘                                        │
└───────────────────────────────────────────────────────────────┘
```

- **hclang thread** — started once at `initialize()` / `activate()`. Boots the
  class library (from the embedded pack), then sits idle until the user hits
  Evaluate or loads a preset. Writes log lines to a ring buffer read by the UI.
- **hcsynth thread** — the audio thread. Calls `hc_wasm_render(nframes)` each
  block and dequeues inbound OSC packets into `hc_wasm_osc_dispatch` before each
  render call (identical to the `hcsynth_native` offline render loop in Phase 7).
- **UI thread** — draws the editor, log panel, preset list, and transport buttons
  via Dear ImGui. No WASM calls happen here; UI actions post to a command queue
  consumed by the hclang thread.

The two WAMR instances each run their own `wasm_exec_env_t` and share no WAMR
state. The only inter-thread channel between hclang and hcsynth is the UDP socket
pair (loopback, ephemeral port), the same mechanism as the bench pipeline.

### `HCLangEngine` (`src/core/hclang_engine.{h,cpp}`)

The central object owned by the `Processor` (VST3) or `QEchoClapPlugin`-equivalent:

```cpp
class HCLangEngine {
public:
    struct Config {
        uint32_t sampleRate    { 48000 };
        uint32_t maxBlockSize  { 512   };
        bool     isEffect      { false }; // true → 2-in/2-out
    };

    // Lifecycle
    bool init(const Config&);
    void shutdown();

    // Called on the audio thread each block.
    // inputL/R may be nullptr for the instrument variant.
    void render(const float* inputL, const float* inputR,
                float* outputL, float* outputR,
                uint32_t nframes);

    // Called from the UI / main thread.
    void evaluate(std::string_view code);  // posts to command queue
    void stop();                           // sends /n_free 0 + /g_freeAll

    // Preset management
    void loadPreset(const PresetInfo&);

    // Log ring buffer (lock-free, read by UI thread)
    bool pollLogLine(std::string& out);

    // State serialisation (VST3 setState / CLAP state save)
    std::vector<uint8_t> saveState() const;
    bool                 loadState(const std::vector<uint8_t>&);

    const std::string& currentCode() const;
    void               setCode(std::string_view);

private:
    WasmHost      m_lang;   // hclang WASI module
    WasmHost      m_synth;  // hcsynth WASI module

    // background lang thread
    std::thread              m_langThread;
    std::atomic<bool>        m_langRunning { false };
    // command queue: UI → lang thread
    moodycamel::ConcurrentQueue<std::string> m_cmdQueue;

    // log ring: hc_host_post callback → UI thread
    moodycamel::ConcurrentQueue<std::string> m_logQueue;

    // OSC bridge: lang → synth
    int m_synthOscPort { 0 };  // ephemeral loopback UDP port
    int m_udpSendSock  { -1 };
    int m_udpRecvSock  { -1 };

    std::string m_currentCode;
    Config      m_config;
};
```

### Preset format

Presets are the same JSON format used by the browser IDE and CLI preset manager:

```json
{
  "name": "SineTone",
  "code": "{ SinOsc.ar(440, 0, 0.2) ! 2 }.play",
  "synthDefName": "SineTone",
  "params": {},
  "createdAt": 1715000000000
}
```

`params` is optional metadata (shown in the UI but not auto-mapped to VST3
parameters — SC controls its own state). The plugin has a small set of
host-visible DAW automation parameters separate from SC state: **Master Volume**
and **Bypass** for the effect variant.

Factory presets are embedded in the binary via a `CMakeLists.txt` hex blob (same
pattern as `EmbedWasm.cmake`). User presets are stored in:
- macOS: `~/Library/Application Support/HCPlugin/presets/`
- Windows: `%APPDATA%\HCPlugin\presets\`
- Linux: `~/.local/share/HCPlugin/presets/`

---

## UI design (`src/ui/editor_core.cpp`)

The Dear ImGui panel mirrors the browser IDE's layout as closely as possible,
adapted for a fixed plugin window (default 900 × 600, resizable).

```
┌────────────────────────────────────────────────────────────┐
│ [▶ Evaluate]  [■ Stop]  │  Preset: [dropdown ▼] [Save] [+]│ ← toolbar
├────────────────────────────────────────────────────────────┤
│                                                            │
│  SC code editor (syntax-highlighted, monospace font)       │ ← editor
│  - line numbers                                            │
│  - bracket matching highlight                              │
│  - SC / SCSCM keywords coloured (teal/orange/purple)       │
│                                                            │
├────────────────────────────────────────────────────────────┤
│ ● Ready  │  hclang 0.1.0  │  48000 Hz  │ [×] Clear log    │ ← status bar
├────────────────────────────────────────────────────────────┤
│ > SinOsc: booted                                           │
│ > a SynthDef                                               │ ← log panel
│ ! error: undefined method 'fooo'                           │   (scrollable)
└────────────────────────────────────────────────────────────┘
```

### Colour palette (matches `sc_ide.html`)

| Token | Colour |
|---|---|
| Background | `#1e1e1e` |
| Toolbar / status bar | `#333333` |
| Editor background | `#1e1e1e` |
| Default text | `#e0e0e0` |
| Keywords (`SinOsc`, `SynthDef`, class names) | `#4ec9b0` (teal) |
| Numbers | `#b5cea8` (sage green) |
| Strings | `#ce9178` (warm orange) |
| Comments | `#6a9955` (muted green) |
| Symbols (`\kick`) | `#c586c0` (mauve) |
| Log normal line | `#e0e0e0` |
| Log error line | `#f44747` (red) |
| Evaluate button | `#0e639c` (VS Code blue) |
| Stop button | `#8b3e3e` (dark red) |

These map directly to Dear ImGui `ImVec4` values in `imgui_style.h`.

### Syntax highlighting (`src/ui/syntax_highlight.h`)

ImGui has no built-in syntax highlighter. The editor uses a simple line-scan
approach: each frame, the visible portion of the code text is tokenised with a
state machine that recognises:

- SC class names (start with uppercase letter) → teal
- SC keywords: `var`, `arg`, `this`, `true`, `false`, `nil`, `inf`, `if`, `while`,
  `do`, `collect`, `select`, `reject`, `detect` → teal (dimmer shade)
- Numbers (int / float / hex) → sage green
- Double-quoted strings → warm orange
- Single-quoted symbols → mauve
- Line comments `//` and block comments `/* */` → muted green
- `\symbol` notation → mauve

For a richer editing experience (bracket matching, multi-cursor, undo stack),
replace the single `ImGui::InputTextMultiline` with an integration of
**[ImGuiColorTextEdit](https://github.com/BalazsJako/ImGuiColorTextEdit)** — a
well-maintained C++ syntax-coloured editor widget that works inside any ImGui
context. It supports:
- Configurable token→colour language definitions
- Undo/redo
- Find + replace
- Line numbers
- Read-only mode

Add it via `FetchContent_Declare` in `CMakeLists.txt`:
```cmake
FetchContent_Declare(
    imguicolortextedit
    GIT_REPOSITORY https://github.com/BalazsJako/ImGuiColorTextEdit.git
    GIT_TAG master GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imguicolortextedit)
```

---

## Phases

### Phase 1 — CMake scaffolding + WasmHost integration

**Goal**: build a VST3 bundle on macOS that links WAMR and loads `hclang.wasm` /
`hcsynth.wasm` from disk (no embedding yet).

#### 1.1 — Repository skeleton

Copy the `CMakeLists.txt` from `flashkick-vst3/` as the starting template.
Replace the `qecho_core` target with `hypercollider_core` that links:
- WAMR `vmlib` (via `FetchContent` pointing at the same WAMR version as
  `native/CMakeLists.txt`)
- `nlohmann_json` (preset serialisation)
- A shared `hypercollider_wasm_host` static library built from
  `native/hclang_host/wasm_host.{h,cpp}` (copied or referenced via relative
  path).

Options:
```cmake
option(HC_EMBED_WASM    "Embed hclang.wasm + hcsynth.wasm into the binary" OFF)
option(BUILD_INSTRUMENT "Build instrument variant"  ON)
option(BUILD_EFFECT     "Build effect variant"      ON)
option(BUILD_CLAP       "Build CLAP variants"       OFF)
option(BUILD_IMGUI_UI   "Build Dear ImGui UI"       ON)
option(BUILD_STANDALONE "Build standalone test app" ON)
```

#### 1.2 — `HCLangEngine` stub

Implement `init()` to load `hclang.wasm` + `hcsynth.wasm` from file paths passed
via environment variables or CMake defines during development:
```cpp
bool HCLangEngine::init(const Config& cfg) {
    if (!m_lang.init())  return false;
    if (!m_synth.init()) return false;
    const char* langPath  = getenv("HC_LANG_WASM")  ?: "hclang.wasm";
    const char* synthPath = getenv("HC_SYNTH_WASM") ?: "hcsynth.wasm";
    if (!m_lang.load_file(langPath))   return false;
    if (!m_synth.load_file(synthPath)) return false;
    // ... WASI pre-opens for class library ...
    return true;
}
```

#### 1.3 — VST3 `Processor` skeleton

Based on `flashkick-vst3/src/plugin/processor.{h,cpp}`. For the instrument
variant: inherit from `Steinberg::Vst::AudioEffect`, declare a stereo output bus
and an event input bus. For the effect variant: also declare a stereo input bus.

`process()` calls `m_engine.render(inputL, inputR, outputL, outputR, nframes)`.

**Acceptance**: plugin loads in a VST3 host (e.g. Reaper) without crashing.
hclang + hcsynth WASM modules load and initialise. No audio yet.

---

### Phase 2 — Audio rendering (hcsynth on the audio thread)

**Goal**: evaluate `{ SinOsc.ar(440, 0, 0.2) ! 2 }.play` from a hardcoded string
and hear audio in the host.

#### 2.1 — hcsynth audio thread setup

`activate(sampleRate, minBlockSize, maxBlockSize)` calls `m_engine.init(cfg)`.
`deactivate()` calls `m_engine.shutdown()`.

In `render()`:
1. Drain the inbound OSC ring (packets enqueued by hclang) into
   `hc_wasm_osc_dispatch` — same pattern as `hcsynth_native` Phase 7.
2. Call `hc_wasm_render(nframes)` on the hcsynth instance.
3. Copy the WASM output buffer (obtained via `wasm_host.addr_to_native`) into the
   host's output buffers.

#### 2.2 — hclang language boot + loopback OSC

On `init()`:
1. Open a UDP socket pair on `127.0.0.1:0` (OS assigns ephemeral port) for lang→synth.
2. Register `hc_host_osc_send` native symbol pointing at `sendto()` on that socket.
3. Start the background lang thread. The thread:
   a. Boots the class library (`hc_wasm_eval_boot_sequence`).
   b. Loops waiting for commands from `m_cmdQueue`.
   c. Calls `hc_wasm_eval_string(code)` for each command.
   d. On any `hc_host_post` / `hc_host_error` callback, pushes the line to `m_logQueue`.

#### 2.3 — Hardcoded evaluate

After boot, push `"{ SinOsc.ar(440, 0, 0.2) ! 2 }.play"` to `m_cmdQueue` and
confirm audio arrives in the host.

**Acceptance**: sine tone at 440 Hz heard in the DAW with no UI yet.

---

### Phase 3 — Dear ImGui UI

**Goal**: editor window opens with a code text area, Evaluate / Stop buttons, and
a scrolling log pane.

#### 3.1 — Platform editor setup

Follow the `flashkick-vst3` pattern:
- `imgui_editor_mac.mm` — creates an `NSView`-hosted `MTKView` (Metal) or
  `NSOpenGLView` (OpenGL) and sets up an `ImGui_ImplMetal_*` or `ImGui_ImplOSX_*`
  backend.
- `imgui_editor_win32.cpp` — creates a child `HWND` with a D3D11 or OpenGL3
  context.
- `imgui_editor_linux.cpp` — creates an X11 window child with OpenGL.

These files are nearly identical to their counterparts in `flashkick-vst3/` and
`combover-vst3/` — copy and adapt.

#### 3.2 — `EditorCore::draw()`

```cpp
void EditorCore::draw(HCLangEngine& engine, float w, float h,
                      const char* buildHash) const;
```

Layout:
1. **Toolbar** (`ImGui::BeginMenuBar()` or a custom top bar at fixed height 38px):
   - `[▶ Evaluate]` button (blue) — calls `engine.evaluate(state_.codeText)`
   - `[■ Stop]` button (dark red) — calls `engine.stop()`
   - Separator
   - Preset dropdown (`ImGui::Combo`) showing `presetUiState_.presetNames`
   - `[Save]` button — saves current code as a new preset (opens name dialog)
   - `[+]` button — opens "new file" (clears editor to empty)
2. **Editor pane** (resizable, takes most of the vertical space):
   - `TextEditor` from ImGuiColorTextEdit, configured with the SC language
     definition from `syntax_highlight.h`. Bound to `state_.codeText`.
3. **Status bar** (fixed 24px at bottom of editor, above log):
   - Status LED (green = ready, orange = booting, red = error)
   - Engine status string (`"Booting..."` / `"Ready"` / `"Error"`)
   - Sample rate pill
   - `[× Clear]` button
4. **Log pane** (fixed ~140px, below editor):
   - Scrollable `ImGui::InputTextMultiline` (read-only) or custom `ImGui::Text`
     loop pulling from `logLines_`.
   - Auto-scroll when new lines arrive.
   - Error lines shown in red (`ImGui::PushStyleColor(ImGuiCol_Text, red)`).

#### 3.3 — Log polling

Each frame, `EditorCore::draw()` calls `engine.pollLogLine()` in a loop (drains
the ring buffer) and appends to `logLines_` (a `std::deque<LogEntry>` capped at
500 lines). `LogEntry` carries the text and a boolean `isError` flag.

**Acceptance**: plugin window opens; typing in the editor and pressing Evaluate
runs the code and prints output to the log pane.

---

### Phase 4 — Preset manager

**Goal**: presets load from disk / embedded factory presets; users can save and
delete presets from the UI.

#### 4.1 — `PresetManager` (`src/core/preset_manager.{h,cpp}`)

Mirrors `flashkick-vst3/src/core/preset_manager.{h,cpp}`:

```cpp
class PresetManager {
public:
    struct PresetInfo {
        std::string name;
        std::string path;         // empty for factory
        bool isFactory { false };
        std::string code;
        std::string synthDefName;
    };

    explicit PresetManager();

    std::vector<PresetInfo> list()    const;
    PresetInfo              load(const std::string& name) const;
    bool                    save(const std::string& name,
                                 const std::string& code,
                                 const std::string& synthDefName = "");
    bool                    remove(const std::string& name);

    // Factory presets embedded as a hex blob in the binary.
    static std::vector<PresetInfo> builtins();

private:
    std::filesystem::path userDir_;
};
```

JSON round-trip uses `nlohmann::json`. The `code` field holds the full SC text.

#### 4.2 — Factory preset embedding

`CMakeLists.txt` iterates over `presets/factory/*.json` and generates a C array
via a small `cmake -P` script (same mechanism as `EmbedWasm.cmake`). The array is
linked into `hypercollider_core` and parsed at startup by `PresetManager::builtins()`.

#### 4.3 — UI wiring

- Preset dropdown populates from `PresetManager::list()` (factory first, then
  user presets in alphabetical order).
- Selecting a preset calls `engine.loadPreset(info)` which pushes the code to the
  editor text area and calls `engine.evaluate(info.code)`.
- `[Save]` opens a name-entry dialog (`ImGui::InputText` modal). On confirm calls
  `PresetManager::save(name, engine.currentCode())` and refreshes the list.
- `[Delete]` appears on hover next to user presets in the dropdown (not factory
  presets). Calls `PresetManager::remove(name)`.

**Acceptance**: shipping with three factory presets (`Init`, `SineTone`,
`FMBass`). User can save a custom preset, reload it after closing and reopening
the plugin.

---

### Phase 5 — VST3 state serialisation + CLAP state

**Goal**: DAW session save/restore preserves the current SC code and active preset
name.

#### 5.1 — VST3 `setState` / `getState`

Serialise to a compact JSON blob:
```json
{
  "version": 1,
  "code": "{ SinOsc.ar(440, 0, 0.2) ! 2 }.play",
  "presetName": "SineTone",
  "masterVolume": 1.0,
  "bypass": false
}
```

`getState` → `nlohmann::json::dump()` → write to `IBStream`.
`setState` → read from `IBStream` → `nlohmann::json::parse()` → apply to engine.

On `setState`, the engine re-evaluates the code automatically (same as loading a
preset).

#### 5.2 — CLAP state

`clap_state.{h,cpp}` follows the pattern from `flashkick-vst3/src/clap/clap_state.{h,cpp}`.
Uses the same JSON blob as VST3.

**Acceptance**: save a session in Reaper with custom SC code, close and reopen
Reaper, the code is restored and audio resumes on playback.

---

### Phase 6 — CLAP plugin build

**Goal**: parallel CLAP target (`-DBUILD_CLAP=ON`) builds a `.clap` bundle
alongside the `.vst3`.

Follow the `flashkick-vst3/src/clap/` pattern verbatim:

- `clap_entry.cpp` — `clap_entry` symbol, `CLAP_PLUGIN_DESCRIPTOR`
- `clap_plugin.{h,cpp}` — `create()`/`destroy()`, extension dispatch
- `clap_process.{h,cpp}` — `impl_process()` calls `engine.render()`
- `clap_params.{h,cpp}` — Master Volume + Bypass (i32)
- `clap_state.{h,cpp}` — `save()`/`load()` using the same JSON blob as VST3
- `clap_gui.{h,cpp}` + platform `.mm`/`.cpp` — same Dear ImGui setup as VST3

**Acceptance**: `.clap` bundle loads in Reaper / Bitwig / Clap-Info validator.
Audio and UI work identically to the VST3 build.

---

### Phase 7 — WASM embedding (self-contained binary)

**Goal**: `-DHC_EMBED_WASM=ON` bakes `hclang.wasm`, `hcsynth.wasm`, and the class
library pack into the plugin binary so no external files are needed.

Reuse `native/cmake/EmbedWasm.cmake` / `EmbedWasmRun.cmake` verbatim:

```cmake
if(HC_EMBED_WASM)
    embed_wasm(hypercollider_core "${HC_LANG_WASM_PATH}"   hclang_wasm_blob)
    embed_wasm(hypercollider_core "${HC_SYNTH_WASM_PATH}"  hcsynth_wasm_blob)
    embed_wasm(hypercollider_core "${HC_CLASSLIB_PACK}"    hclang_classlib_pack_blob)
    target_compile_definitions(hypercollider_core PRIVATE
        HC_EMBEDDED_WASM=1 HC_EMBEDDED_CLASSLIB_PACK=1)
endif()
```

`HCLangEngine::init()` switches between `load(embedded_blob, blob_size)` and
`load_file(path)` depending on `HC_EMBEDDED_WASM`.

For the class library, extract the pack to `mkdtemp()` (same as Phase 9 of the
native host) and pass the temp dir as the WASI pre-open. Temp dir is deleted in
`HCLangEngine::shutdown()`.

**Acceptance**: copy the `.vst3` bundle to another machine with no hypercollider
install, load it in a DAW — audio works.

---

### Phase 8 — Instrument variant MIDI input

**Goal**: the instrument variant responds to MIDI note-on / note-off events from
the host, forwarding them as SC `MIDIIn` events into the running hclang instance.

#### 8.1 — VST3 event input bus

Add `addEventInput("MIDI In", 1)` in `Processor::initialize()` for the instrument
variant. In `process()`, iterate `data.inputEvents` and dispatch each note event.

#### 8.2 — MIDI → SC bridge

Register a `hc_host_midi_event` native symbol in the hclang WAMR instance:

```cpp
// NativeSymbol: hc_host_midi_event(status, note, vel)
// Pushes a MIDI event into the MIDIIn dispatch queue inside hclang.
```

In `wasm_runtime_bridge.cpp` (WASI path), add `hc_midi_event_in(int status,
int note, int vel)` — calls `MIDIIn::doAction(type, channel, note, vel)` on the
running interpreter. This mirrors what `hc_midi.js` does in the Node.js CLI.

**Acceptance**: create a simple SC instrument (`MIDIIn.connectAll;
MIDIdef.noteOn(\play, { |vel, note| Synth("default", [\freq, note.midicps]) })`),
press keys in the DAW piano roll, hear notes.

---

### Phase 9 — CI + cross-platform builds

**Goal**: GitHub Actions matrix builds `vst3-instrument`, `vst3-effect`,
`clap-instrument`, `clap-effect` on macOS arm64, macOS x86_64, Windows x86_64,
Linux x86_64.

```yaml
# .github/workflows/plugin-build.yml
matrix:
  os: [macos-14, macos-13, windows-2022, ubuntu-22.04]
  variant: [vst3, clap]
```

Each job:
1. Checks out hypercollider (for WASM blobs + cmake helpers) and hcsynth-vst3.
2. Restores WASM build artifacts from cache (keyed on `src/lang/**` hash,
   same as `native-host.yml`).
3. Runs CMake + ninja with `-DHC_EMBED_WASM=ON`.
4. Runs a smoke test: load the `.vst3` / `.clap` in `clap-validator` or
   `vstplugintest`, assert no crash.

**Not in scope for Phase 9**: code signing / notarisation (macOS), PACE iLok,
Windows code signing. These are distribution concerns, not CI concerns.

---

## Effect vs instrument distinction

The two variants are compiled from the same `hypercollider_core` library with a
`#define HC_PLUGIN_IS_EFFECT 0/1` switch:

| Aspect | Instrument | Effect |
|---|---|---|
| VST3 category | `kInstrumentSynth` | `kFx` |
| CLAP features | `CLAP_PLUGIN_FEATURE_INSTRUMENT` | `CLAP_PLUGIN_FEATURE_AUDIO_EFFECT` |
| Audio input bus | none | stereo |
| Audio output bus | stereo | stereo |
| MIDI input | yes | optional |
| hcsynth bus config | `hc_wasm_eval_boot_sequence` boots with output-only | boots with input+output |
| Bypass parameter | no | yes |

For the effect variant, `render()` copies the host input buffer into the WASM
input bus before calling `hc_wasm_render`, using the same `hc_wasm_set_input_bus`
export (to be added to `HC_Wasm_Eval.h` if not already present).

---

## Known constraints and limitations

- **Boot latency**: hclang takes ~640 ms (from embedded pack, per Phase 9
  measurements) to compile the class library. The plugin signals "Booting..." in
  the status bar during this window. Audio is silent (not bypassed to input) until
  boot completes. The DAW continues processing; there is no startup stall.
- **No `thisProcess.recompile`**: re-evaluating code re-sends OSC to hcsynth but
  does not reload the class library. Full class library recompile requires
  deactivate + reactivate (equivalent to a DAW bounce/restart of the plugin).
- **Single SC runtime per instance**: each plugin instance has its own WAMR
  pair. Two instances of the plugin in the same DAW session each boot independently
  and do not share any state.
- **WAMR AOT (Phase 8 of native plan)**: the lang-side AOT bug blocks a 4×
  cold-start reduction. Once that wamrc issue is resolved, the plugin can use
  `hclang.aot` instead of `hclang.wasm` to reduce boot time further.
- **No host transport sync**: `TempoClock` in SC runs at its own rate. Syncing
  to the DAW tempo requires forwarding `ProcessContext::tempo` from the audio
  thread into SC via a custom native symbol.
- **Plugin window size**: default 900 × 600. Resize is supported by re-specifying
  the `ViewRect` in `onSize()`. A minimum of 640 × 400 is enforced to keep the
  editor usable.
- **Linux**: ImGui on Linux uses X11 + OpenGL. Wayland hosting requires
  `clap_gui_linux_wayland.cpp` (XDG surface). Out of scope for Phase 3; add in a
  follow-up.

---

## Key files to adapt from sibling projects

| Source in sibling | Purpose | Adapt for |
|---|---|---|
| `flashkick-vst3/CMakeLists.txt` | Entire build structure | Root `CMakeLists.txt` |
| `flashkick-vst3/src/plugin/processor.{h,cpp}` | VST3 AudioEffect subclass | Swap `EchoEngine` → `HCLangEngine` |
| `flashkick-vst3/src/plugin/controller.{h,cpp}` | EditController | Master Volume + Bypass params only |
| `flashkick-vst3/src/ui/imgui_editor_mac.mm` | macOS NSView/Metal ImGui host | Identical — copy verbatim |
| `flashkick-vst3/src/ui/imgui_editor_win32.cpp` | Win32 D3D11 ImGui host | Identical — copy verbatim |
| `combover-vst3/src/ui/imgui_editor_linux.cpp` | X11 OpenGL ImGui host | Identical — copy verbatim |
| `flashkick-vst3/src/ui/imgui_style.h` | ImGui style constants | Update colours to browser IDE palette |
| `flashkick-vst3/src/clap/` | All CLAP wrapper files | Copy + swap engine type |
| `combover-vst3/src/core/socket_logger.h` | UDP debug logging to port 9999 | Copy verbatim |
| `combover-vst3/src/fonts/` | DM Sans + Material Icons + ShareTechMono | Copy + add FiraCode for editor |
| `native/hclang_host/wasm_host.{h,cpp}` | WAMR wrapper | Copy into `src/core/` |
| `native/cmake/EmbedWasm.cmake` | WASM blob embedding | Copy into `cmake/` |

---

## Quick-start commands (once the repo exists)

```bash
# Point at WASI build artifacts from the parent repo
export HC_LANG_WASM=$(realpath ../hypercollider/build/wasi/lang/hclang/hclang.wasm)
export HC_SYNTH_WASM=$(realpath ../hypercollider/build/wasi/server/hcsynth/hcsynth.wasm)
export HC_CLASSLIB_PACK=$(realpath ../hypercollider/build/wasi/lang/hclang/hclang_classlib.pack)

# Configure (development — loads WASM from file)
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DHC_LANG_WASM_PATH=$HC_LANG_WASM \
    -DHC_SYNTH_WASM_PATH=$HC_SYNTH_WASM \
    -DHC_CLASSLIB_PACK=$HC_CLASSLIB_PACK

# Configure (release — embed all blobs)
cmake -B build_release -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DHC_EMBED_WASM=ON \
    -DHC_LANG_WASM_PATH=$HC_LANG_WASM \
    -DHC_SYNTH_WASM_PATH=$HC_SYNTH_WASM \
    -DHC_CLASSLIB_PACK=$HC_CLASSLIB_PACK

cmake --build build -j$(nproc)

# Install VST3 to system location (macOS)
cmake --install build --component vst3_instrument
cmake --install build --component vst3_effect
```
