#include "wobblin_win.h"

#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QPushButton>
#include <QGuiApplication>
#include <QStyle>
#include <QLocale>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QSizePolicy>
#include <QPainter>
#include <QColor>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QPixmap>
#include <QPainterPath>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QSurfaceFormat>
#include <QRectF>
#include <QPointF>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QByteArray>
#include <QTimer>
#include <QScreen>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QListWidget>
#include <QCheckBox>
#include <QFileDialog>
#include <QStackedWidget>
#include <QGridLayout>
#include <QButtonGroup>
#include <set>
#include <QCoreApplication>
#include <QDir>
#include <QStringList>
#include <QtSvg/QSvgRenderer>
#include <QSettings>
#include <QString>
#include <QVariant>
#include <QPointer>
#include <QRegularExpression>
#include <QSharedMemory>
#include <QSystemSemaphore>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QComboBox>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageLogContext>
#include <QtGlobal>
#include <QLoggingCategory>
#include <QShowEvent>
#include <QCloseEvent>
#include <cmath>
#include <algorithm>
#include <limits>
#include <exception>
#include <new>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <fstream>
#include <vector>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <thread>
#include <chrono>
#include <cstring>

// (Windows / DirectX headers are pulled in via "wobblin_win.h" at the top,
// before any Qt header, so their macros can't poison Qt's moc output.)

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 2
#endif

// --- Temporary diagnostic logger (Step 1 of the fix plan) -----------------
// Writes append-only lines to %TEMP%/wobblin_debug.log. Thread-safe. Remove
// once the four broken window animations are confirmed working.
namespace dbglog {
    inline std::mutex& mtx() { static std::mutex m; return m; }
    inline const wchar_t* path() {
        static wchar_t buf[MAX_PATH] = { 0 };
        if (!buf[0]) {
            if (GetTempPathW(MAX_PATH, buf)) {
                wcscat_s(buf, MAX_PATH, L"wobblin_debug.log");
            }
        }
        return buf;
    }
    inline void log(const char* fmt, ...) {
        va_list ap; va_start(ap, fmt);
        char tmp[1024];
        int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
        va_end(ap);
        if (n <= 0) return;
        auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::lock_guard<std::mutex> lk(mtx());
        std::ofstream f(path(), std::ios::app);
        if (f) {
            char ts[32]; ctime_s(ts, sizeof(ts), &t); ts[strcspn(ts, "\n")] = 0;
            f << "[" << ts << "] " << tmp << "\n";
        }
    }
}
#define DBG(...) dbglog::log(__VA_ARGS__)


namespace cfg {
    constexpr int kGridW = 4;
    constexpr int kGridH = 4;
    constexpr int kTilesX = 32;
    constexpr int kTilesY = 32;
    inline float friction = 3.2f;
    inline float stiffness = 2.0f;
    inline float mass = 22.0f;
    inline float restVel = 0.02f;
    inline float restPos = 0.3f;
    inline float inertiaGradient = 2.0f;
    constexpr int kStepIters = 3;
    constexpr int kRenderSleepMs = 8;
    constexpr int kCaptureSleepMs = 120;
    constexpr int kMaxSettleFrames = 1200;
    constexpr int kSteadyExit = 4;
    inline float boundRestitution = 0.20f;
    constexpr int kMinCaptionVisible = 80;
    constexpr int kMaxCaptionVisible = 320;
    constexpr UINT kBaseDpi = 96;
    constexpr float kMinScale = 0.25f;
    constexpr float kMaxScale = 8.0f;

    inline void applyRealismLevel(int level) {
        switch (level) {
        case 1:
            // Level 1: Subtle Pro (Crisp, Responsive, Zero Excess Wobble)
            friction = 5.2f; stiffness = 3.6f; mass = 28.0f;
            inertiaGradient = 1.3f;
            restVel = 0.01f; restPos = 0.12f; boundRestitution = 0.08f;
            break;
        case 2:
            // Level 2: KDE Plasma Natural (Classic Smooth Linux Experience)
            friction = 3.2f; stiffness = 2.0f; mass = 22.0f;
            inertiaGradient = 2.0f;
            restVel = 0.02f; restPos = 0.30f; boundRestitution = 0.20f;
            break;
        case 3:
            // Level 3: Compiz Classic 3D (Bouncy Fluid Jelly Wave)
            friction = 1.9f; stiffness = 1.1f; mass = 16.0f;
            inertiaGradient = 2.7f;
            restVel = 0.035f; restPos = 0.45f; boundRestitution = 0.30f;
            break;
        default:
            // Level 4: Hyper Pudding / Squishy Jello (Maximum Fun Elasticity)
            friction = 1.0f; stiffness = 0.52f; mass = 11.0f;
            inertiaGradient = 3.5f;
            restVel = 0.055f; restPos = 0.65f; boundRestitution = 0.42f;
            break;
        }
    }

}

enum class BurnEffect : int {
    Incinerate = 1,
    Matrix     = 2,
    Pixelate   = 3,
    TvOff      = 4,
    Glitch     = 5,
    Wisps      = 6,
    AuraGlow   = 7
};

namespace burn_cfg {
    inline BurnEffect effect      = BurnEffect::AuraGlow;
    inline int  durationMs        = 750;
    inline bool onClose           = true;
    inline bool onMinimize        = true;
    inline bool onOpen            = true;
    inline bool onMaximize        = true;
    inline bool onRestore         = true;
    inline bool onTaskbar         = true;
}

typedef LONG NTSTATUS;
struct RTL_OSVERSIONINFOW_CUSTOM {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
};
typedef NTSTATUS(WINAPI* RtlGetVersionPtr)(RTL_OSVERSIONINFOW_CUSTOM*);

struct GridNode { float x, y, vx, vy, fx, fy; int immobile; float massMul; };

struct GridSpring { int a, b; float restX, restY; };
struct MeshVertex { float x, y, u, v; };
struct ShaderConstants { float screenW, screenH, time, progress, texW, texH, radius, effectId, mode, pad0, pad1, pad2; };
struct ScreenInfo { int vox, voy, w, h; };

namespace dpiapi {
    typedef HRESULT(WINAPI* PFN_GetDpiForMonitor)(HMONITOR, int, UINT*, UINT*);
    typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);
    typedef UINT(WINAPI* PFN_GetDpiForSystem)(void);
    typedef int(WINAPI* PFN_GetSystemMetricsForDpi)(int, UINT);

    static PFN_GetDpiForMonitor pGetDpiForMonitor = nullptr;
    static PFN_GetDpiForWindow pGetDpiForWindow = nullptr;
    static PFN_GetDpiForSystem pGetDpiForSystem = nullptr;
    static PFN_GetSystemMetricsForDpi pGetSystemMetricsForDpi = nullptr;
    static std::atomic<bool> ready{ false };

    static void init() {
        HMODULE sh = LoadLibraryW(L"shcore.dll");
        if (sh) pGetDpiForMonitor = (PFN_GetDpiForMonitor)GetProcAddress(sh, "GetDpiForMonitor");
        HMODULE u = GetModuleHandleW(L"user32.dll");
        if (u) {
            pGetDpiForWindow = (PFN_GetDpiForWindow)GetProcAddress(u, "GetDpiForWindow");
            pGetDpiForSystem = (PFN_GetDpiForSystem)GetProcAddress(u, "GetDpiForSystem");
            pGetSystemMetricsForDpi = (PFN_GetSystemMetricsForDpi)GetProcAddress(u, "GetSystemMetricsForDpi");
        }
        ready.store(true);
    }

    static UINT systemDpi() {
        if (pGetDpiForSystem) { UINT d = pGetDpiForSystem(); if (d) return d; }
        HDC dc = GetDC(NULL);
        UINT d = cfg::kBaseDpi;
        if (dc) { int v = GetDeviceCaps(dc, LOGPIXELSX); if (v > 0) d = (UINT)v; ReleaseDC(NULL, dc); }
        return d;
    }

    static UINT forMonitor(HMONITOR mon) {
        if (mon && pGetDpiForMonitor) {
            UINT dx = 0, dy = 0;
            if (SUCCEEDED(pGetDpiForMonitor(mon, 0, &dx, &dy)) && dx) return dx;
        }
        return systemDpi();
    }

    static UINT forPoint(POINT pt) {
        return forMonitor(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST));
    }

    static UINT forRect(const RECT& rc) {
        return forMonitor(MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST));
    }

    static UINT forWindow(HWND w) {
        if (w && pGetDpiForWindow) { UINT d = pGetDpiForWindow(w); if (d) return d; }
        return forMonitor(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST));
    }

    static int sysMetric(int idx, UINT dpi) {
        if (pGetSystemMetricsForDpi && dpi) return pGetSystemMetricsForDpi(idx, dpi);
        return GetSystemMetrics(idx);
    }

    static float clampScale(float s) {
        if (!(s == s)) return 1.0f;
        if (s < cfg::kMinScale) return cfg::kMinScale;
        if (s > cfg::kMaxScale) return cfg::kMaxScale;
        return s;
    }
}

static const char* kVsSource =
"cbuffer CB : register(b0) { float4 screen; float4 texInfo; float4 misc; };\n"
"struct VSIn { float2 pos : POSITION; float2 uv : TEXCOORD0; };\n"
"struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
"VSOut main(VSIn i) {\n"
"  VSOut o;\n"
"  float x = (i.pos.x / screen.x)* 2.0 - 1.0;\n"
"  float y = 1.0 - (i.pos.y / screen.y)* 2.0;\n"
"  o.pos = float4(x, y, 0.0, 1.0);\n"
"  o.uv = i.uv;\n"
"  return o;\n"
"}\n";

static const char* kPsSource =
"cbuffer CB : register(b0) { float4 screen; float4 texInfo; float4 misc; };\n"
"Texture2D tex : register(t0);\n"
"SamplerState samp : register(s0);\n"
"struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
"float4 main(VSOut i) : SV_TARGET {\n"
"  float4 c = tex.Sample(samp, i.uv);\n"
"  if (texInfo.z > 0.0) {\n"
"    float2 px = i.uv* texInfo.xy;\n"
"    float r = texInfo.z;\n"
"    float a = 1.0;\n"
"    if (px.x < r && px.y < r) a = clamp(r - length(px - float2(r, r)), 0.0, 1.0);\n"
"    else if (px.x > texInfo.x - r && px.y < r) a = clamp(r - length(px - float2(texInfo.x - r, r)), 0.0, 1.0);\n"
"    else if (px.x < r && px.y > texInfo.y - r) a = clamp(r - length(px - float2(r, texInfo.y - r)), 0.0, 1.0);\n"
"    else if (px.x > texInfo.x - r && px.y > texInfo.y - r) a = clamp(r - length(px - float2(texInfo.x - r, texInfo.y - r)), 0.0, 1.0);\n"
"    c.a *= a;\n"
"  }\n"
"  return float4(c.rgb, c.a);\n"
"}\n";

static const char* kBurnPsSource =
"cbuffer CB : register(b0) { float4 screen; float4 texInfo; float4 misc; };\n"
"Texture2D tex : register(t0);\n"
"SamplerState samp : register(s0);\n"
"struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
"float hash21(float2 p) { p = frac(p * float2(123.34, 456.21)); p += dot(p, p + 45.32); return frac(p.x * p.y); }\n"
"float vnoise(float2 p) { float2 i = floor(p), f = frac(p); f = f * f * (3.0 - 2.0 * f);\n"
"  float a = hash21(i), b = hash21(i + float2(1.0, 0.0)), c = hash21(i + float2(0.0, 1.0)), d = hash21(i + float2(1.0, 1.0));\n"
"  return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y); }\n"
"float fbm(float2 p) { float s = 0.0, v = 0.5; for (int i = 0; i < 5; i++) { s += v * vnoise(p); p *= 2.0; v *= 0.5; } return s; }\n"
"float easeInSine(float t) { return 1.0 - cos((t * 3.141592653589793) / 2.0); }\n"
"float easeOutSine(float t) { return sin((t * 3.141592653589793) / 2.0); }\n"
"float easeOutCubic(float t) { float f = t - 1.0; return f * f * f + 1.0; }\n"
"float2 hash22(float2 p) {\n"
"  float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));\n"
"  p3 += dot(p3, p3.yzx + 33.33);\n"
"  return frac((p3.xx + p3.yz) * p3.zy);\n"
"}\n"
"float2 simplex2D(float2 p) {\n"
"  const float K1 = 0.366025404;\n"
"  const float K2 = 0.211324865;\n"
"  float2 i = floor(p + (p.x + p.y) * K1);\n"
"  float2 a = p - i + (i.x + i.y) * K2;\n"
"  float m = step(a.y, a.x);\n"
"  float2 o = float2(m, 1.0 - m);\n"
"  float2 b = a - o + K2;\n"
"  float2 c = a - 1.0 + 2.0 * K2;\n"
"  float3 h = max(0.5 - float3(dot(a, a), dot(b, b), dot(c, c)), 0.0);\n"
"  float3 n = h * h * h * h * float3(dot(a, -1.0 + 2.0 * hash22(i + 0.0)), dot(b, -1.0 + 2.0 * hash22(i + o)), dot(c, -1.0 + 2.0 * hash22(i + 1.0)));\n"
"  return 0.5 + 0.5 * dot(n, float3(70.0, 70.0, 70.0));\n"
"}\n"
"float3 offsetHue(float3 color, float hueOffset) {\n"
"  float maxC = max(max(color.r, color.g), color.b);\n"
"  float minC = min(min(color.r, color.g), color.b);\n"
"  float delta = maxC - minC;\n"
"  float hue = 0.0;\n"
"  if (delta > 0.0) {\n"
"    if (maxC == color.r) hue = (color.g - color.b) / delta;\n"
"    else if (maxC == color.g) hue = (color.b - color.r) / delta + 2.0;\n"
"    else hue = (color.r - color.g) / delta + 4.0;\n"
"  }\n"
"  hue /= 6.0;\n"
"  float saturation = (maxC > 0.0) ? (delta / maxC) : 0.0;\n"
"  float value = maxC;\n"
"  hue = frac(hue + hueOffset);\n"
"  float cc = value * saturation;\n"
"  float x = cc * (1.0 - abs(frac(hue * 6.0) - 1.0));\n"
"  float mm = value - cc;\n"
"  float3 rgb;\n"
"  if (hue < 1.0 / 6.0) rgb = float3(cc, x, 0.0);\n"
"  else if (hue < 2.0 / 6.0) rgb = float3(x, cc, 0.0);\n"
"  else if (hue < 3.0 / 6.0) rgb = float3(0.0, cc, x);\n"
"  else if (hue < 4.0 / 6.0) rgb = float3(0.0, x, cc);\n"
"  else if (hue < 5.0 / 6.0) rgb = float3(x, 0.0, cc);\n"
"  else rgb = float3(cc, 0.0, x);\n"
"  return rgb + mm;\n"
"}\n"
"float4 alphaOver(float4 under, float4 over) {\n"
"  if (under.a == 0.0 && over.a == 0.0) return float4(0.0, 0.0, 0.0, 0.0);\n"
"  float alpha = lerp(under.a, 1.0, over.a);\n"
"  return float4(lerp(under.rgb * under.a, over.rgb, over.a) / alpha, alpha);\n"
"}\n"
"float4 getBlurredInputColor(float2 uv, float radius, float samples) {\n"
"  float4 color = float4(0.0, 0.0, 0.0, 0.0);\n"
"  const float tau = 6.28318530718;\n"
"  const float directions = 15.0;\n"
"  for (float d = 0.0; d < tau; d += tau / directions) {\n"
"    for (float s = 0.0; s < 1.0; s += 1.0 / samples) {\n"
"      float2 offset = float2(cos(d), sin(d)) * radius * (1.0 - s) / float2(texInfo.x, texInfo.y);\n"
"      color += tex.Sample(samp, clamp(uv + offset, 0.0, 1.0));\n"
"    }\n"
"  }\n"
"  return color / samples / directions;\n"
"}\n"
"float4 main(VSOut i) : SV_TARGET {\n"
"  float t = screen.z;\n"
"  float pr = screen.w;\n"
"  float eid = texInfo.w;\n"
"  float tw = texInfo.x, th = texInfo.y;\n"
"  float4 c = tex.Sample(samp, i.uv);\n"
"  if (eid == 1.0) {\n"
"    float2 fp = i.uv + float2(sin(i.uv.y * 8.0 + t * 3.0) * 0.015, t * 0.4);\n"
"    float n = fbm(fp * float2(1.5, 2.0));\n"
"    float fl = 1.0 - pr;\n"
"    float m = smoothstep(fl - 0.15, fl + 0.05, i.uv.y + n * 0.3);\n"
"    float3 fire = lerp(float3(1.0, 0.25, 0.0), float3(1.0, 0.85, 0.2), m);\n"
"    c.rgb = lerp(c.rgb, fire, step(0.25, m));\n"
"    c.a *= (1.0 - m);\n"
"  } else if (eid == 2.0) {\n"
"    float col = floor(i.uv.x * 32.0) / 32.0;\n"
"    float dl = hash21(float2(col, 1.0));\n"
"    float cp = saturate((pr - dl * 0.4) / 0.6);\n"
"    float fy = frac(i.uv.y + t * (0.5 + hash21(float2(col, 2.0))));\n"
"    float mm = step(fy, cp);\n"
"    c.rgb = lerp(c.rgb, float3(0.1, 1.0, 0.35) * (0.5 + 0.5 * sin(t * 5.0)), mm);\n"
"    c.a *= (1.0 - cp);\n"
"  } else if (eid == 3.0) {\n"
"    float bs = lerp(1.0, 48.0, pr * pr);\n"
"    float2 bu = floor(i.uv * float2(tw, th) / bs) * bs / float2(tw, th);\n"
"    c = tex.Sample(samp, bu) * (1.0 - smoothstep(0.7, 1.0, pr));\n"
"  } else if (eid == 4.0) {\n"
"    float sq = 1.0 - pr;\n"
"    float sc = abs(i.uv.y - 0.5);\n"
"    float inScan = step(sc, sq * 0.5);\n"
"    c.rgb *= (1.0 + 2.0 * (1.0 - sq));\n"
"    c.a *= inScan * (1.0 - smoothstep(0.8, 1.0, pr));\n"
"  } else if (eid == 5.0) {\n"
"    float gt = frac(t * 3.0 + pr * 10.0);\n"
"    float rn = hash21(float2(floor(i.uv.y * 30.0), floor(gt * 8.0)));\n"
"    float2 gu = i.uv + float2((rn - 0.5) * 0.04 * pr, 0.0);\n"
"    c = tex.Sample(samp, saturate(gu));\n"
"    c.a *= (1.0 - smoothstep(0.6, 1.0, pr) + rn * 0.2 * pr);\n"
"  } else if (eid == 6.0) {\n"
"    float ang = hash21(floor(i.uv.x * 20.0) * 17.3 + 5.7) * 6.2831853;\n"
"    float2 off = float2(cos(ang), -1.0 - sin(ang * 0.5)) * pr * 0.3;\n"
"    float2 suv = i.uv - off;\n"
"    float ib = step(0.0, suv.x) * step(suv.x, 1.0) * step(0.0, suv.y) * step(suv.y, 1.0);\n"
"    c = tex.Sample(samp, suv) * ib;\n"
"    c.a *= (1.0 - pr) * (0.5 + 0.5 * sin(t * 4.0 + i.uv.x * 10.0));\n"
"  } else if (eid == 7.0) {\n""    float md = misc.x;\n""    float p = pr;\n""    float wp = pow(max(p, 0.0), 1.6);\n""    float2 uv = i.uv;\n""    float shape = lerp(2.0, 100.0, pow(p, 5.0));\n""    float g = pow(abs(uv.x - 0.5) * 2.0, shape) + pow(abs(uv.y - 0.5) * 2.0, shape);\n""    float n = simplex2D(uv + float2(0.137, 0.211)) * 0.5;\n""    g += n;\n""    float aura = exp(-abs(g - p) * 3.0);\n""    aura += exp(-abs(g - p) * 9.0) * 0.6;\n""    float glowTiming;\n""    if (md < 0.5) { glowTiming = 1.0 - smoothstep(0.0, 0.85, p); }\n""    else { float rise = 1.0 - p; glowTiming = smoothstep(0.0, 0.92, rise); }\n""    aura *= glowTiming;\n""    float3 col = cos(p * 2.5 + uv.xyx + float3(0.0, 2.0, 4.0)).xyz;\n""    col = offsetHue(col, 0.1);\n""    col = clamp(col * 0.85, float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));\n""    float4 win = getBlurredInputColor(uv, (1.0 - p) * 15.0, 3.0);\n""    float softCrop = 1.0 - smoothstep(wp - 0.5, wp + 0.5, g);\n""    float hardCrop = 1.0 - smoothstep(wp - 0.05, wp + 0.05, g);\n""    float cropMask = lerp(softCrop, hardCrop, 0.25);\n""    cropMask *= easeInSine(min(1.0, wp * 2.0));\n""    if (md >= 0.5) { cropMask = max(cropMask, 1.0 - easeOutSine(min(1.0, (1.0 - wp) * 4.0))); }\n""    else { cropMask = max(cropMask, 1.0 - easeOutSine(min(1.0, (1.0 - p) * 4.0))); }\n""    win.a *= cropMask;\n""    float3 outc = win.rgb + col * aura;\n""    float outa = max(win.a, aura * 0.8);\n""    c = float4(outc, outa);\n"
"  } else {\n"
"    float ang = hash21(floor(i.uv.x * 20.0) * 17.3 + 5.7) * 6.2831853;\n"
"    float2 off = float2(cos(ang), -1.0 - sin(ang * 0.5)) * pr * 0.3;\n"
"    float2 suv = i.uv - off;\n"
"    float ib = step(0.0, suv.x) * step(suv.x, 1.0) * step(0.0, suv.y) * step(suv.y, 1.0);\n"
"    c = tex.Sample(samp, suv) * ib;\n"
"    c.a *= (1.0 - pr) * (0.5 + 0.5 * sin(t * 4.0 + i.uv.x * 10.0));\n"
"  }\n"
"  return float4(c.rgb, c.a);\n"
"}\n";

static float Bernstein(int i, float t) {
    float u = 1.0f - t;
    if (i == 0) return u *u* u;
    if (i == 1) return 3.0f *u* u* t;
    if (i == 2) return 3.0f *u* t* t;
    return t *t* t;
}

static bool IsExcludedClass(HWND hw) {
    wchar_t cls[128];
    if (!GetClassNameW(hw, cls, 128)) return true;
    if (wcscmp(cls, L"Progman") == 0) return true;
    if (wcscmp(cls, L"WorkerW") == 0) return true;
    if (wcscmp(cls, L"Shell_TrayWnd") == 0) return true;
    if (wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0) return true;
    if (wcscmp(cls, L"WobblyCtrl") == 0) return true;
    if (wcscmp(cls, L"WobblyOverlay") == 0) return true;
    return false;
}

// Resolve the base executable name (e.g. "notepad.exe") for a window's process.
// Falls back to the window class name if the process handle cannot be opened.
static QString GetProcessImageName(HWND hw) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hw, &pid);
    if (pid == 0) return QString();
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h == NULL) return QString();
    wchar_t buf[MAX_PATH] = { 0 };
    DWORD sz = MAX_PATH;
    QString name;
    if (QueryFullProcessImageNameW(h, 0, buf, &sz) && sz > 0) {
        name = QFileInfo(QString::fromWCharArray(buf, (int)sz)).fileName();
    }
    CloseHandle(h);
    return name;
}

// Case-insensitive ordering for QString keys in exclusion sets.
struct IStringLess {
    bool operator()(const QString& a, const QString& b) const noexcept {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    }
};

// Case-insensitive membership test of an HWND's exe name against an exclusion set.
static bool IsAppExcluded(HWND hw, const std::set<QString, IStringLess>& list) {
    if (list.empty() || hw == NULL || !IsWindow(hw)) return false;
    QString name = GetProcessImageName(hw);
    if (name.isEmpty()) return false;
    return list.find(name) != list.end();
}

// When our Window Animation is active we suppress Windows' own min/max window
// animation so ours is the only one shown. The user's original setting is saved
// on first disable and restored when our feature is turned off / on exit.
static void ConfigureNativeWindowAnimations(bool enableOurs, int& saved) {
    ANIMATIONINFO ai;
    ai.cbSize = sizeof(ai);
    if (enableOurs) {
        if (saved < 0) {
            ANIMATIONINFO cur;
            cur.cbSize = sizeof(cur);
            if (SystemParametersInfoW(SPI_GETANIMATION, sizeof(cur), &cur, 0))
                saved = cur.iMinAnimate;
        }
        ai.iMinAnimate = 0;
        SystemParametersInfoW(SPI_SETANIMATION, sizeof(ai), &ai, SPIF_SENDCHANGE);
    } else if (saved >= 0) {
        ai.iMinAnimate = saved;
        SystemParametersInfoW(SPI_SETANIMATION, sizeof(ai), &ai, SPIF_SENDCHANGE);
        saved = -1;
    }
}

class SoftBody {
public:
    std::vector<GridNode> nodes;
    std::vector<GridSpring> springs;
    int anchor = -1;
    float friction = 0.0f;
    float stiffness = 0.0f;
    float mass = 0.0f;
    float baseW = 0.0f, baseH = 0.0f;

    void build(float ox, float oy, float w, float h) {
        nodes.assign((size_t)cfg::kGridW* cfg::kGridH, GridNode{});
        springs.clear();
        anchor = -1;
        friction = cfg::friction;
        stiffness = cfg::stiffness;
        mass = cfg::mass;
        baseW = w; baseH = h;
        const float sx = w / float(cfg::kGridW - 1);
        const float sy = h / float(cfg::kGridH - 1);

        for (int r = 0; r < cfg::kGridH; ++r) {
            float rowT = float(r) / float(cfg::kGridH - 1);
            float rowMassMul = 1.0f + (cfg::inertiaGradient - 1.0f) * rowT;
            for (int c = 0; c < cfg::kGridW; ++c) {
                GridNode& n = nodes[(size_t)r* cfg::kGridW + c];
                n = GridNode{};
                n.x = ox + c* sx;
                n.y = oy + r* sy;
                n.massMul = rowMassMul;
            }
        }
        for (int r = 0; r < cfg::kGridH; ++r) {
            for (int c = 0; c < cfg::kGridW; ++c) {
                int idx = r* cfg::kGridW + c;
                if (c < cfg::kGridW - 1) springs.push_back({ idx, idx + 1, sx, 0.0f });
                if (r < cfg::kGridH - 1) springs.push_back({ idx, idx + cfg::kGridW, 0.0f, sy });
            }
        }
    }

