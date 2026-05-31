# Changelog

All notable changes to this project are documented here. Format based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the project follows
[Semantic Versioning](https://semver.org/) once it cuts a tagged release.

Every entry corresponds to a single commit and references the GitHub issue
that motivated the change.

## [Unreleased]

### Fixed
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
