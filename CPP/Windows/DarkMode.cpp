// Windows/DarkMode.cpp

#include "StdAfx.h"

#ifndef UNDER_CE

#include <CommCtrl.h>
#include <math.h>

#include "DarkMode.h"
#include "Control/Dialog.h"

/*
  Notes:
  - SetWindowTheme(hwnd, L"DarkMode_Explorer") gives dark scrollbars, buttons, listviews, treeviews.
  - SetWindowTheme(hwnd, L"DarkMode_CFD") gives dark edit and combobox controls.
  - SetWindowTheme(hwnd, L"DarkMode_ItemsView") gives dark header control.
  - checkbox/radio labels, group boxes, status bars and tab controls
    don't use dark colors via themes. So we draw them here.
  - menu bar is drawn via undocumented WM_UAHDRAWMENU / WM_UAHDRAWMENUITEM messages.
  - popup menus use dark colors after SetPreferredAppMode(ForceDark) (uxtheme.dll #135).
*/

namespace NWindows {
namespace NDarkMode {

// ---------- undocumented and dynamically loaded functions ----------

Z7_DIAGNOSTIC_IGNORE_CAST_FUNCTION

typedef void (WINAPI *Func_RtlGetNtVersionNumbers)(LPDWORD major, LPDWORD minor, LPDWORD build);

typedef HRESULT (WINAPI *Func_SetWindowTheme)(HWND hwnd, LPCWSTR subAppName, LPCWSTR subIdList);
typedef HANDLE  (WINAPI *Func_OpenThemeData)(HWND hwnd, LPCWSTR classList);
typedef HRESULT (WINAPI *Func_CloseThemeData)(HANDLE theme);
typedef HRESULT (WINAPI *Func_DrawThemeBackground)(HANDLE theme, HDC hdc,
    int partId, int stateId, const RECT *rect, const RECT *clipRect);
typedef HRESULT (WINAPI *Func_GetThemePartSize)(HANDLE theme, HDC hdc,
    int partId, int stateId, const RECT *rect, int eSize, SIZE *size);
typedef HRESULT (WINAPI *Func_EnableThemeDialogTexture)(HWND hwnd, DWORD flags);

// uxtheme.dll ordinals:
typedef void    (WINAPI *Func_RefreshImmersiveColorPolicyState)(); // #104
typedef BOOLEAN (WINAPI *Func_AllowDarkModeForWindow)(HWND hwnd, BOOLEAN allow); // #133
typedef BOOLEAN (WINAPI *Func_AllowDarkModeForApp)(BOOLEAN allow); // #135 in 1809
typedef int     (WINAPI *Func_SetPreferredAppMode)(int appMode); // #135 in 1903+
typedef void    (WINAPI *Func_FlushMenuThemes)(); // #136

typedef HRESULT (WINAPI *Func_DwmSetWindowAttribute)(HWND hwnd, DWORD attr, LPCVOID val, DWORD size);
typedef BOOL    (WINAPI *Func_GetMenuBarInfo)(HWND hwnd, LONG idObject, LONG idItem, PMENUBARINFO pmbi);

static Func_SetWindowTheme f_SetWindowTheme;
static Func_OpenThemeData f_OpenThemeData;
static Func_CloseThemeData f_CloseThemeData;
static Func_DrawThemeBackground f_DrawThemeBackground;
static Func_GetThemePartSize f_GetThemePartSize;
static Func_EnableThemeDialogTexture f_EnableThemeDialogTexture;
static Func_RefreshImmersiveColorPolicyState f_RefreshImmersiveColorPolicyState;
static Func_AllowDarkModeForWindow f_AllowDarkModeForWindow;
static Func_AllowDarkModeForApp f_AllowDarkModeForApp;
static Func_SetPreferredAppMode f_SetPreferredAppMode;
static Func_FlushMenuThemes f_FlushMenuThemes;
static Func_DwmSetWindowAttribute f_DwmSetWindowAttribute;
static Func_GetMenuBarInfo f_GetMenuBarInfo;

static DWORD g_BuildNumber;
static bool g_Supported;
static bool g_Enabled;
static unsigned g_Mode;

#define k_LOAD_LIBRARY_SEARCH_SYSTEM32  0x00000800

#define k_Build_1809   17763 // first build with dark mode functions in uxtheme.dll
#define k_Build_1903   18362 // SetPreferredAppMode() instead of AllowDarkModeForApp()
#define k_Build_DWMWA_20  18985 // DWMWA_USE_IMMERSIVE_DARK_MODE == 20 (it was 19 before)

// PreferredAppMode for SetPreferredAppMode()
#define k_AppMode_Default    0
#define k_AppMode_ForceDark  2

// uxtheme constants (vsstyle.h / uxtheme.h)
#define k_BP_RADIOBUTTON  2
#define k_BP_CHECKBOX     3
#define k_TS_DRAW  2
#define k_ETDT_DISABLE    0x1
#define k_ETDT_ENABLETAB  0x6

// undocumented messages for menu bar drawing
#define k_WM_UAHDRAWMENU      0x0091
#define k_WM_UAHDRAWMENUITEM  0x0092

struct CUahMenu
{
  HMENU hmenu;
  HDC hdc;
  DWORD dwFlags;
};

struct CUahMenuItem
{
  int iPosition;
  DWORD umim[8];  // UAHMENUITEMMETRICS
  DWORD umpm[5];  // UAHMENUPOPUPMETRICS
};

struct CUahDrawMenuItem
{
  DRAWITEMSTRUCT dis;
  CUahMenu um;
  CUahMenuItem umi;
};

#define k_PropertySheet_TabID  0x3020

#ifndef TBCDRF_USECDCOLORS
#define TBCDRF_USECDCOLORS  0x00800000
#endif

static const COLORREF k_Color_Back         = RGB(0x20, 0x20, 0x20);
static const COLORREF k_Color_Control      = RGB(0x2b, 0x2b, 0x2b);
static const COLORREF k_Color_View         = RGB(0x19, 0x19, 0x19);
static const COLORREF k_Color_Text         = RGB(0xe0, 0xe0, 0xe0);
static const COLORREF k_Color_TextDisabled = RGB(0x80, 0x80, 0x80);
static const COLORREF k_Color_Edge         = RGB(0x4a, 0x4a, 0x4a);
static const COLORREF k_Color_Hot          = RGB(0x3d, 0x3d, 0x3d);
static const COLORREF k_Color_Pushed       = RGB(0x4d, 0x4d, 0x4d);
static const COLORREF k_Color_ProgressTrack  = RGB(0x3a, 0x3a, 0x3a);
static const COLORREF k_Color_ProgressTop    = RGB(0x34, 0xd3, 0x6b);
static const COLORREF k_Color_ProgressBottom = RGB(0x16, 0xa3, 0x4a);

enum
{
  k_Brush_Back,
  k_Brush_Control,
  k_Brush_Edge,
  k_Brush_Hot,
  k_Brush_Pushed,
  k_NumBrushes
};

static HBRUSH g_Brushes[k_NumBrushes];

static HBRUSH GetBrush(unsigned index)
{
  if (!g_Brushes[index])
  {
    COLORREF c;
    switch (index)
    {
      case k_Brush_Control: c = k_Color_Control; break;
      case k_Brush_Edge:    c = k_Color_Edge; break;
      case k_Brush_Hot:     c = k_Color_Hot; break;
      case k_Brush_Pushed:  c = k_Color_Pushed; break;
      default:              c = k_Color_Back; break;
    }
    g_Brushes[index] = ::CreateSolidBrush(c);
  }
  return g_Brushes[index];
}

COLORREF GetBackColor() { return k_Color_Back; }
COLORREF GetControlColor() { return k_Color_Control; }
COLORREF GetViewColor() { return k_Color_View; }
COLORREF GetTextColor() { return k_Color_Text; }
COLORREF GetDisabledTextColor() { return k_Color_TextDisabled; }
HBRUSH GetBackBrush() { return GetBrush(k_Brush_Back); }

bool IsSupported() { return g_Supported; }
bool IsEnabled() { return g_Enabled; }
unsigned GetMode() { return g_Mode; }


static bool LoadFunctions()
{
  {
    const HMODULE hmodule = ::GetModuleHandleW(L"ntdll.dll");
    if (!hmodule)
      return false;
    const
    Func_RtlGetNtVersionNumbers f = Z7_GET_PROC_ADDRESS(
    Func_RtlGetNtVersionNumbers, hmodule,
        "RtlGetNtVersionNumbers");
    if (!f)
      return false;
    DWORD major = 0, minor = 0, build = 0;
    f(&major, &minor, &build);
    build &= ~(DWORD)0xF0000000;
    if (major < 10 || (major == 10 && minor == 0 && build < k_Build_1809))
      return false;
    g_BuildNumber = build;
  }
  {
    const HMODULE hmodule = ::LoadLibraryExW(L"uxtheme.dll", NULL, k_LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!hmodule)
      return false;
    f_SetWindowTheme = Z7_GET_PROC_ADDRESS(
    Func_SetWindowTheme, hmodule, "SetWindowTheme");
    f_OpenThemeData = Z7_GET_PROC_ADDRESS(
    Func_OpenThemeData, hmodule, "OpenThemeData");
    f_CloseThemeData = Z7_GET_PROC_ADDRESS(
    Func_CloseThemeData, hmodule, "CloseThemeData");
    f_DrawThemeBackground = Z7_GET_PROC_ADDRESS(
    Func_DrawThemeBackground, hmodule, "DrawThemeBackground");
    f_GetThemePartSize = Z7_GET_PROC_ADDRESS(
    Func_GetThemePartSize, hmodule, "GetThemePartSize");
    f_EnableThemeDialogTexture = Z7_GET_PROC_ADDRESS(
    Func_EnableThemeDialogTexture, hmodule, "EnableThemeDialogTexture");

    f_RefreshImmersiveColorPolicyState = Z7_GET_PROC_ADDRESS(
    Func_RefreshImmersiveColorPolicyState, hmodule, MAKEINTRESOURCEA(104));
    f_AllowDarkModeForWindow = Z7_GET_PROC_ADDRESS(
    Func_AllowDarkModeForWindow, hmodule, MAKEINTRESOURCEA(133));
    if (g_BuildNumber < k_Build_1903)
      f_AllowDarkModeForApp = Z7_GET_PROC_ADDRESS(
      Func_AllowDarkModeForApp, hmodule, MAKEINTRESOURCEA(135));
    else
      f_SetPreferredAppMode = Z7_GET_PROC_ADDRESS(
      Func_SetPreferredAppMode, hmodule, MAKEINTRESOURCEA(135));
    f_FlushMenuThemes = Z7_GET_PROC_ADDRESS(
    Func_FlushMenuThemes, hmodule, MAKEINTRESOURCEA(136));

    if (!f_SetWindowTheme
        || !f_OpenThemeData
        || !f_CloseThemeData
        || !f_DrawThemeBackground
        || !f_GetThemePartSize
        || !f_AllowDarkModeForWindow
        || (!f_AllowDarkModeForApp && !f_SetPreferredAppMode))
      return false;
  }
  {
    const HMODULE hmodule = ::LoadLibraryExW(L"dwmapi.dll", NULL, k_LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hmodule)
      f_DwmSetWindowAttribute = Z7_GET_PROC_ADDRESS(
      Func_DwmSetWindowAttribute, hmodule, "DwmSetWindowAttribute");
  }
  {
    const HMODULE hmodule = ::GetModuleHandleW(L"user32.dll");
    if (hmodule)
      f_GetMenuBarInfo = Z7_GET_PROC_ADDRESS(
      Func_GetMenuBarInfo, hmodule, "GetMenuBarInfo");
  }
  return true;
}


static bool IsHighContrast()
{
  HIGHCONTRASTW hc;
  memset(&hc, 0, sizeof(hc));
  hc.cbSize = sizeof(hc);
  if (::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0))
    return (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
  return false;
}

static bool IsSystemDark()
{
  HKEY key;
  if (::RegOpenKeyExW(HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      0, KEY_READ, &key) != ERROR_SUCCESS)
    return false;
  DWORD val = 1;
  DWORD size = sizeof(val);
  DWORD type = 0;
  const LONG res = ::RegQueryValueExW(key, L"AppsUseLightTheme", NULL, &type, (BYTE *)&val, &size);
  ::RegCloseKey(key);
  return res == ERROR_SUCCESS && type == REG_DWORD && size == sizeof(val) && val == 0;
}

static bool CalcEnabled()
{
  if (!g_Supported || IsHighContrast())
    return false;
  if (g_Mode == k_Mode_Dark)
    return true;
  if (g_Mode == k_Mode_Light)
    return false;
  return IsSystemDark();
}

static void SetAppMode()
{
  if (f_SetPreferredAppMode)
    f_SetPreferredAppMode(g_Enabled ? k_AppMode_ForceDark : k_AppMode_Default);
  else if (f_AllowDarkModeForApp)
    f_AllowDarkModeForApp(g_Enabled ? TRUE : FALSE);
  if (f_RefreshImmersiveColorPolicyState)
    f_RefreshImmersiveColorPolicyState();
  if (f_FlushMenuThemes)
    f_FlushMenuThemes();
}

// returns true, if dark state was changed
static bool UpdateState()
{
  if (!g_Supported)
    return false;
  const bool enabled = CalcEnabled();
  if (enabled == g_Enabled)
    return false;
  g_Enabled = enabled;
  SetAppMode();
  return true;
}


static bool DialogHook(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, INT_PTR &result);

void Init(unsigned mode)
{
  g_Mode = mode;
  g_Supported = LoadFunctions();
  if (!g_Supported)
    return;
  g_Enabled = CalcEnabled();
  if (g_Enabled)
    SetAppMode();
  NControl::g_DialogHook = DialogHook;
}

bool SetMode(unsigned mode)
{
  g_Mode = mode;
  return UpdateState();
}


// ---------- applying to windows ----------

static const wchar_t * const k_Prop_Applied = L"Z7_DarkMode";
static const wchar_t * const k_Prop_Proc    = L"Z7_DarkMode_Proc";
static const wchar_t * const k_Prop_Kind    = L"Z7_DarkMode_Kind";

static void SetTheme(HWND hwnd, LPCWSTR name)
{
  if (hwnd && f_SetWindowTheme)
    f_SetWindowTheme(hwnd, name, NULL);
}

static void SetTitleBar(HWND hwnd, bool dark)
{
  if (!f_DwmSetWindowAttribute)
    return;
  const BOOL val = dark ? TRUE : FALSE;
  f_DwmSetWindowAttribute(hwnd, g_BuildNumber >= k_Build_DWMWA_20 ? 20 : 19, &val, sizeof(val));
}

static bool IsClass(const wchar_t *name, const wchar_t *className)
{
  return ::lstrcmpiW(name, className) == 0;
}

static bool GetClass(HWND hwnd, wchar_t *name, int size)
{
  name[0] = 0;
  return ::GetClassNameW(hwnd, name, size) != 0;
}

static bool IsPropertySheet(HWND hwnd)
{
  wchar_t name[32];
  const HWND tab = ::GetDlgItem(hwnd, k_PropertySheet_TabID);
  return tab
      && GetClass(tab, name, Z7_ARRAY_SIZE(name))
      && IsClass(name, WC_TABCONTROLW);
}

static bool IsAccelHidden(HWND hwnd)
{
  return (::SendMessageW(hwnd, Z7_WIN_WM_QUERYUISTATE, 0, 0) & Z7_WIN_UISF_HIDEACCEL) != 0;
}

static bool IsFocusHidden(HWND hwnd)
{
  return (::SendMessageW(hwnd, Z7_WIN_WM_QUERYUISTATE, 0, 0) & Z7_WIN_UISF_HIDEFOCUS) != 0;
}


enum
{
  k_Kind_CtlColor = 1,
  k_Kind_PropSheet,
  k_Kind_GroupBox,
  k_Kind_ListView,
  k_Kind_StatusBar,
  k_Kind_Tab,
  k_Kind_ReBar,
  k_Kind_ToolBar,
  k_Kind_Progress
};

static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

/* We use simple subclassing via GWLP_WNDPROC.
   The subclass is not removed, when dark mode is disabled:
   SubclassProc() just calls original window procedure in light mode. */

static void Subclass(HWND hwnd, unsigned kind)
{
  if (::GetPropW(hwnd, k_Prop_Proc))
    return;
  ::SetPropW(hwnd, k_Prop_Kind, (HANDLE)(UINT_PTR)kind);
  const LONG_PTR prev = ::SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)SubclassProc);
  if (prev == 0)
  {
    ::RemovePropW(hwnd, k_Prop_Kind);
    return;
  }
  ::SetPropW(hwnd, k_Prop_Proc, (HANDLE)prev);
}


