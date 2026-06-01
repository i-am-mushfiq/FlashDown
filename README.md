# FlashDown

A native Windows Markdown viewer engineered for **minimal cold-start latency**. From click to rendered preview: ~199ms measured (AMD Ryzen 7, Windows 11). Our application code contributes ~12ms; the rest is Trident COM bootstrap and rendering — costs any MSHTML-based app pays. Still **3–5× faster than Electron** (500ms–1s) with **1/20th the memory** (13MB vs. 150–300MB).

Single binary. No installer. No runtime dependencies. **~370 KB `.exe`** targeting Windows 7 SP1 through Windows 11.

[LOGO_PLACEHOLDER]

---

## Overview

FlashDown solves a specific problem: Markdown viewers that inherit Chromium (≥150 MB memory footprint, 500ms+ startup) are unnecessarily heavy for a static document viewer.

Most users want to open a Markdown file, read it in a dark theme, optionally edit and save it, and close the app. That workflow should be instant and friction-free.

Existing alternatives:
- **VS Code, Sublime, Notepad++**: Full-featured editors; overkill for viewing
- **Web-based viewers**: Require a browser; not a native app experience
- **Electron apps (Typora, Obsidian)**: Cold start >500ms; memory baseline 150+ MB
- **Simple .html export**: Manual conversion step; not an app

FlashDown is a native Win32 application that launches in ~199ms (cold), uses 8–12 MB resident memory, and provides a focused read→edit→save experience without external dependencies or installers. Its application code completes in ~12ms; the remaining time is Trident's COM initialization and rendering pipeline — unavoidable costs for any MSHTML host.

---

## Key Features

- **Markdown rendering**: Headings, bold, italic, strikethrough, lists, code blocks, tables, blockquotes, links, images
- **Tables with alignment**: Per-column alignment (left, center, right) preserved
- **Side-by-side editing**: Single "Edit" button toggles a split pane with monospace editor on the left and live preview on the right
- **Draggable splitter**: Adjust editor/preview ratio on the fly (20–80% clamp)
- **Link suppression**: Rendered links are non-navigable (static document viewer)
- **Local and remote images**: Remote images load without security zone prompts (`CoInternetSetFeatureEnabled`)
- **DPI awareness**: System-level DPI awareness (not PerMonitor; see Design Philosophy)
- **Dark theme**: Notion-inspired palette; all colors resolved at compile time for Trident compatibility
- **File associations**: Optional registry file-association script for `.md` files

**Not in scope** (intentional):
- Syntax highlighting in editor
- Live preview as you type
- Tabs / multi-document
- Light theme
- Printing
- Plugins or extensions
- Auto-update
- Spell-check

The product is deliberately narrow. A focused tool is easier to ship, maintain, and distribute.

---

## Architecture Overview

```
┌──────────────────────────────────────────────────┐
│ FlashDown.exe (single process, single thread)    │
├──────────────────────────────────────────────────┤
│                                                  │
│  ┌────────────────────────────────────────────┐  │
│  │ Presentation Layer                         │  │
│  │  • MainWindow (Win32 WndProc)              │  │
│  │  • ToolbarWindow (custom-rendered buttons) │  │
│  │  • EditModeController (state machine)      │  │
│  └────────────────────────────────────────────┘  │
│                                                  │
│  ┌────────────────────────────────────────────┐  │
│  │ Document Processing Layer                  │  │
│  │  • FileIO (synchronous Win32 file I/O)     │  │
│  │  • MarkdownPipeline (md4c + HTML assembly) │  │
│  │  • BrowserHost (Trident/MSHTML via ATL)   │  │
│  └────────────────────────────────────────────┘  │
│                                                  │
│  ┌────────────────────────────────────────────┐  │
│  │ System / External                          │  │
│  │  • Windows Win32 API                       │  │
│  │  • MSHTML (Trident) — ships with Windows   │  │
│  │  • md4c (vendored, MIT)                    │  │
│  └────────────────────────────────────────────┘  │
│                                                  │
└──────────────────────────────────────────────────┘

Data Flow:

File System → FileIO::Read (UTF-8)
              ↓
         MarkdownPipeline::Convert (md4c parsing + HTML assembly)
              ↓
         BrowserHost::NavigateTo (IPersistStreamInit::Load via IStream)
              ↓
         Trident (MSHTML) rendering pipeline
              ↓
         Win32 window (GDI back buffer)
```