    void setRest(float w, float h) {
        baseW = w; baseH = h;
        const float sx = w / float(cfg::kGridW - 1);
        const float sy = h / float(cfg::kGridH - 1);
        for (auto& s : springs) {
            if (s.restX != 0.0f) s.restX = sx;
            if (s.restY != 0.0f) s.restY = sy;
        }
        for (int r = 0; r < cfg::kGridH; ++r) {
            float rowT = float(r) / float(cfg::kGridH - 1);
            float rowMassMul = 1.0f + (cfg::inertiaGradient - 1.0f) * rowT;
            for (int c = 0; c < cfg::kGridW; ++c) {
                size_t idx = (size_t)r * cfg::kGridW + c;
                if (idx < nodes.size()) nodes[idx].massMul = rowMassMul;
            }
        }
    }

    int nearest(float x, float y) const {
        float best = 1e30f; int bi = 0;
        for (int i = 0; i < (int)nodes.size(); ++i) {
            float d = fabsf(nodes[i].x - x) + fabsf(nodes[i].y - y);
            if (d < best) { best = d; bi = i; }
        }
        return bi;
    }

    void pin(float x, float y) {
        anchor = nearest(x, y);
        GridNode& n = nodes[anchor];
        n.immobile = 1; n.vx = n.vy = n.fx = n.fy = 0.0f;
    }

    void pinAt(float x, float y) {
        anchor = nearest(x, y);
        GridNode& n = nodes[anchor];
        n.x = x; n.y = y;
        n.immobile = 1; n.vx = n.vy = n.fx = n.fy = 0.0f;
    }

    void moveAnchor(float dx, float dy) {
        if (anchor >= 0 && anchor < (int)nodes.size()) {
            int anchorRow = anchor / cfg::kGridW;
            int anchorCol = anchor % cfg::kGridW;

            nodes[anchor].x += dx;
            nodes[anchor].y += dy;
            float stepVx = dx / (float)cfg::kStepIters;
            float stepVy = dy / (float)cfg::kStepIters;
            nodes[anchor].vx = (nodes[anchor].vx * 0.45f) + (stepVx * 0.55f);
            nodes[anchor].vy = (nodes[anchor].vy * 0.45f) + (stepVy * 0.55f);

            // Spatial influence on neighboring top-row nodes to prevent single-point sharp pinch
            if (anchorRow == 0) {
                for (int c = 0; c < cfg::kGridW; ++c) {
                    if (c == anchorCol) continue;
                    float dist = fabsf((float)(c - anchorCol));
                    float weight = expf(-0.5f * dist * dist) * 0.42f;
                    nodes[c].x += dx * weight;
                    nodes[c].y += dy * weight;
                    nodes[c].vx = (nodes[c].vx * 0.6f) + (stepVx * weight * 0.4f);
                    nodes[c].vy = (nodes[c].vy * 0.6f) + (stepVy * weight * 0.4f);
                }
            }
        }
    }

    void release() {
        if (anchor >= 0 && anchor < (int)nodes.size()) {
            nodes[anchor].immobile = 0;
            anchor = -1;
        }
    }

    void integrate(int iters) {
        for (int it = 0; it < iters; ++it) {
            for (auto& s : springs) {
                float fx = stiffness* (nodes[s.b].x - nodes[s.a].x - s.restX);
                nodes[s.a].fx += fx; nodes[s.b].fx -= fx;
                float fy = stiffness* (nodes[s.b].y - nodes[s.a].y - s.restY);
                nodes[s.a].fy += fy; nodes[s.b].fy -= fy;
            }
            for (auto& n : nodes) {
                if (!n.immobile) {
                    float nodeMass = mass * (n.massMul > 0.1f ? n.massMul : 1.0f);
                    n.fx -= friction* n.vx;
                    n.fy -= friction* n.vy;
                    n.vx += n.fx / nodeMass;
                    n.vy += n.fy / nodeMass;
                    n.x += n.vx;
                    n.y += n.vy;
                }
                n.fx = 0.0f; n.fy = 0.0f;
            }
        }
    }

    void clampTo(float minL, float maxL, float minT, float maxT, float restitution) {
        if (nodes.empty()) return;
        float rx = nodes[0].x;
        float ry = nodes[0].y;
        float corrX = 0.0f, corrY = 0.0f;
        int signX = 0, signY = 0;
        if (rx < minL) { corrX = minL - rx; signX = -1; }
        else if (rx > maxL) { corrX = maxL - rx; signX = 1; }
        if (ry < minT) { corrY = minT - ry; signY = -1; }
        else if (ry > maxT) { corrY = maxT - ry; signY = 1; }

        if (corrX != 0.0f || corrY != 0.0f) {
            float pullX = corrX* 0.2f;
            float pullY = corrY* 0.2f;
            for (auto& n : nodes) {
                n.x += pullX;
                n.y += pullY;
            }
        }

        if (signX != 0) {
            for (auto& n : nodes) {
                if ((signX > 0 && n.vx > 0.0f) || (signX < 0 && n.vx < 0.0f))
                    n.vx = -restitution* fabsf(n.vx);
            }
        }
        if (signY != 0) {
            for (auto& n : nodes) {
                if ((signY > 0 && n.vy > 0.0f) || (signY < 0 && n.vy < 0.0f))
                    n.vy = -restitution* fabsf(n.vy);
            }
        }
    }

    void relax() {
        const float sx = baseW / float(cfg::kGridW - 1);
        const float sy = baseH / float(cfg::kGridH - 1);
        float bx = nodes[0].x, by = nodes[0].y;
        for (size_t i = 0; i < nodes.size(); ++i) {
            int c = (int)(i % cfg::kGridW);
            int r = (int)(i / cfg::kGridW);
            nodes[i].x = bx + c* sx;
            nodes[i].y = by + r* sy;
            nodes[i].vx = nodes[i].vy = nodes[i].fx = nodes[i].fy = 0.0f;
        }
    }

    bool settled() const {
        const float sx = baseW / float(cfg::kGridW - 1);
        const float sy = baseH / float(cfg::kGridH - 1);
        float bx = nodes[0].x, by = nodes[0].y;
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (fabsf(nodes[i].vx) > cfg::restVel || fabsf(nodes[i].vy) > cfg::restVel) return false;
            int c = (int)(i % cfg::kGridW);
            int r = (int)(i / cfg::kGridW);
            float ex = bx + c *sx, ey = by + r* sy;
            if (fabsf(nodes[i].x - ex) > cfg::restPos || fabsf(nodes[i].y - ey) > cfg::restPos) return false;
        }
        return true;
    }
};
class ScreenGrabber {
public:
    ScreenGrabber() = default;
    ~ScreenGrabber() { stop(); }
    ScreenGrabber(const ScreenGrabber&) = delete;
    ScreenGrabber& operator=(const ScreenGrabber&) = delete;

    bool prepare(HWND hwnd) {
        target_ = hwnd;
        captureDpi_ = dpiapi::forWindow(hwnd);
        if (captureDpi_ == 0) captureDpi_ = cfg::kBaseDpi;
        RECT fr;
        if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &fr, sizeof(fr)))) {
            if (!GetWindowRect(hwnd, &fr)) return false;
        }
        RECT wr;
        if (!GetWindowRect(hwnd, &wr)) return false;
        shadow_.x = fr.left - wr.left;
        shadow_.y = fr.top - wr.top;
        texW_ = fr.right - fr.left;
        texH_ = fr.bottom - fr.top;
        frame_ = fr; wnd_ = wr;
        if (texW_ <= 0 || texH_ <= 0) return false;
        size_t bytes = (size_t)texW_ *texH_* 4;
        buf0_.assign(bytes, 0);
        buf1_.assign(bytes, 0);
        ready_.store(-1);
        return true;
    }

    void captureInitial() {
        if (grabOnce(buf0_)) ready_.store(0);
    }

    void start() {
        run_.store(true);
        thread_ = std::thread([this] { loop(); });
    }

    void stop() {
        run_.store(false);
        if (thread_.joinable()) thread_.join();
    }

    bool hasReady() const { return ready_.load() >= 0; }

    // True if the captured buffer has no visible (non-black) pixels. A freshly
    // opened window can capture blank for one or two frames before it paints.
    bool isBlank() const {
        if (ready_.load() < 0) return true;
        const uint32_t* px = reinterpret_cast<const uint32_t*>(buf0_.data());
        size_t n = (size_t)texW_ * texH_;
        for (size_t i = 0; i < n; ++i) {
            if ((px[i] & 0x00FFFFFF) != 0) return false;
        }
        return true;
    }

    bool copyReadyInto(void* dst, UINT rowPitch) {
        std::lock_guard<std::mutex> lk(mtx_);
        int idx = ready_.load();
        if (idx < 0) return false;
        const BYTE* src = (idx == 0) ? buf0_.data() : buf1_.data();
        if (rowPitch == (UINT)texW_* 4) {
            memcpy(dst, src, (size_t)texW_ *texH_* 4);
        } else {
            BYTE* d = (BYTE*)dst;
            for (int y = 0; y < texH_; ++y)
                memcpy(d + (size_t)y *rowPitch, src + (size_t)y* texW_ *4, (size_t)texW_* 4);
        }
        ready_.store(-1);
        return true;
    }

    int texW() const { return texW_; }
    int texH() const { return texH_; }
    RECT frame() const { return frame_; }
    RECT wnd() const { return wnd_; }
    POINT shadowOffset() const { return shadow_; }
    UINT captureDpi() const { return captureDpi_; }

private:
    bool grabOnce(std::vector<BYTE>& out) {
        int ww = wnd_.right - wnd_.left;
        int wh = wnd_.bottom - wnd_.top;
        if (ww <= 0 || wh <= 0 || texW_ <= 0 || texH_ <= 0) return false;

        HDC screenDC = GetDC(NULL);
        HDC fullDC = CreateCompatibleDC(screenDC);
        HBITMAP fullBmp = CreateCompatibleBitmap(screenDC, ww, wh);
        HBITMAP fullOld = (HBITMAP)SelectObject(fullDC, fullBmp);
        HDC cropDC = CreateCompatibleDC(screenDC);
        HBITMAP cropBmp = CreateCompatibleBitmap(screenDC, texW_, texH_);
        HBITMAP cropOld = (HBITMAP)SelectObject(cropDC, cropBmp);

        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = texW_;
        bi.bmiHeader.biHeight = -texH_;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        bool ok = false;
        if (PrintWindow(target_, fullDC, PW_RENDERFULLCONTENT)) {
            BitBlt(cropDC, 0, 0, texW_, texH_, fullDC, shadow_.x, shadow_.y, SRCCOPY);
            GetDIBits(cropDC, cropBmp, 0, texH_, out.data(), &bi, DIB_RGB_COLORS);
            uint32_t* px = reinterpret_cast<uint32_t*>(out.data());
            size_t n = (size_t)texW_* texH_;
            for (size_t i = 0; i < n; ++i) px[i] |= 0xFF000000;
            ok = true;
        }

        SelectObject(cropDC, cropOld);
        DeleteObject(cropBmp);
        DeleteDC(cropDC);
        SelectObject(fullDC, fullOld);
        DeleteObject(fullBmp);
        DeleteDC(fullDC);
        ReleaseDC(NULL, screenDC);
        return ok;
    }

    void loop() {
        int ww = wnd_.right - wnd_.left;
        int wh = wnd_.bottom - wnd_.top;

        HDC screenDC = GetDC(NULL);
        HDC fullDC = CreateCompatibleDC(screenDC);
        HBITMAP fullBmp = CreateCompatibleBitmap(screenDC, ww, wh);
        HBITMAP fullOld = (HBITMAP)SelectObject(fullDC, fullBmp);
        HDC cropDC = CreateCompatibleDC(screenDC);
        HBITMAP cropBmp = CreateCompatibleBitmap(screenDC, texW_, texH_);
        HBITMAP cropOld = (HBITMAP)SelectObject(cropDC, cropBmp);

        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = texW_;
        bi.bmiHeader.biHeight = -texH_;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        std::vector<BYTE> local((size_t)texW_ *texH_* 4);
        uint32_t* px = reinterpret_cast<uint32_t*>(local.data());
        size_t n = (size_t)texW_* texH_;

        while (run_.load()) {
            if (!target_ || !IsWindow(target_)) break;
            if (PrintWindow(target_, fullDC, PW_RENDERFULLCONTENT)) {
                BitBlt(cropDC, 0, 0, texW_, texH_, fullDC, shadow_.x, shadow_.y, SRCCOPY);
                GetDIBits(cropDC, cropBmp, 0, texH_, local.data(), &bi, DIB_RGB_COLORS);
                for (size_t i = 0; i < n; ++i) px[i] |= 0xFF000000;
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    int w = (ready_.load() == 0) ? 1 : 0;
                    if (w == 0) memcpy(buf0_.data(), local.data(), local.size());
                    else memcpy(buf1_.data(), local.data(), local.size());
                    ready_.store(w);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg::kCaptureSleepMs));
        }

        SelectObject(cropDC, cropOld);
        DeleteObject(cropBmp);
        DeleteDC(cropDC);
        SelectObject(fullDC, fullOld);
        DeleteObject(fullBmp);
        DeleteDC(fullDC);
        ReleaseDC(NULL, screenDC);
    }

    HWND target_ = NULL;
    RECT frame_ = {};
    RECT wnd_ = {};
    POINT shadow_ = {};
    int texW_ = 0, texH_ = 0;
    UINT captureDpi_ = cfg::kBaseDpi;
    std::vector<BYTE> buf0_;
    std::vector<BYTE> buf1_;
    std::atomic<int> ready_{ -1 };
    std::mutex mtx_;
    std::thread thread_;
    std::atomic<bool> run_{ false };
};

class GpuCompositor {
public:
    bool init(HWND hwnd, const ScreenInfo& si) {
        si_ = si;
        meshScratch_.resize((size_t)(cfg::kTilesX + 1)* (cfg::kTilesY + 1));

        for (int i = 0; i <= cfg::kTilesX; ++i) {
            float u = (float)i / (float)cfg::kTilesX;
            for (int k = 0; k < 4; ++k) BuContent_[i][k] = Bernstein(k, u);
        }
        for (int j = 0; j <= cfg::kTilesY; ++j) {
            float v = (float)j / (float)cfg::kTilesY;
            for (int k = 0; k < 4; ++k) BvContent_[j][k] = Bernstein(k, v);
        }

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL fl;
        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
        if (FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags,
            levels, 3, D3D11_SDK_VERSION, &device_, &fl, &ctx_))) return false;

        ComPtr<IDXGIDevice> dxgiDevice;
        if (FAILED(device_.As(&dxgiDevice))) return false;
        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(dxgiDevice->GetAdapter(&adapter))) return false;
        ComPtr<IDXGIFactory2> factory;
        if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return false;

        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.Width = si_.w;
        sd.Height = si_.h;
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        if (FAILED(factory->CreateSwapChainForComposition(device_.Get(), &sd, NULL, &swap_))) return false;

        if (FAILED(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&dcompDevice_)))) return false;
        if (FAILED(dcompDevice_->CreateTargetForHwnd(hwnd, TRUE, &dcompTarget_))) return false;
        if (FAILED(dcompDevice_->CreateVisual(&dcompVisual_))) return false;
        dcompVisual_->SetContent(swap_.Get());
        dcompTarget_->SetRoot(dcompVisual_.Get());
        dcompDevice_->Commit();

        ComPtr<ID3D11Texture2D> backbuf;
        if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&backbuf)))) return false;
        if (FAILED(device_->CreateRenderTargetView(backbuf.Get(), NULL, &rtv_))) return false;

        ComPtr<ID3DBlob> vsb, psb, errb;
        if (FAILED(D3DCompile(kVsSource, strlen(kVsSource), NULL, NULL, NULL, "main", "vs_4_0", 0, 0, &vsb, &errb))) return false;
        if (FAILED(D3DCompile(kPsSource, strlen(kPsSource), NULL, NULL, NULL, "main", "ps_4_0", 0, 0, &psb, &errb))) return false;
        if (FAILED(device_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), NULL, &vs_))) return false;
        if (FAILED(device_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), NULL, &ps_))) return false;

        D3D11_INPUT_ELEMENT_DESC ied[2];
        ied[0] = { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
        ied[1] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 };
        if (FAILED(device_->CreateInputLayout(ied, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layout_))) return false;

        int nVerts = (cfg::kTilesX + 1)* (cfg::kTilesY + 1);
        D3D11_BUFFER_DESC vbd = {};
        vbd.ByteWidth = sizeof(MeshVertex)* nVerts;
        vbd.Usage = D3D11_USAGE_DYNAMIC;
        vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device_->CreateBuffer(&vbd, NULL, &vb_))) return false;

        std::vector<UINT> indices;
        indices.reserve((size_t)cfg::kTilesX *cfg::kTilesY* 6);
        for (int j = 0; j < cfg::kTilesY; ++j) {
            for (int i = 0; i < cfg::kTilesX; ++i) {
                UINT i0 = j* (cfg::kTilesX + 1) + i;
                UINT i1 = i0 + 1;
                UINT i2 = i0 + (cfg::kTilesX + 1);
                UINT i3 = i2 + 1;
                indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
                indices.push_back(i1); indices.push_back(i3); indices.push_back(i2);
            }
        }
        D3D11_BUFFER_DESC ibd = {};
        ibd.ByteWidth = sizeof(UINT)* (UINT)indices.size();
        ibd.Usage = D3D11_USAGE_IMMUTABLE;
        ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA ibsd = {}; ibsd.pSysMem = indices.data();
        if (FAILED(device_->CreateBuffer(&ibd, &ibsd, &ib_))) return false;

        D3D11_BUFFER_DESC cbd = {};
        cbd.ByteWidth = sizeof(ShaderConstants);
        cbd.Usage = D3D11_USAGE_DYNAMIC;
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device_->CreateBuffer(&cbd, NULL, &cb_))) return false;

        D3D11_SAMPLER_DESC sad = {};
        sad.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sad.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sad.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sad.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sad.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device_->CreateSamplerState(&sad, &sampler_))) return false;

        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(device_->CreateBlendState(&bd, &blend_))) return false;

        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.FrontCounterClockwise = FALSE;
        rd.DepthClipEnable = TRUE;
        if (FAILED(device_->CreateRasterizerState(&rd, &rast_))) return false;

        initBurnShader();
        return true;
    }

    bool initBurnShader() {
        ComPtr<ID3DBlob> psb, errb;
        if (FAILED(D3DCompile(kBurnPsSource, strlen(kBurnPsSource), NULL, NULL, NULL, "main", "ps_4_0", 0, 0, &psb, &errb)))
            return false;
        return SUCCEEDED(device_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), NULL, &ps_burn_));
    }

    bool hasBurnShader() const { return ps_burn_ != nullptr; }

    void cleanup() {
        shadowSrv_.Reset(); shadowTex_.Reset();
        contentSrv_.Reset(); contentTex_.Reset();
        rast_.Reset(); blend_.Reset(); sampler_.Reset();
        cb_.Reset(); ib_.Reset(); vb_.Reset(); layout_.Reset();
        ps_.Reset(); vs_.Reset(); rtv_.Reset();
        ps_burn_.Reset();
        dcompVisual_.Reset(); dcompTarget_.Reset(); dcompDevice_.Reset();
        swap_.Reset(); ctx_.Reset(); device_.Reset();
    }

    void setContentSize(int w, int h) {
        contentSrv_.Reset();
        contentTex_.Reset();
        texW_ = w; texH_ = h;
        if (w <= 0 || h <= 0) return;
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w; td.Height = h;
        td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DYNAMIC;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device_->CreateTexture2D(&td, NULL, &contentTex_))) return;
        device_->CreateShaderResourceView(contentTex_.Get(), NULL, &contentSrv_);
    }

    void buildShadow() {
        int padX = (int)(si_.w* 0.02f);
        int padY = (int)(si_.h* 0.02f);
        int sw = texW_ + padX* 2;
        int sh = texH_ + padY* 2;
        shadowTex_.Reset();
        shadowSrv_.Reset();
        if (sw <= 0 || sh <= 0) return;

        std::vector<BYTE> data((size_t)sw *sh* 4);
        float sigmaX = (float)padX / 2.5f;
        float sigmaY = (float)padY / 2.5f;
        float root2 = 1.41421356f;

        std::vector<float> bx(sw), by(sh);
        for (int x = 0; x < sw; ++x)
            bx[x] = 0.5f *(std::erf((x - padX) / (sigmaX* root2)) - std::erf((x - (padX + texW_)) / (sigmaX* root2)));
        for (int y = 0; y < sh; ++y)
            by[y] = 0.5f *(std::erf((y - padY) / (sigmaY* root2)) - std::erf((y - (padY + texH_)) / (sigmaY* root2)));

        float opacity = 0.325f;
        for (int y = 0; y < sh; ++y) {
            for (int x = 0; x < sw; ++x) {
                float a = bx[x] *by[y]* opacity;
                if (a < 0.0f) a = 0.0f;
                if (a > 1.0f) a = 1.0f;
                size_t idx = ((size_t)y *sw + x)* 4;
                data[idx] = 0; data[idx + 1] = 0; data[idx + 2] = 0;
                data[idx + 3] = (BYTE)(a* 255.0f);
            }
        }

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = sw; td.Height = sh;
        td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = data.data();
        initData.SysMemPitch = sw* 4;
        device_->CreateTexture2D(&td, &initData, &shadowTex_);
        device_->CreateShaderResourceView(shadowTex_.Get(), NULL, &shadowSrv_);
    }

    bool ready() const { return contentSrv_ && contentTex_ && shadowSrv_; }

    void updateContent(ScreenGrabber& grabber) {
        if (!contentTex_ || !grabber.hasReady()) return;
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx_->Map(contentTex_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            grabber.copyReadyInto(m.pData, m.RowPitch);
            ctx_->Unmap(contentTex_.Get(), 0);
        }
    }

    void drawScene(const GridNode* ctrl, bool win11, bool dropShadow, float scale) {
        float clear[4] = { 0, 0, 0, 0 };
        ctx_->ClearRenderTargetView(rtv_.Get(), clear);
        ID3D11RenderTargetView* rtvs[] = { rtv_.Get() };
        ctx_->OMSetRenderTargets(1, rtvs, NULL);

        D3D11_VIEWPORT vp = {};
        vp.Width = (float)si_.w;
        vp.Height = (float)si_.h;
        vp.MaxDepth = 1.0f;
        ctx_->RSSetViewports(1, &vp);
        ctx_->RSSetState(rast_.Get());

        UINT stride = sizeof(MeshVertex), offset = 0;
        ID3D11Buffer* vbs[] = { vb_.Get() };
        ctx_->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
        ctx_->IASetIndexBuffer(ib_.Get(), DXGI_FORMAT_R32_UINT, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->IASetInputLayout(layout_.Get());
        ctx_->VSSetShader(vs_.Get(), NULL, 0);
        ID3D11Buffer* cbs[] = { cb_.Get() };
        ctx_->VSSetConstantBuffers(0, 1, cbs);
        ctx_->PSSetConstantBuffers(0, 1, cbs);
        ctx_->PSSetShader(ps_.Get(), NULL, 0);
        ID3D11SamplerState* samps[] = { sampler_.Get() };
        ctx_->PSSetSamplers(0, 1, samps);
        float bf[4] = { 0, 0, 0, 0 };
        ctx_->OMSetBlendState(blend_.Get(), bf, 0xFFFFFFFF);

        float padXpct = (si_.w* 0.02f) / (float)texW_;
        float padYpct = (si_.h* 0.02f) / (float)texH_;

        if (dropShadow)
            drawMesh(-padXpct, 1.0f + padXpct, -padYpct, 1.0f + padYpct, shadowSrv_.Get(), 0.0f, ctrl);
        drawMesh(0.0f, 1.0f, 0.0f, 1.0f, contentSrv_.Get(), win11 ? (8.0f* scale) : 0.0f, ctrl);
    }

    void present() { swap_->Present(1, 0); }

    void drawBurnScene(const GridNode* ctrl, float time, float progress,
                       BurnEffect eff, bool win11, float scale, int mode) {
        if (!ps_burn_ || !contentSrv_ || !contentTex_) return;
        float clear[4] = { 0, 0, 0, 0 };
        ctx_->ClearRenderTargetView(rtv_.Get(), clear);
        ID3D11RenderTargetView* rtvs[] = { rtv_.Get() };
        ctx_->OMSetRenderTargets(1, rtvs, NULL);

        D3D11_VIEWPORT vp = {};
        vp.Width = (float)si_.w; vp.Height = (float)si_.h; vp.MaxDepth = 1.0f;
        ctx_->RSSetViewports(1, &vp);
        ctx_->RSSetState(rast_.Get());

        UINT stride = sizeof(MeshVertex), offset = 0;
        ID3D11Buffer* vbs[] = { vb_.Get() };
        ctx_->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
        ctx_->IASetIndexBuffer(ib_.Get(), DXGI_FORMAT_R32_UINT, 0);
        ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx_->IASetInputLayout(layout_.Get());
        ctx_->VSSetShader(vs_.Get(), NULL, 0);
        ID3D11Buffer* cbs[] = { cb_.Get() };
        ctx_->VSSetConstantBuffers(0, 1, cbs);
        ctx_->PSSetConstantBuffers(0, 1, cbs);
        ctx_->PSSetShader(ps_burn_.Get(), NULL, 0);
        ID3D11SamplerState* samps[] = { sampler_.Get() };
        ctx_->PSSetSamplers(0, 1, samps);
        float bf[4] = { 0, 0, 0, 0 };
        ctx_->OMSetBlendState(blend_.Get(), bf, 0xFFFFFFFF);

        ShaderConstants c = { (float)si_.w, (float)si_.h, time, progress,
                              (float)texW_, (float)texH_, 0.0f, (float)(int)eff, (float)mode, 0, 0, 0 };
        // Aura Glow needs room for the aura to spill outside the window, so draw the
        // burn mesh with a large margin; other effects stay flush to the window.
        float burnPad = 0.0f; // Aura Glow drawn flush to the window (no expansion) so size stays correct
        drawMesh(-burnPad, 1.0f + burnPad, -burnPad, 1.0f + burnPad, contentSrv_.Get(), 0.0f, ctrl, &c);
        swap_->Present(1, 0);
    }

    int texW() const { return texW_; }
    int texH() const { return texH_; }

private:
    void drawMesh(float uMin, float uMax, float vMin, float vMax,
        ID3D11ShaderResourceView* srv, float radius, const GridNode* ctrl,
        const ShaderConstants* overrideCb = nullptr) {
        if (!srv) return;

        D3D11_MAPPED_SUBRESOURCE mc;
        if (SUCCEEDED(ctx_->Map(cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mc))) {
            ShaderConstants c;
            if (overrideCb) {
                c = *overrideCb;
            } else {
                c = ShaderConstants{ (float)si_.w, (float)si_.h, 0, 0, (float)texW_, (float)texH_, radius, 0, 0, 0, 0, 0 };
            }
            memcpy(mc.pData, &c, sizeof(c));
            ctx_->Unmap(cb_.Get(), 0);
        }

        const float (*BuPtr)[4] = nullptr;
        const float (*BvPtr)[4] = nullptr;
        float BuLocal[cfg::kTilesX + 1][4];
        float BvLocal[cfg::kTilesY + 1][4];

        if (uMin == 0.0f && uMax == 1.0f && vMin == 0.0f && vMax == 1.0f) {
            BuPtr = BuContent_;
            BvPtr = BvContent_;
        } else {
            for (int i = 0; i <= cfg::kTilesX; ++i) {
                float u = uMin + ((float)i / cfg::kTilesX)* (uMax - uMin);
                for (int k = 0; k < 4; ++k) BuLocal[i][k] = Bernstein(k, u);
            }
            for (int j = 0; j <= cfg::kTilesY; ++j) {
                float v = vMin + ((float)j / cfg::kTilesY)* (vMax - vMin);
                for (int k = 0; k < 4; ++k) BvLocal[j][k] = Bernstein(k, v);
            }
            BuPtr = BuLocal;
            BvPtr = BvLocal;
        }

        for (int j = 0; j <= cfg::kTilesY; ++j) {
            for (int i = 0; i <= cfg::kTilesX; ++i) {
                float px = 0, py = 0;
                for (int jj = 0; jj < 4; ++jj) {
                    float bj = BvPtr[j][jj];
                    for (int ii = 0; ii < 4; ++ii) {
                        float bi = BuPtr[i][ii];
                        const GridNode& n = ctrl[jj* 4 + ii];
                        float w = bi* bj;
                        px += w* n.x;
                        py += w* n.y;
                    }
                }
                MeshVertex& vt = meshScratch_[(size_t)j* (cfg::kTilesX + 1) + i];
                vt.x = px - (float)si_.vox;
                vt.y = py - (float)si_.voy;
                vt.u = (float)i / cfg::kTilesX;
                vt.v = (float)j / cfg::kTilesY;
            }
        }

        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx_->Map(vb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            memcpy(m.pData, meshScratch_.data(), meshScratch_.size()* sizeof(MeshVertex));
            ctx_->Unmap(vb_.Get(), 0);
            ID3D11ShaderResourceView* srvs[] = { srv };
            ctx_->PSSetShaderResources(0, 1, srvs);
            ctx_->DrawIndexed(cfg::kTilesX *cfg::kTilesY* 6, 0, 0);
        }
    }

    ScreenInfo si_ = {};
    int texW_ = 0, texH_ = 0;
    std::vector<MeshVertex> meshScratch_;
    float BuContent_[cfg::kTilesX + 1][4] = {};
    float BvContent_[cfg::kTilesY + 1][4] = {};

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> ctx_;
    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<IDCompositionDevice> dcompDevice_;
    ComPtr<IDCompositionTarget> dcompTarget_;
    ComPtr<IDCompositionVisual> dcompVisual_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    ComPtr<ID3D11VertexShader> vs_;
    ComPtr<ID3D11PixelShader> ps_;
    ComPtr<ID3D11PixelShader> ps_burn_;
    ComPtr<ID3D11InputLayout> layout_;
    ComPtr<ID3D11Buffer> vb_;
    ComPtr<ID3D11Buffer> ib_;
    ComPtr<ID3D11Buffer> cb_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11Texture2D> contentTex_;
    ComPtr<ID3D11ShaderResourceView> contentSrv_;
    ComPtr<ID3D11Texture2D> shadowTex_;
    ComPtr<ID3D11ShaderResourceView> shadowSrv_;
    ComPtr<ID3D11BlendState> blend_;
    ComPtr<ID3D11RasterizerState> rast_;
};