// ---------- animated progress bar ----------

/* In dark mode the progress bar is drawn by us:
   rounded (pill) track and bar with anti-aliased edges, percentage text,
   the bar slides smoothly to new position, and soft sheen moves along the bar. */

static const UINT_PTR k_Progress_TimerId = 0x7A37;
static const UINT k_Progress_TimerPeriod = 15; // ms
static const DWORD k_Progress_SheenPeriod = 2000; // ms
static const wchar_t * const k_Prop_Anim = L"Z7_DarkMode_Anim";

struct CProgressAnim
{
  double Shown; // currently drawn fraction [0, 1]
  DWORD LastTick;
  DWORD StartTick;
};

static double Clamp01(double v)
{
  return v < 0 ? 0 : (v > 1 ? 1 : v);
}

static double Progress_GetTarget(HWND hwnd)
{
  PBRANGE range;
  range.iLow = 0;
  range.iHigh = 100;
  ::SendMessageW(hwnd, PBM_GETRANGE, TRUE, (LPARAM)&range);
  const int pos = (int)::SendMessageW(hwnd, PBM_GETPOS, 0, 0);
  if (range.iHigh <= range.iLow)
    return 0;
  return Clamp01(((double)pos - (double)range.iLow) / ((double)range.iHigh - (double)range.iLow));
}

