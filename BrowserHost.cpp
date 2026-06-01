// BrowserHost.cpp
// Hosts Trident (MSHTML) via AtlAxWin.
//
// INCLUDE ORDER IS CRITICAL:
// atlbase.h establishes COM infrastructure before any COM/Shell headers.
//
// HTML loading: IPersistStreamInit::Load() fast path (bypasses SAFEARRAY
// and document.open/write/close, saving ~5-10ms). Falls back to the
// original IHTMLDocument2::write() path if IPersistStreamInit is absent
// (should never happen on supported Windows versions).
// No WaitReady() / message pumping — all document readiness is handled
// via the DWebBrowserEvents2 event sink (DISPID_DOCUMENTCOMPLETE).
//
// R01: BeforeNavigate2 cancels all user link navigation.
// R02: CoInternetSetFeatureEnabled disables LMZ lockdown before first load.

#include <atlbase.h>
#include <atlwin.h>
#include <atlhost.h>

#include <windows.h>
#include <ocidl.h>        // IPersistStreamInit
#include <exdisp.h>
#include <exdispid.h>
#include <mshtml.h>
#include <mshtmhst.h>      // IDocHostUIHandler (#17)
#include <urlmon.h>

#include <new>
#include <string>

#include "BrowserHost.h"

// ---------------------------------------------------------------------------
// Benchmarking: set to 1 to emit WriteHTML() timing to OutputDebugStringW.
// Build Release, run with DebugView (SysInternals), open the same file on
// old vs new builds, compare the logged values.
// ---------------------------------------------------------------------------
#define BENCHMARK_HTML_WRITE 0
#if BENCHMARK_HTML_WRITE
#include <stdio.h>
#define BENCH_START() LARGE_INTEGER _freq, _t0, _t1; \
                      QueryPerformanceFrequency(&_freq); \
                      QueryPerformanceCounter(&_t0)
#define BENCH_END(lbl) QueryPerformanceCounter(&_t1); do { \
    double _ms = (double)(_t1.QuadPart - _t0.QuadPart) * 1000.0 / _freq.QuadPart; \
    wchar_t _buf[128]; int _n = swprintf_s(_buf, L"[WriteHTML] %s: %.3f ms", \
                                           (lbl), _ms); \
    if (_n > 0) OutputDebugStringW(_buf); \
    QueryPerformanceCounter(&_t0); \
} while(0)
#else
#define BENCH_START()  ((void)0)
#define BENCH_END(lbl) ((void)0)
#endif

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "urlmon.lib")

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------
static HWND          s_hWnd        = nullptr;
static IWebBrowser2* s_pBrowser    = nullptr;
static DWORD         s_cookie      = 0;

// Pending HTML to load on next DocumentComplete
static std::wstring  s_pendingHTML;

// Forward declarations
static HWND FindIEServerRecursive(HWND hParent);
static void InstallUIHandler();

