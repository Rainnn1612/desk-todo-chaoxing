/* login.c - 「登录学习通」窗口：让用户自己输入账号密码。
 *
 * 账号密码用 Windows DPAPI（CryptProtectData, 当前用户）加密后写到
 * %APPDATA%\DeskTodo\cx.dat，格式是明文的 "账号\n密码" 的密文，
 * 同步脚本用 .NET 的 ProtectedData.Unprotect 读回来 —— 两者是同一套 DPAPI，
 * 所以 C 侧加密、PowerShell 侧解密是互通的。
 *
 * 密码只留在本机这个文件里，不会发到任何别的服务器；换机器/换 Windows 账号就解不开，
 * 到时重新输一遍即可。
 */

#include "app.h"
#include <wincrypt.h>

#define CLS_LOGIN L"DeskTodoLogin"

static struct {
    App  *app;
    HWND  hwnd;
    HWND  h_user, h_pass, h_ok, h_cancel;
    HFONT font, font_mid;
    HFONT topmost_fix_unused;
} L;

static int g_done;
static int g_do_sync;

static int fzd(int v) { return fz(L.app, v); }

static HWND mk(const WCHAR *cls, const WCHAR *text, DWORD style,
               int x, int y, int w, int h, int id, HFONT f)
{
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             fzd(x), fzd(y), fzd(w), fzd(h),
                             L.hwnd, (HMENU)(INT_PTR)id,
                             GetModuleHandleW(NULL), NULL);
    if (c && f) SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
    return c;
}

/* ---------------- 写账号文件 ---------------- */