static CProgressAnim *Progress_GetAnim(HWND hwnd)
{
  return (CProgressAnim *)::GetPropW(hwnd, k_Prop_Anim);
}

static void Progress_Start(HWND hwnd)
{
  if (!Progress_GetAnim(hwnd))
  {
    CProgressAnim *anim = new CProgressAnim;
    anim->Shown = Progress_GetTarget(hwnd);
    anim->LastTick = anim->StartTick = ::GetTickCount();
    ::SetPropW(hwnd, k_Prop_Anim, (HANDLE)anim);
  }
  ::SetTimer(hwnd, k_Progress_TimerId, k_Progress_TimerPeriod, NULL);
}

static void Progress_Free(HWND hwnd)
{
  CProgressAnim *anim = Progress_GetAnim(hwnd);
  if (anim)
  {
    ::RemovePropW(hwnd, k_Prop_Anim);
    delete anim;
  }
}

static void Progress_OnTimer(HWND hwnd)
{
  CProgressAnim *anim = Progress_GetAnim(hwnd);
  if (!anim)
    return;
  const DWORD now = ::GetTickCount();
  DWORD dt = now - anim->LastTick;
  anim->LastTick = now;
  if (dt > 100)
    dt = 100;
  const double target = Progress_GetTarget(hwnd);
  // easing to target position with time constant ~90 ms
  anim->Shown += (target - anim->Shown) * ((double)dt / ((double)dt + 90.0));
  const double diff = target - anim->Shown;
  if (diff < 0.0005 && diff > -0.0005)
    anim->Shown = target;
  if (::IsWindowVisible(hwnd))
    ::InvalidateRect(hwnd, NULL, FALSE);
}

// coverage of pixel center (px, dy) by horizontal capsule [x0, x1] with radius (r)
static double CapsuleCoverage(double px, double dy, double x0, double x1, double r)
{
  const double ax = x0 + r;
  double bx = x1 - r;
  if (bx < ax)
    bx = ax;
  const double dx = px < ax ? ax - px : (px > bx ? px - bx : 0);
  return Clamp01(r - sqrt(dx * dx + dy * dy) + 0.5);
}

static double Chan(COLORREF c, unsigned shift)
{
  return (double)((c >> shift) & 0xff);
}

static double Mix(double a, double b, double t)
{
  return a + (b - a) * t;
}