// ---------------------------------------------------------------------------
// Write html into the live document.
// Returns false if the document object is not yet available.
//
// Fast path: IPersistStreamInit::Load() — bypasses document.open/write/close
// and the SAFEARRAY/BSTR allocation dance. Single COM call loads and renders
// the HTML in one shot via an in-memory IStream. Saves ~5-10ms.
//
// Slow path: IHTMLDocument2::open/write/close + SAFEARRAY — the original
// approach. Only reached if IPersistStreamInit QI fails (should never happen
// on supported Windows versions, but kept as a defensive fallback).
// ---------------------------------------------------------------------------
static bool WriteHTML(const std::wstring& html)
{
    BENCH_START();
    if (!s_pBrowser) return false;

    IDispatch* pDisp = nullptr;
    if (FAILED(s_pBrowser->get_Document(&pDisp)) || !pDisp)
        return false;

    // --- Fast path: IPersistStreamInit::Load() ----------------------------
    // Reduces COM round-trips from ~7 (open + write + close + SAFEARRAY
    // alloc/fill/destroy + BSTR alloc/free) to 1 (Load).
    HRESULT hr;
    IPersistStreamInit* pPSI = nullptr;
    hr = pDisp->QueryInterface(IID_IPersistStreamInit,
                                reinterpret_cast<void**>(&pPSI));
    if (SUCCEEDED(hr) && pPSI)
    {
        // InitNew() resets the document to uninitialised state so Load()
        // succeeds even on a document that was previously written.
        // On a fresh document this is a harmless no-op.
        pPSI->InitNew();

        // Build an IStream over the UTF-16 HTML buffer.
        // CreateStreamOnHGlobal with fDeleteOnRelease=TRUE gives the stream
        // ownership of the HGLOBAL — no separate free needed on success.
        const UINT cb = static_cast<UINT>(html.size() * sizeof(wchar_t));
        HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, cb);
        if (hGlobal)
        {
            void* pData = GlobalLock(hGlobal);
            if (pData)
            {
                memcpy(pData, html.c_str(), cb);
                GlobalUnlock(hGlobal);
            }
            IStream* pStream = nullptr;
            hr = CreateStreamOnHGlobal(hGlobal, TRUE, &pStream);
            if (SUCCEEDED(hr) && pStream)
            {
                hr = pPSI->Load(pStream);
                pStream->Release();
            }
            else
            {
                GlobalFree(hGlobal);
                hr = E_OUTOFMEMORY;
            }
        }
        else
        {
            hr = E_OUTOFMEMORY;
        }
        pPSI->Release();
        pDisp->Release();
        BENCH_END(L"IPersistStreamInit::Load");
        return SUCCEEDED(hr);
    }

    // --- Slow path: SAFEARRAY + document.write() --------------------------
    // Kept as a defensive fallback; Trident in all supported Windows
    // versions (7+) supports IPersistStreamInit, so this code is unlikely
    // to execute outside of a corrupted COM registration.
    IHTMLDocument2* pDoc = nullptr;
    hr = pDisp->QueryInterface(IID_IHTMLDocument2,
                                reinterpret_cast<void**>(&pDoc));
    pDisp->Release();
    if (FAILED(hr) || !pDoc) return false;

    {
        VARIANT vEmpty; VariantInit(&vEmpty);
        BSTR bstrMime = SysAllocString(L"text/html");
        IDispatch* pWin = nullptr;
        pDoc->open(bstrMime, vEmpty, vEmpty, vEmpty, &pWin);
        SysFreeString(bstrMime);
        if (pWin) pWin->Release();
    }

    SAFEARRAY* psa = SafeArrayCreateVector(VT_VARIANT, 0, 1);
    if (psa)
    {
        VARIANT var; VariantInit(&var);
        var.vt     = VT_BSTR;
        var.bstrVal = SysAllocString(html.c_str());
        LONG idx = 0;
        SafeArrayPutElement(psa, &idx, &var);
        VariantClear(&var);
        hr = pDoc->write(psa);
        SafeArrayDestroy(psa);
    }
    else { hr = E_OUTOFMEMORY; }

    pDoc->close();
    pDoc->Release();
    BENCH_END(L"SAFEARRAY write (fallback)");
    return SUCCEEDED(hr);
}

