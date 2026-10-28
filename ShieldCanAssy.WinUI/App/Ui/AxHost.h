#pragma once
#include <initializer_list>
#include <string>

// Minimal ActiveX container built on ATL's AtlAxWin (CAxWindow) + IDispatch late binding.
// VM controls are regasm'd .NET WinForms UserControls (ElementHost -> WPF inside).
//
// Two hosting modes:
//  - child HWND of the WinUI window: NOT visible (WinUI 3 composites its XAML over sibling HWNDs, z-order ignored)
//  - owned WS_POPUP window (ConvertToPopup): visible; positioned over a XAML placeholder in screen coordinates
class AxHost
{
public:
    HRESULT Create(HWND parent, const wchar_t* clsid);
    void    ConvertToPopup();                       // move the control into an owned top-level popup
    // r = control rect in pixels relative to the owner's client area; clip = the part of r that may show (popup mode:
    // the popup takes clip's size, the control is offset inside it so the hidden part lies outside the popup).
    void    MoveInOwnerClient(const RECT& r, const RECT& clip);
    void    MoveInOwnerClient(const RECT& r) { MoveInOwnerClient(r, r); }
    void    SetBleed(int px) { m_bleed = px; }      // popup mode: control overflows the popup by px on each side (clipped)
    void    Show(bool visible);
    void    Enable(bool enabled);                   // block input (modal dialogs) without hiding
    void    Destroy();
    HWND    Hwnd() const { return m_wnd.m_hWnd; }
    HWND    Popup() const { return m_popup; }
    bool    Valid() const { return m_disp != nullptr; }

    HRESULT   Invoke(DISPID id, VARIANT* result, std::initializer_list<VARIANT> args = {});
    HRESULT   CallVoid(DISPID id) { return Invoke(id, nullptr); }
    HRESULT   CallPtr(DISPID id, const void* p);
    HRESULT   CallPtrBool(DISPID id, const void* p, bool b);
    HRESULT   CallStr(DISPID id, const wchar_t* s);   // one VT_BSTR argument
    HRESULT   CallBool(DISPID id, bool b);             // one VT_BOOL argument
    long long CallI8(DISPID id, HRESULT* hr = nullptr);

    const std::wstring& LastError() const { return m_lastErr; }

private:
    void Move(const RECT& r);
    ATL::CAxWindow          m_wnd;
    ATL::CComPtr<IDispatch> m_disp;
    HWND                    m_owner{};
    HWND                    m_popup{};
    int                     m_bleed{};
    std::wstring            m_lastErr;
};