static void Progress_Paint(HWND hwnd)
{
  PAINTSTRUCT ps;
  const HDC hdcWin = ::BeginPaint(hwnd, &ps);
  if (!hdcWin)
    return;
  RECT rc;
  ::GetClientRect(hwnd, &rc);
  const int w = rc.right;
  const int h = rc.bottom;
  const CProgressAnim *anim = Progress_GetAnim(hwnd);
  const double shown = anim ? anim->Shown : Progress_GetTarget(hwnd);

  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h; // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void *bits = NULL;
  const HDC hdc = ::CreateCompatibleDC(hdcWin);
  const HBITMAP bmp = (w > 0 && h > 0 && hdc) ?
      ::CreateDIBSection(hdcWin, &bi, DIB_RGB_COLORS, &bits, NULL, 0) : NULL;
  if (!bmp || !bits)
  {
    if (bmp)
      ::DeleteObject(bmp);
    if (hdc)
      ::DeleteDC(hdc);
    ::EndPaint(hwnd, &ps);
    return;
  }
  const HGDIOBJ oldBmp = ::SelectObject(hdc, bmp);

  const double r = (double)h / 2;
  const double fillX1 = shown > 0 ? 2 * r + ((double)w - 2 * r) * shown : 0;

  // sheen: soft highlight that moves from left to right along the filled part
  const double band = (double)(h * 3 > 48 ? h * 3 : 48);
  double sheenX = -1e9;
  if (anim && shown > 0)
  {
    const DWORD phase = (::GetTickCount() - anim->StartTick) % k_Progress_SheenPeriod;
    sheenX = -band + (fillX1 + 2 * band) * ((double)phase / (double)k_Progress_SheenPeriod);
  }

  const COLORREF back = k_Color_Back;
  UInt32 *p = (UInt32 *)bits;
  for (int y = 0; y < h; y++)
  {
    const double dy = (double)y + 0.5 - r;
    const double ty = h > 1 ? (double)y / (double)(h - 1) : 0;
    const double fr = Mix(Chan(k_Color_ProgressTop, 0), Chan(k_Color_ProgressBottom, 0), ty);
    const double fg = Mix(Chan(k_Color_ProgressTop, 8), Chan(k_Color_ProgressBottom, 8), ty);
    const double fb = Mix(Chan(k_Color_ProgressTop, 16), Chan(k_Color_ProgressBottom, 16), ty);
    for (int x = 0; x < w; x++)
    {
      const double px = (double)x + 0.5;
      const double cT = CapsuleCoverage(px, dy, 0, (double)w, r);
      double cr = Mix(Chan(back, 0), Chan(k_Color_ProgressTrack, 0), cT);
      double cg = Mix(Chan(back, 8), Chan(k_Color_ProgressTrack, 8), cT);
      double cb = Mix(Chan(back, 16), Chan(k_Color_ProgressTrack, 16), cT);
      if (fillX1 > 0)
      {
        const double cF = CapsuleCoverage(px, dy, 0, fillX1, r) * cT;
        if (cF > 0)
        {
          const double t = (px - sheenX) / band;
          double s = 1 - t * t;
          s = s > 0 ? s * s * 0.3 : 0;
          cr = Mix(cr, Mix(fr, 255, s), cF);
          cg = Mix(cg, Mix(fg, 255, s), cF);
          cb = Mix(cb, Mix(fb, 255, s), cF);
        }
      }
      *p++ = ((UInt32)(cr + 0.5) << 16) | ((UInt32)(cg + 0.5) << 8) | (UInt32)(cb + 0.5);
    }
  }

  {
    unsigned percent = (unsigned)(shown * 100);
    if (percent > 100)
      percent = 100;
    wchar_t text[8];
    unsigned pos = 0;
    if (percent >= 100) text[pos++] = (wchar_t)('0' + percent / 100);
    if (percent >= 10)  text[pos++] = (wchar_t)('0' + (percent / 10) % 10);
    text[pos++] = (wchar_t)('0' + percent % 10);
    text[pos++] = '%';
    text[pos] = 0;

    const HFONT font = (HFONT)::SendMessageW(::GetParent(hwnd), WM_GETFONT, 0, 0);
    const HGDIOBJ oldFont = font ? ::SelectObject(hdc, font) : NULL;
    ::SetBkMode(hdc, TRANSPARENT);
    const UINT format = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    RECT shadow = rc;
    ::OffsetRect(&shadow, 0, 1);
    ::SetTextColor(hdc, RGB(0x10, 0x10, 0x10));
    ::DrawTextW(hdc, text, -1, &shadow, format);
    ::SetTextColor(hdc, RGB(0xff, 0xff, 0xff));
    ::DrawTextW(hdc, text, -1, &rc, format);
    if (oldFont)
      ::SelectObject(hdc, oldFont);
  }

  ::BitBlt(hdcWin, 0, 0, w, h, hdc, 0, 0, SRCCOPY);
  ::SelectObject(hdc, oldBmp);
  ::DeleteObject(bmp);
  ::DeleteDC(hdc);
  ::EndPaint(hwnd, &ps);
}


static void ApplyToControl(HWND hwnd)
{
  wchar_t name[64];
  if (!GetClass(hwnd, name, Z7_ARRAY_SIZE(name)))
    return;
  const bool dark = g_Enabled;
  const LONG_PTR style = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
  const LPCWSTR themeExplorer = dark ? L"DarkMode_Explorer" : NULL;

  if (IsClass(name, L"Button"))
  {
    const unsigned type = (unsigned)(style & BS_TYPEMASK);
    if (type == BS_OWNERDRAW)
      return;
    if (type == BS_GROUPBOX)
    {
      if (dark)
        Subclass(hwnd, k_Kind_GroupBox);
    }
    else
      SetTheme(hwnd, themeExplorer);
  }
  else if (IsClass(name, L"Edit"))
    SetTheme(hwnd, dark ? ((style & ES_MULTILINE) ? L"DarkMode_Explorer" : L"DarkMode_CFD") : NULL);
  else if (IsClass(name, L"ComboBox"))
  {
    SetTheme(hwnd, dark ? L"DarkMode_CFD" : NULL);
    COMBOBOXINFO info;
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    #ifndef CB_GETCOMBOBOXINFO
    #define CB_GETCOMBOBOXINFO  0x0164
    #endif
    if (::SendMessageW(hwnd, CB_GETCOMBOBOXINFO, 0, (LPARAM)&info))
      SetTheme(info.hwndList, themeExplorer);
  }
  else if (IsClass(name, WC_COMBOBOXEXW))
  {
    if (dark)
      Subclass(hwnd, k_Kind_CtlColor);
  }
  else if (IsClass(name, L"ListBox")
      || IsClass(name, L"ScrollBar")
      || IsClass(name, L"tooltips_class32"))
    SetTheme(hwnd, themeExplorer);
  else if (IsClass(name, WC_LISTVIEWW))
  {
    SetTheme(hwnd, themeExplorer);
    const COLORREF back = dark ? k_Color_View : ::GetSysColor(COLOR_WINDOW);
    ::SendMessageW(hwnd, LVM_SETBKCOLOR, 0, (LPARAM)back);
    ::SendMessageW(hwnd, LVM_SETTEXTBKCOLOR, 0, (LPARAM)back);
    ::SendMessageW(hwnd, LVM_SETTEXTCOLOR, 0, (LPARAM)(dark ? k_Color_Text : ::GetSysColor(COLOR_WINDOWTEXT)));
    SetTheme((HWND)::SendMessageW(hwnd, LVM_GETTOOLTIPS, 0, 0), themeExplorer);
    if (dark)
      Subclass(hwnd, k_Kind_ListView);
  }
  else if (IsClass(name, WC_HEADERW))
    SetTheme(hwnd, dark ? L"DarkMode_ItemsView" : NULL);
  else if (IsClass(name, WC_TREEVIEWW))
  {
    SetTheme(hwnd, themeExplorer);
    ::SendMessageW(hwnd, TVM_SETBKCOLOR, 0, (LPARAM)(dark ? k_Color_View : (COLORREF)(Int32)-1));
    ::SendMessageW(hwnd, TVM_SETTEXTCOLOR, 0, (LPARAM)(dark ? k_Color_Text : (COLORREF)(Int32)-1));
  }
  else if (IsClass(name, TOOLBARCLASSNAMEW))
  {
    if (dark)
      Subclass(hwnd, k_Kind_ToolBar);
    SetTheme(hwnd, themeExplorer);
    SetTheme((HWND)::SendMessageW(hwnd, TB_GETTOOLTIPS, 0, 0), themeExplorer);
  }
  else if (IsClass(name, REBARCLASSNAMEW))
  {
    if (dark)
      Subclass(hwnd, k_Kind_ReBar);
  }
  else if (IsClass(name, STATUSCLASSNAMEW))
  {
    if (dark)
      Subclass(hwnd, k_Kind_StatusBar);
  }
  else if (IsClass(name, WC_TABCONTROLW))
  {
    if (dark)
      Subclass(hwnd, k_Kind_Tab);
    SetTheme((HWND)::SendMessageW(hwnd, TCM_GETTOOLTIPS, 0, 0), themeExplorer);
  }
  else if (IsClass(name, PROGRESS_CLASSW))
  {
    // in dark mode we draw our own animated pill-style progress bar
    if (dark)
    {
      Subclass(hwnd, k_Kind_Progress);
      Progress_Start(hwnd);
    }
  }
  else if (IsClass(name, L"#32770"))
  {
    if (style & WS_CHILD)
    {
      // property page: we disable white tab texture background in dark mode
      const HWND parent = ::GetParent(hwnd);
      if (parent && IsPropertySheet(parent) && f_EnableThemeDialogTexture)
        f_EnableThemeDialogTexture(hwnd, dark ? k_ETDT_DISABLE : k_ETDT_ENABLETAB);
    }
    else if (dark && IsPropertySheet(hwnd))
      Subclass(hwnd, k_Kind_PropSheet);
  }
  ::InvalidateRect(hwnd, NULL, TRUE);
}


static BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM /* lParam */)
{
  ApplyToControl(hwnd);
  return TRUE;
}

/* ComboBoxEx doesn't draw the image of edit item after theme change of its child controls.
   Setting of same image list again fixes it. */
static BOOL CALLBACK EnumChildProc_ComboBoxEx(HWND hwnd, LPARAM /* lParam */)
{
  wchar_t name[32];
  if (GetClass(hwnd, name, Z7_ARRAY_SIZE(name)) && IsClass(name, WC_COMBOBOXEXW))
  {
    const LRESULT imageList = ::SendMessageW(hwnd, CBEM_GETIMAGELIST, 0, 0);
    if (imageList)
      ::SendMessageW(hwnd, CBEM_SETIMAGELIST, 0, imageList);
  }
  return TRUE;
}

void ApplyToWindowTree(HWND hwnd)
{
  if (!g_Supported || !hwnd)
    return;
  const bool dark = g_Enabled;
  const bool wasApplied = (::GetPropW(hwnd, k_Prop_Applied) != NULL);
  if (!dark && !wasApplied)
    return;
  if (dark)
    ::SetPropW(hwnd, k_Prop_Applied, (HANDLE)1);
  else
    ::RemovePropW(hwnd, k_Prop_Applied);

  const LONG_PTR style = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
  if ((style & WS_CHILD) == 0 && (style & WS_CAPTION) == WS_CAPTION)
  {
    f_AllowDarkModeForWindow(hwnd, dark ? TRUE : FALSE);
    SetTitleBar(hwnd, dark);
  }
  ApplyToControl(hwnd);
  ::EnumChildWindows(hwnd, EnumChildProc, 0);
  ::EnumChildWindows(hwnd, EnumChildProc_ComboBoxEx, 0);
}