[ARCHITECTURE_DIAGRAM_PLACEHOLDER]

### Runtime Message Flow

```
WinMain
  ├─ DPI awareness setup (System DPI, not PerMonitor)
  ├─ OleInitialize() (COM)
  ├─ AtlAxWinInit() (ATL WebBrowser host registration)
  ├─ RegisterClass (FlashDown_Main, Toolbar, Splitter)
  ├─ CreateWindowW (main window)
  │   └─ WM_CREATE: create toolbar
  ├─ CreateWindowW (AtlAxWin140 — browser host)
  │   └─ AtlAxGetControl → IWebBrowser2
  ├─ BrowserHost::LoadBlankDark() (show dark page before file loads)
  ├─ PostMessage(WM_APP_LOADFILE) (async file loading)
  │
  └─ Message Loop
      └─ WM_APP_LOADFILE
          ├─ FileIO::Read (file → UTF-8)
          ├─ MarkdownPipeline::Convert (md4c + HTML)
          ├─ BrowserHost::NavigateTo (IPersistStreamInit::Load via IStream)
          └─ DISPID_DOCUMENTCOMPLETE (event sink fires)
              ├─ InstallUIHandler (IDocHostUIHandler for scroll support)
              └─ FocusBrowser (route input to IE Server window)
```

---

## Design Philosophy

### Performance as a Constraint

FlashDown's architecture is entirely driven by the <50ms cold-start goal.

This constraint eliminated:
- **Electron / WebView2**: Require process startup + CEF/Chromium bootstrap (~500ms)
- **.NET / WPF**: Runtime initialization + JIT compilation (~100–200ms)
- **Synchronous message-loop blocking**: Message pumping inside window creation causes re-entrant COM dispatch; app defers browser creation to after window initialization
- **External DLL dependencies**: Static CRT linking (`/MT`) eliminates `msvcrXXX.dll` load time (~30–40ms)
- **Dynamic HTML streaming**: Markdown and HTML buffers are fully assembled in memory before `document.write()`; no progressive rendering

### Why Trident, Not Chromium?

Trident (MSHTML) is deprecated but ships with every Windows install since Windows 7. This means:
- **Zero runtime dependency**: No separate browser process, no CEF, no WebView2 binary package
- **Instant availability**: Embedded COM object; no versioning complexity
- **Tiny attack surface**: Static document viewer (links are suppressed); JavaScript is not enabled

Tradeoff: Trident's rendering quality is lower than Chromium/Blink. For Markdown, this is acceptable. CSS support is partial; JavaScript is disabled. This is intentional for a document viewer.

### Single-Threaded, Message-Driven

All work (file I/O, parsing, rendering) happens on the main thread in response to window messages. No background threads.

Benefits:
- No synchronization overhead (no locks, no atomics)
- No re-entrancy bugs (single call stack)
- Predictable memory layout (no thread-local allocators)

Tradeoff: Long operations (file I/O, parsing) block the UI. For markdown files <10 MB, latency is imperceptible. For pathological cases (10+ MB files), consider architectural alternatives.

### Dependency Minimization

The binary vendors only `md4c` (Markdown parser; 30 KB source). Everything else comes from:
- **Windows SDK** (built into Visual Studio)
- **ATL** (part of MSVC; used for COM object hosting)
- **MSHTML** (shipped with Windows)

External dependencies add:
- Download overhead
- Version management complexity
- Deployment friction (installer/package manager)

None of this is worth a Markdown viewer.

