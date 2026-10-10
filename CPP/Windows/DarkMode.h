// Windows/DarkMode.h

#ifndef ZIP7_INC_WINDOWS_DARK_MODE_H
#define ZIP7_INC_WINDOWS_DARK_MODE_H

#include "../Common/MyWindows.h"

#ifndef UNDER_CE

/*
  Dark mode support for Win32 controls (Windows 10 1809+ / Windows 11).
  It uses some undocumented functions from uxtheme.dll (loaded by ordinals)
  and DwmSetWindowAttribute() from dwmapi.dll.
  All functions are loaded dynamically, so the code works (as no-op) in old Windows.
  If dark mode is not active, these functions don't change any window,
  so the program looks exactly as without dark mode support.
*/

namespace NWindows {
namespace NDarkMode {

enum EMode
{
  k_Mode_System = 0, // follow Windows "app mode" setting
  k_Mode_Light  = 1,
  k_Mode_Dark   = 2
};

/* Init() must be called once at program start before creating of windows.
   It loads the functions, sets the app mode for popup menus,
   and installs the hook for all NControl::CDialog based dialogs. */
void Init(unsigned mode);

// SetMode() changes user's preference. It returns true, if dark state was changed.
bool SetMode(unsigned mode);
unsigned GetMode();

bool IsSupported(); // the OS supports dark mode
bool IsEnabled();   // dark mode is active now

// these colors and brushes are for dark mode
COLORREF GetBackColor();     // window and dialog background
COLORREF GetControlColor();  // background of edit, combobox, listbox
COLORREF GetViewColor();     // background of listview and treeview
COLORREF GetTextColor();
COLORREF GetDisabledTextColor();
HBRUSH GetBackBrush();

/* ApplyToWindowTree() sets dark (or light) state to window and all its child windows.
   If dark mode is not active and it was not applied to that window before,
   the function does nothing. */
void ApplyToWindowTree(HWND hwnd);

// it applies current state to all top-level windows of current thread and redraws them.
void ApplyToThreadWindows();

/* it processes WM_SETTINGCHANGE message for top-level window.
   If Windows color mode was changed, it updates dark state and the window. */
void OnSettingChange(HWND hwnd, WPARAM wParam, LPARAM lParam);

// for WM_CTLCOLOR* messages
bool OnCtlColor(UINT message, WPARAM wParam, LRESULT &result);

// for WM_NOTIFY: NM_CUSTOMDRAW of checkboxes, radio buttons and toolbars
bool OnNotify(const NMHDR *header, LRESULT &result);

/* for main application window that has menu bar:
   it draws menu bar, background and processes WM_SETTINGCHANGE, WM_CTLCOLOR*, WM_NOTIFY.
   If it returns true, the caller must return (result) from window procedure. */
bool OnMainWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT &result);

}}

#endif

#endif