// Forward-declared here, defined after WobblyController (which owns s_self and
// the exclusion sets). Lets WobblyEngine::beginBurnAnim skip animations for
// apps excluded from the Animation list without reaching into WobblyController.
bool wobblyAnimExcluded(HWND hw);

class WobblyEngine {
public:
    enum class BurnMode { None, Close, Minimize, Open, Maximize, Taskbar, Restore };
    bool init(HWND overlay, const ScreenInfo& si, bool win11) {
        overlay_ = overlay;
        si_ = si;
        win11_ = win11;
        return gpu_.init(overlay, si);
    }

    void shutdown() {
        stopThreads();
        gpu_.cleanup();
    }

    bool isDragging() const { return dragging_.load(); }
    bool isSettling() const { return settling_.load(); }

    void beginDrag(HWND target, POINT pt) {
        stopThreads();
        dragging_.store(false);
        settling_.store(false);
        pendingSnap_ = 0;
        target_ = target;
        if (!IsWindow(target)) { target_ = NULL; return; }

        LONG style = GetWindowLongW(target, GWL_STYLE);
        bool maximized = (style & WS_MAXIMIZE) != 0;

        if (maximized) {
            RECT mf;
            if (FAILED(DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &mf, sizeof(mf)))) {
                if (!GetWindowRect(target, &mf)) { target_ = NULL; return; }
            }
            int mfw = mf.right - mf.left;
            int mfh = mf.bottom - mf.top;

            captureLayeredState(target);
            SendMessageTimeoutW(target, WM_SYSCOMMAND, SC_RESTORE, 0, SMTO_ABORTIFHUNG | SMTO_NORMAL, 100, nullptr);

            if (!grabber_.prepare(target)) { target_ = NULL; return; }
            cacheGeometry();
            grabber_.captureInitial();
            gpu_.setContentSize(grabber_.texW(), grabber_.texH());
            gpu_.buildShadow();
            if (!gpu_.ready()) { target_ = NULL; return; }

            {
                std::lock_guard<std::mutex> lk(bodyMtx_);
                body_.build((float)mf.left, (float)mf.top, (float)mfw, (float)mfh);
                body_.setRest((float)grabber_.texW(), (float)grabber_.texH());
                body_.pinAt((float)pt.x, (float)pt.y);
            }
            { std::lock_guard<std::mutex> lk(mouseMtx_); lastMouse_ = pt; curMouse_ = pt; }

            showOverlay(true);
            dragging_.store(true);
            renderRun_.store(true);
            renderThread_ = std::thread([this] { renderLoop(); });
            return;
        }

        WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
        if (GetWindowPlacement(target, &wp)) {
            RECT rc;
            if (GetWindowRect(target, &rc)) {
                int currentWidth = rc.right - rc.left;
                int currentHeight = rc.bottom - rc.top;
                int normalWidth = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
                int normalHeight = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
                if (currentWidth != normalWidth || currentHeight != normalHeight) {
                    RECT mf;
                    if (FAILED(DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, &mf, sizeof(mf)))) {
                        mf = rc;
                    }
                    int mfw = mf.right - mf.left;
                    int mfh = mf.bottom - mf.top;

                    double relativeX = static_cast<double>(pt.x - rc.left) / currentWidth;
                    int newLeft = pt.x - static_cast<int>(relativeX* normalWidth);
                    int newTop = rc.top;

                    HMONITOR srcMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
                    MONITORINFO srcMi; srcMi.cbSize = sizeof(srcMi);
                    RECT srcWork = {};
                    if (GetMonitorInfoW(srcMon, &srcMi)) srcWork = srcMi.rcWork;
                    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &srcWork, 0);

                    captureLayeredState(target);

                    wp.showCmd = SW_SHOWNORMAL;
                    wp.rcNormalPosition.left = newLeft - srcWork.left;
                    wp.rcNormalPosition.top = newTop - srcWork.top;
                    wp.rcNormalPosition.right = wp.rcNormalPosition.left + normalWidth;
                    wp.rcNormalPosition.bottom = wp.rcNormalPosition.top + normalHeight;
                    SetWindowPlacement(target, &wp);

                    if (!grabber_.prepare(target)) { target_ = NULL; return; }
                    cacheGeometry();
                    grabber_.captureInitial();
                    gpu_.setContentSize(grabber_.texW(), grabber_.texH());
                    gpu_.buildShadow();
                    if (!gpu_.ready()) { target_ = NULL; return; }

                    RECT nf = grabber_.frame();
                    int nfw = nf.right - nf.left;
                    int nfh = nf.bottom - nf.top;
                    if (nfw <= 0) { nfw = mfw; }
                    if (nfh <= 0) { nfh = mfh; }

                    {
                        std::lock_guard<std::mutex> lk(bodyMtx_);
                        body_.build((float)nf.left, (float)nf.top, (float)nfw, (float)nfh);
                        body_.setRest((float)grabber_.texW(), (float)grabber_.texH());
                        body_.pinAt((float)pt.x, (float)pt.y);
                    }
                    { std::lock_guard<std::mutex> lk(mouseMtx_); lastMouse_ = pt; curMouse_ = pt; }

                    showOverlay(true);
                    dragging_.store(true);
                    renderRun_.store(true);
                    renderThread_ = std::thread([this] { renderLoop(); });
                    return;
                }
            }
        }

        if (!grabber_.prepare(target)) { target_ = NULL; return; }
        cacheGeometry();
        grabber_.captureInitial();
        gpu_.setContentSize(grabber_.texW(), grabber_.texH());
        gpu_.buildShadow();
        if (!gpu_.ready()) { target_ = NULL; return; }

        RECT fr = grabber_.frame();
        int fw = fr.right - fr.left;
        int fh = fr.bottom - fr.top;
        {
            std::lock_guard<std::mutex> lk(bodyMtx_);
            body_.build((float)fr.left, (float)fr.top, (float)fw, (float)fh);
            body_.setRest((float)grabber_.texW(), (float)grabber_.texH());
            body_.pin((float)pt.x, (float)pt.y);
        }
        { std::lock_guard<std::mutex> lk(mouseMtx_); lastMouse_ = pt; curMouse_ = pt; }

        captureLayeredState(target);
        showOverlay(true);
        dragging_.store(true);
        renderRun_.store(true);
        renderThread_ = std::thread([this] { renderLoop(); });
    }

    void updateDrag(POINT pt) {
        std::lock_guard<std::mutex> lk(mouseMtx_);
        curMouse_ = pt;
    }

    void endDrag(POINT pt) {
        { std::lock_guard<std::mutex> lk(mouseMtx_); curMouse_ = pt; }

        HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        mi.cbSize = sizeof(mi);
        pendingSnap_ = 0;
        if (GetMonitorInfoW(hMon, &mi)) {
            UINT mdpi = dpiapi::forMonitor(hMon);
            int edge = (int)(5.0f* ((float)mdpi / (float)cfg::kBaseDpi));
            if (edge < 5) edge = 5;
            bool hitLeft = (pt.x <= mi.rcMonitor.left + edge);
            bool hitRight = (pt.x >= mi.rcMonitor.right - edge);
            bool hitTop = (pt.y <= mi.rcMonitor.top + edge);
            bool hitBottom = (pt.y >= mi.rcMonitor.bottom - edge);

            if (hitTop && hitLeft) pendingSnap_ = 4;
            else if (hitBottom && hitLeft) pendingSnap_ = 5;
            else if (hitTop && hitRight) pendingSnap_ = 6;
            else if (hitBottom && hitRight) pendingSnap_ = 7;
            else if (hitTop) pendingSnap_ = 1;
            else if (hitLeft) pendingSnap_ = 2;
            else if (hitRight) pendingSnap_ = 3;
        }

        { std::lock_guard<std::mutex> lk(bodyMtx_); body_.release(); }
        dragging_.store(false);
        settling_.store(true);
    }

    bool burnIdle() const { return burnMode_.load() == BurnMode::None; }

    bool beginBurnAnim(HWND hw, BurnMode mode) {
        // Maximize / Restore: let Windows play its own native animation; skip the
        // burn overlay entirely so Aura Glow only covers Open / Exit / Minimize.
        if (mode == BurnMode::Maximize || mode == BurnMode::Restore) {
            return false;
        }
        // Suppress self-generated SHOW events (final SW_MAXIMIZE / SW_RESTORE /
        // SW_RESTORE for taskbar) so they are not re-detected as a new Open.
        if (mode == BurnMode::Maximize || mode == BurnMode::Taskbar
            || mode == BurnMode::Restore) {
            showSuppressUntilTick_ = GetTickCount() + (DWORD)burn_cfg::durationMs + 1000;
        }
        if (!hw || !IsWindow(hw) || IsExcludedClass(hw) || hw == overlay_
            || wobblyAnimExcluded(hw)) {
            DBG("beginBurnAnim: rejected hw=%p", (void*)hw);
            return false;
        }
        if (dragging_.load() || settling_.load() || burnMode_.load() != BurnMode::None) {
            DBG("beginBurnAnim: busy hw=%p mode=%d", (void*)hw, (int)mode);
            return false;
        }
        stopThreads();
        if (!grabber_.prepare(hw)) { DBG("beginBurnAnim: prepare failed hw=%p mode=%d", (void*)hw, (int)mode); return false; }
        cacheGeometry();

        // Capture the window. A just-opened window (BurnMode::Open) can render
        // blank for a frame or two, so retry after nudging a paint. For Open we
        // bail out (window simply appears natively) if it stays blank; other
        // modes fall back to the best capture we got.
        bool captured = false;
        bool blank = true;
        for (int attempt = 0; attempt < 4; ++attempt) {
            grabber_.captureInitial();
            if (grabber_.hasReady()) {
                blank = grabber_.isBlank();
                // For Open we always animate once PrintWindow succeeds (a blank
                // first frame still burns in visibly rather than appearing with no
                // animation). For other modes a blank frame is accepted too.
                captured = true;
                if (!blank) break;
            }
            RedrawWindow(hw, NULL, NULL, RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_FRAME);
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        if (!captured) {
            DBG("beginBurnAnim: capture failed hw=%p mode=%d", (void*)hw, (int)mode);
            return false;
        }
        if (blank) DBG("beginBurnAnim: WARNING capture blank hw=%p mode=%d (using fallback)", (void*)hw, (int)mode);

        gpu_.setContentSize(grabber_.texW(), grabber_.texH());
        gpu_.buildShadow();
        if (!gpu_.ready() || !gpu_.hasBurnShader()) { DBG("beginBurnAnim: gpu not ready hw=%p", (void*)hw); return false; }

        RECT fr = grabber_.frame();
        {
            std::lock_guard<std::mutex> lk(bodyMtx_);
            body_.build((float)fr.left, (float)fr.top,
                        (float)(fr.right - fr.left), (float)(fr.bottom - fr.top));
            body_.setRest((float)grabber_.texW(), (float)grabber_.texH());
            body_.relax();
        }

        // Capture the frame while the window is still visible, then hide it so the
        // animation overlay (not the real window) is what the user sees.
        // Open is special: a brand-new window can capture blank if hidden first, so it
        // stays visible and the overlay is drawn on top of it.
        // Hide the real window for every mode; the overlay (snapshot) is what the
        // user sees animating. Capture already happened above, so hiding now is safe.
        ShowWindow(hw, SW_HIDE);
        {
            std::lock_guard<std::mutex> lk(burnMtx_);
            burnTarget_ = hw;
            burnEffect_ = burn_cfg::effect;
        }
        burnProgress_.store(0.0f);
        burnMode_.store(mode);

        DBG("beginBurnAnim: START hw=%p mode=%d effect=%d dur=%d "
            "onOpen=%d onMax=%d onRestore=%d onTaskbar=%d onClose=%d onMin=%d",
            (void*)hw, (int)mode, (int)burn_cfg::effect, burn_cfg::durationMs,
            burn_cfg::onOpen, burn_cfg::onMaximize, burn_cfg::onRestore,
            burn_cfg::onTaskbar, burn_cfg::onClose, burn_cfg::onMinimize);

        showOverlay(true);
        renderRun_.store(true);
        try {
            renderThread_ = std::thread([this] { burnLoop(); });
        } catch (...) {
            // Thread spawn failed: undo the hide so the target is not lost.
            renderRun_.store(false);
            burnMode_.store(BurnMode::None);
            burnProgress_.store(0.0f);
            {
                std::lock_guard<std::mutex> lk(burnMtx_);
                burnTarget_ = NULL;
            }
            hideOverlay();
            ShowWindow(hw, SW_SHOWNORMAL);
            return false;
        }
        return true;
    }

    bool beginCloseAnim(HWND hw)      { return beginBurnAnim(hw, BurnMode::Close); }
    bool beginMinimizeAnim(HWND hw)   {
        return beginBurnAnim(hw, BurnMode::Minimize);
    }
    bool beginOpenAnim(HWND hw)       { return beginBurnAnim(hw, BurnMode::Open); }
    bool beginMaximizeAnim(HWND hw)   { return beginBurnAnim(hw, BurnMode::Maximize); }
    bool beginTaskbarAnim(HWND hw)    { return beginBurnAnim(hw, BurnMode::Taskbar); }
    bool beginRestoreAnim(HWND hw)    { return beginBurnAnim(hw, BurnMode::Restore); }

    // GetTickCount() window during which EVENT_OBJECT_HIDE for our own minimize
    // animation must be ignored.
    DWORD minSuppressUntilTick_ = 0;
    // GetTickCount() window during which EVENT_OBJECT_SHOW fired by the final
    // SW_MAXIMIZE / SW_RESTORE / SW_RESTORE (taskbar) of our own animation must
    // be ignored, so it is not re-detected as a new Open.
    DWORD showSuppressUntilTick_ = 0;

private:
    void cacheGeometry() {
        baseShadow_ = grabber_.shadowOffset();
        shadowOffset_ = baseShadow_;
        wndRect_ = grabber_.wnd();
        baseTexW_ = grabber_.texW();
        baseTexH_ = grabber_.texH();
        baseWndW_ = wndRect_.right - wndRect_.left;
        baseWndH_ = wndRect_.bottom - wndRect_.top;
        captureDpi_ = grabber_.captureDpi();
        if (captureDpi_ == 0) captureDpi_ = cfg::kBaseDpi;
        int capH = dpiapi::sysMetric(SM_CYCAPTION, captureDpi_) + dpiapi::sysMetric(SM_CYSIZEFRAME, captureDpi_) + dpiapi::sysMetric(SM_CXPADDEDBORDER, captureDpi_);
        if (capH < 24) capH = 24;
        if (baseTexH_ > 0 && capH > baseTexH_) capH = baseTexH_;
        baseCaptionH_ = capH;
        frameW_ = baseTexW_;
        frameH_ = baseTexH_;
        captionH_ = baseCaptionH_;
        curScale_ = 1.0f;
        lastScale_ = 1.0f;

        BOOL ds = FALSE;
        SystemParametersInfoW(SPI_GETDROPSHADOW, 0, &ds, 0);
        dropShadow_ = (ds != FALSE);
    }

    void applyScale(float s) {
        curScale_ = s;
        frameW_ = (int)(baseTexW_* s + 0.5f);
        frameH_ = (int)(baseTexH_* s + 0.5f);
        if (frameW_ < 1) frameW_ = 1;
        if (frameH_ < 1) frameH_ = 1;
        captionH_ = (int)(baseCaptionH_* s + 0.5f);
        if (captionH_ < 1) captionH_ = 1;
        if (frameH_ > 0 && captionH_ > frameH_) captionH_ = frameH_;
        shadowOffset_.x = (LONG)(baseShadow_.x* s + 0.5f);
        shadowOffset_.y = (LONG)(baseShadow_.y* s + 0.5f);
    }

    float scaleForPoint(POINT pt) {
        UINT dpi = dpiapi::forPoint(pt);
        UINT base = captureDpi_ ? captureDpi_ : cfg::kBaseDpi;
        return dpiapi::clampScale((float)dpi / (float)base);
    }

    void boundsFor(float rx, float ry, float& minL, float& maxL, float& minT, float& maxT) {
        RECT cap;
        cap.left = (LONG)floorf(rx);
        cap.top = (LONG)floorf(ry);
        cap.right = cap.left + (frameW_ > 0 ? frameW_ : 1);
        cap.bottom = cap.top + (captionH_ > 0 ? captionH_ : 1);

        RECT m;
        HMONITOR mon = MonitorFromRect(&cap, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        if (mon && GetMonitorInfoW(mon, &mi)) {
            m = mi.rcWork;
        } else {
            m.left = si_.vox; m.top = si_.voy;
            m.right = si_.vox + si_.w; m.bottom = si_.voy + si_.h;
        }

        int visX = frameW_ / 4;
        UINT base = captureDpi_ ? captureDpi_ : cfg::kBaseDpi;
        UINT mdpi = dpiapi::forMonitor(mon);
        float vscale = (float)mdpi / (float)cfg::kBaseDpi;
        int minVis = (int)(cfg::kMinCaptionVisible* vscale + 0.5f);
        int maxVis = (int)(cfg::kMaxCaptionVisible* vscale + 0.5f);
        if (visX < minVis) visX = minVis;
        if (visX > maxVis) visX = maxVis;
        if (visX > frameW_) visX = frameW_;

        int visY = captionH_;
        int mh = m.bottom - m.top;
        if (visY > mh) visY = mh;
        if (visY < 1) visY = 1;

        minL = (float)(m.left - (frameW_ - visX));
        maxL = (float)(m.right - visX);
        minT = (float)m.top;
        maxT = (float)(m.bottom - visY);
        if (maxL < minL) maxL = minL;
        if (maxT < minT) maxT = minT;
        (void)base;
    }

    void captureLayeredState(HWND w) {
        origExStyle_ = GetWindowLongPtrW(w, GWL_EXSTYLE);
        origLayered_ = (origExStyle_ & WS_EX_LAYERED) != 0;
        if (origLayered_) {
            if (!GetLayeredWindowAttributes(w, NULL, &origAlpha_, &origFlags_)) {
                origAlpha_ = 255; origFlags_ = LWA_ALPHA;
            }
        } else {
            origAlpha_ = 255; origFlags_ = 0;
            SetWindowLongPtrW(w, GWL_EXSTYLE, origExStyle_ | WS_EX_LAYERED);
        }
        SetLayeredWindowAttributes(w, 0, 0, LWA_ALPHA);
    }

    void showOverlay(bool show) {
        if (!overlay_ || !IsWindow(overlay_)) return;
        HWND tb = FindWindowW(L"Shell_TrayWnd", NULL);
        HWND tb2 = FindWindowW(L"Shell_SecondaryTrayWnd", NULL);
        UINT flags = SWP_NOACTIVATE | SWP_NOSENDCHANGING;
        if (show) flags |= SWP_SHOWWINDOW;
        SetWindowPos(overlay_, HWND_TOPMOST, si_.vox, si_.voy, si_.w, si_.h - 1, flags);
        if (tb && IsWindowVisible(tb))
            SetWindowPos(tb, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
        if (tb2 && IsWindowVisible(tb2))
            SetWindowPos(tb2, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
    }

    void hideOverlay() {
        if (!overlay_ || !IsWindow(overlay_)) return;
        SetWindowPos(overlay_, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSENDCHANGING | SWP_HIDEWINDOW);
    }

    void stopThreads() {
        renderRun_.store(false);
        if (renderThread_.joinable()) renderThread_.join();
        grabber_.stop();
    }

    void burnLoop() {
        using Clock = std::chrono::steady_clock;
        auto start = Clock::now();
        float dur = (float)burn_cfg::durationMs / 1000.0f;
        if (dur < 0.05f) dur = 0.05f;

        gpu_.updateContent(grabber_);

        while (renderRun_.load()) {
            float e = std::chrono::duration<float>(Clock::now() - start).count();
            float p = std::min(e / dur, 1.0f);
            burnProgress_.store(p);

            GridNode ctrl[16];
            {
                std::lock_guard<std::mutex> lk(bodyMtx_);
                if (body_.nodes.size() < 16) break;
                for (int i = 0; i < 16; ++i) ctrl[i] = body_.nodes[i];
            }
            BurnEffect ef;
            {
                std::lock_guard<std::mutex> lk(burnMtx_);
                ef = burnEffect_;
            }
            // AuraGlow: window animates (shrinks to a circle on exit/minimize,
            // grows from a circle on open). KWin runs progress 1->0 for closing;
            // flip for AuraGlow Close & Minimize so the window shrinks. Pass the
            // mode so the shader can time the glow differently per mode.
            float pp = p;
            int modeCode = 1; // 0=open, 1=exit/close, 2=minimize
            BurnMode bm = burnMode_.load();
            if (ef == BurnEffect::AuraGlow) {
                if (bm == BurnMode::Open) { pp = p; modeCode = 0; }
                else if (bm == BurnMode::Minimize) { pp = 1.0f - p; modeCode = 2; }
                else { pp = 1.0f - p; modeCode = 1; }
            }
            gpu_.drawBurnScene(ctrl, e, pp, ef, win11_, curScale_, modeCode);

            if (p >= 1.0f) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg::kRenderSleepMs));
        }

        grabber_.stop();
        HWND bt = NULL;
        BurnMode m = BurnMode::None;
        {
            std::lock_guard<std::mutex> lk(burnMtx_);
            bt = burnTarget_;
            burnTarget_ = NULL;
        }
        m = burnMode_.load();
        if (bt && IsWindow(bt)) {
            switch (m) {
                case BurnMode::Close: {
                    // The target was SW_HIDE-d in beginBurnAnim. If the app defers
                    // closing (e.g. a "save changes?" dialog), it would otherwise
                    // vanish while still running. Post WM_CLOSE, then watch briefly
                    // and re-show it if it is still alive. Detached so burnLoop does
                    // not block (the overlay is hidden immediately after this switch).
                    HWND w = bt;
                    PostMessageW(w, WM_CLOSE, 0, 0);
                    std::thread([w]() {
                        std::this_thread::sleep_for(std::chrono::milliseconds(800));
                        if (IsWindow(w)) {
                            ShowWindow(w, SW_SHOW);
                            BringWindowToTop(w);
                        }
                    }).detach();
                    break;
                }
                case BurnMode::Minimize: {
                    // Force-disable transition animations for this window so the native
                    // minimize slide never competes with our burn overlay (iMinAnimate is
                    // left enabled for maximize/restore). The target was SW_HIDE-d during
                    // capture, so no native animation is expected, but guard the edge case
                    // where SW_MINIMIZE leaves it hidden-but-not-iconic.
                    BOOL forced = TRUE;
                    DwmSetWindowAttribute(bt, DWMWA_TRANSITIONS_FORCEDISABLED, &forced, sizeof(forced));
                    ShowWindow(bt, SW_MINIMIZE);
                    if (!IsIconic(bt)) {
                        ShowWindow(bt, SW_RESTORE);
                        ShowWindow(bt, SW_MINIMIZE);
                    }
                    forced = FALSE;
                    DwmSetWindowAttribute(bt, DWMWA_TRANSITIONS_FORCEDISABLED, &forced, sizeof(forced));
                    break;
                }
                case BurnMode::Open:
                    // Was hidden at start so the overlay could own the emerge animation;
                    // reveal it now that the burn is finished.
                    ShowWindow(bt, SW_SHOWNORMAL);
                    break;
                case BurnMode::Maximize:
                    ShowWindow(bt, SW_MAXIMIZE);
                    break;
                case BurnMode::Taskbar:
                case BurnMode::Restore:
                    ShowWindow(bt, SW_RESTORE);
                    break;
                default:
                    break;
            }
        }
        // Reveal the real window before removing the overlay so there is no gap/flash.
        hideOverlay();
        burnMode_.store(BurnMode::None);
        burnProgress_.store(0.0f);
        renderRun_.store(false);
    }

    void renderFrame() {
        if (!gpu_.ready()) return;
        gpu_.updateContent(grabber_);

        GridNode ctrl[16];
        {
            std::lock_guard<std::mutex> lk(bodyMtx_);
            if (body_.nodes.size() < 16) return;
            for (int i = 0; i < 16; ++i) ctrl[i] = body_.nodes[i];
        }

        gpu_.drawScene(ctrl, win11_, dropShadow_, curScale_);
        gpu_.present();
    }

    void renderLoop() {
        int steady = 0;
        int frame = 0;
        while (renderRun_.load()) {
            ++frame;
            POINT cur;
            if (!GetCursorPos(&cur)) {
                std::lock_guard<std::mutex> lk(mouseMtx_);
                cur = curMouse_;
            }

            POINT anchorPt;
            {
                std::lock_guard<std::mutex> lk(bodyMtx_);
                int a = (body_.anchor >= 0 && body_.anchor < (int)body_.nodes.size()) ? body_.anchor : 0;
                anchorPt.x = (LONG)floorf(body_.nodes[a].x);
                anchorPt.y = (LONG)floorf(body_.nodes[a].y);
            }
            float scale = scaleForPoint(anchorPt);
            if (fabsf(scale - lastScale_) > 0.001f) {
                lastScale_ = scale;
                applyScale(scale);
                std::lock_guard<std::mutex> lk(bodyMtx_);
                body_.setRest((float)frameW_, (float)frameH_);
            }

            if (dragging_.load()) {
                float dx = (float)(cur.x - lastMouse_.x);
                float dy = (float)(cur.y - lastMouse_.y);
                lastMouse_ = cur;
                { std::lock_guard<std::mutex> lk(bodyMtx_); body_.moveAnchor(dx, dy); body_.integrate(cfg::kStepIters); }
                steady = 0;
            } else if (settling_.load()) {
                bool rest;
                {
                    std::lock_guard<std::mutex> lk(bodyMtx_);
                    body_.integrate(cfg::kStepIters);
                    float minL, maxL, minT, maxT;
                    boundsFor(body_.nodes[0].x, body_.nodes[0].y, minL, maxL, minT, maxT);
                    body_.clampTo(minL, maxL, minT, maxT, cfg::boundRestitution);
                    rest = body_.settled();
                }
                if (rest || pendingSnap_ != 0) ++steady; else steady = 0;
                if (steady > cfg::kSteadyExit || frame > cfg::kMaxSettleFrames || pendingSnap_ != 0) break;
            }

            renderFrame();
        }

        grabber_.stop();

        if (target_ && IsWindow(target_)) {
            POINT anchorPt;
            { std::lock_guard<std::mutex> lk(bodyMtx_); anchorPt.x = (LONG)floorf(body_.nodes[0].x); anchorPt.y = (LONG)floorf(body_.nodes[0].y); }
            float fscale = scaleForPoint(anchorPt);
            applyScale(fscale);

            float bx, by;
            {
                std::lock_guard<std::mutex> lk(bodyMtx_);
                body_.setRest((float)frameW_, (float)frameH_);
                body_.relax();
                float minL, maxL, minT, maxT;
                boundsFor(body_.nodes[0].x, body_.nodes[0].y, minL, maxL, minT, maxT);
                float rx = body_.nodes[0].x;
                float ry = body_.nodes[0].y;
                if (rx < minL) rx = minL; else if (rx > maxL) rx = maxL;
                if (ry < minT) ry = minT; else if (ry > maxT) ry = maxT;
                bx = rx; by = ry;
            }
            int fl = (int)floorf(bx + 0.5f);
            int ft = (int)floorf(by + 0.5f);
            int sox = (int)(baseShadow_.x* fscale + 0.5f);
            int soy = (int)(baseShadow_.y* fscale + 0.5f);
            int nl = fl - sox;
            int nt = ft - soy;
            SetWindowPos(target_, HWND_TOP, nl, nt, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);

            {
                WINDOWPLACEMENT fwp = { sizeof(WINDOWPLACEMENT) };
                if (GetWindowPlacement(target_, &fwp)) {
                    RECT cr;
                    if (GetWindowRect(target_, &cr)) {
                        int curW = cr.right - cr.left;
                        int curH = cr.bottom - cr.top;
                        RECT finalRect = { nl, nt, nl + curW, nt + curH };
                        HMONITOR fmon = MonitorFromRect(&finalRect, MONITOR_DEFAULTTONEAREST);
                        MONITORINFO fmi; fmi.cbSize = sizeof(fmi);
                        if (fmon && GetMonitorInfoW(fmon, &fmi)) {
                            OffsetRect(&finalRect, fmi.rcMonitor.left - fmi.rcWork.left, fmi.rcMonitor.top - fmi.rcWork.top);
                        }
                        fwp.flags = 0;
                        fwp.showCmd = SW_SHOWNORMAL;
                        fwp.rcNormalPosition = finalRect;
                        SetWindowPlacement(target_, &fwp);
                    }
                }
            }

            if (origLayered_) {
                SetLayeredWindowAttributes(target_, 0, origAlpha_, origFlags_);
                SetWindowPos(target_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
            } else {
                SetLayeredWindowAttributes(target_, 0, 255, LWA_ALPHA);
                SetWindowLongPtrW(target_, GWL_EXSTYLE, origExStyle_);
                SetWindowPos(target_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOSENDCHANGING);
                RedrawWindow(target_, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
            }
            SetForegroundWindow(target_);
            BringWindowToTop(target_);

            if (pendingSnap_ != 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                auto sendKey = [](WORD vk, bool down) {
                    INPUT in = { 0 };
                    in.type = INPUT_KEYBOARD;
                    in.ki.wVk = vk;
                    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
                    SendInput(1, &in, sizeof(INPUT));
                };

                if (pendingSnap_ == 1) {
                    sendKey(VK_LWIN, true); sendKey(VK_UP, true); sendKey(VK_UP, false); sendKey(VK_LWIN, false);
                } else if (pendingSnap_ == 2) {
                    sendKey(VK_LWIN, true); sendKey(VK_LEFT, true); sendKey(VK_LEFT, false); sendKey(VK_LWIN, false);
                } else if (pendingSnap_ == 3) {
                    sendKey(VK_LWIN, true); sendKey(VK_RIGHT, true); sendKey(VK_RIGHT, false); sendKey(VK_LWIN, false);
                } else if (pendingSnap_ == 4) {
                    sendKey(VK_LWIN, true); sendKey(VK_LEFT, true); sendKey(VK_LEFT, false);
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    sendKey(VK_UP, true); sendKey(VK_UP, false); sendKey(VK_LWIN, false);
                } else if (pendingSnap_ == 5) {
                    sendKey(VK_LWIN, true); sendKey(VK_LEFT, true); sendKey(VK_LEFT, false);
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    sendKey(VK_DOWN, true); sendKey(VK_DOWN, false); sendKey(VK_LWIN, false);
                } else if (pendingSnap_ == 6) {
                    sendKey(VK_LWIN, true); sendKey(VK_RIGHT, true); sendKey(VK_RIGHT, false);
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    sendKey(VK_UP, true); sendKey(VK_UP, false); sendKey(VK_LWIN, false);
                } else if (pendingSnap_ == 7) {
                    sendKey(VK_LWIN, true); sendKey(VK_RIGHT, true); sendKey(VK_RIGHT, false);
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    sendKey(VK_DOWN, true); sendKey(VK_DOWN, false); sendKey(VK_LWIN, false);
                }
                pendingSnap_ = 0;
            }
        }

        hideOverlay();
        settling_.store(false);
        renderRun_.store(false);
        target_ = NULL;
    }

    SoftBody body_;
    GpuCompositor gpu_;
    ScreenGrabber grabber_;
    std::thread renderThread_;
    std::atomic<bool> renderRun_{ false };
    std::atomic<bool> dragging_{ false };
    std::atomic<bool> settling_{ false };
    HWND target_ = NULL;
    HWND overlay_ = NULL;
    ScreenInfo si_ = {};
    bool win11_ = false;
    POINT lastMouse_ = {};
    POINT curMouse_ = {};
    std::mutex mouseMtx_;
    std::mutex bodyMtx_;
    POINT shadowOffset_ = {};
    POINT baseShadow_ = {};
    RECT wndRect_ = {};
    int frameW_ = 0;
    int frameH_ = 0;
    int captionH_ = 0;
    int baseTexW_ = 0;
    int baseTexH_ = 0;
    int baseWndW_ = 0;
    int baseWndH_ = 0;
    int baseCaptionH_ = 0;
    UINT captureDpi_ = cfg::kBaseDpi;
    float curScale_ = 1.0f;
    float lastScale_ = 1.0f;
    bool dropShadow_ = false;
    LONG_PTR origExStyle_ = 0;
    BYTE origAlpha_ = 255;
    DWORD origFlags_ = 0;
    bool origLayered_ = false;
    int pendingSnap_ = 0;

    std::atomic<BurnMode> burnMode_{ BurnMode::None };
    std::atomic<float>    burnProgress_{ 0.0f };
    BurnEffect            burnEffect_{ BurnEffect::Incinerate };
    HWND                  burnTarget_{ NULL };
    std::mutex            burnMtx_;
};


#ifdef _WIN32
namespace {

std::wstring narrow_to_wide(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return {};
    }
    const int length = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(length - 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text, -1, wide.data(), length);
    return wide;
}

std::wstring quote_command_line_arg(const std::wstring& arg) {
    if (arg.find_first_of(L" \t\"") == std::wstring::npos) {
        return arg;
    }
    std::wstring quoted = L"\"";
    for (const wchar_t ch : arg) {
        if (ch == L'"') {
            quoted += L"\\\"";
        } else {
            quoted += ch;
        }
    }
    quoted += L'"';
    return quoted;
}

std::wstring build_command_line_params(int argc, char* argv[]) {
    std::wstring params;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr || argv[i][0] == '\0') {
            continue;
        }
        if (!params.empty()) {
            params += L' ';
        }
        params += quote_command_line_arg(narrow_to_wide(argv[i]));
    }
    return params;
}

bool has_background_arg(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr && std::strcmp(argv[i], "--background") == 0) {
            return true;
        }
    }
    return false;
}

} // namespace
#endif