static void Redraw(HWND hwnd)
{
  const LONG_PTR style = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
  if ((style & WS_CHILD) == 0)
  {
    // it's required to update title bar in some Windows versions
    ::SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    if (::GetMenu(hwnd))
      ::DrawMenuBar(hwnd);
  }
  ::RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

static BOOL CALLBACK EnumThreadWndProc(HWND hwnd, LPARAM /* lParam */)
{
  const bool wasApplied = (::GetPropW(hwnd, k_Prop_Applied) != NULL);
  if (wasApplied == g_Enabled)
    return TRUE;
  ApplyToWindowTree(hwnd);
  if (::IsWindowVisible(hwnd))
    Redraw(hwnd);
  return TRUE;
}

void ApplyToThreadWindows()
{
  if (g_Supported)
    ::EnumThreadWindows(::GetCurrentThreadId(), EnumThreadWndProc, 0);
}

static bool IsColorSchemeChange(WPARAM wParam, LPARAM lParam)
{
  if (wParam == SPI_SETHIGHCONTRAST)
    return true;
  return lParam && ::lstrcmpiW((LPCWSTR)lParam, L"ImmersiveColorSet") == 0;
}

void OnSettingChange(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
  if (!g_Supported || !IsColorSchemeChange(wParam, lParam))
    return;
  UpdateState();
  const bool wasApplied = (::GetPropW(hwnd, k_Prop_Applied) != NULL);
  if (wasApplied == g_Enabled)
    return;
  ApplyToWindowTree(hwnd);
  Redraw(hwnd);
}


// ---------- message handlers ----------

bool OnCtlColor(UINT message, WPARAM wParam, LRESULT &result)
{
  if (!g_Enabled)
    return false;
  const HDC hdc = (HDC)wParam;
  switch (message)
  {
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
      ::SetTextColor(hdc, k_Color_Text);
      ::SetBkColor(hdc, k_Color_Control);
      result = (LRESULT)GetBrush(k_Brush_Control);
      return true;
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
      ::SetTextColor(hdc, k_Color_Text);
      ::SetBkColor(hdc, k_Color_Back);
      result = (LRESULT)GetBrush(k_Brush_Back);
      return true;
    default: break;
  }
  return false;
}


static int GetTextLen(HWND hwnd, wchar_t *buf, int size)
{
  buf[0] = 0;
  const int len = ::GetWindowTextW(hwnd, buf, size);
  return len < 0 ? 0 : len;
}


static bool OnButtonCustomDraw(NMCUSTOMDRAW *cd, LRESULT &result)
{
  const HWND hwnd = cd->hdr.hwndFrom;
  const LONG_PTR style = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
  if (style & BS_PUSHLIKE)
    return false;
  const unsigned type = (unsigned)(style & BS_TYPEMASK);
  int part;
  if (type == BS_CHECKBOX || type == BS_AUTOCHECKBOX
      || type == BS_3STATE || type == BS_AUTO3STATE)
    part = k_BP_CHECKBOX;
  else if (type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON)
    part = k_BP_RADIOBUTTON;
  else
    return false;
  if (cd->dwDrawStage != CDDS_PREPAINT)
    return false;

  const HDC hdc = cd->hdc;
  const RECT rc = cd->rc;
  const LRESULT check = ::SendMessageW(hwnd, BM_GETCHECK, 0, 0);
  const bool disabled = (cd->uItemState & CDIS_DISABLED) != 0 || !::IsWindowEnabled(hwnd);

  // CBS_UNCHECKEDNORMAL / RBS_UNCHECKEDNORMAL = 1, (CHECKED = 5), (MIXED = 9)
  int state = 1;
  if (check == BST_CHECKED)
    state = 5;
  else if (check == BST_INDETERMINATE && part == k_BP_CHECKBOX)
    state = 9;
  if (disabled)
    state += 3;
  else if (cd->uItemState & CDIS_SELECTED)
    state += 2;
  else if (cd->uItemState & CDIS_HOT)
    state += 1;

  ::FillRect(hdc, &rc, GetBrush(k_Brush_Back));

  SIZE size;
  size.cx = size.cy = ::MulDiv(13, ::GetDeviceCaps(hdc, LOGPIXELSY), 96);
  const HANDLE theme = f_OpenThemeData(hwnd, L"Button");
  if (theme)
    f_GetThemePartSize(theme, hdc, part, state, NULL, k_TS_DRAW, &size);
  RECT glyph;
  glyph.left = rc.left;
  glyph.right = rc.left + size.cx;
  glyph.top = rc.top + (rc.bottom - rc.top - size.cy) / 2;
  glyph.bottom = glyph.top + size.cy;
  if (theme)
  {
    f_DrawThemeBackground(theme, hdc, part, state, &glyph, NULL);
    f_CloseThemeData(theme);
  }
  else
    ::DrawFrameControl(hdc, &glyph, DFC_BUTTON,
        (part == k_BP_CHECKBOX ? DFCS_BUTTONCHECK : DFCS_BUTTONRADIO)
        | (check == BST_CHECKED ? DFCS_CHECKED : 0)
        | (disabled ? DFCS_INACTIVE : 0));

  wchar_t text[512];
  const int len = GetTextLen(hwnd, text, Z7_ARRAY_SIZE(text));
  if (len > 0)
  {
    const HFONT font = (HFONT)::SendMessageW(hwnd, WM_GETFONT, 0, 0);
    const HGDIOBJ oldFont = font ? ::SelectObject(hdc, font) : NULL;
    RECT tr = rc;
    tr.left = glyph.right + (size.cx + 2) / 4;
    UINT flags = DT_LEFT;
    if (IsAccelHidden(hwnd))
      flags |= DT_HIDEPREFIX;
    RECT calc = tr;
    if (style & BS_MULTILINE)
      flags |= DT_WORDBREAK;
    else
      flags |= DT_SINGLELINE;
    ::DrawTextW(hdc, text, len, &calc, flags | DT_CALCRECT);
    {
      const int h = calc.bottom - calc.top;
      tr.top += (tr.bottom - tr.top - h) / 2;
      tr.bottom = tr.top + h;
    }
    ::SetBkMode(hdc, TRANSPARENT);
    ::SetTextColor(hdc, disabled ? k_Color_TextDisabled : k_Color_Text);
    ::DrawTextW(hdc, text, len, &tr, flags);
    if ((cd->uItemState & CDIS_FOCUS) && !IsFocusHidden(hwnd))
    {
      RECT fr = tr;
      fr.right = fr.left + (calc.right - calc.left);
      ::InflateRect(&fr, 1, 0);
      ::SetTextColor(hdc, k_Color_Text);
      ::DrawFocusRect(hdc, &fr);
    }
    if (oldFont)
      ::SelectObject(hdc, oldFont);
  }
  result = CDRF_SKIPDEFAULT;
  return true;
}


static bool OnToolbarCustomDraw(NMTBCUSTOMDRAW *cd, LRESULT &result)
{
  switch (cd->nmcd.dwDrawStage)
  {
    case CDDS_PREPAINT:
      result = CDRF_NOTIFYITEMDRAW;
      return true;
    case CDDS_ITEMPREPAINT:
      cd->clrText = k_Color_Text;
      cd->clrTextHighlight = k_Color_Text;
      cd->clrHighlightHotTrack = k_Color_Hot;
      result = TBCDRF_USECDCOLORS | TBCDRF_HILITEHOTTRACK;
      return true;
    default: break;
  }
  return false;
}


bool OnNotify(const NMHDR *header, LRESULT &result)
{
  if (!g_Enabled || !header || header->code != (UINT)NM_CUSTOMDRAW)
    return false;
  wchar_t name[32];
  if (!GetClass(header->hwndFrom, name, Z7_ARRAY_SIZE(name)))
    return false;
  if (IsClass(name, L"Button"))
    return OnButtonCustomDraw((NMCUSTOMDRAW *)(void *)header, result);
  if (IsClass(name, TOOLBARCLASSNAMEW))
    return OnToolbarCustomDraw((NMTBCUSTOMDRAW *)(void *)header, result);
  return false;
}


static void DrawText_Simple(HDC hdc, HWND hwnd, const wchar_t *text, RECT &r, UINT flags, COLORREF color)
{
  const HFONT font = (HFONT)::SendMessageW(hwnd, WM_GETFONT, 0, 0);
  const HGDIOBJ oldFont = font ? ::SelectObject(hdc, font) : NULL;
  ::SetBkMode(hdc, TRANSPARENT);
  ::SetTextColor(hdc, color);
  ::DrawTextW(hdc, text, -1, &r, flags);
  if (oldFont)
    ::SelectObject(hdc, oldFont);
}


static void PaintGroupBox(HWND hwnd)
{
  PAINTSTRUCT ps;
  const HDC hdc = ::BeginPaint(hwnd, &ps);
  if (!hdc)
    return;
  RECT rc;
  ::GetClientRect(hwnd, &rc);

  wchar_t text[256];
  const int len = GetTextLen(hwnd, text, Z7_ARRAY_SIZE(text));

  const HFONT font = (HFONT)::SendMessageW(hwnd, WM_GETFONT, 0, 0);
  const HGDIOBJ oldFont = font ? ::SelectObject(hdc, font) : NULL;
  TEXTMETRICW tm;
  if (!::GetTextMetricsW(hdc, &tm))
  {
    tm.tmHeight = 16;
    tm.tmAveCharWidth = 7;
  }
  RECT frame = rc;
  frame.top += tm.tmHeight / 2;
  ::FrameRect(hdc, &frame, GetBrush(k_Brush_Edge));

  if (len > 0)
  {
    UINT flags = DT_LEFT | DT_SINGLELINE;
    if (IsAccelHidden(hwnd))
      flags |= DT_HIDEPREFIX;
    RECT tr;
    tr.left = rc.left + tm.tmAveCharWidth;
    tr.top = rc.top;
    tr.right = rc.right - tm.tmAveCharWidth;
    tr.bottom = rc.top + tm.tmHeight;
    RECT calc = tr;
    ::DrawTextW(hdc, text, len, &calc, flags | DT_CALCRECT);
    if (calc.right > tr.right)
      calc.right = tr.right;
    RECT back = calc;
    back.left -= 2;
    back.right += 2;
    ::FillRect(hdc, &back, GetBrush(k_Brush_Back));
    ::SetBkMode(hdc, TRANSPARENT);
    ::SetTextColor(hdc, ::IsWindowEnabled(hwnd) ? k_Color_Text : k_Color_TextDisabled);
    ::DrawTextW(hdc, text, len, &calc, flags | DT_END_ELLIPSIS);
  }
  if (oldFont)
    ::SelectObject(hdc, oldFont);
  ::EndPaint(hwnd, &ps);
}


static void PaintStatusBar(HWND hwnd)
{
  PAINTSTRUCT ps;
  const HDC hdc = ::BeginPaint(hwnd, &ps);
  if (!hdc)
    return;
  RECT rc;
  ::GetClientRect(hwnd, &rc);
  ::FillRect(hdc, &rc, GetBrush(k_Brush_Back));
  {
    RECT line = rc;
    line.bottom = line.top + 1;
    ::FillRect(hdc, &line, GetBrush(k_Brush_Edge));
  }
  const bool simple = (::SendMessageW(hwnd, SB_ISSIMPLE, 0, 0) != 0);
  const int numParts = simple ? 1 : (int)::SendMessageW(hwnd, SB_GETPARTS, 0, 0);
  for (int i = 0; i < numParts; i++)
  {
    const WPARAM index = simple ? (WPARAM)SB_SIMPLEID : (WPARAM)(unsigned)i;
    RECT r;
    if (simple)
      r = rc;
    else if (!::SendMessageW(hwnd, SB_GETRECT, index, (LPARAM)&r))
      continue;
    if (!simple && i + 1 < numParts)
    {
      RECT sep = r;
      sep.left = sep.right - 1;
      sep.top += 3;
      sep.bottom -= 2;
      ::FillRect(hdc, &sep, GetBrush(k_Brush_Edge));
    }
    const LRESULT lenRes = ::SendMessageW(hwnd, SB_GETTEXTLENGTHW, index, 0);
    if (HIWORD(lenRes) & SBT_OWNERDRAW)
      continue;
    wchar_t text[512];
    if (LOWORD(lenRes) >= Z7_ARRAY_SIZE(text))
      continue;
    text[0] = 0;
    ::SendMessageW(hwnd, SB_GETTEXTW, index, (LPARAM)text);
    r.left += 4;
    r.right -= 2;
    DrawText_Simple(hdc, hwnd, text, r,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS, k_Color_Text);
  }
  ::EndPaint(hwnd, &ps);
}


static void PaintTab(HWND hwnd)
{
  PAINTSTRUCT ps;
  const HDC hdc = ::BeginPaint(hwnd, &ps);
  if (!hdc)
    return;
  RECT rc;
  ::GetClientRect(hwnd, &rc);
  const int numItems = (int)::SendMessageW(hwnd, TCM_GETITEMCOUNT, 0, 0);
  const int sel = (int)::SendMessageW(hwnd, TCM_GETCURSEL, 0, 0);

  RECT frame = rc;
  if (numItems > 0)
  {
    RECT r;
    if (::SendMessageW(hwnd, TCM_GETITEMRECT, 0, (LPARAM)&r))
      frame.top = r.bottom;
  }
  // display area is covered by property page. We don't paint it.
  RECT disp = rc;
  ::SendMessageW(hwnd, TCM_ADJUSTRECT, FALSE, (LPARAM)&disp);
  const int saved = ::SaveDC(hdc);
  if (disp.right > disp.left && disp.bottom > disp.top)
    ::ExcludeClipRect(hdc, disp.left, disp.top, disp.right, disp.bottom);
  ::FillRect(hdc, &rc, GetBrush(k_Brush_Back));
  ::FrameRect(hdc, &frame, GetBrush(k_Brush_Edge));

  for (int i = 0; i < numItems; i++)
  {
    RECT r;
    if (!::SendMessageW(hwnd, TCM_GETITEMRECT, (WPARAM)(unsigned)i, (LPARAM)&r))
      continue;
    const bool isSel = (i == sel);
    if (isSel)
    {
      r.left -= 2;
      r.right += 2;
      r.top -= 2;
      r.bottom += 1;
    }
    ::FillRect(hdc, &r, GetBrush(isSel ? k_Brush_Control : k_Brush_Back));
    {
      RECT r2 = r;
      if (isSel)
        r2.bottom++;
      ::FrameRect(hdc, &r2, GetBrush(k_Brush_Edge));
      if (isSel)
      {
        RECT b = r;
        b.left++;
        b.right--;
        b.top = r.bottom - 1;
        b.bottom = r.bottom + 1;
        ::FillRect(hdc, &b, GetBrush(k_Brush_Control));
      }
    }
    wchar_t text[256];
    text[0] = 0;
    TCITEMW item;
    memset(&item, 0, sizeof(item));
    item.mask = TCIF_TEXT;
    item.pszText = text;
    item.cchTextMax = Z7_ARRAY_SIZE(text);
    if (::SendMessageW(hwnd, TCM_GETITEMW, (WPARAM)(unsigned)i, (LPARAM)&item))
    {
      UINT flags = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
      if (IsAccelHidden(hwnd))
        flags |= DT_HIDEPREFIX;
      DrawText_Simple(hdc, hwnd, text, r, flags,
          ::IsWindowEnabled(hwnd) ? k_Color_Text : k_Color_TextDisabled);
    }
  }
  ::RestoreDC(hdc, saved);
  ::EndPaint(hwnd, &ps);
}


// it fills non-client area of child window with background color
static void PaintNonClient(HWND hwnd)
{
  RECT wr;
  if (!::GetWindowRect(hwnd, &wr))
    return;
  RECT cr;
  ::GetClientRect(hwnd, &cr);
  ::MapWindowPoints(hwnd, NULL, (POINT *)(void *)&cr, 2);
  ::OffsetRect(&cr, -wr.left, -wr.top);
  ::OffsetRect(&wr, -wr.left, -wr.top);
  const HDC hdc = ::GetWindowDC(hwnd);
  if (!hdc)
    return;
  ::ExcludeClipRect(hdc, cr.left, cr.top, cr.right, cr.bottom);
  ::FillRect(hdc, &wr, GetBrush(k_Brush_Back));
  ::ReleaseDC(hwnd, hdc);
}


static bool OnListViewMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT &result)
{
  if (message == WM_NOTIFY)
  {
    NMCUSTOMDRAW *cd = (NMCUSTOMDRAW *)lParam;
    if (cd->hdr.code == (UINT)NM_CUSTOMDRAW
        && cd->hdr.hwndFrom == (HWND)::SendMessageW(hwnd, LVM_GETHEADER, 0, 0))
    {
      if (cd->dwDrawStage == CDDS_PREPAINT)
      {
        result = CDRF_NOTIFYITEMDRAW;
        return true;
      }
      if (cd->dwDrawStage == CDDS_ITEMPREPAINT)
      {
        ::SetTextColor(cd->hdc, k_Color_Text);
        result = CDRF_DODEFAULT;
        return true;
      }
    }
    return false;
  }
  // label editing control
  return OnCtlColor(message, wParam, result);
}