// ---------------------------------------------------------------------------
// DWebBrowserEvents2 sink
// BeforeNavigate2: cancels all user link clicks (R01)
// DocumentComplete: loads any pending HTML once the document is ready
// ---------------------------------------------------------------------------
class CNavSink : public IDispatch
{
    ULONG m_refs = 1;
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IDispatch ||
            riid == DIID_DWebBrowserEvents2)
        {
            *ppv = static_cast<IDispatch*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef()  override { return ++m_refs; }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG r = --m_refs;
        if (!r) delete this;
        return r;
    }
    STDMETHODIMP GetTypeInfoCount(UINT*)                   override { return E_NOTIMPL; }
    STDMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo**)      override { return E_NOTIMPL; }
    STDMETHODIMP GetIDsOfNames(REFIID, LPOLESTR*, UINT,
                                LCID, DISPID*)              override { return E_NOTIMPL; }

    STDMETHODIMP Invoke(DISPID id, REFIID, LCID, WORD,
                        DISPPARAMS* p, VARIANT*, EXCEPINFO*, UINT*) override
    {
        if (id == DISPID_BEFORENAVIGATE2 && p && p->cArgs >= 7)
        {
            // rgvarg[0]=Cancel, rgvarg[5]=URL (reverse order)
            VARIANT* pCancel = &p->rgvarg[0];
            VARIANT* pUrl    = &p->rgvarg[5];
            if (pCancel->vt == (VT_BYREF | VT_BOOL) && pCancel->pboolVal)
            {
                bool isBlank = pUrl->vt == VT_BSTR && pUrl->bstrVal &&
                               wcscmp(pUrl->bstrVal, L"about:blank") == 0;
                if (!isBlank)
                    *pCancel->pboolVal = VARIANT_TRUE;
            }
        }
        else if (id == DISPID_DOCUMENTCOMPLETE)
        {
            // Deliver any pending HTML now that the document is ready
            if (!s_pendingHTML.empty())
            {
                std::wstring html;
                html.swap(s_pendingHTML);
                WriteHTML(html);
            }
            // Install IDocHostUIHandler on the new document (#17).
            // Without it, Trident suppresses scrollbars and doesn't route
            // wheel / keyboard input to its scroll machinery.
            InstallUIHandler();
            // Push focus into the IE Server so wheel / arrow / PgDn
            // are routed there by the OS.
            BrowserHost::FocusBrowser();
        }
        return S_OK;
    }
};

static CNavSink* s_pSink = nullptr;

// ---------------------------------------------------------------------------
// CUIHandler — IDocHostUIHandler implementation (#17).
//
// Trident hosted inside AtlAxWin without an IDocHostUIHandler ambient site
// runs in a degraded mode: scrollbars are suppressed and wheel / keyboard
// input is not routed to the scroll dispatcher, even when the layout
// reports the document as scrollable. Confirmed via diagnostic build:
//   - documentElement.scrollHeight (8109) > clientHeight (1991), so
//     Trident knows the doc overflows.
//   - IHTMLElement2::put_scrollTop() works programmatically.
//   - WM_MOUSEWHEEL and WM_KEYDOWN delivered straight to the
//     Internet Explorer_Server window produce no scroll.
//
// Installing this handler via ICustomDoc::SetUIHandler after every
// document load re-enables the normal hosted-Trident input behaviour.
//
// Most methods return E_NOTIMPL — Trident treats that as "host has no
// opinion, use defaults." GetHostInfo returns minimal flags so we don't
// suppress anything we want. ShowContextMenu returns S_OK to suppress
// the default IE right-click menu (FlashDown is a viewer, not a browser).
// TranslateAccelerator returns S_FALSE so Trident processes its own keys.
// ---------------------------------------------------------------------------
class CUIHandler : public IDocHostUIHandler
{
    ULONG m_refs = 1;
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IDocHostUIHandler)
        {
            *ppv = static_cast<IDocHostUIHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef()  override { return ++m_refs; }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG r = --m_refs;
        if (!r) delete this;
        return r;
    }

    STDMETHODIMP ShowContextMenu(DWORD, POINT*, IUnknown*, IDispatch*) override
    {
        return S_OK; // suppress IE context menu
    }

    STDMETHODIMP GetHostInfo(DOCHOSTUIINFO* pInfo) override
    {
        if (!pInfo) return E_POINTER;
        pInfo->cbSize = sizeof(DOCHOSTUIINFO);
        // No suppression flags — we want scrollbars and default behaviour.
        // DOCHOSTUIFLAG_THEME picks up the visual style.
        pInfo->dwFlags = DOCHOSTUIFLAG_NO3DBORDER | DOCHOSTUIFLAG_THEME;
        pInfo->dwDoubleClick = DOCHOSTUIDBLCLK_DEFAULT;
        pInfo->pchHostCss = nullptr;
        pInfo->pchHostNS  = nullptr;
        return S_OK;
    }

    STDMETHODIMP ShowUI(DWORD, IOleInPlaceActiveObject*, IOleCommandTarget*,
                        IOleInPlaceFrame*, IOleInPlaceUIWindow*) override
    { return S_FALSE; }
    STDMETHODIMP HideUI() override                            { return S_OK; }
    STDMETHODIMP UpdateUI() override                          { return S_OK; }
    STDMETHODIMP EnableModeless(BOOL) override                { return E_NOTIMPL; }
    STDMETHODIMP OnDocWindowActivate(BOOL) override           { return E_NOTIMPL; }
    STDMETHODIMP OnFrameWindowActivate(BOOL) override         { return E_NOTIMPL; }
    STDMETHODIMP ResizeBorder(LPCRECT, IOleInPlaceUIWindow*, BOOL) override
    { return E_NOTIMPL; }

    STDMETHODIMP TranslateAccelerator(LPMSG, const GUID*, DWORD) override
    {
        // S_FALSE = "I didn't translate, let Trident handle it" — which is
        // what enables built-in keyboard scrolling (arrows, PgDn, Home, End).
        return S_FALSE;
    }

    STDMETHODIMP GetOptionKeyPath(LPOLESTR*, DWORD) override  { return E_NOTIMPL; }
    STDMETHODIMP GetDropTarget(IDropTarget*, IDropTarget**) override
    { return E_NOTIMPL; }
    STDMETHODIMP GetExternal(IDispatch** ppDisp) override
    {
        if (ppDisp) *ppDisp = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP TranslateUrl(DWORD, LPWSTR, LPWSTR*) override { return E_NOTIMPL; }
    STDMETHODIMP FilterDataObject(IDataObject*, IDataObject** ppDO) override
    {
        if (ppDO) *ppDO = nullptr;
        return E_NOTIMPL;
    }
};

