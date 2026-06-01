# Changelog

All notable changes to this project are documented here. Format based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the project follows
[Semantic Versioning](https://semver.org/) once it cuts a tagged release.

Every entry corresponds to a single commit and references the GitHub issue
that motivated the change.

## [Unreleased]

### Changed (perf/ipersiststreaminit-load branch)
- HTML loading switched from `IHTMLDocument2::open/write/close` + SAFEARRAY
  to `IPersistStreamInit::Load()` via in-memory `IStream`. Reduces COM
  round-trips from ~7 to 1 per load, eliminates BSTR and SAFEARRAY
  allocations. Benchmarked on AMD Ryzen 7 (Windows 11):
  - LoadBlankDark (blank page): 13.052ms → 4.008ms (**−69%**)
  - NavigateTo (full markdown doc): 37.467ms → 2.575ms (**−93%**)
  - Combined savings: ~44ms cold-start reduction.
  The original path is preserved as a defensive fallback.
- **First measured cold-start profile** (16-stage QPC benchmark, AMD Ryzen 7,
  RTX 4070 SUPER, Windows 11, 532-byte test.md):
  ```
  wWinMain entry                          +  0.0 ms  (total:   0.0)
  DPI + IE registry + cmdline             +  0.3 ms  (total:   0.3)
  OleInitialize                           +  1.6 ms  (total:   1.9)
  AtlAxWinInit + class registration       +  0.5 ms  (total:   2.4)
  MainWindow::Create (WM_CREATE)          +  2.9 ms  (total:   5.3)
  ShowWindow + UpdateWindow               + 20.0 ms  (total:  25.2) ← DWM
  BrowserHost::Create (MSHTML COM init)   + 40.6 ms  (total:  65.9) ← Trident
  WM_APP_LOADFILE dispatch                +  0.1 ms  (total:  65.9)
  LoadBlankDark (blank page)              +  4.4 ms  (total:  70.3)
  FileIO::Read (532 bytes)                +  0.1 ms  (total:  70.5)
  MarkdownPipeline::Convert (md4c)        +  0.1 ms  (total:  70.5)
  BrowserHost::NavigateTo (Load)          +  2.6 ms  (total:  73.1)
  DocumentComplete (Trident render)       +125.7 ms  (total: 198.8) ← 63% of total
  ```
  Our code accounts for ~12ms (6%). The remaining ~187ms is Trident COM
  init (41ms), DWM window animation (20ms), and Trident's internal
  parse+layout+rasterize pipeline (126ms). The `IPersistStreamInit`
  optimization works as designed — HTML delivery is 2.6ms where it was
  37ms before. Cold-start: **198.8ms measured** (vs. previous 47–67ms
  estimate). Still 3–5× faster than Electron (500ms–1s) with 13MB
  working set vs. 150–300MB. Closes #20.

### Fixed (scroll-redo branch)
- Wheel scrolling and scrollbar visibility (#13, #14, #17). Three coordinated
  changes after a diagnostic-driven investigation:
  - **CSS** (`ThemeConstants.h`): set `html { height:100%; overflow-y:auto;
    overflow-x:hidden }`. Without this, Trident in our hosting config runs
    in a degraded mode — `documentElement.scrollHeight` reports the doc as
    scrollable, but scrollbars are suppressed and wheel/keyboard input is
    not routed to the scroll dispatcher. Explicit overflow on the html
    element puts the viewport into an unambiguously scrollable state that
    Trident's input pipeline responds to.
  - **Focus management** (`MainWindow.cpp`, `BrowserHost.cpp`):
    `WM_SETFOCUS` / `WM_ACTIVATE` forward focus to the `Internet Explorer_Server`
    HWND (unless Edit mode is active), and `DISPID_DOCUMENTCOMPLETE` calls
    `SetFocus(IE Server)` so the first scroll after load works. Required
    because `SPI_GETMOUSEWHEELROUTING` is `2` (mouse-focus) and without the
    IE Server holding focus, the OS doesn't deliver wheel to it.
  - **`IDocHostUIHandler`** (`BrowserHost.cpp`): minimal 14-method ambient
    site installed on each new document via `ICustomDoc::SetUIHandler`.
    Most methods return `E_NOTIMPL`; the meaningful ones are `GetHostInfo`
    (no suppression flags), `TranslateAccelerator` (`S_FALSE` so Trident
    processes its own keys), and `ShowContextMenu` (`S_OK` to suppress
    the IE right-click menu). Defensive — proper hosting practice and
    avoids future degraded-mode surprises.
  - **Reverted attempts** retained for revision history: the WH_GETMESSAGE
    hook from #12 (crashed on wheel) and the WM_MOUSEWHEEL forwarder from
    #10 (didn't reach the cursor-window). Lesson: cross-process synthetic
    input tests on Win10+ produce false-negatives. The next time tests
    disagree with manual observation, manual observation wins.

  Closes #13, #14, #17.

### Changed
- Window opens **maximized** by default instead of the PRD's 900x700.
  The restore size and the 400x300 minimum still apply when the user
  un-maximizes. Closes #16.

### Fixed
- Residual text jitter / soft rendering. Three changes together:
  switch DPI awareness from `PerMonitorV2` to `System` (Trident pre-dates
  per-monitor DPI; PMv2 produces layout-in-DIPs + raster-on-primary-grid +
  bitmap-rescale, which is exactly the observed jitter); bump
  `FEATURE_BROWSER_EMULATION` from `11000` (IE11 default) to `12001`
  (IE11 Standards forced) for consistent metrics across reloads; remove
  the `max-width: 1100px; margin: 40px auto;` CSS pair that created a
  fractional pixel column boundary on every resize, replace with a
  pixel-aligned `margin: 48px 10%` layout. Manifest and runtime fallback
  both updated. Closes #15.
- Mouse-wheel scrolling really works now. The #10 fix forwarded wheel
  events from the main window's WndProc, but on Win10/11 wheel goes to
  the window under the cursor — the `AtlAxWin140` host control — which
  doesn't reach the main window. Replaced with a thread-local
  `WH_GETMESSAGE` hook that rewrites the target HWND to the inner
  `Internet Explorer_Server` whenever a wheel event lands inside the
  host. Non-invasive (no `GWLP_WNDPROC` swap, so ATL's hosting state
  stays intact). Closes #12.

### Added
- Initial public release of the v1.0 codebase (PRD-driven implementation).
- Notion-dark Markdown preview rendered via Trident (`AtlAxWin`).
- Side-by-side editor with draggable splitter (20–80% clamp).
- Bottom toolbar (28&nbsp;px) with `Edit` / `Save` / `Preview` buttons.
- Static-CRT build (`/MT /O2 /LTCG`) producing a ~370&nbsp;KB single `.exe`.
- Vendored [md4c](https://github.com/mity/md4c) for Markdown → HTML conversion.
- PerMonitor V2 DPI awareness via application manifest.
- Multi-size (16/32/48&nbsp;px) placeholder ICO and Win32 resource script.
- Compile-time CSS (`ThemeConstants.h`) with all `var()` references resolved
  for IE/Trident compatibility.
- Link navigation suppressed via `DWebBrowserEvents2::BeforeNavigate2` cancel
  (closes #1).
- LMZ lockdown disabled via `CoInternetSetFeatureEnabled` so remote `<img>`
  loads without security prompts (closes #2).
- Per-cell table alignment preserved by removing global `<td>` `text-align`
  in CSS (closes #3).
- DPI-awareness via `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)`
  plus `X-UA-Compatible IE=edge` and viewport meta (closes #7).
- Body font bumped from 16&nbsp;px to 32&nbsp;px; `-apple-system` aliases
  removed from the font stack to stop Trident's unpredictable fallback
  (closes #8, closes #9).
- Mouse-wheel forwarding from parent window to inner
  `Internet Explorer_Server` HWND; focus restored on `WM_SETFOCUS` /
  `WM_ACTIVATE` / `DISPID_DOCUMENTCOMPLETE` (closes #10).
- Placeholder application icon set via `WNDCLASSEX::hIcon` / `hIconSm`
  (closes #11).

### Fixed
- `_pAtlModule` is `NULL` in apps that don't declare a `CAtlExeModuleT`,
  causing an access violation at offset `0xB29E9` inside
  `CComPolyObject<CAxHostWindow>::FinalConstruct`. Added a
  `CFlashDownModule : ATL::CAtlExeModuleT<>` global (closes #4).
- `CreateWindowW(L"AtlAxWin", ...)` failed because ATL 14.x registers the
  class as `"AtlAxWin140"`. Switched to `_T(ATLAXWIN_CLASS)` so the macro
  expansion stays correct across compiler versions (closes #5).
- `WaitReady()`-style synchronous message pumping inside `WM_APP_LOADFILE`
  caused re-entrant COM dispatch. Replaced with a `DISPID_DOCUMENTCOMPLETE`
  event sink that consumes pending HTML asynchronously (closes #6).