static bool Subclass_OnMessage(unsigned kind, HWND hwnd, WNDPROC proc,
    UINT message, WPARAM wParam, LPARAM lParam, LRESULT &result)
{
  if (message == WM_SETTINGCHANGE && kind == k_Kind_PropSheet)
  {
    result = ::CallWindowProcW(proc, hwnd, message, wParam, lParam);
    OnSettingChange(hwnd, wParam, lParam);
    return true;
  }
  if (kind == k_Kind_Progress && message == WM_TIMER && wParam == k_Progress_TimerId)
  {
    if (g_Enabled)
      Progress_OnTimer(hwnd);
    else
    {
      // dark mode was disabled: return to standard drawing
      ::KillTimer(hwnd, k_Progress_TimerId);
      ::SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
          SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
      ::InvalidateRect(hwnd, NULL, TRUE);
    }
    result = 0;
    return true;
  }
  if (!g_Enabled)
    return false;
  switch (kind)
  {
    case k_Kind_Progress:
      switch (message)
      {
        case WM_NCPAINT:    PaintNonClient(hwnd); result = 0; return true;
        case WM_ERASEBKGND: result = 1; return true;
        case WM_PAINT:      Progress_Paint(hwnd); result = 0; return true;
        default: break;
      }
      return false;

    case k_Kind_CtlColor:
      return OnCtlColor(message, wParam, result);

    case k_Kind_PropSheet:
      if (message == WM_NOTIFY)
        return OnNotify((const NMHDR *)lParam, result);
      return OnCtlColor(message, wParam, result);

    case k_Kind_ReBar:
      if (message == WM_ERASEBKGND)
      {
        RECT rc;
        ::GetClientRect(hwnd, &rc);
        ::FillRect((HDC)wParam, &rc, GetBrush(k_Brush_Back));
        result = 1;
        return true;
      }
      if (message == WM_NOTIFY)
        return OnNotify((const NMHDR *)lParam, result);
      return OnCtlColor(message, wParam, result);

    case k_Kind_ToolBar:
      // toolbar without CCS_NODIVIDER draws light divider line in non-client area
      if (message == WM_NCPAINT)
      {
        PaintNonClient(hwnd);
        result = 0;
        return true;
      }
      return false;

    case k_Kind_GroupBox:
      if (message == WM_PAINT)
      {
        PaintGroupBox(hwnd);
        result = 0;
        return true;
      }
      if (message == WM_SETTEXT || message == WM_ENABLE)
      {
        result = ::CallWindowProcW(proc, hwnd, message, wParam, lParam);
        ::InvalidateRect(hwnd, NULL, TRUE);
        return true;
      }
      return false;

    case k_Kind_ListView:
      return OnListViewMessage(hwnd, message, wParam, lParam, result);

    case k_Kind_StatusBar:
    case k_Kind_Tab:
      if (message == WM_ERASEBKGND)
      {
        result = 1;
        return true;
      }
      if (message == WM_PAINT)
      {
        if (kind == k_Kind_Tab)
          PaintTab(hwnd);
        else
          PaintStatusBar(hwnd);
        result = 0;
        return true;
      }
      return false;

    default: break;
  }
  return false;
}


