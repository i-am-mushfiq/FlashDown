// BrowserHost.cpp
// Hosts Trident (MSHTML) via AtlAxWin.
//
// INCLUDE ORDER IS CRITICAL:
// atlbase.h establishes COM infrastructure before any COM/Shell headers.
//
// HTML loading: IHTMLDocument2::write() via SAFEARRAY.
// No WaitReady() / message pumping — all document readiness is handled
// via the DWebBrowserEvents2 event sink (DISPID_DOCUMENTCOMPLETE).
//
// R01: BeforeNavigate2 cancels all user link navigation.
// R02: CoInternetSetFeatureEnabled disables LMZ lockdown before first load.

#include <atlbase.h>
#include <atlwin.h>
#include <atlhost.h>

#include <windows.h>
#include <exdisp.h>
#include <exdispid.h>
#include <mshtml.h>
#include <urlmon.h>

#include <new>
#include <string>

#include "BrowserHost.h"

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

// ---------------------------------------------------------------------------
// Write html into the live document via IHTMLDocument2::write().
// Returns false if the document object is not yet available.
// ---------------------------------------------------------------------------
static bool WriteHTML(const std::wstring& html)
{
    if (!s_pBrowser) return false;

    IDispatch* pDisp = nullptr;
    if (FAILED(s_pBrowser->get_Document(&pDisp)) || !pDisp)
        return false;

    IHTMLDocument2* pDoc = nullptr;
    HRESULT hr = pDisp->QueryInterface(IID_IHTMLDocument2,
                                        reinterpret_cast<void**>(&pDoc));
    pDisp->Release();
    if (FAILED(hr) || !pDoc) return false;

    // document.open() clears existing content and opens document for writing.
    {
        VARIANT vEmpty; VariantInit(&vEmpty);
        BSTR bstrMime = SysAllocString(L"text/html");
        IDispatch* pWin = nullptr;
        pDoc->open(bstrMime, vEmpty, vEmpty, vEmpty, &pWin);
        SysFreeString(bstrMime);
        if (pWin) pWin->Release();
    }

    // document.write([html])
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
            // Push focus into the IE Server so wheel / arrow / PgDn
            // are routed there by the OS. Without this the parent
            // FlashDown_Main keeps focus and the document is unscrollable.
            BrowserHost::FocusBrowser();
        }
        return S_OK;
    }
};

static CNavSink* s_pSink = nullptr;

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

    // Try to write immediately. If the document isn't ready (e.g. a prior
    // document.open() is still in flight), navigate to about:blank and let
    // the DocumentComplete sink deliver the HTML.
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
    if (s_pSink)    { s_pSink->Release();    s_pSink    = nullptr; }
    if (s_pBrowser) { s_pBrowser->Release(); s_pBrowser = nullptr; }
    s_hWnd = nullptr;
}