#define WOBBLIN_CLICK_INJECTED_MAGIC 0xB675AF3C

class WobblyController {
public:
    // Lets the free function wobblyAnimExcluded() (defined after this class)
    // read the private static s_self + excludedAnim_ set.
    friend bool wobblyAnimExcluded(HWND hw);

    // which is a separate class, hence these public helpers instead of direct
    // access to the private sets.
    void setWobbleExclusions(const std::set<QString, IStringLess>& s) { excludedWobble_ = s; }
    void setAnimExclusions(const std::set<QString, IStringLess>& s)   { excludedAnim_   = s; }
    void clearExclusions() { excludedWobble_.clear(); excludedAnim_.clear(); }

    static bool ensureElevated(int argc, char* argv[]) {
        BOOL elevated = FALSE;
        HANDLE tok = NULL;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
            TOKEN_ELEVATION e;
            DWORD cb = sizeof(e);
            if (GetTokenInformation(tok, TokenElevation, &e, sizeof(e), &cb))
                elevated = e.TokenIsElevated;
            CloseHandle(tok);
        }
        if (!elevated) {
            wchar_t path[MAX_PATH];
            if (GetModuleFileNameW(NULL, path, MAX_PATH)) {
                const std::wstring params = build_command_line_params(argc, argv);
                SHELLEXECUTEINFOW sei = { 0 };
                sei.cbSize = sizeof(SHELLEXECUTEINFOW);
                sei.lpVerb = L"runas";
                sei.lpFile = path;
                sei.lpParameters = params.empty() ? nullptr : params.c_str();
                sei.nShow = has_background_arg(argc, argv) ? SW_HIDE : SW_SHOWNORMAL;
                if (ShellExecuteExW(&sei)) return false;
            }
        }
        return true;
    }

    bool init(HINSTANCE hInst, HWND uiHwnd) {
        s_self = this;
        hInst_ = hInst;
        ui_hwnd_ = uiHwnd;
        detectWin11();

        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;
        com_init_ = SUCCEEDED(hr);

        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        dpiapi::init();

        si_.vox = GetSystemMetrics(SM_XVIRTUALSCREEN);
        si_.voy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        si_.w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        si_.h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (si_.w <= 0) si_.w = GetSystemMetrics(SM_CXSCREEN);
        if (si_.h <= 0) si_.h = GetSystemMetrics(SM_CYSCREEN);

        WNDCLASSW wc2 = {};
        wc2.lpfnWndProc = OverlayProc;
        wc2.hInstance = hInst;
        wc2.lpszClassName = L"WobblyOverlay";
        wc2.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassW(&wc2);

        overlay_ = CreateWindowExW(
            WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
            L"WobblyOverlay", L"", WS_POPUP,
            si_.vox, si_.voy, si_.w, si_.h, NULL, NULL, hInst, NULL);

        if (!engine_.init(overlay_, si_, win11_)) return false;

        hook_ = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, hInst, 0);
        kb_hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, KbProc, hInst, 0);

        // Set up WinEvent hook for open / taskbar restore / taskbar minimize detection.
        // eventMin must be <= eventMax: EVENT_OBJECT_SHOW (0x8002) < EVENT_OBJECT_HIDE
        // (0x8003). The original order was reversed, so the hook delivered nothing.
        winEventHook_ = SetWinEventHook(
            EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE,
            NULL, WinEventProc, 0, 0,
            WINEVENT_OUTOFCONTEXT);

        // Populate knownWindows_ with already-visible captioned windows so
        // focus switches between them don't trigger "Open" animations.
        EnumWindows([](HWND hw, LPARAM) -> BOOL {
            if (!hw || !IsWindowVisible(hw)) return TRUE;
            LONG style = GetWindowLongW(hw, GWL_STYLE);
            if (((style & WS_CAPTION) || (hw == s_self->ui_hwnd_)) && !(style & WS_MINIMIZE)) {
                HWND root = GetAncestor(hw, GA_ROOT);
                if (root && root != hw) hw = root; // use top-level
                if (!IsExcludedClass(hw)) {
                    auto* self = WobblyController::s_self;
                    if (self) {
                        std::lock_guard<std::mutex> lk(self->minMtx_);
                        self->knownWindows_.insert(hw);
                    }
                }
            }
            return TRUE;
        }, 0);

        return hook_ != NULL;
    }

    void shutdown() {
        ConfigureNativeWindowAnimations(false, savedMinAnimate_);
        if (winEventHook_) { UnhookWinEvent(winEventHook_); winEventHook_ = NULL; }
        if (kb_hook_) { UnhookWindowsHookEx(kb_hook_); kb_hook_ = NULL; }
        if (hook_) { UnhookWindowsHookEx(hook_); hook_ = NULL; }
        engine_.shutdown();
        if (com_init_) CoUninitialize();
        s_self = nullptr;
    }

    void setEnabled(bool on) { enabled_.store(on); }
    bool isEnabled() const { return enabled_.load(); }

    void setRealismLevel(int level) {
        cfg::applyRealismLevel(level);
        realism_ = level;
    }

    int realismLevel() const { return realism_; }

    void setBurnEnabled(bool on) {
        burnEnabled_ = on;
        // Leave Windows' built-in window animation enabled so that maximize /
        // restore use the native effect. Custom Open / Close / Minimize hide the
        // target window before animating, so they never conflict with native
        // transitions. We therefore no longer disable iMinAnimate globally.
    }
    void setBurnEffect(int e) {
        burnEffect_ = static_cast<BurnEffect>(e);
        burn_cfg::effect = burnEffect_;
    }
    void setBurnDuration(int ms) {
        burnDurationMs_ = ms;
        burn_cfg::durationMs = ms;
    }

    bool handleUiMessage(UINT msg, WPARAM wParam, LPARAM) {
        if (msg == WM_USER + 1) {
            HWND target = pendingTarget_;
            POINT pt = pendingPt_;
            pendingTarget_ = NULL;
            dragRequested_.store(false);
            if (target && !engine_.isDragging() && !engine_.isSettling()) {
                SetWindowPos(target, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOSENDCHANGING);
                SetForegroundWindow(target);
                BringWindowToTop(target);
                engine_.beginDrag(target, pt);
            }
            return true;
        }
        if (msg == WM_USER + 2) {
            INPUT inputs[2] = {};
            inputs[0].type = INPUT_MOUSE;
            inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            inputs[0].mi.dwExtraInfo = WOBBLIN_CLICK_INJECTED_MAGIC;
            inputs[1].type = INPUT_MOUSE;
            inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
            inputs[1].mi.dwExtraInfo = WOBBLIN_CLICK_INJECTED_MAGIC;
            SendInput(2, inputs, sizeof(INPUT));
            return true;
        }
        if (msg == WM_USER + 3) {
            HWND tgt = (HWND)wParam;
            bool go = tgt && burn_cfg::onClose && burnEnabled_
                && !engine_.isDragging() && !engine_.isSettling()
                && engine_.burnIdle();
            DBG("handleUi WM_USER+3 Close hw=%p onClose=%d go=%d", (void*)tgt, burn_cfg::onClose, go);
            if (go) {
                if (!engine_.beginCloseAnim(tgt) && IsWindow(tgt))
                    PostMessageW(tgt, WM_CLOSE, 0, 0);
            }
            return true;
        }
        if (msg == WM_USER + 4) {
            HWND tgt = (HWND)wParam;
            bool go = tgt && burn_cfg::onMinimize && burnEnabled_
                && !engine_.isDragging() && !engine_.isSettling()
                && engine_.burnIdle();
            DBG("handleUi WM_USER+4 Minimize hw=%p onMinimize=%d go=%d", (void*)tgt, burn_cfg::onMinimize, go);
            if (go) {
                if (!engine_.beginMinimizeAnim(tgt) && IsWindow(tgt))
                    ShowWindow(tgt, SW_MINIMIZE);
            }
            return true;
        }
        if (msg == WM_USER + 5) {
            HWND tgt = (HWND)wParam;
            bool go = tgt && burn_cfg::onOpen && burnEnabled_
                && !engine_.isDragging() && !engine_.isSettling()
                && engine_.burnIdle();
            DBG("handleUi WM_USER+5 Open hw=%p onOpen=%d go=%d", (void*)tgt, burn_cfg::onOpen, go);
            if (go) {
                engine_.beginOpenAnim(tgt);
            }
            return true;
        }        return false;
    }

private:
    void detectWin11() {
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        if (nt) {
            RtlGetVersionPtr p = (RtlGetVersionPtr)GetProcAddress(nt, "RtlGetVersion");
            if (p) {
                RTL_OSVERSIONINFOW_CUSTOM v = { 0 };
                v.dwOSVersionInfoSize = sizeof(v);
                if (p(&v) == 0 && v.dwBuildNumber >= 22000) win11_ = true;
            }
        }
    }

    LRESULT onMouse(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode >= 0) {
            MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lParam;
            if (ms && ms->dwExtraInfo == WOBBLIN_CLICK_INJECTED_MAGIC) {
                return CallNextHookEx(hook_, nCode, wParam, lParam);
            }
        }

        // BURN pre-check: INDEPENDENT of enabled_ (wobbly drag toggle)
        if (nCode >= 0 && burnEnabled_
            && !engine_.isDragging() && !engine_.isSettling() && engine_.burnIdle()
            && wParam == WM_LBUTTONDOWN) {
            MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lParam;
            if (ms) {
                HWND hw = WindowFromPoint(ms->pt);
                if (hw) {
                    HWND top = GetAncestor(hw, GA_ROOT);
                    if (top && IsWindowVisible(top) && !IsExcludedClass(top)
                        && top != overlay_) {
                        if (IsAppExcluded(top, excludedAnim_)) {
                            return CallNextHookEx(hook_, nCode, wParam, lParam);
                        }
                        LONG style = GetWindowLongW(top, GWL_STYLE);
                        if (((style & WS_CAPTION) || (top == ui_hwnd_)) && !(style & WS_MINIMIZE)) {
                            DWORD_PTR htRes = 0;
                            if (SendMessageTimeoutW(top, WM_NCHITTEST, 0,
                                    MAKELPARAM(ms->pt.x, ms->pt.y),
                                    SMTO_ABORTIFHUNG, 80, &htRes)) {
                                bool maximized = (style & WS_MAXIMIZE) != 0;
                                DBG("onMouse: ht=%ld hw=%p maximized=%d onMax=%d onRestore=%d onClose=%d onMin=%d",
                                    (long)htRes, (void*)top, maximized,
                                    burn_cfg::onMaximize, burn_cfg::onRestore,
                                    burn_cfg::onClose, burn_cfg::onMinimize);
                                if ((LRESULT)htRes == HTCLOSE && burn_cfg::onClose) {
                                    DBG("onMouse: -> WM_USER+3 (Close) hw=%p", (void*)top);
                                    if (ui_hwnd_) PostMessageW(ui_hwnd_, WM_USER + 3, (WPARAM)top, 0);
                                    return 1;
                                } else if ((LRESULT)htRes == HTMINBUTTON && burn_cfg::onMinimize) {
                                    DBG("onMouse: -> WM_USER+4 (Minimize) hw=%p", (void*)top);
                                    if (ui_hwnd_) PostMessageW(ui_hwnd_, WM_USER + 4, (WPARAM)top, 0);
                                    return 1;
                                }
                            }
                        }
                    }
                }
            }
        }

        // WOBBLY drag: gated on enabled_
        if (nCode >= 0 && enabled_.load()) {
            MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lParam;
            if (wParam == WM_LBUTTONDOWN && !engine_.isDragging() && !engine_.isSettling() && pendingTarget_ == NULL) {
                POINT pt = ms->pt;
                HWND hw = WindowFromPoint(pt);
                if (hw) {
                    HWND top = GetAncestor(hw, GA_ROOT);
                    if (top && IsWindowVisible(top) && !IsExcludedClass(top) && top != overlay_) {
                        if (IsAppExcluded(top, excludedWobble_)) {
                            return CallNextHookEx(hook_, nCode, wParam, lParam);
                        }
                        LONG style = GetWindowLongW(top, GWL_STYLE);
                        if (!(style & WS_MINIMIZE) && ((style & WS_CAPTION) || (top == ui_hwnd_))) {
                            DWORD_PTR result = 0;
                            SendMessageTimeoutW(top, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y), SMTO_ABORTIFHUNG, 80, &result);
                            if (result == HTCAPTION) {
                                DWORD nowTick = GetTickCount();
                                if (nowTick - lastCaptionClickTick_ <= (DWORD)GetDoubleClickTime()) {
                                    // Genuine title-bar double-click: let the OS toggle
                                    // maximize/restore instead of starting a wobbly drag.
                                    lastCaptionClickTick_ = 0;
                                    PostMessageW(top, WM_NCLBUTTONDBLCLK, HTCAPTION, MAKELPARAM(pt.x, pt.y));
                                    return 1;
                                }
                                lastCaptionClickTick_ = nowTick;
                                LRESULT defResult = DefWindowProcW(top, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y));
                                if (defResult == HTMINBUTTON || defResult == HTMAXBUTTON || defResult == HTCLOSE || defResult == HTSYSMENU || defResult == HTHELP) {
                                    return CallNextHookEx(hook_, nCode, wParam, lParam);
                                }
                                RECT btnRect = { 0 };
                                if (SUCCEEDED(DwmGetWindowAttribute(top, 5, &btnRect, sizeof(btnRect)))) {
                                    if (btnRect.right > btnRect.left && btnRect.bottom > btnRect.top) {
                                        if (PtInRect(&btnRect, pt)) {
                                            return CallNextHookEx(hook_, nCode, wParam, lParam);
                                        }
                                        RECT wndRect;
                                        if (GetWindowRect(top, &wndRect)) {
                                            RECT screenBtnRect = {
                                                wndRect.left + btnRect.left,
                                                wndRect.top + btnRect.top,
                                                wndRect.left + btnRect.right,
                                                wndRect.top + btnRect.bottom
                                            };
                                            if (PtInRect(&screenBtnRect, pt)) {
                                                return CallNextHookEx(hook_, nCode, wParam, lParam);
                                            }
                                        }
                                    }
                                }
                                SetWindowPos(top, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                                SetForegroundWindow(top);
                                BringWindowToTop(top);
                                pendingTarget_ = top;
                                pendingPt_ = pt;
                                dragRequested_.store(false);
                                return 1;
                            }
                        }
                    }
                }
            } else if (wParam == WM_MOUSEMOVE) {
                if (pendingTarget_ != NULL && !engine_.isDragging() && !dragRequested_.load()) {
                    int dx = ms->pt.x - pendingPt_.x;
                    int dy = ms->pt.y - pendingPt_.y;
                    UINT mdpi = dpiapi::forPoint(pendingPt_);
                    float ds = (float)mdpi / (float)cfg::kBaseDpi;
                    int thresh = (int)(16.0f* ds* ds);
                    if (thresh < 16) thresh = 16;
                    if (dx *dx + dy* dy > thresh) {
                        dragRequested_.store(true);
                        if (ui_hwnd_) PostMessageW(ui_hwnd_, WM_USER + 1, 0, 0);
                    }
                }
                if (engine_.isDragging()) engine_.updateDrag(ms->pt);
            } else if (wParam == WM_LBUTTONUP) {
                if (pendingTarget_ != NULL && !engine_.isDragging()) {
                    pendingTarget_ = NULL;
                    dragRequested_.store(false);
                    if (ui_hwnd_) {
                        PostMessageW(ui_hwnd_, WM_USER + 2, 0, 0);
                    }
                    return 1;
                }
                if (engine_.isDragging()) {
                    engine_.endDrag(ms->pt);
                    return 1;
                }
            }
        }
        return CallNextHookEx(hook_, nCode, wParam, lParam);
    }

    static LRESULT CALLBACK OverlayProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (m == WM_SETCURSOR) { SetCursor(LoadCursor(NULL, IDC_ARROW)); return TRUE; }
        return DefWindowProcW(h, m, w, l);
    }
    static LRESULT CALLBACK MouseProc(int n, WPARAM w, LPARAM l) {
        return s_self ? s_self->onMouse(n, w, l) : CallNextHookEx(NULL, n, w, l);
    }

    static LRESULT CALLBACK KbProc(int n, WPARAM w, LPARAM l) {
        if (n >= 0 && w == WM_SYSKEYDOWN) {
            KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)l;
            if (k && k->vkCode == VK_F4 && (GetAsyncKeyState(VK_MENU) & 0x8000)) {
                if (s_self && burn_cfg::onClose && s_self->burnEnabled_
                    && !s_self->engine_.isDragging() && !s_self->engine_.isSettling()
                    && s_self->engine_.burnIdle()) {
                    HWND fg = GetForegroundWindow();
                    if (fg && fg != s_self->overlay_
                        && !IsExcludedClass(fg)) {
                        LONG st = GetWindowLongW(fg, GWL_STYLE);
                        if (((st & WS_CAPTION) || (fg == s_self->ui_hwnd_)) && !(st & WS_MINIMIZE)) {
                            s_self->pendingBurnTarget_ = fg;
                            if (s_self->ui_hwnd_)
                                PostMessageW(s_self->ui_hwnd_, WM_USER + 3, (WPARAM)fg, 0);
                            return 1;
                        }
                    }
                }
            }
        }
        HHOOK kh = (s_self ? s_self->kb_hook_ : NULL);
        return CallNextHookEx(kh, n, w, l);
    }

    static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
        if (!s_self || !s_self->burnEnabled_) return;
        if (event != EVENT_OBJECT_SHOW && event != EVENT_OBJECT_HIDE && event != EVENT_OBJECT_DESTROY) return;
        // For DESTROY the window may already be gone, so skip the IsWindow liveness
        // check there; we only need its (still-valid) handle value to clean up sets.
        if (event != EVENT_OBJECT_DESTROY && (!hwnd || !IsWindow(hwnd))) return;

        // Derive the root owner window. WinEvent can report a child/sub-window;
        // OBJID_WINDOW is not always honoured by every target process, so we
        // resolve to GA_ROOT and gate on having a real captioned window instead.
        HWND target = GetAncestor(hwnd, GA_ROOT);
        if (!target) target = hwnd;
        if (target == s_self->overlay_) return;
        if (IsExcludedClass(target)) return;
        // Only react to events for the window itself (idChild == CHILDID_SELF / 0).
        // Inner controls, video frames, tab contents, etc. fire SHOW/HIDE/DESTROY
        // with idChild != 0; treating those as window Open/Minimize is what caused
        // stray animations during in-app activity (e.g. YouTube seek, clicking
        // controls). Ignore them so animation only happens for real app
        // open / close / minimize.
        if (idChild != 0) return;

        if (event == EVENT_OBJECT_DESTROY) {
            // Window destroyed: drop tracking entries so a later HWND reuse does
            // not misclassify a brand-new window (false Open skip / false restore).
            // Also record process ID to suppress Open animation on same-process
            // window replacement (e.g. Notepad/PowerShell tab switching).
            DWORD pid = 0;
            GetWindowThreadProcessId(target, &pid);
            {
                std::lock_guard<std::mutex> lk(s_self->minMtx_);
                s_self->knownWindows_.erase(target);
                s_self->recentlyMinimized_.erase(target);
                if (pid) {
                    s_self->recentlyDestroyed_.push_back({pid, GetTickCount()});
                }
            }
            return;
        }

        LONG style = GetWindowLongW(target, GWL_STYLE);
        if (!(style & WS_CAPTION) && (target != s_self->ui_hwnd_)) {
            DBG("WinEvent: drop hw=%p idObj=%ld ev=%lu no-caption", (void*)target, idObject, event);
            return;
        }

        if (event == EVENT_OBJECT_HIDE) {
            // Do NOT play a custom minimize for hides we did not initiate from the
            // caption button (e.g. Win+M / taskbar context menu) -- those use
            // Windows' built-in minimize animation. Track iconic hides so a later
            // SHOW is recognized as a restore (not a fresh Open).
            // Non-iconic hides (e.g. tab switch) do NOT erase from knownWindows_,
            // because the top-level window still exists and will show again.
            bool iconic = IsIconic(target);
            DBG("WinEvent: HIDE hw=%p idObj=%ld iconic=%d", (void*)target, idObject, iconic);
            if (iconic) {
                std::lock_guard<std::mutex> lk(s_self->minMtx_);
                s_self->recentlyMinimized_.insert(target);
            } else {
                // Non-iconic hide (e.g. tab switch) - keep in knownWindows_ so the
                // subsequent SHOW on the same window doesn't trigger Open animation.
                // Actual destruction is handled by EVENT_OBJECT_DESTROY.
                std::lock_guard<std::mutex> lk(s_self->minMtx_);
                s_self->recentlyMinimized_.erase(target);
            }
            return;
        }

        if (!IsWindowVisible(target)) {
            DBG("WinEvent: SHOW hw=%p not-visible", (void*)target);
            return;
        }
        // Ignore SHOW events our own Maximize/Restore/Taskbar animation produces.
        if (GetTickCount() < s_self->engine_.showSuppressUntilTick_) {
            DBG("WinEvent: SHOW hw=%p self-trigger skipped (suppress window)", (void*)target);
            return;
        }
        bool restored = false;
        bool known = false;
        bool sameProcessReplacement = false;
        DWORD pid = 0;
        GetWindowThreadProcessId(target, &pid);
        {
            std::lock_guard<std::mutex> lk(s_self->minMtx_);
            auto it = s_self->recentlyMinimized_.find(target);
            if (it != s_self->recentlyMinimized_.end()) {
                restored = true;
                s_self->recentlyMinimized_.erase(it);
            }
            known = s_self->knownWindows_.find(target) != s_self->knownWindows_.end();
            if (!known) {
                // Check if this is a same-process window replacement (tab switch).
                if (pid) {
                    DWORD now = GetTickCount();
                    for (auto it2 = s_self->recentlyDestroyed_.begin(); it2 != s_self->recentlyDestroyed_.end(); ) {
                        if (it2->pid == pid && now - it2->tick < 1500) {
                            sameProcessReplacement = true;
                            break;
                        }
                        if (now - it2->tick >= 1500) {
                            it2 = s_self->recentlyDestroyed_.erase(it2);
                        } else {
                            ++it2;
                        }
                    }
                }
                if (!sameProcessReplacement) {
                    s_self->knownWindows_.insert(target);
                }
            }
        }

        if ((restored || (!known && !sameProcessReplacement)) && burn_cfg::onOpen && s_self->ui_hwnd_) {
            if (IsAppExcluded(target, s_self->excludedAnim_)) {
                DBG("WinEvent: SHOW hw=%p -> OPEN skipped (anim excluded)", (void*)target);
            } else {
            DBG("WinEvent: SHOW hw=%p -> OPEN (WM_USER+5) restored=%d known=%d sameProc=%d",
                (void*)target, restored, known, sameProcessReplacement);
            PostMessageW(s_self->ui_hwnd_, WM_USER + 5, (WPARAM)target, 0);
            }
        } else {
            DBG("WinEvent: SHOW hw=%p ignored restored=%d known=%d sameProc=%d onOpen=%d",
                (void*)target, restored, known, sameProcessReplacement, burn_cfg::onOpen);
        }
    }

    static WobblyController* s_self;
    HINSTANCE hInst_ = NULL;
    HWND ui_hwnd_ = NULL;
    HWND overlay_ = NULL;
    HHOOK hook_ = NULL;
    ScreenInfo si_ = {};
    bool win11_ = false;
    bool com_init_ = false;
    WobblyEngine engine_;
    std::atomic<bool> enabled_{ false };
    std::atomic<bool> dragRequested_{ false };
    HWND pendingTarget_ = NULL;
    POINT pendingPt_ = {};
    DWORD lastCaptionClickTick_ = 0;
    // Track recently destroyed windows by process ID to suppress Open animation
    // on same-process window replacement (e.g. Notepad/PowerShell tab switching).
    struct DestroyedInfo { DWORD pid; DWORD tick; };
    std::vector<DestroyedInfo> recentlyDestroyed_;
    int realism_ = 3;
    HHOOK kb_hook_ = NULL;
    HWINEVENTHOOK winEventHook_ = NULL;
    HWND  pendingBurnTarget_ = NULL;
    bool  burnEnabled_ = true;
    int   savedMinAnimate_ = -1;
    std::unordered_set<HWND> recentlyMinimized_;
    std::unordered_set<HWND> knownWindows_;
    std::set<QString, IStringLess> excludedWobble_;
    std::set<QString, IStringLess> excludedAnim_;
    std::mutex                minMtx_;
    BurnEffect burnEffect_ = BurnEffect::Incinerate;
    int   burnDurationMs_ = 600;
};

