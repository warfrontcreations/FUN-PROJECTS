/*
 * Screen Shield (native)
 * ======================
 * A small, always-on-top solid-black box you drag over whatever part of your
 * screen you want hidden. It stays fully visible to you, but is marked with
 * Windows' SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) API so screenshot
 * and screen-recording tools that respect it (Snipping Tool, PrintScreen,
 * Xbox Game Bar, OBS's "Windows Graphics Capture" source, Teams/Zoom screen
 * share) render that region as solid black instead of what's really there.
 *
 * This is a single native Win32 executable -- no .NET, no Python, no runtime
 * to install. It only links against user32.dll / gdi32.dll / comdlg32.dll,
 * which are already part of every Windows install. Just double-click it.
 *
 * CONTROLS
 * --------
 *  - Drag anywhere on the black box to move it.
 *  - Drag the bottom-right corner to resize it.
 *  - Click "-" (top-right of the box) to collapse it to a small tab without
 *    turning off protection; click it again ("+") to restore it.
 *  - Click "x" to quit.
 *  - Right-click anywhere on the box for more options: color, opacity,
 *    click-through (mouse clicks pass through to whatever's underneath),
 *    lock position/size, stick to tab (rides along with a chosen browser
 *    tab and hides itself whenever a different tab is active), toggle
 *    capture-exclusion, collapse/restore, quit.
 *
 * IMPORTANT -- test before you rely on it:
 * Capture-exclusion behavior can vary by Windows build and by which capture
 * method a given tool uses under the hood. Requires Windows 10 build 19041
 * (version 2004) or later, or Windows 11. Before relying on this for
 * anything sensitive, take a screenshot/recording with the exact tool you
 * plan to use and confirm the shielded area actually comes out solid black.
 * It will NOT protect against someone photographing your monitor, Remote
 * Desktop/VNC viewers, or very old capture tools using legacy GDI BitBlt
 * instead of the modern Windows Graphics Capture API.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <string.h>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

#define HANDLE_H     22
#define BTN_W        26
#define GRIP_SIZE    14
#define PILL_SIZE    30
#define MIN_W        60
#define MIN_H        40

#define ID_COLOR         101
#define ID_OPACITY_100   110
#define ID_OPACITY_80    111
#define ID_OPACITY_60    112
#define ID_OPACITY_40    113
#define ID_OPACITY_20    114
#define ID_CLICKTHROUGH  120
#define ID_LOCK          121
#define ID_EXCLUDE       122
#define ID_COLLAPSE      123
#define ID_QUIT          124
#define ID_STICKWINDOW   125

#define TRACK_TIMER_ID   1
#define TRACK_TIMER_MS   50

static COLORREF g_color = RGB(0, 0, 0);
static BOOL g_locked = FALSE;
static BOOL g_clickThrough = FALSE;
static BOOL g_excluded = TRUE;
static BOOL g_collapsed = FALSE;
static RECT g_savedRect;
static HBRUSH g_bodyBrush = NULL;
static HBRUSH g_handleBrush = NULL;
static HBRUSH g_trackedHandleBrush = NULL;

/* "Stick to tab" state. Windows doesn't expose individual browser tabs to
 * other apps -- a browser is a single top-level window whose *title* is
 * kept in sync with whichever tab is currently active. So "sticking to a
 * tab" is built from two parts:
 *   1. Position: g_trackedWindow/g_trackOffset track the browser's WINDOW
 *      (see WM_TIMER) so we move/resize along with it regardless of tab.
 *   2. Visibility: g_tabKeyword is text the user expects to see in that
 *      tab's title (e.g. "Gmail"). Every timer tick we compare it against
 *      the window's current title and hide ourselves when it doesn't
 *      match (i.e. some other tab is active), showing again the moment
 *      the matching tab comes back to the front.
 * g_lastForeign is continuously updated to whatever top-level window
 * other than our own last had foreground focus, so "Stick to tab..."
 * knows which window the user meant: click the browser tab first, then
 * right-click Screen Shield. */
