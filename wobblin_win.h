// Windows + DirectX platform includes, isolated so their macros cannot
// poison Qt's headers (especially qtmochelpers.h pulled in by app.moc).
//
// Include this BEFORE any Qt header. After the Windows/DirectX includes we
// #undef only the legacy macros they define that actually collide with Qt /
// standard library (interface, small, min, max, far, near, OPTIONAL, IN, OUT).
//
// NOTE: Do NOT #undef standard tokens like NULL, TRUE, FALSE, DWORD, BOOL,
// HANDLE, BYTE, WORD, etc. — those are required by the UCRT headers and
// undef'ing them breaks <cstdio>/<cstring>. The qtmochelpers.h failure seen
// earlier was a cascade from an unrelated parser error, not from these macros.
#ifndef WOBBLIN_WIN_H
#define WOBBLIN_WIN_H

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00

// <d3d11.h>/<dxgi1_2.h>/<dcomp.h> pull in <wrl/client.h> transitively. The
// WRL client header (Windows Kits/10/include/.../winrt/wrl/client.h) redefines
// the `namespace` keyword as a macro, which then breaks Qt's qtmochelpers.h
// (included via app.moc) with a 'namespace' syntax error. We provide our own
// ComPtr (see below), so pre-define its include guard to skip the real header.
#define _WRL_CLIENT_H_

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <processthreadsapi.h>
#include <heapapi.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <d3dcompiler.h>
#include <timeapi.h>

// We DO NOT include <wrl/client.h>: on this toolchain its header (or something
// it pulls in) redefines the `namespace` keyword as a macro, which then breaks
// Qt's qtmochelpers.h (included via app.moc) with a "namespace" syntax error.
// Instead we provide a minimal ComPtr that covers exactly what app.cpp uses
// (Get/Reset/As/operator->/operator&/operator T*/assignment/nullptr/null test).
namespace Microsoft { namespace WRL {
template <class T>
class ComPtr {
    T* p_ = nullptr;
public:
    ComPtr() noexcept = default;
    ComPtr(T* p) noexcept : p_(p) {}
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ~ComPtr() noexcept { if (p_) p_->Release(); }
    T* Get() const noexcept { return p_; }
    T* operator->() const noexcept { return p_; }
    T** operator&() noexcept { return &p_; }
    operator T*() const noexcept { return p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }
    ComPtr& operator=(T* other) noexcept {
        if (p_) p_->Release();
        p_ = other;
        return *this;
    }
    void Reset() noexcept {
        if (p_) { p_->Release(); p_ = nullptr; }
    }
    template <class U>
    HRESULT As(U** pp) const noexcept {
        if (!p_) return E_POINTER;
        return p_->QueryInterface(__uuidof(U), reinterpret_cast<void**>(pp));
    }
};
}}

// Bring ComPtr into the global namespace (as the real wrl header would).
using Microsoft::WRL::ComPtr;

// Strip Windows macros that conflict with Qt / standard headers.
#ifdef interface
#undef interface
#endif
#ifdef small
#undef small
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef far
#undef far
#endif
#ifdef near
#undef near
#endif
#ifdef FAR
#undef FAR
#endif
#ifdef NEAR
#undef NEAR
#endif
#ifdef OPTIONAL
#undef OPTIONAL
#endif
#ifdef IN
#undef IN
#endif
#ifdef OUT
#undef OUT
#endif

#endif // WOBBLIN_WIN_H