static CUIHandler* s_pUIHandler = nullptr;

// Install our IDocHostUIHandler on the live document via ICustomDoc.
// Must be called every time a new document is loaded (each IPersistStreamInit
// ::Load(), document.write(), or navigation produces a fresh IHTMLDocument
// and the binding is lost).
static void InstallUIHandler()
{
    if (!s_pBrowser) return;
    if (!s_pUIHandler) s_pUIHandler = new(std::nothrow) CUIHandler();
    if (!s_pUIHandler) return;

    IDispatch* pDisp = nullptr;
    if (FAILED(s_pBrowser->get_Document(&pDisp)) || !pDisp) return;

    ICustomDoc* pCustomDoc = nullptr;
    if (SUCCEEDED(pDisp->QueryInterface(IID_ICustomDoc,
                                         reinterpret_cast<void**>(&pCustomDoc))) && pCustomDoc)
    {
        pCustomDoc->SetUIHandler(s_pUIHandler);
        pCustomDoc->Release();
    }
    pDisp->Release();
}

static void ConnectSink()
{
    if (!s_pBrowser) return;
    IConnectionPointContainer* pCPC = nullptr;
    if (FAILED(s_pBrowser->QueryInterface(IID_IConnectionPointContainer,
                                           reinterpret_cast<void**>(&pCPC))) || !pCPC)
        return;
    IConnectionPoint* pCP = nullptr;
    if (SUCCEEDED(pCPC->FindConnectionPoint(DIID_DWebBrowserEvents2, &pCP)) && pCP)
    {
        s_pSink = new(std::nothrow) CNavSink();
        if (s_pSink)
            pCP->Advise(static_cast<IDispatch*>(s_pSink), &s_cookie);
        pCP->Release();
    }
    pCPC->Release();
}