WobblyController* WobblyController::s_self = nullptr;

// Defined here (after WobblyController) so it can read s_self + the Animation
// exclusion set. Called from WobblyEngine::beginBurnAnim to skip burn animations
// for apps excluded from the Animation list.
bool wobblyAnimExcluded(HWND hw) {
    if (WobblyController::s_self == nullptr) return false;
    return IsAppExcluded(hw, WobblyController::s_self->excludedAnim_);
}

namespace {

    constexpr int    kWindowWidth        = 410;
    constexpr int    kWindowHeight       = 670;
    constexpr int    kToggleWidth        = 48;
    constexpr int    kToggleHeight       = 24;
    constexpr int    kSliderWidth        = 330;
    constexpr int    kSliderHeight       = 52;
    constexpr int    kSliderLevels       = 4;
    constexpr int    kToggleAnimMs       = 180;
    constexpr int    kSliderAnimMs       = 160;
    constexpr int    kIconButtonSz       = 32;
    constexpr double kToggleGapPx        = 4.0;

    [[nodiscard]] bool is_valid_hex_color(const QString& s) noexcept {
        static const QRegularExpression rx(QStringLiteral("^#[0-9A-Fa-f]{6}"));
        if (s.isEmpty()) {
            return false;
        }
        return rx.match(s).hasMatch();
    }

    [[nodiscard]] QString safe_hex_color(const QString& s, const QString& fallback) noexcept {
        if (is_valid_hex_color(s)) {
            return s;
        }
        if (is_valid_hex_color(fallback)) {
            return fallback;
        }
        return QStringLiteral("#ffffff");
    }

    [[nodiscard]] int clamp_channel(int v) noexcept {
        if (v < 0) {
            return 0;
        }
        if (v > 255) {
            return 255;
        }
        return v;
    }

    [[nodiscard]] double clamp_double(double v, double lo, double hi) noexcept {
        if (std::isnan(v) || std::isinf(v)) {
            return lo;
        }
        if (v < lo) {
            return lo;
        }
        if (v > hi) {
            return hi;
        }
        return v;
    }

    void secure_qt_message_handler(QtMsgType, const QMessageLogContext&, const QString&) noexcept {}

    [[noreturn]] void secure_terminate_handler() noexcept {
        std::_Exit(EXIT_FAILURE);
    }

    void secure_new_handler() {
        std::_Exit(EXIT_FAILURE);
    }

#ifdef _WIN32
    void apply_windows_mitigations() noexcept {
        ::HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
        ::SetDllDirectoryW(L"");
        ::SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);

        PROCESS_MITIGATION_DEP_POLICY dep{};
        dep.Enable    = 1;
        dep.Permanent = 1;
        (void)::SetProcessMitigationPolicy(ProcessDEPPolicy, &dep, sizeof(dep));

        PROCESS_MITIGATION_ASLR_POLICY aslr{};
        aslr.EnableBottomUpRandomization = 1;
        aslr.EnableForceRelocateImages   = 1;
        aslr.EnableHighEntropy           = 1;
        aslr.DisallowStrippedImages      = 1;
        (void)::SetProcessMitigationPolicy(ProcessASLRPolicy, &aslr, sizeof(aslr));

        PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY shcp{};
        shcp.RaiseExceptionOnInvalidHandleReference = 1;
        shcp.HandleExceptionsPermanentlyEnabled     = 1;
        (void)::SetProcessMitigationPolicy(ProcessStrictHandleCheckPolicy, &shcp, sizeof(shcp));

        PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY epd{};
        epd.DisableExtensionPoints = 1;
        (void)::SetProcessMitigationPolicy(ProcessExtensionPointDisablePolicy, &epd, sizeof(epd));

        PROCESS_MITIGATION_IMAGE_LOAD_POLICY ilp{};
        ilp.NoRemoteImages            = 1;
        ilp.NoLowMandatoryLabelImages = 1;
        ilp.PreferSystem32Images      = 1;
        (void)::SetProcessMitigationPolicy(ProcessImageLoadPolicy, &ilp, sizeof(ilp));

        PROCESS_MITIGATION_CONTROL_FLOW_GUARD_POLICY cfg{};
        cfg.EnableControlFlowGuard  = 1;
        cfg.EnableExportSuppression = 0;
        cfg.StrictMode              = 0;
        (void)::SetProcessMitigationPolicy(ProcessControlFlowGuardPolicy, &cfg, sizeof(cfg));
    }
#endif

    class SemaphoreGuard final {
    public:
        explicit SemaphoreGuard(QSystemSemaphore& s) noexcept : m_sem(s), m_acquired(false) {
            m_acquired = m_sem.acquire();
        }
        ~SemaphoreGuard() noexcept {
            if (m_acquired) {
                m_sem.release();
            }
        }
        SemaphoreGuard(const SemaphoreGuard&)            = delete;
        SemaphoreGuard& operator=(const SemaphoreGuard&) = delete;
        SemaphoreGuard(SemaphoreGuard&&)                 = delete;
        SemaphoreGuard& operator=(SemaphoreGuard&&)      = delete;

        [[nodiscard]] bool acquired() const noexcept { return m_acquired; }

    private:
        QSystemSemaphore& m_sem;
        bool              m_acquired;
    };

    constexpr int kDonateWindowWidth  = 440;
    constexpr int kDonateWindowHeight = 280;

    inline const QString kLocalServerName = QStringLiteral("Wobblin.LocalServer.v1");
    inline const QString kStartupArg      = QStringLiteral("--background");

    void center_window_on_screen(QWidget* widget) noexcept {
        if (widget == nullptr) {
            return;
        }
        QScreen* const screen = QGuiApplication::primaryScreen();
        if (screen == nullptr) {
            return;
        }
        const QRect avail = screen->availableGeometry();
        const int   x     = avail.x() + (avail.width()  - widget->width())  / 2;
        const int   y     = avail.y() + (avail.height() - widget->height()) / 2;
        widget->move(x, y);
    }

    namespace AppPersistence {

        [[nodiscard]] QSettings settings() {
            return QSettings(
                QStringLiteral("Wobblin"),
                QStringLiteral("Wobblin")
            );
        }

        [[nodiscard]] bool wobblyEnabled() {
            return settings().value(QStringLiteral("wobbly/enabled"), true).toBool();
        }

        void setWobblyEnabled(bool enabled) {
            settings().setValue(QStringLiteral("wobbly/enabled"), enabled);
            settings().sync();
        }

        [[nodiscard]] int wobblyRealism(int max_level) {
            return std::clamp(
                settings().value(QStringLiteral("wobbly/realism"), 4).toInt(),
                1,
                max_level
            );
        }

        void setWobblyRealism(int level) {
            settings().setValue(QStringLiteral("wobbly/realism"), level);
            settings().sync();
        }

        [[nodiscard]] bool burnEnabled() {
            static bool migrated = false;
            if (!migrated) {
                migrated = true;
                QSettings s = settings();
                if (!s.contains(QStringLiteral("burn/migrated"))) {
                    // Discard any stale pre-default-on value so the ON default applies.
                    s.remove(QStringLiteral("burn/enabled"));
                    s.setValue(QStringLiteral("burn/migrated"), 1);
                    s.sync();
                }
            }
            return settings().value(QStringLiteral("burn/enabled"), true).toBool();
        }
        static void setBurnEnabled(bool v) {
            settings().setValue(QStringLiteral("burn/enabled"), v);
            settings().sync();
        }
        [[nodiscard]] bool burnCloseEnabled() {
            return settings().value(QStringLiteral("burn/close"), true).toBool();
        }
        static void setBurnCloseEnabled(bool v) {
            settings().setValue(QStringLiteral("burn/close"), v);
            settings().sync();
        }
        [[nodiscard]] bool burnMinimizeEnabled() {
            return settings().value(QStringLiteral("burn/minimize"), true).toBool();
        }
        static void setBurnMinimizeEnabled(bool v) {
            settings().setValue(QStringLiteral("burn/minimize"), v);
            settings().sync();
        }
        [[nodiscard]] bool burnOpenEnabled() {
            return settings().value(QStringLiteral("burn/open"), true).toBool();
        }
        static void setBurnOpenEnabled(bool v) {
            settings().setValue(QStringLiteral("burn/open"), v);
            settings().sync();
        }        [[nodiscard]] int burnEffect() {
            return std::clamp(settings().value(QStringLiteral("burn/effect"), 7).toInt(), 1, 7);
        }
        static void setBurnEffect(int e) {
            settings().setValue(QStringLiteral("burn/effect"), e);
            settings().sync();
        }
        [[nodiscard]] int burnDuration() {
            return settings().value(QStringLiteral("burn/duration"), 750).toInt();
        }
        static void setBurnDuration(int ms) {
            settings().setValue(QStringLiteral("burn/duration"), ms);
            settings().sync();
        }

        void ensureStartupRegistration() {
#ifdef _WIN32
            const QString exe_path = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
            const QString entry    = QStringLiteral("\"%1\" %2").arg(exe_path, kStartupArg);
            
            QSettings run_key(
                QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                QSettings::NativeFormat
            );
            if (run_key.value(QStringLiteral("Wobblin")).toString() != entry) {
                run_key.setValue(QStringLiteral("Wobblin"), entry);
                run_key.sync();
            }

            QSettings approved_key(
                QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"),
                QSettings::NativeFormat
            );
            if (approved_key.contains(QStringLiteral("Wobblin"))) {
                approved_key.remove(QStringLiteral("Wobblin"));
                approved_key.sync();
            }
#endif
        }

        [[nodiscard]] bool notifyExistingInstance() {
            QLocalSocket socket;
            socket.connectToServer(kLocalServerName);
            if (!socket.waitForConnected(750)) {
                return false;
            }
            socket.write("show");
            socket.flush();
            socket.waitForBytesWritten(750);
            socket.disconnectFromServer();
            return true;
        }

        [[nodiscard]] QStringList wobbleExclusions() {
            return settings().value(QStringLiteral("exclude/wobble"), QStringList()).toStringList();
        }
        void setWobbleExclusions(const QStringList& v) {
            settings().setValue(QStringLiteral("exclude/wobble"), v);
            settings().sync();
        }
        [[nodiscard]] QStringList animExclusions() {
            return settings().value(QStringLiteral("exclude/anim"), QStringList()).toStringList();
        }
        void setAnimExclusions(const QStringList& v) {
            settings().setValue(QStringLiteral("exclude/anim"), v);
            settings().sync();
        }

    } // namespace AppPersistence

}

class ThemeColors final {
public:
    ThemeColors()                              = delete;
    ~ThemeColors()                             = delete;
    ThemeColors(const ThemeColors&)            = delete;
    ThemeColors& operator=(const ThemeColors&) = delete;
    ThemeColors(ThemeColors&&)                 = delete;
    ThemeColors& operator=(ThemeColors&&)      = delete;

    static void set_dark(bool dark) noexcept {
        s_dark = dark;
        refresh_accent();
    }

    [[nodiscard]] static bool is_dark() noexcept {
        return s_dark;
    }

    static void refresh_accent() noexcept {
        s_accent = s_dark ? QColor(0x22, 0xD3, 0xEE) : QColor(0x00, 0x99, 0xBC);
    }

    [[nodiscard]] static QColor window_bg()              noexcept { return s_dark ? QColor(QStringLiteral("#0d0e14")) : QColor(QStringLiteral("#f3f3f3")); }
    [[nodiscard]] static QColor card_bg()                noexcept { return s_dark ? QColor(QStringLiteral("#161821")) : QColor(QStringLiteral("#ffffff")); }
    [[nodiscard]] static QColor separator()              noexcept { return s_dark ? QColor(QStringLiteral("#3d3d3d")) : QColor(QStringLiteral("#e5e5e5")); }
    [[nodiscard]] static QColor title_text()             noexcept { return s_dark ? QColor(QStringLiteral("#ffffff")) : QColor(QStringLiteral("#1a1a1a")); }
    [[nodiscard]] static QColor primary_text()           noexcept { return s_dark ? QColor(QStringLiteral("#ffffff")) : QColor(QStringLiteral("#1a1a1a")); }
    [[nodiscard]] static QColor secondary_text()         noexcept { return s_dark ? QColor(QStringLiteral("#9a9a9a")) : QColor(QStringLiteral("#666666")); }
    [[nodiscard]] static QColor disabled_text()          noexcept { return s_dark ? QColor(QStringLiteral("#5a5a5a")) : QColor(QStringLiteral("#b0b0b0")); }
    [[nodiscard]] static QColor accent()                 noexcept { return s_accent; }
    [[nodiscard]] static QColor toggle_off()             noexcept { return s_dark ? QColor(QStringLiteral("#2a2d3a")) : QColor(QStringLiteral("#cccccc")); }
    [[nodiscard]] static QColor toggle_thumb()           noexcept { return QColor(QStringLiteral("#ffffff")); }
    [[nodiscard]] static QColor slider_track()           noexcept { return s_dark ? QColor(QStringLiteral("#2a2d3a")) : QColor(QStringLiteral("#cccccc")); }
    [[nodiscard]] static QColor slider_thumb_border()    noexcept { return s_dark ? QColor(QStringLiteral("#161821")) : QColor(QStringLiteral("#ffffff")); }
    [[nodiscard]] static QColor slider_thumb_outline()   noexcept { return s_dark ? QColor(QStringLiteral("#161821")) : QColor(QStringLiteral("#ffffff")); }
    [[nodiscard]] static QColor slider_text()            noexcept { return s_dark ? QColor(QStringLiteral("#e0e0e0")) : QColor(QStringLiteral("#333333")); }
    [[nodiscard]] static QColor disabled_accent()        noexcept { return s_dark ? QColor(QStringLiteral("#3d3d3d")) : QColor(QStringLiteral("#c0c0c0")); }
    [[nodiscard]] static QColor disabled_track()         noexcept { return s_dark ? QColor(QStringLiteral("#3d3d3d")) : QColor(QStringLiteral("#e0e0e0")); }
    [[nodiscard]] static QColor disabled_thumb()         noexcept { return s_dark ? QColor(QStringLiteral("#3d3d3d")) : QColor(QStringLiteral("#c0c0c0")); }
    [[nodiscard]] static QColor disabled_thumb_border()  noexcept { return s_dark ? QColor(QStringLiteral("#161821")) : QColor(QStringLiteral("#ffffff")); }
    [[nodiscard]] static QColor disabled_thumb_outline() noexcept { return s_dark ? QColor(QStringLiteral("#161821")) : QColor(QStringLiteral("#ffffff")); }
    [[nodiscard]] static QColor disabled_slider_text()   noexcept { return s_dark ? QColor(QStringLiteral("#5a5a5a")) : QColor(QStringLiteral("#b0b0b0")); }

    [[nodiscard]] static QString github_normal()  noexcept { return QStringLiteral("#FFFFFF"); }
    [[nodiscard]] static QString github_pressed() noexcept { return QStringLiteral("#AAAAAA"); }

    // --- Aurora / Aero Glass extensions ---
    [[nodiscard]] static QColor glass_bg() noexcept {
        return s_dark ? QColor(20, 22, 33, 150) : QColor(245, 247, 250, 170);
    }
    [[nodiscard]] static QColor glass_border() noexcept {
        return s_dark ? QColor(255, 255, 255, 40) : QColor(120, 130, 150, 120);
    }
    [[nodiscard]] static QColor sidebar_bg() noexcept {
        return s_dark ? QColor(15, 17, 26, 175) : QColor(235, 238, 245, 185);
    }
    [[nodiscard]] static QColor accent_magenta() noexcept { return s_dark ? QColor(0xE8, 0x79, 0xF9) : QColor(0xC0, 0x4A, 0xE0); }
    [[nodiscard]] static QString glass_stylesheet() noexcept {
        return s_dark
            ? QStringLiteral("rgba(255,255,255,0.055)")
            : QStringLiteral("rgba(255,255,255,0.55)");
    }
    [[nodiscard]] static QString glass_border_stylesheet() noexcept {
        return s_dark
            ? QStringLiteral("rgba(255,255,255,0.14)")
            : QStringLiteral("rgba(80,90,110,0.35)");
    }
    [[nodiscard]] static QString window_fill_stylesheet() noexcept {
        // Translucent COLOURED glass (not fully transparent): a frosted tint
        // that still lets the OS backdrop blur read through softly.
        return s_dark
            ? QStringLiteral("rgba(13,14,20,0.62)")
            : QStringLiteral("rgba(236,240,247,0.66)");
    }
    [[nodiscard]] static QString accent_stylesheet() noexcept {
        return s_dark ? QStringLiteral("#22d3ee") : QStringLiteral("#0a9bc0");
    }
    [[nodiscard]] static QString accent_magenta_stylesheet() noexcept {
        return s_dark ? QStringLiteral("#e879f9") : QStringLiteral("#b04ad0");
    }
    [[nodiscard]] static QString text_primary() noexcept {
        return s_dark ? QStringLiteral("#f5f6fa") : QStringLiteral("#16202e");
    }
    [[nodiscard]] static QString text_secondary() noexcept {
        return s_dark ? QStringLiteral("#9aa0b4") : QStringLiteral("#55617a");
    }

private:
    static bool   s_dark;
    static QColor s_accent;
};

bool   ThemeColors::s_dark   = true;
QColor ThemeColors::s_accent = QColor(0x00, 0x78, 0xD4);

[[nodiscard]] static bool detect_system_dark_mode() noexcept {
    const QSettings reg(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat
    );
    const QVariant v = reg.value(QStringLiteral("AppsUseLightTheme"));
    if (!v.isValid() || !v.canConvert<int>()) {
        return false;
    }
    bool ok = false;
    const int value = v.toInt(&ok);
    if (!ok) {
        return false;
    }
    return value == 0;
}

[[nodiscard]] static QIcon load_application_icon() noexcept {
    const QString app_dir = QDir(QCoreApplication::applicationDirPath()).canonicalPath();
    if (!app_dir.isEmpty()) {
        const QString icon_path = QDir(app_dir).filePath(QStringLiteral("icon.ico"));
        if (QFile::exists(icon_path)) {
            const QIcon icon(icon_path);
            if (!icon.isNull() && !icon.availableSizes().isEmpty()) {
                return icon;
            }
        }
        const QString png_path = QDir(app_dir).filePath(QStringLiteral("wobblin_icon.png"));
        if (QFile::exists(png_path)) {
            const QIcon icon(png_path);
            if (!icon.isNull()) {
                return icon;
            }
        }
    }
    if (QFile::exists(QStringLiteral("icon.ico"))) {
        const QIcon icon(QStringLiteral("icon.ico"));
        if (!icon.isNull() && !icon.availableSizes().isEmpty()) return icon;
    }
    if (QFile::exists(QStringLiteral("wobblin_icon.png"))) {
        const QIcon icon(QStringLiteral("wobblin_icon.png"));
        if (!icon.isNull()) return icon;
    }
    if (QFile::exists(QStringLiteral("D:/Project/Deskwarp/icon.ico"))) {
        const QIcon icon(QStringLiteral("D:/Project/Deskwarp/icon.ico"));
        if (!icon.isNull()) return icon;
    }
    if (QFile::exists(QStringLiteral("D:/Project/Deskwarp/wobblin_icon.png"))) {
        const QIcon icon(QStringLiteral("D:/Project/Deskwarp/wobblin_icon.png"));
        if (!icon.isNull()) return icon;
    }
    return QIcon();
}

