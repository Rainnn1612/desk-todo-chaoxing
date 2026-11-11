/* trash.c - 「垃圾箱」窗口：手动删掉的待办会先放这里，最多留 TRASH_MAX 条。
 *
 * 数据在 %APPDATA%\DeskTodo\trash.json，由 store.c 的 trash_* 系列负责读写；
 * 这个窗口只做「看 / 恢复 / 彻底删除 / 清空」，全是标准控件，不发任何网络请求。
 *
 * 恢复 = 把条目按删除时的样子（标题、DDL、完成状态、说明）塞回待办列表末尾。
 */
#include "app.h"

#define CLS_TRASH L"DeskTodoTrash"

#define ID_LIST    4001
#define ID_RESTORE 4002
#define ID_DROP    4003
#define ID_CLEARB  4004
#define ID_CLOSEB  4005
#define ID_INFO    4006

typedef struct {
    App   *app;
    HWND   hwnd, h_list, h_restore, h_drop, h_clear, h_close, h_info;
    HFONT  font, fontb;
    HBRUSH br_page;
    int    dpi, fs;
} TrashW;

static TrashW T;
static int g_done;

static int fzd(int v)
{
    return MulDiv(MulDiv(v, T.dpi, 96), T.fs, 100);
}

static HFONT mk_font(int qpt, int bold)
{
    int q = MulDiv(qpt * 4, T.fs, 100);
    if (q < 20) q = 20;
    return CreateFontW(-MulDiv(q, T.dpi, 4 * 72), 0, 0, 0,
                       bold ? FW_SEMIBOLD : FW_NORMAL, 0, 0, 0,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"Microsoft YaHei UI");
}

static HWND mk(const WCHAR *cls, const WCHAR *txt, DWORD st, DWORD ex,
               int x, int y, int w, int h, int id, HFONT f)
{
    HWND r = CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | st,
                             fzd(x), fzd(y), fzd(w), fzd(h),
                             T.hwnd, (HMENU)(INT_PTR)id, NULL, NULL);
    if (r && f) SendMessageW(r, WM_SETFONT, (WPARAM)f, TRUE);
    return r;
}

static void free_res(void)
{
    if (T.font)    { DeleteObject(T.font);    T.font = NULL; }
    if (T.fontb)   { DeleteObject(T.fontb);   T.fontb = NULL; }
    if (T.br_page) { DeleteObject(T.br_page); T.br_page = NULL; }
}

/* 一条垃圾箱记录显示成一行文字：标题 · DDL · 删于什么时候 */
static void item_text(const TrashItem *it, WCHAR *buf, int n)
{
    int i, cut;
    WCHAR tmp[40];

    buf[0] = 0;
    cut = wlen(it->title);
    if (cut > 60) cut = 60;                     /* 太长的标题截断，后面还有信息要放 */
    for (i = 0; i < cut && i < n - 1; i++) buf[i] = it->title[i];
    buf[i] = 0;
    if ((int)wlen(it->title) > cut) wcat(buf, L"…", n);

    if (it->note && it->note[0]) {
        note_head(it->note, tmp, 24);
        if (tmp[0]) { wcat(buf, L"  ·  ", n); wcat(buf, tmp, n); }
    }
    if (it->has_ddl) {
        wcat(buf, L"  ·  DDL ", n);
        ddl_to_text(&it->ddl, tmp, 32);
        wcat(buf, tmp, n);
    }
    if (it->when[0]) {
        wcat(buf, L"  ·  删于 ", n);
        wcat(buf, it->when, n);
    }
}

static void upd_info(void)
{
    WCHAR t[128];
    int sel;
    if (!T.h_info) return;
    sel = T.h_list ? (int)SendMessageW(T.h_list, LB_GETCURSEL, 0, 0) : -1;
    if (T.app->trash_n == 0) {
        wcpy(t, L"垃圾箱是空的。", 128);
    } else {
        wcpy(t, L"共 ", 128);
        wcat_int(t, T.app->trash_n, 128);
        wcat(t, L" 条（最多保留 ", 128);
        wcat_int(t, TRASH_MAX, 128);
        wcat(t, L" 条）", 128);
        if (sel >= 0) { wcat(t, L"，已选中第 ", 128); wcat_int(t, sel + 1, 128); wcat(t, L" 条", 128); }
    }
    SetWindowTextW(T.h_info, t);
    if (T.h_restore) EnableWindow(T.h_restore, sel >= 0);
    if (T.h_drop)    EnableWindow(T.h_drop, sel >= 0);
    if (T.h_clear)   EnableWindow(T.h_clear, T.app->trash_n > 0);
}

/* 整张列表按 app->trash 重画（下标一一对应：第 0 条就是最新的） */
static void fill_list(void)
{
    int i;
    if (!T.h_list) return;
    SendMessageW(T.h_list, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < T.app->trash_n; i++) {
        WCHAR t[400];
        item_text(&T.app->trash[i], t, 400);
        SendMessageW(T.h_list, LB_ADDSTRING, 0, (LPARAM)t);
    }
    SendMessageW(T.h_list, LB_SETTOPINDEX, 0, 0);
    upd_info();
}

static void on_restore(void)
{
    int sel = (int)SendMessageW(T.h_list, LB_GETCURSEL, 0, 0);
    if (sel < 0) return;
    if (trash_restore(T.app, sel)) {
        widget_fit(T.app);
        InvalidateRect(T.app->hwnd, NULL, FALSE);
    }
    fill_list();
}