static void DisconnectSink()
{
    if (!s_pBrowser || !s_cookie) return;
    IConnectionPointContainer* pCPC = nullptr;
    if (SUCCEEDED(s_pBrowser->QueryInterface(IID_IConnectionPointContainer,
                                              reinterpret_cast<void**>(&pCPC))) && pCPC)
    {
        IConnectionPoint* pCP = nullptr;
        if (SUCCEEDED(pCPC->FindConnectionPoint(DIID_DWebBrowserEvents2, &pCP)) && pCP)
        {
            pCP->Unadvise(s_cookie);
            pCP->Release();
        }
        pCPC->Release();
    }
    s_cookie = 0;
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------
HWND BrowserHost::Create(HWND hParent, HINSTANCE hInst, RECT rect)
{
    s_hWnd = CreateWindowW(
        _T(ATLAXWIN_CLASS),
        L"about:blank",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        rect.left, rect.top,
        rect.right  - rect.left,
        rect.bottom - rect.top,
        hParent, nullptr, hInst, nullptr
    );
    if (!s_hWnd) return nullptr;

    IUnknown* pUnk = nullptr;
    if (SUCCEEDED(AtlAxGetControl(s_hWnd, &pUnk)) && pUnk)
    {
        pUnk->QueryInterface(IID_IWebBrowser2,
                              reinterpret_cast<void**>(&s_pBrowser));
        pUnk->Release();
    }

    ConnectSink();
    return s_hWnd;
}

void BrowserHost::LoadBlankDark()
{
    // R02: disable LMZ lockdown so remote <img> loads without prompts.
    CoInternetSetFeatureEnabled(FEATURE_LOCALMACHINE_LOCKDOWN,
                                SET_FEATURE_ON_PROCESS, FALSE);

    // Try to write the blank dark page immediately.
    // If the document isn't ready (about:blank still loading), store as
    // pending so the sink delivers it on DocumentComplete.
    static const wchar_t kBlank[] =
        L"<!DOCTYPE html><html><head>"
        L"<meta charset=\"UTF-8\">"
        L"<meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\">"
        L"</head>"
        L"<body style=\"background:#191919;margin:0;\"></body></html>";

    if (!WriteHTML(kBlank))
        s_pendingHTML = kBlank;
}

void BrowserHost::NavigateTo(const std::wstring& html)
{
    if (!s_pBrowser) return;

    // Try to write immediately via IPersistStreamInit::Load() fast path.
    // If the document isn't ready (e.g. about:blank still loading on first
    // paint), navigate to about:blank and let the DocumentComplete sink
    // deliver the HTML.
    if (!WriteHTML(html))
    {
        s_pendingHTML = html;
        // Navigate2 to about:blank; when DocumentComplete fires, the sink
        // will call WriteHTML with s_pendingHTML.
        VARIANT vUrl; VariantInit(&vUrl);
        vUrl.vt     = VT_BSTR;
        vUrl.bstrVal = SysAllocString(L"about:blank");
        s_pBrowser->Navigate2(&vUrl, nullptr, nullptr, nullptr, nullptr);
        SysFreeString(vUrl.bstrVal);
    }
}

void BrowserHost::Reposition(RECT rect)
{
    if (s_hWnd)
        MoveWindow(s_hWnd,
            rect.left, rect.top,
            rect.right  - rect.left,
            rect.bottom - rect.top,
            TRUE);
}

bool BrowserHost::IsCreated() { return s_hWnd != nullptr; }
HWND BrowserHost::GetHWND()   { return s_hWnd; }

// Recursively walk children looking for "Internet Explorer_Server"
static HWND FindIEServerRecursive(HWND hParent)
{
    HWND child = nullptr;
    while ((child = FindWindowExW(hParent, child, nullptr, nullptr)) != nullptr)
    {
        wchar_t cls[64] = {};
        GetClassNameW(child, cls, _countof(cls));
        if (wcscmp(cls, L"Internet Explorer_Server") == 0)
            return child;
        if (HWND found = FindIEServerRecursive(child))
            return found;
    }
    return nullptr;
}

HWND BrowserHost::FindServerHWND()
{
    return s_hWnd ? FindIEServerRecursive(s_hWnd) : nullptr;
}

void BrowserHost::FocusBrowser()
{
    if (HWND srv = FindServerHWND())
        SetFocus(srv);
}

void BrowserHost::Release()
{
    DisconnectSink();
    if (s_pSink)      { s_pSink->Release();      s_pSink      = nullptr; }
    if (s_pUIHandler) { s_pUIHandler->Release(); s_pUIHandler = nullptr; }
    if (s_pBrowser)   { s_pBrowser->Release();   s_pBrowser   = nullptr; }
    s_hWnd = nullptr;
}