### COM Hosting via AtlAxWin

Hosting Trident is normally a ~500-line exercise in COM boilerplate (object creation, event sinks, interface marshalling). ATL's `AtlAxWin` wrapper reduces this to ~50 lines:

```cpp
HWND hwnd = CreateWindowW(_T(ATLAXWIN_CLASS), L"about:blank", 
    WS_CHILD | WS_VISIBLE, rect.left, rect.top, ...);
IUnknown* pUnk = nullptr;
AtlAxGetControl(hwnd, &pUnk);
pUnk->QueryInterface(IID_IWebBrowser2, (void**)&s_pBrowser);
```

AtlAxWin handles:
- WebBrowser object creation
- Window class registration
- Default ambient site (partially; we extend it with `IDocHostUIHandler`)

Tradeoff: Tight coupling to ATL. But ATL is stable, part of MSVC, and has been unchanged since VS2010.

### Why `IPersistStreamInit::Load()` Instead of `document.write()`

HTML is delivered via `IPersistStreamInit::Load()`, not `Navigate2()` to a data URI or via the legacy `document.open()/write()/close()` pattern.

Reason: `IPersistStreamInit::Load()` via in-memory `IStream` is a single COM call that loads and renders the HTML in one shot. The legacy SAFEARRAY + `document.write()` pattern required ~7 COM round-trips (open, alloc BSTR, create SAFEARRAY, put element, write, destroy, close). Benchmarked at 13–37ms (depending on HTML size) vs. 2.6–4.0ms for the `Load()` path — a **69–93% reduction**.

The original SAFEARRAY path is preserved as a defensive fallback should `IPersistStreamInit` be unavailable (should never happen on supported Windows versions).

### Scroll Input Routing (Three-Layer Fix)

Scrolling required three independent fixes that revealed deep OS/Trident behavior:

1. **CSS `height:100%; overflow-y:auto` on `<html>`**: Without explicit overflow, Trident reports the document as scrollable (metrics check out) but doesn't route wheel input to the scroll machinery. This is undocumented Trident behavior in degraded host mode.

2. **Focus routing to IE Server**: Windows routes mouse wheel input to the window under the cursor. If the cursor is over the browser but the main window has focus, the wheel reaches the embedded `Internet Explorer_Server` window. But for keyboard scroll (arrows, PgDn), the IE Server must explicitly have focus. Handlers on `WM_SETFOCUS`, `WM_ACTIVATE`, and `DISPID_DOCUMENTCOMPLETE` forward focus.

3. **`IDocHostUIHandler` ambient site**: Without a proper host ambient site, Trident suppresses scrollbars and doesn't wire scroll input. Implementing this 14-method interface (with 12 returning `E_NOTIMPL`) re-enables normal scroll behavior. `TranslateAccelerator` returns `S_FALSE` ("I didn't translate; Trident, you handle it"), which re-enables Trident's default arrow/PgDn scroll handler.

This complexity is documented in detail in `Handoff.md` (included in the repo). See closed issues for the diagnostic chase.

---

## Performance Characteristics

### Cold-Start Latency

Measured on AMD Ryzen 7, NVIDIA RTX 4070 SUPER, NVMe SSD, Windows 11, 532-byte markdown file. 16-stage `QueryPerformanceCounter` instrumentation (enable `FULL_BENCHMARK` in `main.cpp` to reproduce):