// --- Aurora / Aero Glass: real OS backdrop blur via DWM (dynamic load, no header dep) ---
enum DwmGlassAttrib : int {
    DwmGlass_SystemBackdrop = 35,   // Win11 22H2+
    DwmGlass_Mica           = 1029  // Win11
};

struct WindowCompositionAttribData {
    int  attrib;
    void* pvData;
    std::size_t cbData;
};

[[nodiscard]] static bool applyDwmGlass(HWND hwnd) noexcept {
    if (hwnd == nullptr) { return false; }
    const HMODULE dwm = ::LoadLibraryW(L"dwmapi.dll");
    if (dwm == nullptr) { return false; }

    using SetWindowCompositionAttribute_t = BOOL(WINAPI*)(HWND, const WindowCompositionAttribData*);
    using DwmEnableBlurBehindWindow_t   = HRESULT(WINAPI*)(HWND, const void*); // DWM_BLURBEHIND*

    // DWM_BLURBEHIND (Win10): flags=1 (enable), hRgnBlur=NULL => whole window.
    struct DwmBlurBehind { DWORD dwFlags = 1; BOOL fEnable = TRUE; HRGN hRgnBlur = nullptr; BOOL fTransitionOnMaximized = FALSE; };

    bool ok = false;
    // 1) Win11 22H2+: SYSTEMBACKDROP (true acrylic/mica backdrop from desktop)
    {
        auto fn = reinterpret_cast<SetWindowCompositionAttribute_t>(
            ::GetProcAddress(dwm, "SetWindowCompositionAttribute"));
        if (fn != nullptr) {
            int enable = 1;
            WindowCompositionAttribData data{ DwmGlass_SystemBackdrop, &enable, sizeof(enable) };
            if (fn(hwnd, &data)) { ok = true; }
            if (!ok) {
                int mica = 1;
                WindowCompositionAttribData micaData{ DwmGlass_Mica, &mica, sizeof(mica) };
                if (fn(hwnd, &micaData)) { ok = true; }
            }
        }
    }
    // 2) Win10 fallback: BlurBehind
    if (!ok) {
        auto blur = reinterpret_cast<DwmEnableBlurBehindWindow_t>(
            ::GetProcAddress(dwm, "DwmEnableBlurBehindWindow"));
        if (blur != nullptr) {
            DwmBlurBehind bb;
            if (SUCCEEDED(blur(hwnd, &bb))) { ok = true; }
        }
    }
    ::FreeLibrary(dwm);
    return ok;
}

class GitHubButton final : public QWidget {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(GitHubButton)
public:
    explicit GitHubButton(QWidget* parent = nullptr)
        : QWidget(parent),
          m_url(QStringLiteral(""), QUrl::StrictMode),
          m_svg_template(QStringLiteral(
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 16 16\" width=\"32\" height=\"32\">"
              "<path fill=\"{color}\" d=\"M8 0c4.42 0 8 3.58 8 8a8.013 8.013 0 0 1-5.45 7.59c-.4.08-.55-.17-.55-.38 "
              "0-.27-.01-1.13-.01-2.2 0-.75-.25-1.23-.54-1.48 1.78-.2 3.65-.88 3.65-3.95 0-.88-.31-1.59-.82-2.15"
              ".08-.2.36-1.02-.08-2.12 0 0-.67-.22-2.2.82-.64-.18-1.32-.27-2-.27-.68 0-1.36.09-2 .27-1.53-1.03-"
              "2.2-.82-2.2-.82-.44 1.1-.16 1.92-.08 2.12-.51.56-.82 1.28-.82 2.15 0 3.06 1.86 3.75 3.64 3.95-.23"
              ".2-.44.55-.51 1.07-.46.21-1.61.55-2.33-.66-.15-.24-.6-.83-1.23-.82-.67.01-.27.38.01.53.34.19.73.9"
              ".82 1.13.16.45.68 1.31 2.69.94 0 .67.01 1.3.01 1.49 0 .21-.15.45-.55.38A7.995 7.995 0 0 1 0 8c0-"
              "4.42 3.58-8 8-8Z\"/></svg>")),
          m_renderer(new QSvgRenderer(this)),
          m_pressed(false),
          m_current_color(safe_hex_color(ThemeColors::github_normal(), QStringLiteral("#cccccc")))
    {
        setFixedSize(kIconButtonSz, kIconButtonSz);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_TranslucentBackground);
        update_svg();
    }

    [[nodiscard]] QSize sizeHint() const override { return QSize(kIconButtonSz, kIconButtonSz); }
    [[nodiscard]] QSize minimumSizeHint() const override { return QSize(kIconButtonSz, kIconButtonSz); }

    ~GitHubButton() override = default;

    void apply_theme() noexcept {
        const QString c = m_pressed ? ThemeColors::github_pressed() : ThemeColors::github_normal();
        m_current_color = safe_hex_color(c, QStringLiteral("#cccccc"));
        update_svg();
    }

protected:
    void leaveEvent(QEvent* event) override {
        if (!m_pressed) {
            m_current_color = safe_hex_color(ThemeColors::github_normal(), QStringLiteral("#cccccc"));
            update_svg();
        }
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event != nullptr && event->button() == Qt::LeftButton) {
            m_pressed       = true;
            m_current_color = safe_hex_color(ThemeColors::github_pressed(), QStringLiteral("#999999"));
            update_svg();
        }
        QWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event != nullptr && event->button() == Qt::LeftButton) {
            const bool inside = rect().contains(event->pos());
            m_pressed         = false;
            m_current_color   = safe_hex_color(ThemeColors::github_normal(), QStringLiteral("#cccccc"));
            update_svg();
            if (inside
                && m_url.isValid()
                && m_url.scheme() == QStringLiteral("https")
                && m_url.host()   == QStringLiteral("github.com"))
            {
                QDesktopServices::openUrl(m_url);
            }
        }
        QWidget::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (m_renderer != nullptr && m_renderer->isValid()) {
            m_renderer->render(&painter);
        }
    }

private:
    void update_svg() noexcept {
        if (!is_valid_hex_color(m_current_color)) {
            m_current_color = QStringLiteral("#cccccc");
        }
        QString svg_data = m_svg_template;
        svg_data.replace(QStringLiteral("{color}"), m_current_color);
        const QByteArray bytes = svg_data.toUtf8();
        if (m_renderer != nullptr) {
            m_renderer->load(bytes);
            update();
        }
    }

    const QUrl     m_url;
    const QString  m_svg_template;
    QSvgRenderer* m_renderer;
    bool           m_pressed;
    QString        m_current_color;
};

class ToggleSwitch final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double thumbPosition READ getThumbPosition WRITE setThumbPosition)
    Q_DISABLE_COPY_MOVE(ToggleSwitch)
public:
    explicit ToggleSwitch(QWidget* parent = nullptr, int width = kToggleWidth, int height = kToggleHeight)
        : QWidget(parent),
          m_width(std::clamp(width,  8, 4096)),
          m_height(std::clamp(height, 8, 4096)),
          m_state(false),
          m_radius(static_cast<double>(std::clamp(height, 8, 4096)) / 2.0),
          m_thumb_radius(m_radius - kToggleGapPx),
          m_thumb_x(m_radius),
          m_animation(new QPropertyAnimation(this, QByteArrayLiteral("thumbPosition"), this))
    {
        setFixedSize(m_width, m_height);
        if (m_animation != nullptr) {
            m_animation->setDuration(kToggleAnimMs);
            m_animation->setEasingCurve(QEasingCurve::InOutCubic);
        }
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_TranslucentBackground);
    }

    [[nodiscard]] QSize sizeHint() const override { return QSize(m_width, m_height); }
    [[nodiscard]] QSize minimumSizeHint() const override { return QSize(m_width, m_height); }

    ~ToggleSwitch() override {
        if (m_animation != nullptr) {
            m_animation->stop();
        }
    }

    [[nodiscard]] bool isChecked() const noexcept {
        return m_state;
    }

    void setChecked(bool checked) noexcept {
        if (m_state == checked) {
            return;
        }
        m_state = checked;
        if (m_animation != nullptr) {
            m_animation->stop();
        }
        m_thumb_x = m_state
            ? (static_cast<double>(m_width) - m_radius)
            : m_radius;
        update();
    }

    [[nodiscard]] double getThumbPosition() const noexcept {
        return m_thumb_x;
    }

    void setThumbPosition(double value) noexcept {
        if (std::isnan(value) || std::isinf(value)) {
            return;
        }
        m_thumb_x = clamp_double(value, 0.0, static_cast<double>(m_width));
        update();
    }

    void apply_theme() noexcept {
        update();
    }

signals:
    void toggled(bool state);

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event != nullptr && event->button() == Qt::LeftButton) {
            m_state = !m_state;
            const double target = m_state
                ? (static_cast<double>(m_width) - m_radius)
                : m_radius;
            if (m_animation != nullptr) {
                m_animation->stop();
                m_animation->setStartValue(m_thumb_x);
                m_animation->setEndValue(target);
                m_animation->start();
            } else {
                m_thumb_x = target;
                update();
            }
            emit toggled(m_state);
        }
        QWidget::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const double r            = m_radius;
        const double w            = static_cast<double>(m_width);
        const double h            = static_cast<double>(m_height);
        const double total_travel = w - 2.0 * r;
        double       progress     = (total_travel > 0.0) ? (m_thumb_x - r) / total_travel : 0.0;
        progress                  = clamp_double(progress, 0.0, 1.0);
        const QColor on_color     = ThemeColors::accent();
        const QColor off_color    = ThemeColors::toggle_off();
        const int    cr           = clamp_channel(static_cast<int>(std::round(off_color.red()   + (on_color.red()   - off_color.red())   * progress)));
        const int    cg           = clamp_channel(static_cast<int>(std::round(off_color.green() + (on_color.green() - off_color.green()) * progress)));
        const int    cb           = clamp_channel(static_cast<int>(std::round(off_color.blue()  + (on_color.blue()  - off_color.blue())  * progress)));
        const QColor current_color(cr, cg, cb);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QBrush(current_color));
        QPainterPath path;
        path.addRoundedRect(QRectF(0.0, 0.0, w, h), r, r);
        painter.drawPath(path);
        painter.setBrush(QBrush(ThemeColors::toggle_thumb()));
        painter.drawEllipse(QPointF(m_thumb_x, h / 2.0), m_thumb_radius, m_thumb_radius);
    }

private:
    const int           m_width;
    const int           m_height;
    bool                m_state;
    const double        m_radius;
    const double        m_thumb_radius;
    double              m_thumb_x;
    QPropertyAnimation* m_animation;
};

class ModernSlider final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double thumbX READ getThumbX WRITE setThumbX)
    Q_DISABLE_COPY_MOVE(ModernSlider)
public:
    explicit ModernSlider(QWidget* parent = nullptr,
                          int width  = kSliderWidth,
                          int height = kSliderHeight,
                          int levels = kSliderLevels)
        : QWidget(parent),
          m_slider_width(std::clamp(width,  32, 8192)),
          m_slider_height(std::clamp(height, 16, 8192)),
          m_levels(std::clamp(levels, 2, 1024)),
          m_min_val(1),
          m_max_val(std::clamp(levels, 2, 1024)),
          m_enabled(true),
          m_padding_left(24),
          m_padding_right(24),
          m_track_y(24),
          m_track_height(4),
          m_thumb_radius(8),
          m_thumb_outer_radius(12),
          m_track_start(static_cast<double>(m_padding_left)),
          m_track_end(static_cast<double>(std::clamp(width, 32, 8192) - m_padding_right)),
          m_track_length(std::max(0.0,
              static_cast<double>(std::clamp(width, 32, 8192) - m_padding_right)
              - static_cast<double>(m_padding_left))),
          m_current_value(1),
          m_thumb_x(static_cast<double>(m_padding_left)),
          m_dragging(false),
          m_drag_offset_x(0.0),
          m_animation(new QPropertyAnimation(this, QByteArrayLiteral("thumbX"), this))
    {
        m_thumb_x = value_to_x(m_current_value);
        setMinimumWidth(120);
        setFixedHeight(m_slider_height);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        recompute_track();
        if (m_animation != nullptr) {
            m_animation->setDuration(kSliderAnimMs);
            m_animation->setEasingCurve(QEasingCurve::OutCubic);
        }
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_TranslucentBackground);
    }

    void recompute_track() noexcept {
        const double w = static_cast<double>(this->width() > 0 ? this->width() : m_slider_width);
        m_track_start  = static_cast<double>(m_padding_left);
        m_track_end    = w - static_cast<double>(m_padding_right);
        m_track_length = std::max(0.0, m_track_end - m_track_start);
        m_thumb_x      = value_to_x(m_current_value);
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        recompute_track();
        update();
    }

    [[nodiscard]] QSize sizeHint() const override { return QSize(120, m_slider_height); }
    [[nodiscard]] QSize minimumSizeHint() const override { return QSize(120, m_slider_height); }

    ~ModernSlider() override {
        if (m_animation != nullptr) {
            m_animation->stop();
        }
    }

    void setSliderEnabled(bool enabled) noexcept {
        m_enabled = enabled;
        if (enabled) {
            setCursor(Qt::PointingHandCursor);
        } else {
            setCursor(Qt::ArrowCursor);
            if (m_dragging) {
                m_dragging = false;
                if (m_animation != nullptr) {
                    m_animation->stop();
                }
            }
        }
        update();
    }

    [[nodiscard]] bool isSliderEnabled() const noexcept {
        return m_enabled;
    }

    [[nodiscard]] double getThumbX() const noexcept {
        return m_thumb_x;
    }

    void setThumbX(double value) noexcept {
        if (std::isnan(value) || std::isinf(value)) {
            return;
        }
        m_thumb_x = clamp_double(value, m_track_start, m_track_end);
        update();
    }

    [[nodiscard]] int value() const noexcept {
        return m_current_value;
    }

    void setValueSilent(int value) noexcept {
        const int clamped = std::clamp(value, m_min_val, m_max_val);
        if (m_animation != nullptr) {
            m_animation->stop();
        }
        m_current_value = clamped;
        m_thumb_x       = value_to_x(m_current_value);
        m_dragging      = false;
        update();
    }

    void apply_theme() noexcept {
        update();
    }

signals:
    void valueChanged(int value);

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (!m_enabled || event == nullptr) {
            QWidget::mousePressEvent(event);
            return;
        }
        if (event->button() == Qt::LeftButton) {
            const double pos_x = event->position().x();
            const double dist  = std::abs(pos_x - m_thumb_x);
            if (dist <= static_cast<double>(m_thumb_outer_radius) + 8.0) {
                m_dragging      = true;
                m_drag_offset_x = m_thumb_x - pos_x;
                if (m_animation != nullptr) {
                    m_animation->stop();
                }
            } else {
                m_dragging = false;
                animate_to_value(snap_value(x_to_value(pos_x)));
            }
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!m_enabled || !m_dragging || event == nullptr) {
            QWidget::mouseMoveEvent(event);
            return;
        }
        const double pos_x   = event->position().x() + m_drag_offset_x;
        const int    snapped = snap_value(x_to_value(pos_x));
        if (snapped != m_current_value) {
            animate_to_value(snapped);
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (m_enabled && event != nullptr && event->button() == Qt::LeftButton && m_dragging) {
            m_dragging = false;
            animate_to_value(snap_value(x_to_value(m_thumb_x)));
        }
        QWidget::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QColor accent;
        QColor track;
        QColor thumb;
        QColor thumb_border;
        QColor thumb_outline;
        QColor text;
        if (m_enabled) {
            accent        = ThemeColors::accent();
            track         = ThemeColors::slider_track();
            thumb         = ThemeColors::accent();
            thumb_border  = ThemeColors::slider_thumb_border();
            thumb_outline = ThemeColors::slider_thumb_outline();
            text          = ThemeColors::slider_text();
        } else {
            accent        = ThemeColors::disabled_accent();
            track         = ThemeColors::disabled_track();
            thumb         = ThemeColors::disabled_thumb();
            thumb_border  = ThemeColors::disabled_thumb_border();
            thumb_outline = ThemeColors::disabled_thumb_outline();
            text          = ThemeColors::disabled_slider_text();
        }
        const double ty = static_cast<double>(m_track_y);
        const double th = static_cast<double>(m_track_height);
        const double r  = th / 2.0;
        const double ts = m_track_start;
        const double te = m_track_end;
        painter.setPen(Qt::NoPen);

        const double outer_r = static_cast<double>(m_thumb_outer_radius);
        const double gap     = outer_r + kToggleGapPx;

        const double left_end = m_thumb_x - gap;
        if (left_end > ts - r) {
            painter.setBrush(QBrush(accent));
            QPainterPath left_path;
            left_path.addRoundedRect(QRectF(ts - r, ty - r, left_end - (ts - r), th), r, r);
            painter.drawPath(left_path);
        }

        const double right_start = m_thumb_x + gap;
        if (right_start < te + r) {
            painter.setBrush(QBrush(track));
            QPainterPath right_path;
            right_path.addRoundedRect(QRectF(right_start, ty - r, (te + r) - right_start, th), r, r);
            painter.drawPath(right_path);
        }

        const double inner_r = static_cast<double>(m_thumb_radius);
        const QPointF thumb_center(m_thumb_x, ty);
        painter.setBrush(QBrush(thumb_border));
        painter.setPen(QPen(thumb_outline, 1.0));
        painter.drawEllipse(thumb_center, outer_r, outer_r);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QBrush(thumb));
        painter.drawEllipse(thumb_center, inner_r, inner_r);
        const double label_y = ty + 24.0;
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 9));
        painter.setPen(QPen(text));
        const QFontMetrics fm = painter.fontMetrics();
        for (int i = 0; i < m_levels; ++i) {
            const int     val        = m_min_val + i;
            const double  lx         = value_to_x(val);
            const QString label_text = QString::number(val);
            const int     tw         = fm.horizontalAdvance(label_text);
            const int     th2        = fm.height();
            painter.drawText(QPointF(lx - static_cast<double>(tw) / 2.0,
                                     label_y + static_cast<double>(th2) / 4.0),
                             label_text);
        }
    }

private:
    [[nodiscard]] double value_to_x(double value) const noexcept {
        if (m_max_val == m_min_val) {
            return m_track_start;
        }
        const double ratio   = (value - static_cast<double>(m_min_val))
                               / static_cast<double>(m_max_val - m_min_val);
        const double clamped = clamp_double(ratio, 0.0, 1.0);
        return m_track_start + clamped * m_track_length;
    }

    [[nodiscard]] double x_to_value(double x) const noexcept {
        if (m_track_length <= 0.0) {
            return static_cast<double>(m_min_val);
        }
        const double ratio = clamp_double((x - m_track_start) / m_track_length, 0.0, 1.0);
        return static_cast<double>(m_min_val) + ratio * static_cast<double>(m_max_val - m_min_val);
    }

    [[nodiscard]] int snap_value(double value) const noexcept {
        if (std::isnan(value) || std::isinf(value)) {
            return m_min_val;
        }
        return std::clamp(static_cast<int>(std::round(value)), m_min_val, m_max_val);
    }

    void animate_to_value(int snapped_value) noexcept {
        const int prev = m_current_value;
        m_current_value     = std::clamp(snapped_value, m_min_val, m_max_val);
        const double target = value_to_x(m_current_value);
        if (m_animation != nullptr) {
            m_animation->stop();
            m_animation->setStartValue(m_thumb_x);
            m_animation->setEndValue(target);
            m_animation->start();
        } else {
            m_thumb_x = target;
            update();
        }
        if (m_current_value != prev) {
            emit valueChanged(m_current_value);
        }
    }

    const int           m_slider_width;
    const int           m_slider_height;
    const int           m_levels;
    const int           m_min_val;
    const int           m_max_val;
    bool                m_enabled;
    const int           m_padding_left;
    const int           m_padding_right;
    const int           m_track_y;
    const int           m_track_height;
    const int           m_thumb_radius;
    const int           m_thumb_outer_radius;
    double              m_track_start;
    double              m_track_end;
    double              m_track_length;
    int                 m_current_value;
    double              m_thumb_x;
    bool                m_dragging;
    double              m_drag_offset_x;
    QPropertyAnimation* m_animation;
};

// A single exclusion entry: executable name + two independent toggles
// (Wobble and Animation) so an app can be excluded from either or both.
class ExclusionRow final : public QWidget {
    Q_OBJECT
public:
    explicit ExclusionRow(const QString& exe, bool wobble, bool anim, QWidget* parent = nullptr)
        : QWidget(parent) {
        auto* const lay = new QHBoxLayout(this);
        lay->setContentsMargins(8, 4, 8, 4);
        lay->setSpacing(10);
        auto* const name = new QLabel(exe, this);
        name->setObjectName(QStringLiteral("excl_name"));
        name->setStyleSheet(QStringLiteral("color: #f5f6fa; font-size: 12px;"));
        name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        name->setTextFormat(Qt::PlainText);
        m_name = name;
        m_full_exe = exe;
        lay->addWidget(name);
        m_wobble = new QCheckBox(QStringLiteral("Wobble"), this);
        m_anim   = new QCheckBox(QStringLiteral("Animation"), this);
        m_wobble->setChecked(wobble);
        m_anim->setChecked(anim);
        m_exe = exe;
        m_wobble->setStyleSheet(QStringLiteral("QCheckBox { color: #9aa0b4; spacing: 4px; } QCheckBox::indicator { width: 14px; height: 14px; border-radius: 4px; background: rgba(255,255,255,0.10); border: 1px solid rgba(34,211,238,0.35); } QCheckBox::indicator:checked { background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #22d3ee, stop:1 #e879f9); border: none; }"));
        m_anim->setStyleSheet(m_wobble->styleSheet());
        lay->addWidget(m_wobble);
        lay->addWidget(m_anim);
    }
    QString exeName() const { return m_exe; }
    bool wobbleOn() const { return m_wobble->isChecked(); }
    bool animOn() const { return m_anim->isChecked(); }
    void setExe(const QString& e) { m_exe = e; }
    QCheckBox* wobbleBox() { return m_wobble; }
    QCheckBox* animBox() { return m_anim; }
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        elideName();
    }
    void elideName() {
        if (m_name == nullptr) return;
        const QFontMetrics fm = m_name->fontMetrics();
        m_name->setText(fm.elidedText(m_full_exe, Qt::ElideMiddle, m_name->width()));
    }
private:
    QString    m_exe;
    QString    m_full_exe;
    QLabel*    m_name = nullptr;
    QCheckBox* m_wobble = nullptr;
    QCheckBox* m_anim   = nullptr;
};

class Wobblin final : public QMainWindow {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Wobblin)

public:
    explicit Wobblin(QWidget* parent = nullptr)
        : QMainWindow(parent),
          m_central_widget(nullptr),
          m_title_label(nullptr),
          m_wobbly_frame(nullptr),
          m_wobbly_label(nullptr),
          m_wobbly_toggle(nullptr),
          m_realism_label(nullptr),
          m_realism_slider(nullptr),
          m_github_btn(nullptr)
    {
        setWindowTitle(QStringLiteral("Wobblin"));
        setMinimumSize(kWindowWidth, kWindowHeight);
        resize(kWindowWidth, kWindowHeight);
        const QIcon app_icon = load_application_icon();
        if (!app_icon.isNull()) {
            setWindowIcon(app_icon);
        }
        // Solid theme: frameless, fully opaque
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        build_ui();
        apply_theme();
        load_persisted_settings();
    }

    ~Wobblin() override {
        if (m_wobbly_initialized) {
            m_wobbly.shutdown();
            m_wobbly_initialized = false;
        }
    }

    [[nodiscard]] bool ensureWobblyEngine() {
        if (m_wobbly_initialized) {
            return true;
        }
        (void)winId();
        if (m_wobbly.init(GetModuleHandleW(nullptr), reinterpret_cast<HWND>(winId()))) {
            m_wobbly_initialized = true;
            apply_wobbly_runtime_state();
            return true;
        }
        return false;
    }

    void showMainWindow() {
        showNormal();
        raise();
        activateWindow();
        (void)ensureWobblyEngine();
        if (m_wobbly_initialized && burn_cfg::onOpen) {
            m_wobbly.handleUiMessage(WM_USER + 5, static_cast<WPARAM>(winId()), 0);
        }
    }

    void requestForceQuit() {
        m_force_quit = true;
        if (m_wobbly_initialized) {
            m_wobbly.setEnabled(false);
        }
        hide();
        close();
    }

signals:
    void hiddenToTray();

protected:
    void showEvent(QShowEvent* event) override {
        QMainWindow::showEvent(event);
        (void)ensureWobblyEngine();
    }

    void closeEvent(QCloseEvent* event) override {
        if (!m_force_quit) {
            event->ignore();
            hide();
            emit hiddenToTray();
            return;
        }
        if (m_wobbly_initialized) {
            m_wobbly.setEnabled(false);
            m_wobbly.shutdown();
            m_wobbly_initialized = false;
        }
        QMainWindow::closeEvent(event);
    }

    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override {
#ifdef _WIN32
        if (eventType == QByteArrayLiteral("windows_generic_MSG") && message != nullptr) {
            const MSG* const msg = static_cast<const MSG*>(message);
            if (msg != nullptr) {
                // Frameless window: manual hit-test for drag + snap/resize edges.
                if (msg->message == WM_NCHITTEST) {
                    const int grip = 6;
                    const int caption = 38;                 // title bar height
                    const int btn_zone = 120;              // right-side button strip (min/max/close)
                    const QRect w = geometry();   // window rect (frameless => no extra frame)
                    const int x = static_cast<int>(GET_X_LPARAM(msg->lParam));
                    const int y = static_cast<int>(GET_Y_LPARAM(msg->lParam));
                    int hit = HTCLIENT;
                    if (x < w.left() + grip)               hit = HTLEFT;
                    else if (x > w.right() - grip)         hit = HTRIGHT;
                    else if (y < w.top() + grip)            hit = HTTOP;
                    else if (y > w.bottom() - grip)         hit = HTBOTTOM;
                    else if (y < w.top() + caption && x < w.right() - btn_zone) hit = HTCAPTION;
                    if (hit != HTCLIENT) {
                        if (result) *result = hit;
                        return true;
                    }
                }
                if (m_wobbly_initialized && m_wobbly.handleUiMessage(msg->message, msg->wParam, msg->lParam)) {
                    if (result) *result = 0;
                    return true;
                }
                if (msg->message == 0x0320) {
                    check_system_theme();
                } else if (msg->message == WM_SETTINGCHANGE && msg->lParam != 0) {
                    const wchar_t* const name = reinterpret_cast<const wchar_t*>(msg->lParam);
                    if (name != nullptr && ::lstrcmpW(name, L"ImmersiveColorSet") == 0) {
                        check_system_theme();
                    }
                }
            }
        }
#endif
        return QMainWindow::nativeEvent(eventType, message, result);
    }