static HWND g_lastForeign = NULL;
static HWND g_trackedWindow = NULL;
static POINT g_trackOffset = { 0, 0 };
static BOOL g_tracking = FALSE;
static BOOL g_hiddenByTrack = FALSE;
static char g_tabKeyword[128] = "";
static HINSTANCE g_hInstance = NULL;

/* Tiny hand-rolled "type some text" dialog -- no .rc/resource compiler
 * involved. IsDialogMessage (used in the modal loop below) gives it
 * proper Enter-submits / Escape-cancels / Tab-navigation behavior even
 * though it's just a plain popup window with child controls. */
static HWND g_dlgEdit = NULL;
static BOOL g_dlgActive = FALSE;
static BOOL g_dlgResult = FALSE;

static BOOL ContainsCI(const char *haystack, const char *needle) {
    if (!needle || !*needle) return TRUE;
    size_t hn = strlen(haystack), nn = strlen(needle);
    if (nn > hn) return FALSE;
    for (size_t i = 0; i + nn <= hn; i++) {
        size_t j = 0;
        for (; j < nn; j++) {
            char a = haystack[i + j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
        }
        if (j == nn) return TRUE;
    }
    return FALSE;
}

static LRESULT CALLBACK TabDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND lbl = CreateWindowExA(0, "STATIC",
            "Text that appears in that tab's title (e.g. \"Gmail\", \"YouTube\"):",
            WS_CHILD | WS_VISIBLE, 12, 12, 336, 34, hwnd, NULL, g_hInstance, NULL);
        g_dlgEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            12, 50, 336, 24, hwnd, (HMENU)(UINT_PTR)100, g_hInstance, NULL);
        HWND ok = CreateWindowExA(0, "BUTTON", "OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            192, 86, 75, 26, hwnd, (HMENU)(UINT_PTR)IDOK, g_hInstance, NULL);
        HWND cancel = CreateWindowExA(0, "BUTTON", "Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            273, 86, 75, 26, hwnd, (HMENU)(UINT_PTR)IDCANCEL, g_hInstance, NULL);
        SendMessageA(lbl, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageA(g_dlgEdit, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageA(ok, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageA(cancel, WM_SETFONT, (WPARAM)font, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            g_dlgResult = TRUE; g_dlgActive = FALSE; DestroyWindow(hwnd);
        } else if (LOWORD(wParam) == IDCANCEL) {
            g_dlgResult = FALSE; g_dlgActive = FALSE; DestroyWindow(hwnd);
        }
        return 0;
    case WM_CLOSE:
        g_dlgResult = FALSE; g_dlgActive = FALSE; DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_dlgActive = FALSE;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

/* Modal-ish: disables `owner`, pumps its own message loop via
 * IsDialogMessage (so Enter/Escape/Tab behave like a real dialog), and
 * re-enables `owner` once the user picks OK or Cancel. */
static BOOL PromptTabKeyword(HWND owner, char *outBuf, int outSize) {
    g_dlgResult = FALSE;
    g_dlgEdit = NULL;

    RECT ownerR; GetWindowRect(owner, &ownerR);
    int w = 360, h = 150;
    int x = ownerR.left + ((ownerR.right - ownerR.left) - w) / 2;
    int y = ownerR.top + ((ownerR.bottom - ownerR.top) - h) / 2;

    EnableWindow(owner, FALSE);
    HWND dlg = CreateWindowExA(WS_EX_DLGMODALFRAME, "ScreenShieldTabDlg", "Stick to Tab",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, w, h, owner, NULL, g_hInstance, NULL);

    if (!dlg) { EnableWindow(owner, TRUE); return FALSE; }

    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    SetForegroundWindow(dlg);
    if (g_dlgEdit) SetFocus(g_dlgEdit);

    g_dlgActive = TRUE;
    MSG msg;
    while (g_dlgActive && GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);

    if (g_dlgResult && g_dlgEdit) {
        GetWindowTextA(g_dlgEdit, outBuf, outSize);
    }
    return g_dlgResult;
}

static void ToggleStickToTab(HWND hwnd) {
    if (g_tracking) {
        g_tracking = FALSE;
        g_trackedWindow = NULL;
        g_tabKeyword[0] = '\0';
        if (g_hiddenByTrack) {
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            g_hiddenByTrack = FALSE;
        }
        return;
    }

    if (!g_lastForeign || !IsWindow(g_lastForeign)) {
        MessageBoxA(hwnd,
            "Click the browser tab you want first (so it's the frontmost "
            "window), then right-click Screen Shield and choose this again.",
            "Screen Shield", MB_OK | MB_ICONINFORMATION);
        return;
    }

    char buf[128] = "";
    if (!PromptTabKeyword(hwnd, buf, sizeof(buf))) return;

    HWND target = g_lastForeign;
    RECT tr; GetWindowRect(target, &tr);
    RECT wr; GetWindowRect(hwnd, &wr);
    g_trackOffset.x = wr.left - tr.left;
    g_trackOffset.y = wr.top - tr.top;
    g_trackedWindow = target;
    strncpy(g_tabKeyword, buf, sizeof(g_tabKeyword) - 1);
    g_tabKeyword[sizeof(g_tabKeyword) - 1] = '\0';
    g_tracking = TRUE;
    g_hiddenByTrack = FALSE;
}

static void ApplyExclusion(HWND hwnd, BOOL enable) {
    DWORD affinity = enable ? (DWORD)WDA_EXCLUDEFROMCAPTURE : 0;
    BOOL ok = SetWindowDisplayAffinity(hwnd, affinity);
    if (enable && !ok) {
        g_excluded = FALSE;
        MessageBoxA(hwnd,
            "Couldn't enable capture-exclusion on this system.\n"
            "This needs Windows 10 build 19041+ (2004) or Windows 11.",
            "Screen Shield", MB_OK | MB_ICONWARNING);
    } else {
        g_excluded = enable;
    }
}

static void ApplyClickThrough(HWND hwnd, BOOL enable) {
    LONG_PTR style = GetWindowLongPtrA(hwnd, GWL_EXSTYLE);
    if (enable) {
        style |= WS_EX_TRANSPARENT;
    } else {
        style &= ~WS_EX_TRANSPARENT;
    }
    SetWindowLongPtrA(hwnd, GWL_EXSTYLE, style);
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    g_clickThrough = enable;
}

static void ApplyOpacity(HWND hwnd, int pct) {
    /* WS_EX_LAYERED is only added here, on demand, the first time the user
     * asks for anything less than fully opaque. Setting it unconditionally
     * at window-creation time (before the first paint) was found to make
     * this borderless/topmost window fail to composite on screen at all --
     * so the common case (100% opaque, the default) never touches it. */
    LONG_PTR style = GetWindowLongPtrA(hwnd, GWL_EXSTYLE);
    if (!(style & WS_EX_LAYERED)) {
        SetWindowLongPtrA(hwnd, GWL_EXSTYLE, style | WS_EX_LAYERED);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    BYTE alpha = (BYTE)((pct * 255) / 100);
    SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
}

static void SetBodyColor(HWND hwnd, COLORREF c) {
    g_color = c;
    if (g_bodyBrush) DeleteObject(g_bodyBrush);
    g_bodyBrush = CreateSolidBrush(g_color);
    InvalidateRect(hwnd, NULL, TRUE);
}

static void PickColor(HWND hwnd) {
    static COLORREF custom[16];
    CHOOSECOLORA cc;
    ZeroMemory(&cc, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hwnd;
    cc.rgbResult = g_color;
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorA(&cc)) {
        SetBodyColor(hwnd, cc.rgbResult);
    }
}

static void GetHandleRect(HWND hwnd, RECT *r) {
    GetClientRect(hwnd, r);
    r->bottom = HANDLE_H;
}

static void GetCloseBtnRect(HWND hwnd, RECT *r) {
    RECT c; GetClientRect(hwnd, &c);
    r->left = c.right - BTN_W; r->right = c.right;
    r->top = 0; r->bottom = HANDLE_H;
}

static void GetCollapseBtnRect(HWND hwnd, RECT *r) {
    RECT c; GetClientRect(hwnd, &c);
    r->left = c.right - 2 * BTN_W; r->right = c.right - BTN_W;
    r->top = 0; r->bottom = HANDLE_H;
}

static BOOL PtInRectXY(RECT *r, int x, int y) {
    POINT p = { x, y };
    return PtInRect(r, p);
}

static void ToggleCollapse(HWND hwnd) {
    if (g_collapsed) {
        SetWindowPos(hwnd, NULL, g_savedRect.left, g_savedRect.top,
            g_savedRect.right - g_savedRect.left, g_savedRect.bottom - g_savedRect.top,
            SWP_NOZORDER);
        g_collapsed = FALSE;
    } else {
        RECT wr; GetWindowRect(hwnd, &wr);
        g_savedRect = wr;
        SetWindowPos(hwnd, NULL, wr.left, wr.top, PILL_SIZE, PILL_SIZE, SWP_NOZORDER);
        g_collapsed = TRUE;
    }
    InvalidateRect(hwnd, NULL, TRUE);
}

static void ShowPopupMenu(HWND hwnd, int x, int y) {
    HMENU menu = CreatePopupMenu();
    HMENU opacityMenu = CreatePopupMenu();
    AppendMenuA(opacityMenu, MF_STRING, ID_OPACITY_100, "100%");
    AppendMenuA(opacityMenu, MF_STRING, ID_OPACITY_80, "80%");
    AppendMenuA(opacityMenu, MF_STRING, ID_OPACITY_60, "60%");
    AppendMenuA(opacityMenu, MF_STRING, ID_OPACITY_40, "40%");
    AppendMenuA(opacityMenu, MF_STRING, ID_OPACITY_20, "20%");

    AppendMenuA(menu, MF_STRING, ID_COLOR, "Change color...");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)opacityMenu, "Opacity");
    AppendMenuA(menu, MF_STRING | (g_clickThrough ? MF_CHECKED : 0), ID_CLICKTHROUGH, "Click-through");
    AppendMenuA(menu, MF_STRING | (g_locked ? MF_CHECKED : 0), ID_LOCK, "Lock position/size");
    AppendMenuA(menu, MF_STRING | (g_tracking ? MF_CHECKED : 0), ID_STICKWINDOW,
        g_tracking ? "Unstick from tab" : "Stick to tab...");
    AppendMenuA(menu, MF_STRING | (g_excluded ? MF_CHECKED : 0), ID_EXCLUDE, "Excluded from screenshots");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, ID_COLLAPSE, "Collapse/Restore");
    AppendMenuA(menu, MF_STRING, ID_QUIT, "Quit");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, x, y, 0, hwnd, NULL);
    DestroyMenu(menu);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_bodyBrush = CreateSolidBrush(g_color);
        g_handleBrush = CreateSolidBrush(RGB(40, 40, 40));
        g_trackedHandleBrush = CreateSolidBrush(RGB(30, 90, 170));
        SetTimer(hwnd, TRACK_TIMER_ID, TRACK_TIMER_MS, NULL);
        return 0;

    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        RECT client; GetClientRect(hwnd, &client);

        if (!g_locked && !g_collapsed) {
            RECT grip;
            grip.right = client.right; grip.bottom = client.bottom;
            grip.left = client.right - GRIP_SIZE; grip.top = client.bottom - GRIP_SIZE;
            if (PtInRectXY(&grip, pt.x, pt.y)) return HTBOTTOMRIGHT;
        }

        RECT closeR, collapseR;
        GetCloseBtnRect(hwnd, &closeR);
        GetCollapseBtnRect(hwnd, &collapseR);
        if (PtInRectXY(&closeR, pt.x, pt.y)) return HTCLIENT;
        if (PtInRectXY(&collapseR, pt.x, pt.y)) return HTCLIENT;

        /* While stuck to a tab, position is driven by WM_TIMER -- disable
         * manual dragging so it doesn't fight the auto-follow (resizing
         * via the grip above is still allowed; the offset is anchored to
         * the tracked window's top-left corner, so a size change alone
         * doesn't break tracking). */
        if (g_locked || g_tracking) return HTCLIENT;
        return HTCAPTION;
    }

    case WM_LBUTTONUP: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT closeR, collapseR;
        GetCloseBtnRect(hwnd, &closeR);
        GetCollapseBtnRect(hwnd, &collapseR);
        if (PtInRectXY(&closeR, pt.x, pt.y)) {
            DestroyWindow(hwnd);
        } else if (PtInRectXY(&collapseR, pt.x, pt.y)) {
            ToggleCollapse(hwnd);
        }
        return 0;
    }

    case WM_CONTEXTMENU: {
        ShowPopupMenu(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    }

    /* Most of the window reports HTCAPTION from WM_NCHITTEST (so it's
     * draggable everywhere except the buttons/grip). That means a right
     * click there arrives as a non-client message, and since this window
     * has no system menu (no WS_SYSMENU), Windows' default handling of
     * WM_NCRBUTTONUP would otherwise just silently do nothing. Show our
     * menu ourselves instead of falling through to DefWindowProcA. */
    case WM_NCRBUTTONUP: {
        ShowPopupMenu(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    }

    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case ID_COLOR: PickColor(hwnd); break;
        case ID_OPACITY_100: ApplyOpacity(hwnd, 100); break;
        case ID_OPACITY_80: ApplyOpacity(hwnd, 80); break;
        case ID_OPACITY_60: ApplyOpacity(hwnd, 60); break;
        case ID_OPACITY_40: ApplyOpacity(hwnd, 40); break;
        case ID_OPACITY_20: ApplyOpacity(hwnd, 20); break;
        case ID_CLICKTHROUGH: ApplyClickThrough(hwnd, !g_clickThrough); break;
        case ID_LOCK: g_locked = !g_locked; break;
        case ID_STICKWINDOW: ToggleStickToTab(hwnd); break;
        case ID_EXCLUDE: ApplyExclusion(hwnd, !g_excluded); break;
        case ID_COLLAPSE: ToggleCollapse(hwnd); break;
        case ID_QUIT: DestroyWindow(hwnd); break;
        }
        return 0;
    }

    case WM_TIMER: {
        if (wParam != TRACK_TIMER_ID) break;

        if (!g_tracking) {
            /* Keep a running note of whatever window (other than us) last
             * had focus, so "Stick to tab..." knows what the user meant
             * without needing a click-to-pick UI. */
            HWND fg = GetForegroundWindow();
            if (fg && fg != hwnd && IsWindow(fg) && IsWindowVisible(fg)) {
                g_lastForeign = fg;
            }
            return 0;
        }

        if (!IsWindow(g_trackedWindow)) {
            /* Tracked browser window was closed -- fall back to a normal
             * floating box rather than staying stuck (and hidden). */
            g_tracking = FALSE;
            g_trackedWindow = NULL;
            g_tabKeyword[0] = '\0';
            if (g_hiddenByTrack) {
                ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                g_hiddenByTrack = FALSE;
            }
            return 0;
        }

        if (IsIconic(g_trackedWindow)) {
            if (!g_hiddenByTrack) {
                ShowWindow(hwnd, SW_HIDE);
                g_hiddenByTrack = TRUE;
            }
            return 0;
        }

        char title[256] = "";
        GetWindowTextA(g_trackedWindow, title, sizeof(title));
        BOOL match = ContainsCI(title, g_tabKeyword);

        if (match) {
            RECT tr; GetWindowRect(g_trackedWindow, &tr);
            SetWindowPos(hwnd, NULL, tr.left + g_trackOffset.x, tr.top + g_trackOffset.y,
                0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            if (g_hiddenByTrack) {
                ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                g_hiddenByTrack = FALSE;
            }
        } else if (!g_hiddenByTrack) {
            ShowWindow(hwnd, SW_HIDE);
            g_hiddenByTrack = TRUE;
        }
        return 0;
    }

    case WM_PAINT: {
        /* Double-buffered: draw everything into an off-screen bitmap first,
         * then blit it to the window in a single call. Painting straight to
         * the window DC with several separate FillRect/DrawText calls let
         * DWM occasionally capture a half-drawn frame while the window was
         * being dragged (via the native HTCAPTION move), which showed up as
         * flicker/streaks and made the buttons look like they were "stuck"
         * mid-drag. A single atomic blit removes that window entirely. */
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client; GetClientRect(hwnd, &client);
        int cw = client.right - client.left;
        int ch = client.bottom - client.top;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, cw, ch);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        if (g_collapsed) {
            FillRect(memDC, &client, g_tracking ? g_trackedHandleBrush : g_handleBrush);
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, RGB(255, 255, 255));
            DrawTextA(memDC, "+", -1, &client, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            RECT handleR; GetHandleRect(hwnd, &handleR);
            FillRect(memDC, &handleR, g_tracking ? g_trackedHandleBrush : g_handleBrush);

            RECT bodyR = client; bodyR.top = HANDLE_H;
            FillRect(memDC, &bodyR, g_bodyBrush);

            RECT closeR, collapseR;
            GetCloseBtnRect(hwnd, &closeR);
            GetCollapseBtnRect(hwnd, &collapseR);
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, RGB(255, 255, 255));
            DrawTextA(memDC, "x", -1, &closeR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            DrawTextA(memDC, "-", -1, &collapseR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            if (!g_locked) {
                RECT grip;
                grip.right = client.right; grip.bottom = client.bottom;
                grip.left = client.right - GRIP_SIZE; grip.top = client.bottom - GRIP_SIZE;
                HBRUSH gripBrush = CreateSolidBrush(RGB(90, 90, 90));
                FillRect(memDC, &grip, gripBrush);
                DeleteObject(gripBrush);
            }
        }

        BitBlt(hdc, 0, 0, cw, ch, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        KillTimer(hwnd, TRACK_TIMER_ID);
        if (g_bodyBrush) DeleteObject(g_bodyBrush);
        if (g_handleBrush) DeleteObject(g_handleBrush);
        if (g_trackedHandleBrush) DeleteObject(g_trackedHandleBrush);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrev; (void)lpCmdLine;

    SetProcessDPIAware();
    g_hInstance = hInstance;

    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "ScreenShieldWndClass";
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    WNDCLASSA dlgWc;
    ZeroMemory(&dlgWc, sizeof(dlgWc));
    dlgWc.lpfnWndProc = TabDlgProc;
    dlgWc.hInstance = hInstance;
    dlgWc.lpszClassName = "ScreenShieldTabDlg";
    dlgWc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    dlgWc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassA(&dlgWc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int w = 420, h = 220;
    int x = (screenW - w) / 2;
    int y = (screenH - h) / 2;

    HWND hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_APPWINDOW,
        "ScreenShieldWndClass", "Screen Shield",
        WS_POPUP | WS_VISIBLE,
        x, y, w, h,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) return 0;

    ApplyExclusion(hwnd, TRUE);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