int login_has_cred(void)
{
    WCHAR path[MAX_PATH];
    data_path(path, MAX_PATH, L"cx.dat");
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static void trim(WCHAR *s)
{
    int n = wlen(s), i = 0;
    while (i < n && (s[i] == L' ' || s[i] == L'\t' || s[i] == L'\r' || s[i] == L'\n')) i++;
    if (i) { int k = 0; while (s[i]) s[k++] = s[i++]; s[k] = 0; }
    n = wlen(s);
    while (n > 0 && (s[n - 1] == L' ' || s[n - 1] == L'\t' || s[n - 1] == L'\r' || s[n - 1] == L'\n')) s[--n] = 0;
}

/* 把 "账号\n密码" 用 DPAPI 加密后落盘 */
static int save_cred(const WCHAR *user, const WCHAR *pass)
{
    WCHAR path[MAX_PATH];
    char *plain;
    DATA_BLOB in, out;
    int ok;

    plain = w2u8(user);
    if (!plain) return 0;
    {
        char *p8 = w2u8(pass);
        int lu = (int)strlen(plain), lp = p8 ? (int)strlen(p8) : 0;
        char *all = (char *)malloc(lu + 1 + lp + 1);
        int i;
        if (!all) { free(plain); if (p8) free(p8); return 0; }
        for (i = 0; i < lu; i++) all[i] = plain[i];
        all[lu] = '\n';
        for (i = 0; i < lp; i++) all[lu + 1 + i] = p8[i];
        all[lu + 1 + lp] = 0;
        free(plain);
        if (p8) { SecureZeroMemory(p8, strlen(p8)); free(p8); }
        plain = all;
    }

    in.pbData = (BYTE *)plain;
    in.cbData = (DWORD)strlen(plain);
    out.pbData = NULL;
    out.cbData = 0;

    if (!CryptProtectData(&in, L"DeskTodo cx credential", NULL, NULL, NULL,
                          CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        SecureZeroMemory(plain, strlen(plain)); free(plain);
        return 0;
    }
    SecureZeroMemory(plain, strlen(plain));
    free(plain);

    data_path(path, MAX_PATH, L"cx.dat");
    ensure_data_dir();          /* 全新机器上目录可能还不存在 */
    ok = write_atomic(path, (const char *)out.pbData, (int)out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return ok;
}

/* ---------------- 窗口 ---------------- */

static void on_save(void)
{
    WCHAR user[128] = { 0 };
    WCHAR pass[128] = { 0 };

    GetWindowTextW(L.h_user, user, 128);
    GetWindowTextW(L.h_pass, pass, 128);
    trim(user);
    trim(pass);

    if (!user[0] || !pass[0]) {
        MessageBoxW(L.hwnd, L"账号和密码都要填。", L"登录学习通",
                    MB_ICONWARNING | MB_OK);
        return;
    }
    if (!save_cred(user, pass)) {
        MessageBoxW(L.hwnd, L"保存失败：写不进 %APPDATA%\\DeskTodo\\cx.dat。",
                    L"登录学习通", MB_ICONERROR | MB_OK);
        return;
    }
    SecureZeroMemory(pass, sizeof(pass));
    g_do_sync = 1;
    g_done = 1;
}

static LRESULT CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        L.hwnd = hwnd;
        {
            int y1 = 20, y2 = 60, y3 = 100;
            int bw = 100;
            HFONT f = L.font;
            mk(L"STATIC", L"学习通账号（手机号 / 用户名）", 0, 18, y1 - 16, 380, 18, 0, f);
            L.h_user = mk(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                          18, y1 + 4, 380, 26, 1001, f);
            mk(L"STATIC", L"密码", 0, 18, y2 - 16, 380, 18, 0, f);
            L.h_pass = mk(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL,
                          18, y2 + 4, 380, 26, 1002, f);
            mk(L"STATIC",
               L"密码用 Windows DPAPI 加密后只存在本机，不会上传到任何服务器。\r\n"
               L"换电脑或重装系统后解不开，到时重新输一遍即可。",
               0, 18, y3, 388, 44, 0, f);
            L.h_ok = mk(L"BUTTON", L"保存并同步", WS_TABSTOP | BS_DEFPUSHBUTTON,
                        200, y3 + 58, bw + 30, 30, IDOK, f);
            L.h_cancel = mk(L"BUTTON", L"取消", WS_TABSTOP | BS_PUSHBUTTON,
                            310, y3 + 58, bw, 30, IDCANCEL, f);
        }
        (void)lp;
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) { on_save(); return 0; }
        if (LOWORD(wp) == IDCANCEL) { g_done = 1; return 0; }
        return 0;
    case WM_CLOSE:
        g_done = 1;
        return 0;
    case WM_DESTROY:
        L.hwnd = NULL;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void login_destroy(void)
{
    if (L.hwnd) {
        HWND h = L.hwnd;
        L.hwnd = NULL;
        DestroyWindow(h);
    }
    g_done = 1;
}

void login_open(App *a)
{
    WNDCLASSEXW wc;
    MSG msg;
    RECT ar, wa, wr;
    int cw, ch, ww, wh, x, y;
    DWORD st = WS_CAPTION | WS_SYSMENU;

    if (L.hwnd) { SetForegroundWindow(L.hwnd); return; }

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DlgProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = a->hIcon;
    wc.lpszClassName = CLS_LOGIN;
    RegisterClassExW(&wc);

    L.app = a;
    L.font = a->f_small;

    cw = fzd(420);
    ch = fzd(230);
    ar.left = 0; ar.top = 0; ar.right = cw; ar.bottom = ch;
    AdjustWindowRectEx(&ar, st, FALSE, WS_EX_TOOLWINDOW);
    ww = ar.right - ar.left;
    wh = ar.bottom - ar.top;

    widget_work_area(a->hwnd, &wa);
    GetWindowRect(a->hwnd, &wr);
    x = wr.left + ((wr.right - wr.left) - ww) / 2;
    y = wr.top + ((wr.bottom - wr.top) - wh) / 2;
    if (x + ww > wa.right) x = wa.right - ww;
    if (x < wa.left) x = wa.left;
    if (y + wh > wa.bottom) y = wa.bottom - wh;
    if (y < wa.top) y = wa.top;

    g_done = 0;
    g_do_sync = 0;
    L.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT | WS_EX_TOPMOST,
                             CLS_LOGIN, L"登录学习通", st,
                             x, y, ww, wh, a->hwnd, NULL,
                             GetModuleHandleW(NULL), NULL);
    if (!L.hwnd) { L.app = NULL; return; }

    if (login_has_cred()) {
        /* 已经有账号就只让改密码，不把旧密码显示出来 */
        SetWindowTextW(L.h_user, L"");
    }

    EnableWindow(a->hwnd, FALSE);
    ShowWindow(L.hwnd, SW_SHOW);
    UpdateWindow(L.hwnd);
    SetForegroundWindow(L.hwnd);
    SetFocus(L.h_user);

    while (!g_done && GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (L.hwnd && IsDialogMessageW(L.hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (L.hwnd) {
        HWND h = L.hwnd;
        L.hwnd = NULL;
        DestroyWindow(h);
    }
    EnableWindow(a->hwnd, TRUE);
    InvalidateRect(a->hwnd, NULL, FALSE);
    L.app = NULL;

    if (g_do_sync) sync_start(a);
}