private slots:
    void on_wobbly_toggled(bool state) {
        if (m_realism_slider != nullptr) {
            m_realism_slider->setSliderEnabled(state);
        }
        if (m_wobbly_initialized) {
            m_wobbly.setEnabled(state);
            if (state && m_realism_slider != nullptr) {
                m_wobbly.setRealismLevel(m_realism_slider->value());
            }
        }
        update_realism_label_style();
        if (!m_loading_settings) {
            AppPersistence::setWobblyEnabled(state);
        }
    }

    void on_realism_changed(int level) {
        cfg::applyRealismLevel(level);
        if (m_wobbly_initialized) {
            m_wobbly.setRealismLevel(level);
        }
        if (!m_loading_settings) {
            AppPersistence::setWobblyRealism(level);
        }
    }

    void on_burn_toggled(bool state) {
        if (m_burn_effect_combo != nullptr) m_burn_effect_combo->setEnabled(state);
        if (m_burn_dur_slider != nullptr) m_burn_dur_slider->setSliderEnabled(state);
        update_burn_label_style();
        if (m_wobbly_initialized) m_wobbly.setBurnEnabled(state);
        if (!m_loading_settings) AppPersistence::setBurnEnabled(state);
    }

    void on_burn_close_toggled(bool state) {
        if (m_wobbly_initialized) burn_cfg::onClose = state;
        if (!m_loading_settings) AppPersistence::setBurnCloseEnabled(state);
    }

    void on_burn_minimize_toggled(bool state) {
        if (m_wobbly_initialized) burn_cfg::onMinimize = state;
        if (!m_loading_settings) AppPersistence::setBurnMinimizeEnabled(state);
    }

    void on_burn_open_toggled(bool state) {
        if (m_wobbly_initialized) burn_cfg::onOpen = state;
        if (!m_loading_settings) AppPersistence::setBurnOpenEnabled(state);
    }
    void on_burn_effect_changed(int index) {
        // Combo order is now [Aura Glow, Incinerate, Matrix, Pixelate, TV Off, Glitch, Wisps]
        // (index 0 = Aura Glow = effect id 7). Map explicitly instead of index+1.
        static const int kEffectByIndex[7] = { 7, 1, 2, 3, 4, 5, 6 };
        if (index < 0 || index > 6) index = 0;
        const int eff = kEffectByIndex[index];
        if (m_wobbly_initialized) m_wobbly.setBurnEffect(eff);
        if (!m_loading_settings) AppPersistence::setBurnEffect(eff);
    }

    void on_burn_dur_changed(int level) {
        const int ms = levelToBurnMs(level);
        if (m_wobbly_initialized) m_wobbly.setBurnDuration(ms);
        if (!m_loading_settings) AppPersistence::setBurnDuration(ms);
    }

    // Add (or update) an exclusion row for `exe` with independent wobble/anim flags.
    // Returns the created/updated list item.
    QListWidgetItem* addExclusionRow(const QString& exe, bool wobble, bool anim) {
        if (exe.isEmpty() || !m_exclude_list) return nullptr;
        // Update existing row if present
        for (int i = 0; i < m_exclude_list->count(); ++i) {
            QListWidgetItem* it = m_exclude_list->item(i);
            ExclusionRow* row = it ? qobject_cast<ExclusionRow*>(m_exclude_list->itemWidget(it)) : nullptr;
            if (row && row->exeName().compare(exe, Qt::CaseInsensitive) == 0) {
                row->wobbleBox()->setChecked(wobble);
                row->animBox()->setChecked(anim);
                return it;
            }
        }
        auto* const it = new QListWidgetItem(m_exclude_list);
        it->setSizeHint(QSize(0, 40));
        auto* const row = new ExclusionRow(exe, wobble, anim, m_exclude_list);
        m_exclude_list->setItemWidget(it, row);
        connect(row->wobbleBox(), &QCheckBox::toggled, this, &Wobblin::syncExclusions);
        connect(row->animBox(),   &QCheckBox::toggled, this, &Wobblin::syncExclusions);
        return it;
    }

    void on_exclude_add_current() {
        if (!m_exclude_list) return;
        const QString exe = GetProcessImageName(GetForegroundWindow());
        if (exe.isEmpty()) return;
        addExclusionRow(exe, true, true);
        syncExclusions();
    }

    void on_exclude_browse() {
        if (!m_exclude_list) return;
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Select executable to exclude"),
            QString(), QStringLiteral("Executable (*.exe)"));
        if (path.isEmpty()) return;
        addExclusionRow(QFileInfo(path).fileName(), true, true);
        syncExclusions();
    }

    void on_exclude_remove() {
        if (!m_exclude_list) return;
        QListWidgetItem* it = m_exclude_list->currentItem();
        if (!it) return;
        delete m_exclude_list->takeItem(m_exclude_list->row(it));
        syncExclusions();
    }

private:
    void load_persisted_settings() {
        const bool wobbly_on = AppPersistence::wobblyEnabled();
        const int  realism   = AppPersistence::wobblyRealism(kSliderLevels);
        cfg::applyRealismLevel(realism);
        m_loading_settings = true;
        if (m_wobbly_toggle != nullptr) {
            m_wobbly_toggle->setChecked(wobbly_on);
        }
        if (m_realism_slider != nullptr) {
            m_realism_slider->setValueSilent(realism);
            m_realism_slider->setSliderEnabled(wobbly_on);
        }

        const bool burn_on  = AppPersistence::burnEnabled();
        const int  burn_eff = AppPersistence::burnEffect();
        const int  burn_ms  = AppPersistence::burnDuration();
        if (m_burn_toggle != nullptr) m_burn_toggle->setChecked(burn_on);
        if (m_burn_close_toggle != nullptr) m_burn_close_toggle->setChecked(AppPersistence::burnCloseEnabled());
        if (m_burn_minimize_toggle != nullptr) m_burn_minimize_toggle->setChecked(AppPersistence::burnMinimizeEnabled());
        if (m_burn_open_toggle != nullptr) m_burn_open_toggle->setChecked(AppPersistence::burnOpenEnabled());
        if (m_burn_effect_combo != nullptr) {
            // Map effect id (1..7) back to combo index using the new order
            // [Aura Glow(7), Incinerate(1), Matrix(2), Pixelate(3), TV Off(4), Glitch(5), Wisps(6)].
            static const int kIndexByEffect[8] = { 0, 1, 2, 3, 4, 5, 6, 0 };
            const int idx = (burn_eff >= 1 && burn_eff <= 7) ? kIndexByEffect[burn_eff] : 0;
            m_burn_effect_combo->setCurrentIndex(idx);
            m_burn_effect_combo->setEnabled(burn_on);
        }
        if (m_burn_dur_slider != nullptr) {
            m_burn_dur_slider->setValueSilent(burnMsToLevel(burn_ms));
            m_burn_dur_slider->setSliderEnabled(burn_on);
        }
        m_wobbly.setBurnEnabled(burn_on);
        m_wobbly.setBurnEffect(burn_eff);
        m_wobbly.setBurnDuration(burn_ms);

        // Card 3: load exclusion lists into custom row widgets
        const QStringList wob = AppPersistence::wobbleExclusions();
        const QStringList anim = AppPersistence::animExclusions();
        if (m_exclude_list) {
            QStringList allExes = wob;
            for (const QString& a : anim) if (!allExes.contains(a, Qt::CaseInsensitive)) allExes.append(a);
            for (const QString& exe : allExes) {
                const bool w = wob.contains(exe, Qt::CaseInsensitive);
                const bool a = anim.contains(exe, Qt::CaseInsensitive);
                auto* const it = new QListWidgetItem(m_exclude_list);
                it->setSizeHint(QSize(0, 40));
                auto* const row = new ExclusionRow(exe, w, a, m_exclude_list);
                m_exclude_list->setItemWidget(it, row);
                connect(row->wobbleBox(), &QCheckBox::toggled, this, &Wobblin::syncExclusions);
                connect(row->animBox(),   &QCheckBox::toggled, this, &Wobblin::syncExclusions);
            }
            // Sync runtime sets (engine initialized later in ensureWobblyEngine)
            std::set<QString, IStringLess> wb, an;
            for (const QString& s : wob) wb.insert(s);
            for (const QString& s : anim) an.insert(s);
            m_wobbly.setWobbleExclusions(wb);
            m_wobbly.setAnimExclusions(an);
        }

        m_loading_settings = false;
        update_realism_label_style();
        update_burn_label_style();
    }

    void apply_wobbly_runtime_state() {
        if (!m_wobbly_initialized) {
            return;
        }
        const bool enabled = m_wobbly_toggle != nullptr && m_wobbly_toggle->isChecked();
        const int  realism = m_realism_slider != nullptr ? m_realism_slider->value() : 1;
        m_wobbly.setEnabled(enabled);
        m_wobbly.setRealismLevel(realism);
    }

    void check_system_theme() {
        const bool current_dark = detect_system_dark_mode();
        ThemeColors::set_dark(current_dark);
        apply_theme();
    }

    void build_ui() {
        m_central_widget = new QWidget(this);
        setCentralWidget(m_central_widget);

        auto* const root = new QVBoxLayout(m_central_widget);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        // ---------------- Title bar (min / max / close) ----------------
        m_header = new QWidget(m_central_widget);
        m_header->setObjectName(QStringLiteral("titlebar"));
        m_header->setFixedHeight(38);
        auto* const hdr_lay = new QHBoxLayout(m_header);
        hdr_lay->setContentsMargins(10, 0, 8, 0);
        hdr_lay->setSpacing(4);
        auto* const hdr_title = new QLabel(QStringLiteral("Wobblin"), m_header);
        hdr_title->setObjectName(QStringLiteral("hdr_title"));
        hdr_lay->addWidget(hdr_title, 1);

        m_min_btn = makeTitleButton(QStringLiteral("0"), m_header); // Minimize
        m_max_btn = makeTitleButton(QStringLiteral("1"), m_header); // Maximize
        m_close_btn = makeTitleButton(QStringLiteral("r"), m_header); // Close
        hdr_lay->addWidget(m_min_btn);
        hdr_lay->addWidget(m_max_btn);
        hdr_lay->addWidget(m_close_btn);
        root->addWidget(m_header);

        // ---------------- Body: sidebar + content ----------------
        auto* const body = new QHBoxLayout();
        body->setContentsMargins(0, 0, 0, 0);
        body->setSpacing(0);
        // ---------------- Sidebar ----------------
        m_sidebar = new QWidget(m_central_widget);
        m_sidebar->setObjectName(QStringLiteral("sidebar"));
        m_sidebar->setFixedWidth(74);
        auto* const side_lay = new QVBoxLayout(m_sidebar);
        side_lay->setContentsMargins(10, 16, 10, 14);
        side_lay->setSpacing(10);

        auto* const brand = new QLabel(m_sidebar);
        brand->setObjectName(QStringLiteral("brand"));
        brand->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        brand->setFixedSize(44, 44);
        const QIcon brandIcon = load_application_icon();
        if (!brandIcon.isNull()) {
            brand->setPixmap(brandIcon.pixmap(44, 44));
        }
        side_lay->addWidget(brand);

        m_nav_main  = makeNavButton(QStringLiteral("Main"),       0, m_sidebar);
        m_nav_excl  = makeNavButton(QStringLiteral("Excl"),      1, m_sidebar);
        m_nav_about = makeNavButton(QStringLiteral("About"),      2, m_sidebar);
        side_lay->addWidget(m_nav_main);
        side_lay->addWidget(m_nav_excl);
        side_lay->addWidget(m_nav_about);
        side_lay->addStretch(1);

        m_github_btn = new GitHubButton(m_sidebar);
        side_lay->addWidget(m_github_btn, 0, Qt::AlignHCenter);
        body->addWidget(m_sidebar);

        // ---------------- Content stack ----------------
        m_stack = new QStackedWidget(m_central_widget);
        m_stack->setObjectName(QStringLiteral("content"));
        body->addWidget(m_stack, 1);
        root->addLayout(body);

        // Panel: Main
        m_panel_main = new QWidget(m_stack);
        m_panel_main->setObjectName(QStringLiteral("panel_main"));
        auto* const pm_lay = new QVBoxLayout(m_panel_main);
        pm_lay->setContentsMargins(20, 18, 20, 18);
        pm_lay->setSpacing(14);
        m_title_label = new QLabel(QStringLiteral("Wobblin"), m_panel_main);
        m_title_label->setObjectName(QStringLiteral("page_title"));
        m_title_label->setFont(QFont(QStringLiteral("Segoe UI Semibold"), 24));
        pm_lay->addWidget(m_title_label);
        build_main_cards(m_panel_main, pm_lay);
        pm_lay->addStretch(1);
        m_stack->addWidget(m_panel_main);

        // Panel: Exclusions
        m_panel_excl = new QWidget(m_stack);
        m_panel_excl->setObjectName(QStringLiteral("panel_excl"));
        auto* const pe_lay = new QVBoxLayout(m_panel_excl);
        pe_lay->setContentsMargins(20, 18, 20, 18);
        pe_lay->setSpacing(14);
        auto* const excl_head = new QLabel(QStringLiteral("Exclusions"), m_panel_excl);
        excl_head->setObjectName(QStringLiteral("page_title"));
        excl_head->setFont(QFont(QStringLiteral("Segoe UI Semibold"), 24));
        pe_lay->addWidget(excl_head);
        build_exclusions_card(m_panel_excl, pe_lay);
        m_stack->addWidget(m_panel_excl);

        // Panel: About
        m_panel_about = new QWidget(m_stack);
        m_panel_about->setObjectName(QStringLiteral("panel_about"));
        auto* const pa_lay = new QVBoxLayout(m_panel_about);
        pa_lay->setContentsMargins(20, 18, 20, 18);
        pa_lay->setSpacing(14);
        auto* const about_head = new QLabel(QStringLiteral("About"), m_panel_about);
        about_head->setObjectName(QStringLiteral("page_title"));
        about_head->setFont(QFont(QStringLiteral("Segoe UI Semibold"), 24));
        pa_lay->addWidget(about_head);
        build_about_card(m_panel_about, pa_lay);
        pa_lay->addStretch(1);
        m_stack->addWidget(m_panel_about);

        auto select = [this](int i) { setNav(i); };
        connect(m_nav_main.data(),  &QPushButton::clicked, this, [select] { select(0); });
        connect(m_nav_excl.data(),  &QPushButton::clicked, this, [select] { select(1); });
        connect(m_nav_about.data(), &QPushButton::clicked, this, [select] { select(2); });

        if (m_min_btn != nullptr)
            connect(m_min_btn.data(), &QPushButton::clicked, this, &Wobblin::on_minimize);
        if (m_max_btn != nullptr)
            connect(m_max_btn.data(), &QPushButton::clicked, this, &Wobblin::on_maximize);
        if (m_close_btn != nullptr)
            connect(m_close_btn.data(), &QPushButton::clicked, this, &Wobblin::on_title_close);

        setNav(0);
    }

    QPushButton* makeNavButton(const QString& text, int index, QWidget* parent) {
        auto* const b = new QPushButton(text, parent);
        b->setObjectName(QStringLiteral("navbtn"));
        b->setProperty("navIndex", index);
        b->setFixedHeight(46);
        b->setCursor(Qt::PointingHandCursor);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        return b;
    }

    QPushButton* makeTitleButton(const QString& glyph, QWidget* parent) {
        auto* const b = new QPushButton(glyph, parent);
        b->setObjectName(QStringLiteral("titlebtn"));
        b->setFont(QFont(QStringLiteral("Marlett"), 10));
        b->setFixedSize(34, 28);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    }

    void build_main_cards(QWidget* panel, QVBoxLayout* lay) {
        // Wobbly card
        m_wobbly_frame = new QFrame(panel);
        m_wobbly_frame->setObjectName(QStringLiteral("wobbly_card"));
        auto* const wobbly_layout = new QVBoxLayout(m_wobbly_frame);
        wobbly_layout->setContentsMargins(16, 16, 16, 16);
        wobbly_layout->setSpacing(0);

        auto* const wobbly_top = new QHBoxLayout();
        wobbly_top->setContentsMargins(0, 0, 0, 0);
        wobbly_top->setSpacing(0);
        m_wobbly_label = new QLabel(QStringLiteral("Wobbly Windows"), m_wobbly_frame);
        m_wobbly_label->setFont(QFont(QStringLiteral("Segoe UI Semibold"), 11));
        m_wobbly_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_wobbly_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_wobbly_label->setTextFormat(Qt::PlainText);
        m_wobbly_label->setTextInteractionFlags(Qt::NoTextInteraction);
        wobbly_top->addWidget(m_wobbly_label);
        m_wobbly_toggle = new ToggleSwitch(m_wobbly_frame, kToggleWidth, kToggleHeight);
        wobbly_top->addWidget(m_wobbly_toggle);
        wobbly_layout->addLayout(wobbly_top);

        auto* const slider_container = new QVBoxLayout();
        slider_container->setContentsMargins(0, 10, 0, 0);
        slider_container->setSpacing(2);
        m_realism_label = new QLabel(QStringLiteral("Realism"), m_wobbly_frame);
        m_realism_label->setFont(QFont(QStringLiteral("Segoe UI"), 9));
        m_realism_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_realism_label->setTextFormat(Qt::PlainText);
        m_realism_label->setTextInteractionFlags(Qt::NoTextInteraction);
        slider_container->addWidget(m_realism_label);
        m_realism_slider = new ModernSlider(m_wobbly_frame, kSliderWidth, kSliderHeight, kSliderLevels);
        m_realism_slider->setSliderEnabled(false);
        slider_container->addWidget(m_realism_slider);
        wobbly_layout->addLayout(slider_container);
        lay->addWidget(m_wobbly_frame);

        // Window Animations card
        m_burn_frame = new QFrame(panel);
        m_burn_frame->setObjectName(QStringLiteral("burn_card"));
        auto* const burn_layout = new QVBoxLayout(m_burn_frame);
        burn_layout->setContentsMargins(16, 14, 16, 14);
        burn_layout->setSpacing(8);

        auto* const burn_top = new QHBoxLayout();
        burn_top->setContentsMargins(0, 0, 0, 0);
        burn_top->setSpacing(0);
        m_burn_label = new QLabel(QStringLiteral("Window Animations"), m_burn_frame);
        m_burn_label->setFont(QFont(QStringLiteral("Segoe UI Semibold"), 11));
        m_burn_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_burn_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_burn_label->setTextFormat(Qt::PlainText);
        m_burn_label->setTextInteractionFlags(Qt::NoTextInteraction);
        burn_top->addWidget(m_burn_label);
        m_burn_toggle = new ToggleSwitch(m_burn_frame, kToggleWidth, kToggleHeight);
        burn_top->addWidget(m_burn_toggle);
        burn_layout->addLayout(burn_top);

        auto* const events_container = new QVBoxLayout();
        events_container->setContentsMargins(0, 6, 0, 4);
        events_container->setSpacing(6);

        auto* const close_row = new QHBoxLayout();
        close_row->setContentsMargins(0, 0, 0, 0);
        close_row->setSpacing(12);
        m_burn_close_label = new QLabel(QStringLiteral("Close"), m_burn_frame);
        m_burn_close_label->setFont(QFont(QStringLiteral("Segoe UI"), 9.5));
        m_burn_close_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_burn_close_label->setTextFormat(Qt::PlainText);
        m_burn_close_label->setTextInteractionFlags(Qt::NoTextInteraction);
        close_row->addWidget(m_burn_close_label, 1, Qt::AlignLeft | Qt::AlignVCenter);
        m_burn_close_toggle = new ToggleSwitch(m_burn_frame, kToggleWidth, kToggleHeight);
        close_row->addWidget(m_burn_close_toggle, 0, Qt::AlignRight | Qt::AlignVCenter);
        events_container->addLayout(close_row);

        auto* const minimize_row = new QHBoxLayout();
        minimize_row->setContentsMargins(0, 0, 0, 0);
        minimize_row->setSpacing(12);
        m_burn_minimize_label = new QLabel(QStringLiteral("Minimize"), m_burn_frame);
        m_burn_minimize_label->setFont(QFont(QStringLiteral("Segoe UI"), 9.5));
        m_burn_minimize_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_burn_minimize_label->setTextFormat(Qt::PlainText);
        m_burn_minimize_label->setTextInteractionFlags(Qt::NoTextInteraction);
        minimize_row->addWidget(m_burn_minimize_label, 1, Qt::AlignLeft | Qt::AlignVCenter);
        m_burn_minimize_toggle = new ToggleSwitch(m_burn_frame, kToggleWidth, kToggleHeight);
        minimize_row->addWidget(m_burn_minimize_toggle, 0, Qt::AlignRight | Qt::AlignVCenter);
        events_container->addLayout(minimize_row);

        auto* const open_row = new QHBoxLayout();
        open_row->setContentsMargins(0, 0, 0, 0);
        open_row->setSpacing(12);
        m_burn_open_label = new QLabel(QStringLiteral("Open"), m_burn_frame);
        m_burn_open_label->setFont(QFont(QStringLiteral("Segoe UI"), 9.5));
        m_burn_open_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_burn_open_label->setTextFormat(Qt::PlainText);
        m_burn_open_label->setTextInteractionFlags(Qt::NoTextInteraction);
        open_row->addWidget(m_burn_open_label, 1, Qt::AlignLeft | Qt::AlignVCenter);
        m_burn_open_toggle = new ToggleSwitch(m_burn_frame, kToggleWidth, kToggleHeight);
        open_row->addWidget(m_burn_open_toggle, 0, Qt::AlignRight | Qt::AlignVCenter);
        events_container->addLayout(open_row);
        burn_layout->addLayout(events_container);

        auto* const effect_row = new QHBoxLayout();
        effect_row->setContentsMargins(0, 6, 0, 0);
        effect_row->setSpacing(12);
        m_burn_effect_label = new QLabel(QStringLiteral("Effect"), m_burn_frame);
        m_burn_effect_label->setFont(QFont(QStringLiteral("Segoe UI"), 9.5));
        m_burn_effect_label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        m_burn_effect_label->setTextFormat(Qt::PlainText);
        m_burn_effect_label->setTextInteractionFlags(Qt::NoTextInteraction);
        effect_row->addWidget(m_burn_effect_label);
        m_burn_effect_combo = new QComboBox(m_burn_frame);
        m_burn_effect_combo->addItems(QStringList{
            QStringLiteral("Aura Glow"), QStringLiteral("Incinerate"),
            QStringLiteral("Matrix"), QStringLiteral("Pixelate"),
            QStringLiteral("TV Off"), QStringLiteral("Glitch"),
            QStringLiteral("Wisps") });
        m_burn_effect_combo->setEnabled(false);
        effect_row->addWidget(m_burn_effect_combo, 1);
        burn_layout->addLayout(effect_row);

        auto* const dur_container = new QVBoxLayout();
        dur_container->setContentsMargins(0, 6, 0, 0);
        dur_container->setSpacing(2);
        m_burn_dur_label = new QLabel(QStringLiteral("Duration"), m_burn_frame);
        m_burn_dur_label->setFont(QFont(QStringLiteral("Segoe UI"), 9));
        m_burn_dur_label->setTextInteractionFlags(Qt::NoTextInteraction);
        m_burn_dur_label->setTextFormat(Qt::PlainText);
        dur_container->addWidget(m_burn_dur_label);
        m_burn_dur_slider = new ModernSlider(m_burn_frame, kSliderWidth, kSliderHeight, 5);
        m_burn_dur_slider->setSliderEnabled(false);
        dur_container->addWidget(m_burn_dur_slider);
        burn_layout->addLayout(dur_container);

        lay->addWidget(m_burn_frame);

        // connects (same as before)
        connect(m_wobbly_toggle.data(), &ToggleSwitch::toggled, this, &Wobblin::on_wobbly_toggled);
        connect(m_realism_slider.data(), &ModernSlider::valueChanged, this, &Wobblin::on_realism_changed);
        connect(m_burn_toggle.data(), &ToggleSwitch::toggled, this, &Wobblin::on_burn_toggled);
        connect(m_burn_close_toggle.data(), &ToggleSwitch::toggled, this, &Wobblin::on_burn_close_toggled);
        connect(m_burn_minimize_toggle.data(), &ToggleSwitch::toggled, this, &Wobblin::on_burn_minimize_toggled);
        connect(m_burn_open_toggle.data(), &ToggleSwitch::toggled, this, &Wobblin::on_burn_open_toggled);
        connect(m_burn_effect_combo.data(), QOverload<int>::of(&QComboBox::currentIndexChanged), this, &Wobblin::on_burn_effect_changed);
        connect(m_burn_dur_slider.data(), &ModernSlider::valueChanged, this, &Wobblin::on_burn_dur_changed);
    }

    void build_exclusions_card(QWidget* panel, QVBoxLayout* lay) {
        m_exclude_frame = new QFrame(panel);
        m_exclude_frame->setObjectName(QStringLiteral("exclude_card"));
        auto* const excl_layout = new QVBoxLayout(m_exclude_frame);
        excl_layout->setContentsMargins(16, 14, 16, 14);
        excl_layout->setSpacing(8);

        auto* const excl_title = new QLabel(QStringLiteral("Excluded applications"), m_exclude_frame);
        excl_title->setFont(QFont(QStringLiteral("Segoe UI Semibold"), 11));
        excl_title->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        excl_layout->addWidget(excl_title);

        auto* const btn_row = new QHBoxLayout();
        btn_row->setContentsMargins(0, 0, 0, 0);
        btn_row->setSpacing(8);
        m_exclude_add_current = new QPushButton(QStringLiteral("+ Current"), m_exclude_frame);
        m_exclude_browse = new QPushButton(QStringLiteral("Browse..."), m_exclude_frame);
        m_exclude_remove = new QPushButton(QStringLiteral("Remove"), m_exclude_frame);
        btn_row->addWidget(m_exclude_add_current, 1);
        btn_row->addWidget(m_exclude_browse, 1);
        btn_row->addWidget(m_exclude_remove);
        excl_layout->addLayout(btn_row);

        m_exclude_list = new QListWidget(m_exclude_frame);
        m_exclude_list->setMinimumHeight(140);
        m_exclude_list->setSelectionMode(QAbstractItemView::SingleSelection);
        excl_layout->addWidget(m_exclude_list);

        lay->addWidget(m_exclude_frame);

        if (m_exclude_add_current != nullptr)
            connect(m_exclude_add_current.data(), &QPushButton::clicked, this, &Wobblin::on_exclude_add_current);
        if (m_exclude_browse != nullptr)
            connect(m_exclude_browse.data(), &QPushButton::clicked, this, &Wobblin::on_exclude_browse);
        if (m_exclude_remove != nullptr)
            connect(m_exclude_remove.data(), &QPushButton::clicked, this, &Wobblin::on_exclude_remove);
    }

    void build_about_card(QWidget* panel, QVBoxLayout* lay) {
        auto* const body = new QLabel(
            QStringLiteral("Wobblin adds a playful, GPU-accelerated wobble and burn-in "
                           "animation to your desktop windows. Built with Qt6 + Direct3D 11."),
            panel);
        body->setObjectName(QStringLiteral("about_body"));
        body->setWordWrap(true);
        lay->addWidget(body);

        auto* const ver = new QLabel(QStringLiteral("Version 1.0  -  Wobbly Windows"), panel);
        ver->setObjectName(QStringLiteral("about_ver"));
        lay->addWidget(ver);

        // GitHub button (decorative; opens repo if URL configured)
        m_github_btn->setMinimumSize(40, 40);
        lay->addWidget(m_github_btn, 0, Qt::AlignLeft);
    }

    void setNav(int index) {
        m_active_nav = index;
        auto setActive = [](QPushButton* b, bool on) {
            if (b) { b->setProperty("active", on); b->style()->unpolish(b); b->style()->polish(b); }
        };
        setActive(m_nav_main.data(),  index == 0);
        setActive(m_nav_excl.data(),  index == 1);
        setActive(m_nav_about.data(), index == 2);
        if (m_stack != nullptr) m_stack->setCurrentIndex(index);
    }

    void on_minimize() {
        if (m_wobbly_initialized && burn_cfg::onMinimize) {
            m_wobbly.handleUiMessage(WM_USER + 4, static_cast<WPARAM>(winId()), 0);
        } else {
            showMinimized();
        }
    }
    void on_maximize() {
        if (isMaximized()) { showNormal(); m_maximized = false; }
        else { showMaximized(); m_maximized = true; }
        update_max_button();
    }
    void on_title_close() {
        if (m_wobbly_initialized && burn_cfg::onClose) {
            m_wobbly.handleUiMessage(WM_USER + 3, static_cast<WPARAM>(winId()), 0);
        } else {
            close();
        }
    }

    void update_max_button() {
        if (m_max_btn != nullptr) {
            m_max_btn->setText(m_maximized ? QStringLiteral("3")   // \u25A2 restore
                                           : QStringLiteral("1"));  // \u25A1 maximize
        }
    }

    void changeEvent(QEvent* e) override {
        QMainWindow::changeEvent(e);
        if (e != nullptr && e->type() == QEvent::WindowStateChange) {
            m_maximized = isMaximized();
            update_max_button();
        }
    }

    void apply_theme() {
        const QString win_bg        = QStringLiteral("#090A0F"); // Dark solid background
        const QString title_color   = QStringLiteral("#FFFFFF");
        const QString primary_color = QStringLiteral("#F1F1F1");
        const QString secondary     = QStringLiteral("#A1A6B4");
        const QString card_bg       = QStringLiteral("#161925");
        const QString card_border   = QStringLiteral("#2C314A");
        const QString sidebar_bg    = QStringLiteral("#10121A");

        const QString card_ss = QStringLiteral(
            "QFrame#wobbly_card, QFrame#burn_card, QFrame#exclude_card { "
            "background-color: %1; border: 1px solid %2; border-radius: 12px; }"
            "QLabel { border: none; background-color: transparent; outline: none; }"
            "QFrame { border: none; background-color: transparent; }")
            .arg(card_bg, card_border);

        const QString sidebar_ss = QStringLiteral(
            "QWidget#sidebar { background-color: %1; border: none; "
            "border-right: 1px solid %2; }"
            "QLabel#brand { background-color: transparent; }"
            "QPushButton#navbtn { background-color: transparent; color: %3; border: none; "
            "border-radius: 12px; font: 600 12px 'Segoe UI'; padding: 4px; }"
            "QPushButton#navbtn:hover { background-color: rgba(255,255,255,0.05); }"
            "QPushButton#navbtn[active=\"true\"] { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 rgba(34,211,238,0.20), stop:1 rgba(232,121,249,0.0)); "
            "color: %4; border-left: 3px solid #22d3ee; }")
            .arg(sidebar_bg, card_border, secondary, primary_color);

        const QString content_ss = QStringLiteral(
            "QWidget#panel_main, QWidget#panel_excl, QWidget#panel_about { background-color: transparent; }"
            "QLabel#page_title { color: %1; background: transparent; font: 700 24px 'Segoe UI'; padding-bottom: 2px; }"
            "QLabel#brand { color: #ffffff; }"
            "QLabel#about_body { color: %2; background: transparent; font: 400 13px 'Segoe UI'; }"
            "QLabel#about_ver { color: %3; background: transparent; font: 400 11px 'Segoe UI'; }")
            .arg(title_color, primary_color, secondary);

        const QString btn_ss = QStringLiteral(
            "QPushButton { background-color: %1; color: %2; "
            "border: 1px solid %3; border-radius: 10px; "
            "padding: 7px 8px; font-size: 11px; font-weight: 600; }"
            "QPushButton:hover { background-color: rgba(255,255,255,0.05); border-color: rgba(34,211,238,0.60); }"
            "QPushButton:pressed { background-color: rgba(34,211,238,0.22); }").arg(card_bg, primary_color, card_border);
            
        const QString list_ss = QStringLiteral(
            "QListWidget { background-color: %1; color: %2; "
            "border: 1px solid %3; border-radius: 12px; "
            "padding: 4px; outline: none; }"
            "QListWidget::item { padding: 4px 6px; border-radius: 8px; }"
            "QListWidget::item:selected { background-color: rgba(34,211,238,0.24); color: #ffffff; }"
            "QListWidget::item:hover { background-color: rgba(255,255,255,0.07); }")
            .arg(card_bg, primary_color, card_border);

        const QString header_ss = QStringLiteral(
            "QWidget#titlebar { background-color: %1; border: none; "
            "border-bottom: 1px solid %2; }"
            "QLabel#hdr_title { color: %3; background: transparent; font: 600 13px 'Segoe UI'; }"
            "QPushButton#titlebtn { background-color: transparent; color: %4; border: none; "
            "border-radius: 8px; }"
            "QPushButton#titlebtn:hover { background-color: rgba(255,255,255,0.1); }"
            "QPushButton#titlebtn:pressed { background-color: rgba(255,255,255,0.2); }")
            .arg(win_bg, card_border, primary_color, secondary);

        // Window + central: opaque
        setStyleSheet(QStringLiteral("QMainWindow { background-color: %1; }").arg(win_bg));
        if (m_central_widget != nullptr) m_central_widget->setStyleSheet(QStringLiteral("background-color: %1;").arg(win_bg));
        if (m_header != nullptr) m_header->setStyleSheet(header_ss);
        if (m_sidebar != nullptr) m_sidebar->setStyleSheet(sidebar_ss);
        if (m_stack != nullptr)   m_stack->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        if (m_panel_main != nullptr)  m_panel_main->setStyleSheet(content_ss);
        if (m_panel_excl != nullptr)  m_panel_excl->setStyleSheet(content_ss);
        if (m_panel_about != nullptr) m_panel_about->setStyleSheet(content_ss);
        
        if (m_title_label != nullptr) {
            m_title_label->setStyleSheet(
                QStringLiteral("color: %1; background: transparent; font: 700 24px 'Segoe UI';").arg(title_color));
        }
        if (m_wobbly_frame != nullptr) m_wobbly_frame->setStyleSheet(card_ss);
        if (m_wobbly_label != nullptr) {
            m_wobbly_label->setStyleSheet(
                QStringLiteral("color: %1; background-color: transparent; border: none;").arg(primary_color));
        }
        if (m_burn_frame != nullptr) m_burn_frame->setStyleSheet(card_ss);
        if (m_burn_label != nullptr) {
            m_burn_label->setStyleSheet(
                QStringLiteral("color: %1; background-color: transparent; border: none;").arg(primary_color));
        }
        auto tint = [&](QWidget* w) {
            if (w) w->setStyleSheet(QStringLiteral("color: %1; background-color: transparent; border: none;").arg(secondary));
        };
        if (m_realism_label)  tint(m_realism_label);
        if (m_burn_effect_label) tint(m_burn_effect_label);
        if (m_burn_dur_label)   tint(m_burn_dur_label);
        if (m_burn_close_label)   tint(m_burn_close_label);
        if (m_burn_minimize_label) tint(m_burn_minimize_label);
        if (m_burn_open_label)    tint(m_burn_open_label);
        if (m_exclude_frame != nullptr) m_exclude_frame->setStyleSheet(card_ss);
        if (m_exclude_add_current != nullptr) m_exclude_add_current->setStyleSheet(btn_ss);
        if (m_exclude_browse != nullptr) m_exclude_browse->setStyleSheet(btn_ss);
        if (m_exclude_remove != nullptr) m_exclude_remove->setStyleSheet(btn_ss);
        if (m_exclude_list != nullptr) m_exclude_list->setStyleSheet(list_ss);
        update_realism_label_style();
        update_burn_label_style();
        repaint_accent_consumers();
    }

    void repaint_accent_consumers() noexcept {
        if (m_wobbly_toggle  != nullptr) { m_wobbly_toggle->apply_theme();  }
        if (m_realism_slider != nullptr) { m_realism_slider->apply_theme(); }
        if (m_burn_toggle    != nullptr) { m_burn_toggle->apply_theme();    }
        if (m_burn_close_toggle) m_burn_close_toggle->apply_theme();
        if (m_burn_minimize_toggle) m_burn_minimize_toggle->apply_theme();
        if (m_burn_open_toggle) m_burn_open_toggle->apply_theme();        if (m_burn_dur_slider != nullptr) { m_burn_dur_slider->apply_theme(); }
        if (m_github_btn     != nullptr) { m_github_btn->apply_theme();     }
    }

    void update_realism_label_style() {
        if (m_realism_label == nullptr || m_realism_slider == nullptr) {
            return;
        }
        const QString label_color = m_realism_slider->isSliderEnabled()
            ? safe_hex_color(ThemeColors::secondary_text().name(), QStringLiteral("#9aa0b4"))
            : safe_hex_color(ThemeColors::disabled_text().name(),  QStringLiteral("#5a5a5a"));
        m_realism_label->setStyleSheet(
            QStringLiteral("color: %1; background-color: transparent; border: none;")
                .arg(label_color));
    }

    void update_burn_label_style() {
        if (m_burn_effect_label == nullptr || m_burn_effect_combo == nullptr
            || m_burn_dur_label == nullptr || m_burn_dur_slider == nullptr) {
            return;
        }
        const bool on = m_burn_toggle != nullptr && m_burn_toggle->isChecked();
        const QString label_color = on
            ? safe_hex_color(ThemeColors::secondary_text().name(), QStringLiteral("#9aa0b4"))
            : safe_hex_color(ThemeColors::disabled_text().name(),  QStringLiteral("#5a5a5a"));
        const QString lbl = QStringLiteral("color: %1; background-color: transparent; border: none;").arg(label_color);
        m_burn_effect_label->setStyleSheet(lbl);
        m_burn_dur_label->setStyleSheet(lbl);
        if (m_burn_close_label) m_burn_close_label->setStyleSheet(lbl);
        if (m_burn_minimize_label) m_burn_minimize_label->setStyleSheet(lbl);
        if (m_burn_open_label) m_burn_open_label->setStyleSheet(lbl);
        if (m_burn_effect_combo) {
            m_burn_effect_combo->setStyleSheet(
                QStringLiteral(
                    "QComboBox { background-color: rgba(255,255,255,0.06); color: #f5f6fa; "
                    "border: 1px solid rgba(34,211,238,0.30); border-radius: 8px; padding: 5px 10px; font-size: 13px; }"
                    "QComboBox:hover { background-color: rgba(255,255,255,0.09); border-color: rgba(34,211,238,0.60); }"
                    "QComboBox:focus { border: 1px solid #22d3ee; }"
                    "QComboBox QAbstractItemView { background-color: #161821; color: #f5f6fa; "
                    "selection-background-color: rgba(34,211,238,0.25); border: 1px solid rgba(34,211,238,0.30); border-radius: 8px; padding: 4px; }"
                    "QComboBox::drop-arrow { image: none; }"));
        }
    }

    [[nodiscard]] static int levelToBurnMs(int level) noexcept {
        switch (level) {
            case 1: return 200; case 2: return 400; case 3: return 600;
            case 4: return 800; case 5: return 1000; default: return 600;
        }
    }
    [[nodiscard]] static int burnMsToLevel(int ms) noexcept {
        if (ms <= 200) return 1; if (ms <= 400) return 2;
        if (ms <= 600) return 3; if (ms <= 800) return 4; return 5;
    }

    QPointer<QWidget>      m_central_widget;
    QPointer<QLabel>       m_title_label;
    QPointer<QFrame>       m_wobbly_frame;
    QPointer<QLabel>       m_wobbly_label;
    QPointer<ToggleSwitch> m_wobbly_toggle;
    QPointer<QLabel>       m_realism_label;
    QPointer<ModernSlider> m_realism_slider;
    QPointer<GitHubButton> m_github_btn;
    QPointer<QFrame>       m_burn_frame;
    QPointer<QLabel>       m_burn_label;
    QPointer<ToggleSwitch> m_burn_toggle;
    QPointer<QLabel>       m_burn_close_label;
    QPointer<ToggleSwitch> m_burn_close_toggle;
    QPointer<QLabel>       m_burn_minimize_label;
    QPointer<ToggleSwitch> m_burn_minimize_toggle;
    QPointer<QLabel>       m_burn_open_label;
    QPointer<ToggleSwitch> m_burn_open_toggle;    QPointer<QLabel>       m_burn_effect_label;
    QPointer<QComboBox>    m_burn_effect_combo;
    QPointer<QLabel>       m_burn_dur_label;
    QPointer<ModernSlider> m_burn_dur_slider;
    // Card 3: Exclusions
    QPointer<QFrame>       m_exclude_frame;
    QPointer<QPushButton>  m_exclude_add_current;
    QPointer<QPushButton>  m_exclude_browse;
    QPointer<QPushButton>  m_exclude_remove;
    QPointer<QListWidget>  m_exclude_list;
    WobblyController     m_wobbly;
    bool                 m_wobbly_initialized = false;
    bool                 m_force_quit         = false;
    bool                 m_loading_settings   = false;

    // Aurora / Aero Glass navigation shell
    QPointer<QWidget>      m_sidebar;
    QPointer<QStackedWidget> m_stack;
    QPointer<QWidget>      m_panel_main;
    QPointer<QWidget>      m_panel_excl;
    QPointer<QWidget>      m_panel_about;
    QPointer<QWidget>      m_header;
    QPointer<QPushButton>  m_min_btn;
    QPointer<QPushButton>  m_max_btn;
    QPointer<QPushButton>  m_close_btn;
    QPointer<QPushButton>  m_nav_main;
    QPointer<QPushButton>  m_nav_excl;
    QPointer<QPushButton>  m_nav_about;
    int                    m_active_nav = 0;
    bool                   m_maximized = false;

    // Reflect current m_exclude_list rows into the two persistence sets.
    void syncExclusions() {
        QStringList wob, anim;
        for (int i = 0; i < m_exclude_list->count(); ++i) {
            QListWidgetItem* it = m_exclude_list->item(i);
            ExclusionRow* row = it ? qobject_cast<ExclusionRow*>(m_exclude_list->itemWidget(it)) : nullptr;
            if (!row) continue;
            const QString exe = row->exeName();
            if (exe.isEmpty()) continue;
            if (row->wobbleOn()) wob.append(exe);
            if (row->animOn())   anim.append(exe);
        }
        AppPersistence::setWobbleExclusions(wob);
        AppPersistence::setAnimExclusions(anim);
        if (m_wobbly_initialized) {
            std::set<QString, IStringLess> wb, an;
            for (const QString& s : wob) wb.insert(s);
            for (const QString& s : anim) an.insert(s);
            m_wobbly.setWobbleExclusions(wb);
            m_wobbly.setAnimExclusions(an);
        }
    }