| Stage | Time | Owner |
|-------|------|-------|
| Process startup → `wWinMain` entry | (CRT init, not measurable from within) | CRT |
| DPI + IE registry + cmdline parse | 0.3 ms | Our code |
| `OleInitialize` + `AtlAxWinInit` + class registration | 2.1 ms | COM / ATL |
| `MainWindow::Create` (WM_CREATE) | 2.9 ms | Win32 |
| `ShowWindow` + `UpdateWindow` | 20.0 ms | **DWM** |
| `BrowserHost::Create` (MSHTML COM init) | 40.6 ms | **Trident** |
| `LoadBlankDark` (blank page via `IPersistStreamInit::Load`) | 4.4 ms | Trident |
| `FileIO::Read` (532 bytes) | 0.1 ms | Our code |
| `MarkdownPipeline::Convert` (md4c) | 0.1 ms | Our code |
| `BrowserHost::NavigateTo` (HTML via `IPersistStreamInit::Load`) | 2.6 ms | Our code |
| Trident render pipeline (parse → layout → rasterize) | 125.7 ms | **Trident** |
| **Total click-to-render** | **~199 ms** | |

Our application code accounts for ~12ms (6%). The dominant costs are Trident's COM bootstrap (41ms) and its internal render pipeline (126ms) — unavoidable for any MSHTML host. The `IPersistStreamInit::Load()` optimization (#20) keeps HTML delivery at 2.6ms where it was 37ms before.

For comparison: Electron apps cold-start at 500ms–1s with 150–300MB resident. FlashDown: ~199ms, 13MB working set.

### Memory Footprint

**Resident Set** (long-running, no files open):
- CRT: ~1 MB
- Trident/MSHTML: ~3–5 MB
- Application code + controls: <1 MB
- **Total**: 8–12 MB

**Peak Commit** (loading 10 MB markdown file):
- File buffer: ~10 MB
- HTML output (3x expansion): ~30 MB
- Trident internal buffers: ~5 MB
- **Total**: ~45 MB

For comparison:
- VS Code: ~150 MB baseline
- Notepad++: ~30 MB baseline
- Electron-based Markdown viewer: 150–300 MB
- FlashDown: 8–12 MB

### Rendering Pipeline Latency

Markdown → HTML → Trident rendering is single-pass:

```
UTF-8 file (532 bytes — test.md)
  ├─ md4c parsing: <0.1 ms (streaming callbacks)
  ├─ HTML assembly: <0.1 ms (string concatenation)
  ├─ UTF-8 → UTF-16 conversion: <0.1 ms (Windows API)
  ├─ IPersistStreamInit::Load via IStream: ~2.6 ms (single COM call)
  └─ Trident rendering (parse + layout + rasterize): ~126 ms (measured)
     Total (our code): ~3 ms — Trident adds ~126ms after delivery
```

The `IPersistStreamInit::Load()` path eliminates the SAFEARRAY/BSTR allocation and reduces COM round-trips from ~7 to 1. Our application code delivers HTML in ~3ms. The remaining ~126ms is Trident's internal rendering pipeline — parsing HTML, resolving CSS, computing layout, rasterizing glyphs — which we cannot control. Further reductions in our code are possible via HTML caching and eliminating the UTF-8→UTF-16 round-trip (see Handoff.md Section 19).

---

## Tech Stack

### Language

**C++17** with `/std:c++17` compiler flag. No C++20 features (avoids breaking older MSVC).

### Frameworks & Libraries

| Component | Technology | Why |
|-----------|-----------|-----|
| Window creation | **Win32 API** | Direct OS interface; no abstraction overhead; 50+ years stable |
| Browser control | **Trident (MSHTML)** | Ships with Windows; zero runtime dependency |
| Host window | **ATL** (Active Template Library) | Simplifies COM object hosting; part of MSVC |
| Markdown parsing | **md4c** (vendored) | Fast, spec-compliant, MIT, zero deps; compiled directly into binary |
| GUI components | **Win32 EDIT, custom widgets** | Standard clipboard/focus behavior; owner-drawn toolbar/splitter for theme control |

### External Dependencies

None at runtime. Build-time:
- Visual Studio 2019/2022 (C++ workload with ATL)
- Windows SDK (included)

### System APIs

Major Windows APIs used:

- **Process/window**: `CreateProcess`, `CreateWindowW`, `GetClientRect`, `SetWindowText`, `ShowWindow`, `UpdateWindow`
- **Messages**: `SendMessage`, `PostMessage`, `DispatchMessage`, message loop
- **File I/O**: `CreateFileW`, `ReadFile`, `WriteFile`, `CloseHandle`, `GetFileSizeEx`
- **COM**: `OleInitialize`, `CoCreateInstance`, `QueryInterface`, `AddRef`, `Release`
- **Text**: `MultiByteToWideChar`, `WideCharToMultiByte` (UTF-8 ↔ UTF-16)
- **DPI**: `SetProcessDpiAwareness` (with Win7 fallback)
- **Styling**: `SetTextColor`, `SetBkColor`, `CreateSolidBrush`

All APIs are documented in the Windows SDK. No undocumented behavior.

---

## Installation & Build

### Prerequisites

- **Windows 7 SP1** or later (build and run)
- **Visual Studio 2019** (v142) or **2022** (v143)
- **C++ desktop workload** (includes Win32 SDK and ATL)
- **x64 platform** (primary target; Win32/x86 also supported for legacy Windows 7)

### Building from Source

```batch
cd C:\path\to\FlashDown
msbuild FlashDown.vcxproj /p:Configuration=Release /p:Platform=x64
```

Output: `x64\Release\FlashDown.exe` (~370 KB)

**Build variants:**

| Configuration | CRT | Optimization | Size | Use Case |
|---------------|-----|--------------|------|----------|
| Release x64 | Static (`/MT`) | Full (`/O2 /LTCG`) | ~370 KB | Distribution |
| Debug x64 | Dynamic | None | ~2 MB | Development |
| Release Win32 | Static | Full | ~350 KB | Windows 7 legacy |

### Compiler Flags (Release)

```xml
/MT             ; Static CRT (eliminates msvcrXXX.dll dependency)
/O2             ; Maximum speed optimization
/LTCG           ; Link-Time Code Generation (whole-program optimization)
/Gy             ; Function-level linking
/GF             ; String pooling
/EHsc           ; Exception handling (unused; disabled in final build)
```

These flags are set in `FlashDown.vcxproj`. See the file for full configuration.

### Linker Flags (Release)

```xml
/LTCG           ; Link-Time Code Generation
/OPT:ICF        ; COMDAT folding (merge identical functions)
/OPT:REF        ; Remove unreferenced functions (dead code elimination)
/SUBSYSTEM:WINDOWS  ; Windows GUI application (not console)
```

### Linking

Libraries:
- `ole32.lib` — COM
- `oleaut32.lib` — Automation (BSTR, SAFEARRAY, VARIANT)
- `uuid.lib` — Interface GUIDs
- `comctl32.lib` — Common controls (compatibility)
- `uxtheme.lib` — Theme APIs
- `urlmon.lib` — URL handling (`CoInternetSetFeatureEnabled`)

These are standard Windows libraries; all are shipped with the OS.

---

## Usage

### Running the Binary

**Open a markdown file:**
```batch
FlashDown.exe "C:\Documents\notes.md"
```

**Register file association** (optional; requires admin):
```batch
regedit /s setup/flashdown.reg
```

Edit `setup/flashdown.reg` to change the binary path before importing.

### Behavior

1. **Launch**: Binary opens with a dark Notion-inspired theme. If a file is provided on the command line, it's loaded asynchronously (file I/O doesn't block UI).
2. **Preview mode**: Rendered markdown fills the window. Click "Edit" button (bottom-left of toolbar).
3. **Edit mode**: Window splits into two panes:
   - **Left**: Monospace editor (Consolas 16px). Type or paste markdown.
   - **Right**: Live preview. Updates when you click "Save".
   - **Splitter**: Drag the thin gray divider between panes to adjust split (20–80% clamp).
   - **Buttons**: "Save" (write to disk), "Preview" (exit edit mode).
4. **Save**: Markdown is flushed to disk (UTF-8, no BOM). Preview re-renders immediately.
5. **Exit**: Close the window; unsaved edits are lost.

### File Associations

