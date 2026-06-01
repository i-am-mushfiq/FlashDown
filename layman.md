# FlashDown — What We're Building and Why

## The Problem

You want to open a Markdown file and read it. That should take about as long as opening Notepad — instant. But most Markdown tools today are built on Electron, which bundles an entire copy of Chromium. The result: 500 milliseconds to over a second of startup time, and 150–300 megabytes of memory, just to display styled text.

FlashDown takes the opposite approach. It's a native Windows application — no browser, no JavaScript runtime, no framework. It uses Windows' own built-in rendering engine (the same one Internet Explorer used, called MSHTML or "Trident") to display Markdown. The entire program is about 370 kilobytes — smaller than most JPEGs.

## What It Does

Three things, really:

1. **Open a Markdown file and show it rendered** — headings, tables, code blocks, images, the works. Dark theme by default.

2. **Let you edit** — click "Edit" and the window splits into a text editor on the left and a live preview on the right. Drag the divider to resize.

3. **Save** — writes your changes back to disk. That's it. No tabs, no plugins, no settings panel.

## How It Works (the short version)

When you double-click a `.md` file, FlashDown does this:

1. **Read the file** from disk (a few hundred microseconds for typical files)
2. **Parse the Markdown** into HTML using a library called md4c (single-pass, no recursion — can't crash on weird input)
3. **Hand the HTML to Windows' built-in browser engine** (Trident), which renders it to the screen
4. **Show the window** maximized, dark background, ready to scroll

The whole pipeline is about 35 lines of orchestration code. The rest of the ~3,000 lines handles window management, the toolbar, the splitter, and making scroll work properly (which turned out to be surprisingly hard — more on that later).

## The Architecture (even shorter)

FlashDown is a single-threaded, message-driven Win32 application. If you've ever written a desktop app, this is the oldest and simplest pattern: the operating system sends messages to your window ("the user clicked here," "the window resized," "paint yourself"), and you respond.

There's no event loop library, no async framework, no dependency injection. The module graph is flat:

```
main.cpp          →  Entry point, COM initialization, message loop
MainWindow.cpp    →  Routes window messages (resize, click, focus, paint)
BrowserHost.cpp   →  Talks to the embedded browser engine (Trident)
EditModeController →  Toggles between "view" and "edit" modes
MarkdownPipeline  →  Runs md4c, assembles the final HTML document
FileIO            →  Reads and writes files
ToolbarWindow     →  Custom-drawn Edit/Save/Preview buttons
SplitterWindow    →  Draggable divider between editor and preview
```

That's the whole program. Each file does one thing, and the chain of calls is always the same direction — no cycles.

## Why We Used Trident (the IE Engine)

Trident is technically deprecated. Microsoft moved on to Edge/Chromium years ago. But Trident has one irreplaceable property: **it ships with every version of Windows from Windows 7 onward.** That means FlashDown has zero runtime dependencies. No Chromium Embedded Framework, no WebView2 redistributable, no Node.js. Just `FlashDown.exe` — copy it to any Windows machine and it works.

The trade-off is rendering quality. Trident is from 2013. It doesn't support CSS custom properties (variables), some modern layout features are missing, and its text rendering isn't as crisp as Chromium's. For Markdown — which is fundamentally structured text — these limitations are acceptable.

## The Scroll Bug: A Diagnostic Story

Getting the scroll wheel and arrow keys to work took three independent fixes, each revealing a different layer of Windows' behavior:

1. **CSS overflow**: Without explicit `overflow-y: auto` on the `<html>` element, Trident *reports* the document as scrollable (you can measure it), but doesn't actually route wheel events to the scroll machinery. Undocumented behavior.

2. **Focus routing**: Windows routes mouse wheel events to whatever window is under the cursor, not whatever window has keyboard focus. But arrow keys need focus. So we forward keyboard focus to the inner "Internet Explorer Server" window whenever the main window gains focus or the document finishes loading.

3. **Host interface**: Without implementing a 14-method COM interface (12 of which return "I don't care, use defaults"), Trident runs in a degraded mode where scrollbars are suppressed and scroll input is ignored. Registering this interface — specifically returning "I didn't handle this key, you handle it" — re-enables the built-in scroll behavior.

This took a week of diagnostic work. The lesson: when your tests and manual observation disagree, the tests are wrong, and the OS is doing something your test harness can't see.

## What We Recently Did: Two Performance Optimizations

### Optimization 1: Faster HTML Delivery (Issue #20)

The original code delivered HTML to Trident by calling `document.open()`, `document.write()`, and `document.close()` — the same JavaScript pattern you'd use in a web page. But since we're in C++ talking to a COM object, each of those calls crosses a process boundary. Add in the memory allocation for passing data (SAFEARRAYs, BSTRs — COM's array and string types), and you're looking at 7 COM round-trips per page load. Measured cost: 13–37 milliseconds.

We replaced this with a single call: `IPersistStreamInit::Load()`. Instead of seven COM calls plus allocations, we create a lightweight in-memory stream pointing at the HTML data, and tell Trident "load this." One call. Trident pulls data from the stream as it parses — no copying.

**Result**: 13ms → 4ms for the blank page, 37ms → 2.6ms for the full markdown document. A 69–93% reduction in HTML delivery time.

### Optimization 2: Measuring Reality

The project's original performance targets were based on estimates — "Trident rendering: ~10–20ms." We instrumented the entire startup with a 16-stage high-resolution timer, from the first line of `wWinMain` (the program entry point) to the moment the rendered markdown appears on screen.

We discovered that our application code accounts for about 12 milliseconds — 6% of total startup time. The remaining 187 milliseconds is outside our control: 20ms for the Windows window manager (DWM) to animate the window, 41ms for Trident to bootstrap its COM infrastructure, and 126ms for Trident to parse the HTML, resolve CSS, compute layout, rasterize glyphs, and composite the final image.

We also attempted to eliminate one render cycle by passing the dark background page as the initial navigation (instead of "about:blank" → dark page → markdown). This failed — ATL's window creation doesn't support HTML data URIs as the initial URL. The about:blank cycle is mandatory for Trident's internal initialization.

## Where We Stand

| Metric | FlashDown | Electron-based viewer |
|---|---|---|
| Cold startup | ~199ms | 500ms–1s |
| Memory (resident) | ~13MB | 150–300MB |
| Binary size | ~370KB | ~150MB (with runtime) |
| Dependencies | None (Windows only) | Node.js, Chromium, npm packages |

The sub-50ms startup target we originally set is not achievable with Trident — the browser engine itself needs ~187ms to initialize and render. Getting to genuinely sub-50ms would require replacing Trident with a lighter rendering approach (a lightweight HTML/CSS engine like litehtml, or a custom DirectWrite text renderer). Both are viable but represent significant scope.

For now, FlashDown is 3–5× faster than Electron equivalents, uses 1/20th the memory, and fits in a single file you can email. That's the trade-off we made, and the numbers back it up.