static void on_drop(void)
{
    int sel = (int)SendMessageW(T.h_list, LB_GETCURSEL, 0, 0);
    if (sel < 0) return;
    if (MessageBoxW(T.hwnd, L"这条就彻底删掉了，恢复不了。确定吗？",
                    L"彻底删除", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
        return;
    trash_drop(T.app, sel);
    fill_list();
}

static void on_clear(void)
{
    if (T.app->trash_n == 0) return;
    if (MessageBoxW(T.hwnd, L"清空垃圾箱，里面的条目都会被彻底删除。确定吗？",
                    L"清空垃圾箱", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
        return;
    trash_clear(T.app);
    fill_list();
}

/* ---------------- 窗口过程 ---------------- */

static LRESULT CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        T.hwnd = hwnd;                 /* 子控件的父窗口要用它 */
        mk(L"STATIC", L"删掉的待办会先放这里，选中一条可以恢复。",
           0, 0, 14, 10, 442, 20, 0, T.font);
        T.h_list = mk(L"LISTBOX", NULL,
                      WS_VSCROLL | WS_BORDER | WS_TABSTOP | LBS_NOINTEGRALHEIGHT |
                      LBS_NOTIFY,
                      WS_EX_CLIENTEDGE, 14, 36, 442, 296, ID_LIST, T.font);
        T.h_info = mk(L"STATIC", L"", 0, 0, 14, 340, 442, 20, ID_INFO, T.font);
        T.h_restore = mk(L"BUTTON", L"恢复", WS_TABSTOP | BS_DEFPUSHBUTTON, 0,
                         14, 372, 100, 32, ID_RESTORE, T.fontb);
        T.h_drop    = mk(L"BUTTON", L"彻底删除", WS_TABSTOP, 0,
                         126, 372, 100, 32, ID_DROP, T.font);
        T.h_clear   = mk(L"BUTTON", L"清空垃圾箱", WS_TABSTOP, 0,
                         238, 372, 120, 32, ID_CLEARB, T.font);
        T.h_close   = mk(L"BUTTON", L"关闭", WS_TABSTOP, 0,
                         370, 372, 86, 32, ID_CLOSEB, T.font);
        fill_list();
        return 0;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, RGB(0x4A, 0x46, 0x42));
        return (LRESULT)T.br_page;
    case WM_CTLCOLORLISTBOX:
        SetBkColor((HDC)wp, RGB(0xFF, 0xFD, 0xFA));
        return (LRESULT)T.br_page;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT cr;
        GetClientRect(hwnd, &cr);
        if (T.br_page) FillRect(dc, &cr, T.br_page);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_RESTORE:
            if (HIWORD(wp) == BN_CLICKED) on_restore();
            return 0;
        case ID_DROP:
            if (HIWORD(wp) == BN_CLICKED) on_drop();
            return 0;
        case ID_CLEARB:
            if (HIWORD(wp) == BN_CLICKED) on_clear();
            return 0;
        case ID_CLOSEB:
        case IDOK:
        case IDCANCEL:
            if (HIWORD(wp) == BN_CLICKED || LOWORD(wp) == IDCANCEL || LOWORD(wp) == IDOK)
                DestroyWindow(hwnd);
            return 0;
        case ID_LIST:
            if (HIWORD(wp) == LBN_SELCHANGE || HIWORD(wp) == LBN_DBLCLK) {
                upd_info();
                if (HIWORD(wp) == LBN_DBLCLK) on_restore();   /* 双击 = 直接恢复 */
            }
            return 0;
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_done = 1;
        free_res();
        T.hwnd = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void trash_win_destroy(void)
{
    if (T.hwnd) { DestroyWindow(T.hwnd); return; }
    free_res();
}

void trash_open(App *a)
{
    WNDCLASSEXW wc;
    MSG msg;
    RECT wr, wa, ar;
    int cw, ch, ww, wh, x, y;
    DWORD st = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;

    if (!a) return;
    if (T.hwnd) { SetForegroundWindow(T.hwnd); return; }

    memset(&T, 0, sizeof(T));
    T.app = a;
    T.dpi = a->dpi > 0 ? a->dpi : 96;
    T.fs  = a->cfg.font;
    T.font    = mk_font(9, 0);
    T.fontb   = mk_font(9, 1);
    T.br_page = CreateSolidBrush(RGB(0xF7, 0xF5, 0xF2));

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DlgProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = a->hIcon;
    wc.lpszClassName = CLS_TRASH;
    RegisterClassExW(&wc);

    cw = fzd(470);
    ch = fzd(416);
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
    T.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT | WS_EX_TOPMOST,
                             CLS_TRASH, L"垃圾箱", st,
                             x, y, ww, wh, a->hwnd, NULL,
                             GetModuleHandleW(NULL), NULL);
    if (!T.hwnd) {
        free_res();
        T.app = NULL;
        return;
    }

    EnableWindow(a->hwnd, FALSE);
    ShowWindow(T.hwnd, SW_SHOW);
    UpdateWindow(T.hwnd);
    SetForegroundWindow(T.hwnd);
    if (T.h_list) SetFocus(T.h_list);

    while (!g_done && GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (T.hwnd && IsDialogMessageW(T.hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (T.hwnd) DestroyWindow(T.hwnd);
    free_res();
    T.app = NULL;
    EnableWindow(a->hwnd, TRUE);
    /* 恢复过条目的话窗口高度会变 */
    widget_fit(a);
    InvalidateRect(a->hwnd, NULL, FALSE);
}