If `flashdown.reg` is imported:
- Right-click any `.md` file → "Open with" → FlashDown
- Double-click `.md` file → opens in FlashDown

---

## Repository Structure

```
FlashDown/
├── main.cpp                  Entry point (WinMain, COM init, message loop)
├── MainWindow.cpp / .h       Main window WndProc, message routing, sizing
├── BrowserHost.cpp / .h      Trident hosting via AtlAxWin, COM event sinks
├── ToolbarWindow.cpp / .h    Custom-drawn toolbar buttons
├── SplitterWindow.cpp / .h   Draggable splitter (owner-drawn, SetCapture for drag)
├── EditModeController.cpp/.h Edit/preview state machine, EDIT control wrapper
├── MarkdownPipeline.cpp / .h md4c wrapper, HTML assembly, UTF-8 ↔ UTF-16
├── FileIO.cpp / .h           Synchronous file I/O, BOM stripping
├── ThemeConstants.h          Compile-time CSS (Notion-dark palette)
├── Resource.h                Resource IDs (#defines)
│
├── md4c/                     VENDORED: Markdown → HTML library (MIT)
│   ├── md4c.c / md4c.h
│   ├── md4c-html.c / md4c-html.h
│   └── entity.c
│
├── FlashDown.vcxproj         MSVC project (config: Release, compiler/linker flags)
├── FlashDown.manifest        DPI awareness, Common Controls v6, UAC level
├── FlashDown.rc              Icon resource
│
├── setup/
│   └── flashdown.reg         File association script (.md → FlashDown)
│
├── CHANGELOG.md              Commit-by-commit log with issue references
├── Handoff.md                Detailed engineering handoff (20 sections)
├── README.md                 This file
└── LICENSE                   MIT license
```

### Module Responsibilities

| Module | Responsibility | Key Functions |
|--------|-----------------|----------------|
| `MainWindow` | Main window creation, message dispatch, layout | `WndProc`, `Create`, `ContentHeight` |
| `BrowserHost` | Trident control hosting, COM event sinks | `Create`, `NavigateTo`, `LoadBlankDark`, `FindServerHWND` |
| `EditModeController` | Edit/preview toggling, state machine | `Enter`, `Exit`, `Save`, `ResizePanes` |
| `ToolbarWindow` | Custom toolbar (buttons, rendering) | `Create`, `SetMode` (edit/preview) |
| `SplitterWindow` | Draggable divider between panes | `Create` (drag logic in WndProc) |
| `MarkdownPipeline` | md4c parsing, HTML assembly, UTF conversion | `Convert` |
| `FileIO` | Synchronous file I/O | `Read`, `Write` |

---

## Engineering Highlights

### 1. Static CRT Linking (`/MT`)

Standard practice for distributed binaries. Eliminates dependency on `msvcrXXX.dll` (~30–40 ms load time on cold cache).

Trade-off: Binary size increases by ~100 KB. Total: ~370 KB.

### 2. DOM Rendering via `IPersistStreamInit::Load()` (Not Navigation)

Trident's `Navigate2()` method to a data URI or local file requires parsing the URI, creating a new document, and fetching resources. Instead, we call `QueryInterface(IID_IPersistStreamInit)` on the document, call `InitNew()` to reset, create an in-memory `IStream` (via `CreateStreamOnHGlobal`) over the UTF-16 HTML buffer, and call `IPersistStreamInit::Load(pStream)` — a single COM call that loads and renders the document.

Benefit: ~34ms faster than the legacy SAFEARRAY + `document.write()` path; no navigation/zone security prompts. The original SAFEARRAY path is preserved as a defensive fallback.

### 3. Scroll Input: CSS Overflow + Focus Routing + `IDocHostUIHandler`