static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  const WNDPROC proc = Z7_CAST_FUNC(WNDPROC, ::GetPropW(hwnd, k_Prop_Proc));
  if (!proc)
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
  if (message == WM_NCDESTROY)
  {
    if ((unsigned)(UINT_PTR)::GetPropW(hwnd, k_Prop_Kind) == k_Kind_Progress)
    {
      ::KillTimer(hwnd, k_Progress_TimerId);
      Progress_Free(hwnd);
    }
    ::RemovePropW(hwnd, k_Prop_Proc);
    ::RemovePropW(hwnd, k_Prop_Kind);
    ::RemovePropW(hwnd, k_Prop_Applied);
    ::SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)proc);
    return ::CallWindowProcW(proc, hwnd, message, wParam, lParam);
  }
  const unsigned kind = (unsigned)(UINT_PTR)::GetPropW(hwnd, k_Prop_Kind);
  LRESULT result = 0;
  if (Subclass_OnMessage(kind, hwnd, proc, message, wParam, lParam, result))
    return result;
  return ::CallWindowProcW(proc, hwnd, message, wParam, lParam);
}


// ---------- dialogs ----------

static bool DialogHook(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, INT_PTR &result)
{
  switch (message)
  {
    case WM_INITDIALOG:
    {
      if (!g_Enabled)
        return false;
      ApplyToWindowTree(hwnd);
      const LONG_PTR style = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
      if (style & WS_CHILD)
      {
        const HWND parent = ::GetParent(hwnd);
        if (parent && IsPropertySheet(parent))
          ApplyToWindowTree(parent);
      }
      return false;
    }

    case WM_SETTINGCHANGE:
    {
      const LONG_PTR style = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
      if ((style & WS_CHILD) == 0)
        OnSettingChange(hwnd, wParam, lParam);
      return false;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
      LRESULT res = 0;
      if (!OnCtlColor(message, wParam, res))
        return false;
      result = (INT_PTR)res;
      return true;
    }

    case WM_NOTIFY:
    {
      LRESULT res = 0;
      if (!OnNotify((const NMHDR *)lParam, res))
        return false;
      ::SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, (LONG_PTR)res);
      result = TRUE;
      return true;
    }

    default: break;
  }
  return false;
}


// ---------- main window with menu bar ----------

static void DrawMenuBarItem(CUahDrawMenuItem *p)
{
  wchar_t text[256];
  text[0] = 0;
  MENUITEMINFOW mii;
  memset(&mii, 0, sizeof(mii));
  mii.cbSize = sizeof(mii);
  mii.fMask = MIIM_STRING;
  mii.dwTypeData = text;
  mii.cch = Z7_ARRAY_SIZE(text) - 1;
  ::GetMenuItemInfoW(p->um.hmenu, (UINT)p->umi.iPosition, TRUE, &mii);

  const UINT state = p->dis.itemState;
  unsigned brush = k_Brush_Back;
  if (state & ODS_SELECTED)
    brush = k_Brush_Pushed;
  else if (state & ODS_HOTLIGHT)
    brush = k_Brush_Hot;
  ::FillRect(p->dis.hDC, &p->dis.rcItem, GetBrush(brush));

  UINT flags = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
  if (state & ODS_NOACCEL)
    flags |= DT_HIDEPREFIX;
  ::SetBkMode(p->dis.hDC, TRANSPARENT);
  ::SetTextColor(p->dis.hDC,
      (state & (ODS_INACTIVE | ODS_GRAYED | ODS_DISABLED)) ?
        k_Color_TextDisabled : k_Color_Text);
  RECT r = p->dis.rcItem;
  ::DrawTextW(p->dis.hDC, text, -1, &r, flags);
}

// it paints over the light line between menu bar and client area
static void DrawMenuBarBottomLine(HWND hwnd)
{
  if (!::GetMenu(hwnd))
    return;
  RECT rc;
  ::GetClientRect(hwnd, &rc);
  ::MapWindowPoints(hwnd, NULL, (POINT *)(void *)&rc, 2);
  RECT wr;
  ::GetWindowRect(hwnd, &wr);
  ::OffsetRect(&rc, -wr.left, -wr.top);
  RECT line = rc;
  line.bottom = line.top;
  line.top--;
  const HDC hdc = ::GetWindowDC(hwnd);
  if (hdc)
  {
    ::FillRect(hdc, &line, GetBrush(k_Brush_Back));
    ::ReleaseDC(hwnd, hdc);
  }
}

bool OnMainWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT &result)
{
  if (message == WM_SETTINGCHANGE)
  {
    OnSettingChange(hwnd, wParam, lParam);
    return false;
  }
  if (!g_Enabled)
    return false;
  switch (message)
  {
    case k_WM_UAHDRAWMENU:
    {
      const CUahMenu *um = (const CUahMenu *)lParam;
      MENUBARINFO mbi;
      memset(&mbi, 0, sizeof(mbi));
      mbi.cbSize = sizeof(mbi);
      if (f_GetMenuBarInfo && f_GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi))
      {
        RECT wr;
        ::GetWindowRect(hwnd, &wr);
        RECT r = mbi.rcBar;
        ::OffsetRect(&r, -wr.left, -wr.top);
        ::FillRect(um->hdc, &r, GetBrush(k_Brush_Back));
      }
      result = 0;
      return true;
    }
    case k_WM_UAHDRAWMENUITEM:
      DrawMenuBarItem((CUahDrawMenuItem *)lParam);
      result = 0;
      return true;
    case WM_NCPAINT:
    case WM_NCACTIVATE:
      result = ::DefWindowProcW(hwnd, message, wParam, lParam);
      DrawMenuBarBottomLine(hwnd);
      return true;
    case WM_ERASEBKGND:
    {
      RECT rc;
      ::GetClientRect(hwnd, &rc);
      ::FillRect((HDC)wParam, &rc, GetBrush(k_Brush_Back));
      result = 1;
      return true;
    }
    case WM_NOTIFY:
      return OnNotify((const NMHDR *)lParam, result);
    default:
      return OnCtlColor(message, wParam, result);
  }
}

}}

#endif
