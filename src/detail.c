/* detail.c - 「备注」窗口：给一条待办写大段说明。
 *
 * 列表本身是手绘的，只有这一个窗口用标准控件（EDIT + 按钮）：光标、选区、
 * 换行、输入法、滚动全交给系统，比自己画一个文本编辑器省事得多。
 *
 * 打开时是个局部模态循环（IsDialogMessage），Tab 切控件、Enter 触发默认按钮、
 * Esc 取消都是系统行为，不用手写。
 */
#include "app.h"

#define DLG_CLASS L"DeskTodoDetail"

#define ID_NOTE   2001
#define ID_SAVE   2002
#define ID_CANCEL 2003

typedef struct {
    App   *app;
    HWND   hwnd;
    HWND   h_note, h_save, h_cancel, h_lbl;
    HFONT  font, fontb;
    HBRUSH br_page, br_white;
    int    ti;
    int    dpi, fs;
    WCHAR  title0[MAX_TITLE];
} Detail;

static Detail D;
static int    g_done;

/* 布局尺寸同样跟着 DPI 和字号走，跟主窗口一个倍率 */
static int fzd(int v)
{
    return MulDiv(MulDiv(v, D.dpi, 96), D.fs, 100);
}

static HFONT mk_font(int qpt, int bold)
{
    int q = MulDiv(qpt * 4, D.fs, 100);
    if (q < 20) q = 20;
    return CreateFontW(-MulDiv(q, D.dpi, 4 * 72), 0, 0, 0,
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
                             D.hwnd, (HMENU)(INT_PTR)id, NULL, NULL);
    if (r && f) SendMessageW(r, WM_SETFONT, (WPARAM)f, TRUE);
    return r;
}

static void finish(void)
{
    if (D.font)   { DeleteObject(D.font);   D.font = NULL; }
    if (D.fontb)  { DeleteObject(D.fontb);  D.fontb = NULL; }
    if (D.br_page)  { DeleteObject(D.br_page);  D.br_page = NULL; }
    if (D.br_white) { DeleteObject(D.br_white); D.br_white = NULL; }
    D.hwnd = NULL;
    g_done = 1;
}

static void on_save(void)
{
    App *a = D.app;
    WCHAR *buf;
    int n, ti = D.ti, i, hit = -1, cnt = 0;

    if (!a) return;
    n = GetWindowTextLengthW(D.h_note);
    if (n < 0) n = 0;
    if (n > MAX_NOTE - 1) n = MAX_NOTE - 1;
    buf = (WCHAR *)malloc((size_t)(n + 2) * sizeof(WCHAR));
    if (!buf) return;
    GetWindowTextW(D.h_note, buf, n + 1);
    wtrim(buf);

    /* 详情窗开着的时候主窗是被禁用的，但排序/删除仍可能从别处发生，
       所以落地前按标题快照再确认一次目标行。 */
    if (ti < 0 || ti >= a->todos.n || lstrcmpW(a->todos.v[ti].title, D.title0) != 0) {
        for (i = 0; i < a->todos.n; i++)
            if (!lstrcmpW(a->todos.v[i].title, D.title0)) { hit = i; cnt++; }
        if (cnt == 1) ti = hit;
        else {
            free(buf);
            MessageBoxW(D.hwnd, L"这条待办已经被删掉或改名了，说明没有保存。",
                        L"DeskTodo", MB_ICONWARNING | MB_OK);
            return;
        }
    }
    store_set_detail(a, ti, buf, L"");
    free(buf);
    InvalidateRect(a->hwnd, NULL, FALSE);
    DestroyWindow(D.hwnd);
}