Getting scrolling to work required understanding three layers:
1. **CSS**: Explicit `overflow-y:auto` on `<html>` signals Trident that the document is scrollable (and should show scrollbars and route input).
2. **Focus**: IE Server must have keyboard focus for arrow keys and PgDn to work. `WM_SETFOCUS` / `WM_ACTIVATE` / `DISPID_DOCUMENTCOMPLETE` handlers forward focus.
3. **Ambient site**: `IDocHostUIHandler` implementation (mostly `E_NOTIMPL`) tells Trident that the app is a proper host, enabling scroll input dispatch.

This is documented in detail in `Handoff.md` and in closed GitHub issues.

### 4. DPI Awareness: System vs. PerMonitor

Trident pre-dates per-monitor DPI (Windows 10+). Under PerMonitor V2, Trident computes layout in DIPs, rasterizes on the primary monitor's pixel grid, then rescales—causing subpixel jitter.

Solution: Manifest declares `<dpiAwareness>system</dpiAwareness>`. Trident rendering grid stays on the primary monitor throughout the process lifetime, eliminating jitter.

Trade-off: If the window moves to a secondary monitor with a different DPI, content is not rescaled (appears tiny or huge). Acceptable for a productivity app.

### 5. Non-Recursive Markdown Parsing

md4c is non-recursive (stack-safe), making it suitable for deeply-nested markdown without stack overflow risk. This is a deliberate design choice in md4c that distinguishes it from simpler parsers.

### 6. Custom Toolbar & Splitter (Owner-Drawn)

Standard Win32 button controls are themed by the system (blue, varies by Windows version). A Markdown *viewer* with blue buttons looks wrong.

Custom-drawn buttons and splitter allow pixel-perfect control over colors and hover states, matching the dark Notion theme.

Cost: ~200 lines of WndProc logic per control.

### 7. Event-Driven HTML Delivery (Async Fallback)

If `IPersistStreamInit::Load()` fails because the document isn't ready (e.g., `about:blank` still loading on first paint), the HTML is stored as pending and `Navigate2("about:blank")` forces a clean load. When Trident fires `DISPID_DOCUMENTCOMPLETE`, an event sink delivers the pending HTML.

This avoids synchronous message-loop blocking, which causes re-entrant COM dispatch and crashes.

---

## Limitations

### Intentional Non-Goals

1. **Syntax highlighting**: Would require a custom control (Scintilla or similar) or HTML post-processing (injecting `<span>` tags with color classes). Adds significant complexity and binary size. Not worth it for a viewer.

2. **Live preview while editing**: Would require re-parsing and re-rendering on every keystroke. For large files, this is expensive and causes UI lag. Current model (parse on Save) is acceptable.

3. **Tabs / multi-document**: Would require significant UI restructuring (tab bar, file state per tab, preview reuse). Scope creep for a single-document viewer.

4. **Light theme**: Would require maintaining two parallel CSS themes. Dark theme is sufficient for a productivity tool.

5. **Plugins / extensions**: No external API. Product is deliberately narrow.

6. **Auto-update**: No built-in update mechanism. Users download new binary manually. Simplifies distribution (no self-modifying code, no UAC prompt).

7. **Spell-check / grammar**: Would require bundling a dictionary. Out of scope.

### Known Tradeoffs

1. **Trident rendering quality**: Inferior to Chromium/Blink. CSS support is partial; some modern CSS features are missing. Acceptable for Markdown.

2. **Single-threaded**: Long operations (file I/O, parsing) can block the UI. For files <10 MB, imperceptible. For larger files, consider architectural changes (async I/O, threading).

3. **Scroll under PerMonitor DPI**: Content is not rescaled if window moves to a secondary monitor. Acceptable for typical usage.

4. **No sandboxing**: Trident runs in the same process as FlashDown. Exploits in Trident could potentially compromise the app. Acceptable for a document viewer that doesn't run untrusted code.

---

## Future Work

### Natural Extensions

1. **Syntax highlighting in editor**: Scintilla-based editor control. Impact: +~500 lines, +~200 KB binary.