struct Strings {
    QString title;
    QString body;
    QString button;
    bool rtl = false;
};

[[nodiscard]] Strings localized_strings() {
    const QLocale::Language lang = QLocale::system().language();
    switch (lang) {
    case QLocale::Russian:
        return {
            QString::fromUtf8("Поддержка"),
            QString::fromUtf8("Здравствуйте, мне 15 лет, эта программа абсолютно бесплатная. Я буду очень благодарен вам, если вы поможете накопить мне на хорошее рабочее место. Спасибо за установку Wobblin :)"),
            QString::fromUtf8("Донат"), false };
    case QLocale::Ukrainian:
        return {
            QString::fromUtf8("Підтримка"),
            QString::fromUtf8("Вітаю, мені 15 років, ця програма абсолютно безкоштовна. Я буду дуже вдячний вам, якщо ви допоможете мені накопичити на гарне робоче місце. Дякую за встановлення Wobblin :)"),
            QString::fromUtf8("Донат"), false };
    case QLocale::German:
        return {
            QString::fromUtf8("Unterstützung"),
            QString::fromUtf8("Hallo, ich bin 15 Jahre alt und dieses Programm ist völlig kostenlos. Ich wäre Ihnen sehr dankbar, wenn Sie mir helfen würden, für einen guten Arbeitsplatz zu sparen. Danke, dass Sie Wobblin installiert haben :)"),
            QString::fromUtf8("Spenden"), false };
    case QLocale::French:
        return {
            QString::fromUtf8("Soutien"),
            QString::fromUtf8("Bonjour, j'ai 15 ans et ce programme est entièrement gratuit. Je vous serais très reconnaissant de m'aider à économiser pour un bon poste de travail. Merci d'avoir installé Wobblin :)"),
            QString::fromUtf8("Faire un don"), false };
    case QLocale::Spanish:
        return {
            QString::fromUtf8("Apoyo"),
            QString::fromUtf8("Hola, tengo 15 años y este programa es completamente gratuito. Te estaría muy agradecido si me ayudaras a ahorrar para un buen espacio de trabajo. Gracias por instalar Wobblin :)"),
            QString::fromUtf8("Donar"), false };
    case QLocale::Italian:
        return {
            QString::fromUtf8("Supporto"),
            QString::fromUtf8("Ciao, ho 15 anni e questo programma è completamente gratuito. Ti sarei molto grato se mi aiutassi a risparmiare per una buona postazione di lavoro. Grazie per aver installato Wobblin :)"),
            QString::fromUtf8("Dona"), false };
    case QLocale::Portuguese:
        return {
            QString::fromUtf8("Apoio"),
            QString::fromUtf8("Olá, tenho 15 anos e este programa é totalmente gratuito. Ficaria muito grato se você me ajudasse a juntar dinheiro para um bom espaço de trabalho. Obrigado por instalar o Wobblin :)"),
            QString::fromUtf8("Doar"), false };
    case QLocale::Polish:
        return {
            QString::fromUtf8("Wsparcie"),
            QString::fromUtf8("Cześć, mam 15 lat, a ten program jest całkowicie darmowy. Byłbym bardzo wdzięczny, gdybyś pomógł mi uzbierać na dobre stanowisko pracy. Dziękuję za zainstalowanie Wobblin :)"),
            QString::fromUtf8("Wesprzyj"), false };
    case QLocale::Turkish:
        return {
            QString::fromUtf8("Destek"),
            QString::fromUtf8("Merhaba, 15 yaşındayım ve bu program tamamen ücretsiz. İyi bir çalışma alanı için para biriktirmeme yardımcı olursanız çok minnettar olurum. Wobblin'ı yüklediğiniz için teşekkürler :)"),
            QString::fromUtf8("Bağış yap"), false };
    case QLocale::Dutch:
        return {
            QString::fromUtf8("Ondersteuning"),
            QString::fromUtf8("Hallo, ik ben 15 jaar oud en dit programma is volledig gratis. Ik zou je erg dankbaar zijn als je me zou helpen sparen voor een goede werkplek. Bedankt voor het installeren van Wobblin :)"),
            QString::fromUtf8("Doneren"), false };
    case QLocale::Chinese:
        return {
            QString::fromUtf8("支持"),
            QString::fromUtf8("您好，我今年15岁，这个程序完全免费。如果您能帮助我攒钱购置一个好的工作环境，我将非常感激。感谢您安装 Wobblin :)"),
            QString::fromUtf8("捐赠"), false };
    case QLocale::Japanese:
        return {
            QString::fromUtf8("サポート"),
            QString::fromUtf8("こんにちは、私は15歳です。このプログラムは完全に無料です。良い作業環境のための資金を貯めるのを手伝っていただけると、とても感謝します。Wobblin をインストールしていただきありがとうございます :)"),
            QString::fromUtf8("寄付する"), false };
    case QLocale::Korean:
        return {
            QString::fromUtf8("후원"),
            QString::fromUtf8("안녕하세요, 저는 15살이고 이 프로그램은 완전히 무료입니다. 좋은 작업 공간을 마련할 수 있도록 도와주시면 정말 감사하겠습니다. Wobblin를 설치해 주셔서 감사합니다 :)"),
            QString::fromUtf8("후원하기"), false };
    case QLocale::Arabic:
        return {
            QString::fromUtf8("الدعم"),
            QString::fromUtf8("مرحبًا، عمري 15 عامًا، وهذا البرنامج مجاني تمامًا. سأكون ممتنًا جدًا لك إذا ساعدتني في توفير المال لمكان عمل جيد. شكرًا لتثبيت Wobblin :)"),
            QString::fromUtf8("تبرع"), true };
    case QLocale::English:
    default:
        return {
            QString::fromUtf8("Support"),
            QString::fromUtf8("Hello, I'm 15 years old, and this program is completely free. I would be very grateful if you could help me save up for a good workspace. Thank you for installing Wobblin :)"),
            QString::fromUtf8("Donate"), false };
    }
}

[[nodiscard]] QString contrast_text_for(const QColor& bg) {
    const double luminance = (0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue()) / 255.0;
    return luminance > 0.6 ? QStringLiteral("#000000") : QStringLiteral("#ffffff");
}

}; // class Wobblin

// Undef Windows macros that collide with Qt's moc-generated qtmochelpers.h
// (windows.h / d3d11.h / dxgi / dcomp #define 'interface' as 'struct', plus
// other legacy macros). Without this, qtmochelpers.h fails to parse when
// app.moc pulls it in, breaking every Q_OBJECT class with
// "QtMocHelpers: is not a class" / "stringData not found".
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

#include "app.moc"

int main(int argc, char* argv[]) {
    std::set_terminate(secure_terminate_handler);
    std::set_new_handler(secure_new_handler);
    qInstallMessageHandler(secure_qt_message_handler);

#ifdef _WIN32
    timeBeginPeriod(1);
    apply_windows_mitigations();
    ::SetCurrentProcessExplicitAppUserModelID(L"Wobblin.utility.v1");
    if (!WobblyController::ensureElevated(argc, argv)) {
        return EXIT_SUCCESS;
    }
#endif

    try {
        QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

        QSurfaceFormat fmt;
        fmt.setSamples(4);
        fmt.setSwapInterval(1);
        fmt.setRenderableType(QSurfaceFormat::OpenGL);
        QSurfaceFormat::setDefaultFormat(fmt);

        QApplication app(argc, argv);
        app.setQuitOnLastWindowClosed(false);
        app.setStyle(QStringLiteral("Fusion"));
        app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
        app.setApplicationName(QStringLiteral("Wobblin"));
        app.setApplicationDisplayName(QStringLiteral("Wobblin"));
        app.setOrganizationName(QStringLiteral("Wobblin"));

        const bool background_launch = app.arguments().contains(kStartupArg);

        if (AppPersistence::notifyExistingInstance()) {
            return EXIT_SUCCESS;
        }

        QSystemSemaphore sem(QStringLiteral("Wobblin.SingleInstance.Sem.v1"), 1);
        QSharedMemory shared(QStringLiteral("Wobblin.SingleInstance.Mem.v1"));
        {
            SemaphoreGuard guard(sem);
            if (!guard.acquired()) {
                return EXIT_FAILURE;
            }
            const bool already_running = shared.attach();
            if (already_running) {
                shared.detach();
                if (AppPersistence::notifyExistingInstance()) {
                    return EXIT_SUCCESS;
                }
                return EXIT_SUCCESS;
            }
            if (!shared.create(1)) {
                return EXIT_FAILURE;
            }
        }

        QLocalServer local_server;
        QLocalServer::removeServer(kLocalServerName);
        if (!local_server.listen(kLocalServerName)) {
            return EXIT_FAILURE;
        }

        ThemeColors::set_dark(detect_system_dark_mode());
        ThemeColors::refresh_accent();

        SetCurrentProcessExplicitAppUserModelID(L"Wobblin.WobblyWindows.App");
        const QIcon app_icon = load_application_icon();
        if (!app_icon.isNull()) {
            app.setWindowIcon(app_icon);
        }

        Wobblin window;
        if (!app_icon.isNull()) {
            window.setWindowIcon(app_icon);
        }

        center_window_on_screen(&window);

        AppPersistence::ensureStartupRegistration();

        if (!QSystemTrayIcon::isSystemTrayAvailable()) {
            return EXIT_FAILURE;
        }

        QSystemTrayIcon tray_icon;
        if (!app_icon.isNull()) {
            tray_icon.setIcon(app_icon);
        }
        tray_icon.setToolTip(QStringLiteral("Wobblin"));

        QMenu tray_menu;
        auto* const open_action = tray_menu.addAction(QStringLiteral("Open"));
        auto* const exit_action = tray_menu.addAction(QStringLiteral("Exit"));
        tray_icon.setContextMenu(&tray_menu);
        tray_icon.show();

        const auto show_windows = [&window]() {
            center_window_on_screen(&window);
            window.showMainWindow();
        };

        QObject::connect(open_action, &QAction::triggered, &app, show_windows);
        QObject::connect(&tray_icon, &QSystemTrayIcon::activated, &app,
            [&window](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                    center_window_on_screen(&window);
                    window.showMainWindow();
                }
            });

        QObject::connect(exit_action, &QAction::triggered, &app, [&]() {
            window.requestForceQuit();
            tray_icon.hide();
            app.quit();
        });

        QObject::connect(&local_server, &QLocalServer::newConnection, &app, [&local_server, show_windows]() {
            QLocalSocket* const client = local_server.nextPendingConnection();
            if (client == nullptr) {
                return;
            }
            if (client->waitForReadyRead(500)) {
                if (client->readAll() == QByteArrayLiteral("show")) {
                    show_windows();
                }
            }
            client->disconnectFromServer();
            client->deleteLater();
        });

        if (background_launch) {
            (void)window.winId();
            (void)window.ensureWobblyEngine();
        } else {
            window.show();
            (void)window.ensureWobblyEngine();
        }

        const int rc = app.exec();
        if (shared.isAttached()) {
            shared.detach();
        }
        local_server.close();
        QLocalServer::removeServer(kLocalServerName);
        return rc;
    } catch (const std::bad_alloc&) {
        return EXIT_FAILURE;
    } catch (const std::exception&) {
        return EXIT_FAILURE;
    } catch (...) {
        return EXIT_FAILURE;
    }
}