static LRESULT CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        /* WM_CREATE 是在 CreateWindowExW 返回之前进来的，那时 D.hwnd 还是 NULL，
           mk() 用它当父窗口就会把控件创建成孤儿——必须先在这里补上。 */
        D.hwnd = hwnd;
        D.h_lbl = mk(L"STATIC", L"说明（写清楚作业要求、页码、提交方式…）", SS_LEFT,
                     0, 14, 14, 424, 18, -1, D.fontb);
        D.h_note = mk(L"EDIT", L"",
                      WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
                      WS_EX_CLIENTEDGE, 14, 36, 424, 160, ID_NOTE, D.font);
        SendMessageW(D.h_note, EM_SETLIMITTEXT, (WPARAM)(MAX_NOTE - 1), 0);
        D.h_save   = mk(L"BUTTON", L"保存", WS_TABSTOP | BS_DEFPUSHBUTTON,
                        0, 294, 206, 72, 28, ID_SAVE, D.font);
        D.h_cancel = mk(L"BUTTON", L"取消", WS_TABSTOP | BS_PUSHBUTTON,
                        0, 374, 206, 64, 28, ID_CANCEL, D.font);
        if (D.app && D.ti >= 0 && D.ti < D.app->todos.n) {
            Todo *t = &D.app->todos.v[D.ti];
            if (t->note) SetWindowTextW(D.h_note, t->note);
        }
        SetFocus(D.h_note);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, RGB(0x4A, 0x46, 0x42));
        return (LRESULT)D.br_page;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
        SetBkColor((HDC)wp, RGB(255, 255, 255));
        SetTextColor((HDC)wp, RGB(0x22, 0x20, 0x1E));
        return (LRESULT)D.br_white;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT cr;
        GetClientRect(hwnd, &cr);
        if (D.br_page) FillRect(dc, &cr, D.br_page);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_SAVE:
            if (HIWORD(wp) == BN_CLICKED) on_save();
            return 0;
        case IDOK:
            /* IsDialogMessageW 处理回车时合成的是 IDOK(1)，不是我们自己那个 2002 的 id；
               不认这条的话在非多行 EDIT 上按回车没反应。 */
            on_save();
            return 0;
        case ID_CANCEL:
        case IDCANCEL:
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        finish();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void detail_destroy(void)
{
    if (D.hwnd) DestroyWindow(D.hwnd);
}

HWND detail_hwnd(void)
{
    return D.hwnd;
}

void detail_open(App *a, int ti)
{
    WNDCLASSEXW wc;
    MSG msg;
    RECT wr, wa, ar;
    int cw, ch, ww, wh, x, y;
    DWORD st = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;

    if (!a || ti < 0 || ti >= a->todos.n) return;
    if (D.hwnd) {
        SetForegroundWindow(D.hwnd);
        return;
    }

    memset(&D, 0, sizeof(D));
    D.app = a;
    D.ti = ti;
    D.dpi = a->dpi > 0 ? a->dpi : 96;
    D.fs = a->cfg.font;
    wcpy(D.title0, a->todos.v[ti].title, MAX_TITLE);
    D.font  = mk_font(9, 0);
    D.fontb = mk_font(9, 1);
    D.br_page  = CreateSolidBrush(RGB(0xF7, 0xF5, 0xF2));
    D.br_white = CreateSolidBrush(RGB(255, 255, 255));

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DlgProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = a->hIcon;
    wc.lpszClassName = DLG_CLASS;
    RegisterClassExW(&wc);

    cw = fzd(452);
    ch = fzd(250);
    /* CreateWindowExW 的宽高是整个窗口（含标题栏），所以先按客户区反算一次 */
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

    D.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT |
                             WS_EX_TOPMOST,
                             DLG_CLASS, L"备注", st,
                             x, y, ww, wh, a->hwnd, NULL,
                             GetModuleHandleW(NULL), NULL);
    if (!D.hwnd) {
        finish();
        D.app = NULL;
        return;
    }

    g_done = 0;
    EnableWindow(a->hwnd, FALSE);
    ShowWindow(D.hwnd, SW_SHOW);
    UpdateWindow(D.hwnd);
    SetForegroundWindow(D.hwnd);
    if (D.h_note) SetFocus(D.h_note);

    while (!g_done && GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (D.hwnd && IsDialogMessageW(D.hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (D.hwnd) DestroyWindow(D.hwnd);
    D.app = NULL;
    EnableWindow(a->hwnd, TRUE);
    InvalidateRect(a->hwnd, NULL, FALSE);
}