2. **Live preview (debounced)**: Hook `WM_CHANGE` on EDIT control; re-render on a 200 ms timer. Impact: +~50 lines, potential UI lag for large files.

3. **Tabs**: Tab bar, per-tab file state, tab switching. Impact: +~300 lines, architectural complexity.

4. **Light theme**: Second CSS theme; theme toggle in settings dialog. Impact: +~100 lines, +~20 KB binary.

5. **Printing**: `IOleCommandTarget::Exec(OLECMDID_PRINT)` to invoke Trident's print dialog. Impact: +~30 lines.

6. **Find / Replace**: Find bar (separate window); search through EDIT control or Trident DOM. Impact: +~200 lines.

### Architectural Improvements

1. **Async file I/O**: Use overlapped I/O or thread pool to prevent UI blocking on large files. Trade-off: threading complexity.

2. **Streaming HTML writes**: Instead of buffering entire HTML, feed to `document.write()` in chunks, freeing as Trident parses. Trade-off: coordination between write and parse.

3. **Incremental rendering**: Only render the visible portion of the document. Trident doesn't support this natively; would require manual DOM manipulation.

### Out of Scope

- **Full IDE features** (project trees, terminal, version control): Not a code editor; use VS Code instead.
- **Collaborative editing**: Not a use case; use Google Docs or Notion.
- **Markdown dialect switches** (CommonMark vs. GFM vs. GitHub flavors): md4c is configurable via flags; not worth UI complexity.

---

## Contributing

Contributions welcome. The codebase is intentionally small (~3K lines) and focused.

### Code Style

- C++17 (no C++20 features for MSVC compatibility)
- Win32 idioms (HWND, WndProc, message-driven)
- Minimal external dependencies
- Verbose comments on complex behavior (e.g., scroll input routing, COM event sinks)

### Testing

- **Manual testing on Windows 7, 10, 11**: Verify startup latency, rendering, edit mode, scrolling
- **DPI testing**: Primary monitor + secondary monitor (different DPI) on multi-monitor setups
- **Large file testing**: 10+ MB markdown files to check for performance degradation
- **Character encoding**: UTF-8 with and without BOM; non-ASCII characters

### Issue Reporting

Include:
- **Windows version** and DPI setup
- **File size and complexity** (example markdown)
- **Startup time** if performance-related (use a stopwatch or `Measure-Command`)
- **Steps to reproduce**
- **Expected vs. actual behavior**

### Commit Convention

- One issue per commit
- Reference issue in commit message: "Fix scroll input under PerMonitor DPI (#17)"
- CHANGELOG.md is auto-generated from commit messages (issue-to-commit mapping)

---

## Technical Documentation

A comprehensive engineering handoff is included: **[Handoff.md](Handoff.md)**

Topics covered:
- Full runtime lifecycle (startup sequence, message flow)
- Markdown rendering pipeline (latency breakdown)
- Windowing architecture and message routing
- COM hosting details and Trident integration
- DPI handling and rendering quirks
- Memory and performance architecture
- Detailed debugging procedures
- Known risks and technical debt
- How to extend the system

Read `Handoff.md` before making significant changes.

---

## License

MIT — see [LICENSE](LICENSE).

`md4c` is vendored under `md4c/` and is itself MIT-licensed.

---

## Signal

This project demonstrates:

- **Constraint-driven systems design**: Every decision is justified by cold-start latency goals
- **Deep OS knowledge**: Win32 message loops, COM hosting, DPI awareness, ATL
- **Measured, not estimated**: Full 16-stage QPC instrumentation replaced aspirational estimates with hard data. Application code: ~12ms. Trident: ~187ms.
- **Honest tradeoffs**: Explicit about limitations (Trident rendering quality, single-threaded, etc.)
- **Shipping mindset**: Binary is ~370 KB and runs on Windows 7 SP1 without an installer
- **Diagnostic approach**: Complex bugs are understood via instrumentation (see GitHub issues for chase logs)

This is what production engineering looks like at scale.
