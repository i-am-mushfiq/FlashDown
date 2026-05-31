# FlashDown

A native Windows Markdown viewer optimised for **cold-start latency**: from
double-click to fully rendered preview in under 50&nbsp;ms on a typical SSD.

No installer. No background services. No Electron, .NET, WebView2, or VC++
runtime to chase. The shipped binary is a **single ~370&nbsp;KB `.exe`** that
runs on a fresh Windows&nbsp;7&nbsp;SP1 install through Windows&nbsp;11.

---

## Why

Most Markdown viewers spin up a full Chromium process (≥120&nbsp;MB) and take
hundreds of milliseconds to show the first character. Notion-flavoured dark
preview, a one-click split editor, and a binary you can drop in `Program Files`
without an installer is the whole product.

## How

| Layer | Choice | Reason |
|---|---|---|
| Window / message loop | **Win32, single-threaded** | Sub-50&nbsp;ms cold start, predictable memory |
| Markdown → HTML | **[md4c](https://github.com/mity/md4c)** compiled in | <2&nbsp;ms for a 100&nbsp;KB file, MIT-licensed, zero deps |
| Preview rendering | **Trident (MSHTML)** via `AtlAxWin` | Ships in every Windows since 7; no runtime dependency |
| HTML delivery | `IHTMLDocument2::write()` via SAFEARRAY | Avoids navigation, no zone prompts |
| Link suppression | `DWebBrowserEvents2::BeforeNavigate2` cancel | Static document; no in-app browsing |
| Theme | Embedded CSS, Notion-dark palette | All `var()` resolved at compile time for IE compatibility |
| Editor | Win32 `EDIT` control, `WM_CTLCOLOREDIT` dark-themed | Standard clipboard/selection behaviour comes free |
| Splitter | 4-px custom child window, `SetCapture` drag | Clamped 20–80% |
| CRT / libs | `/MT` static link | No VC++ redist required |

### Architecture

```
┌──────────────────────────────────────────────────┐
│  FlashDown.exe (single process, single thread)   │
│                                                  │
│  ┌────────────────────────────────────────────┐  │
│  │  WebBrowser (Trident, AtlAxWin)            │  │
│  │  or [Edit | Splitter | WebBrowser]         │  │
│  └────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────┐  │
│  │  Toolbar (28 px, owner-drawn)              │  │
│  └────────────────────────────────────────────┘  │
│                                                  │
│  Pipeline: FileIO → md4c → HTMLAssembler         │
│            → IHTMLDocument2::write()             │
└──────────────────────────────────────────────────┘
```

### Cold-start budget

| Stage | Target | Mechanism |
|---|---|---|
| `CreateProcess` → `WinMain` | ~5 ms | Static CRT, small binary |
| Window class + main window | ~10 ms | Single-window, dark `WM_ERASEBKGND` brush |
| `WM_APP_LOADFILE` posted | — | Window already visible before disk I/O |
| File read (100 KB on SSD) | <1 ms | Synchronous `ReadFile` |
| Markdown → HTML | <2 ms | md4c |
| HTML write + paint | ~15 ms | Trident `IPersistStreamInit` / `document.write` |
| **Total** | **<50 ms** | |

---

## Build

Requirements: **Visual Studio 2019 (v142) or 2022 (v143)** with the C++ desktop workload (includes ATL).

```bat
msbuild FlashDown.vcxproj /p:Configuration=Release /p:Platform=x64
```

Output: `x64\Release\FlashDown.exe`.

To build x86 (32-bit) for Windows 7 compatibility: `/p:Platform=Win32`.

## Run

```bat
FlashDown.exe "C:\path\to\file.md"
```

Or associate it with `.md` files via the bundled
[`setup/flashdown.reg`](setup/flashdown.reg) (edit the path inside first).

## Features

- Headings 1–6, bold, italic, strikethrough
- Inline code, fenced code blocks (plain monospace, no syntax highlight)
- Unordered / ordered lists, nested
- Blockquotes
- Tables with per-column alignment
- Horizontal rules
- Links (display only — clicks do not navigate)
- Local and remote images (no security zone prompts)
- DPI-aware (PerMonitor v2)
- Side-by-side editor (single click "Edit", "Save", "Preview")

## What's not in scope

Syntax highlighting, live preview, tabs, light theme, printing, plugins,
auto-update, spell-check. See the [PRD](#) — kept narrow intentionally.

## Repository layout

```
.
├── *.cpp / *.h          single-file-per-module Win32 C++17
├── ThemeConstants.h     embedded Notion-dark CSS
├── FlashDown.manifest   PerMonitor V2 DPI + comctl32 v6
├── FlashDown.rc         icon
├── FlashDown.vcxproj    MSVC project (/MT /O2 /LTCG)
├── md4c/                vendored md4c (MIT)
├── setup/flashdown.reg  file-association script
├── CHANGELOG.md         commit-by-commit log
└── README.md
```

## Engineering notes

Detailed write-ups of the harder bugs caught during implementation live in the
issue tracker — see closed issues for the chase logs (the `_pAtlModule` null
crash, Trident DPI scaling, message re-entrancy inside `WM_CREATE`, etc.).

## License

MIT — see [LICENSE](LICENSE).

md4c is vendored under [`md4c/`](md4c/) and is itself MIT-licensed.
