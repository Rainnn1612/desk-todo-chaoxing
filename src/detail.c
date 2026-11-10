/* detail.c - 「备注与图片」窗口。
 *
 * 列表本身是手绘的，但这个窗口是给用户输入大段文字用的，所以直接上标准控件
 * （EDIT + 按钮）：光标、选区、换行、输入法、滚动全都由系统负责，比自己画
 * 一个文本编辑器省事得多。只有图片预览区是自绘的。
 *
 * 打开时是个局部模态循环（IsDialogMessage），这样 Tab 切控件、Enter 触发
 * 默认按钮、Esc 取消都是系统行为，不用手写。
 */
#include "app.h"
#include <commdlg.h>        /* GetOpenFileNameW */

#define DLG_CLASS L"DeskTodoDetail"

#define ID_NOTE   2001
#define ID_SAVE   2002
#define ID_CANCEL 2003
#define ID_PICK   2004
#define ID_CLEAR  2005
#define ID_OPEN   2006

typedef struct {
    App   *app;
    HWND   hwnd;
    HWND   h_note, h_imgn, h_save, h_cancel, h_pick, h_clear, h_open;
    HWND   h_lbl1, h_lbl2;
    HFONT  font, fontb;
    HBRUSH br_page, br_white;
    int    ti;
    WCHAR  title0[MAX_TITLE];
    WCHAR *img;
    Img   *pic;
    int    pic_fail;
    RECT   pv;
    int    dpi, fs;
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

static void reload_img(void)
{
    WCHAR p[MAX_PATH], lab[300];
    img_close(D.pic);
    D.pic = NULL;
    D.pic_fail = 0;
    if (D.img && D.img[0]) {
        img_path_of(D.img, p, MAX_PATH);
        D.pic = img_open(p);
        if (!D.pic) D.pic_fail = 1;
    }
    if (!D.img || !D.img[0]) {
        wcpy(lab, L"（没有图片）", 300);
    } else if (D.pic) {
        wcpy(lab, D.img, 300);
        wcat(lab, L"    ", 300);
        wcat_int(lab, img_width(D.pic), 300);
        wcat(lab, L" × ", 300);
        wcat_int(lab, img_height(D.pic), 300);
    } else {
        wcpy(lab, D.img, 300);
        wcat(lab, L"    （打不开这个格式）", 300);
    }
    if (D.h_imgn) SetWindowTextW(D.h_imgn, lab);
    if (D.hwnd) InvalidateRect(D.hwnd, NULL, FALSE);
}

static void on_pick(void)
{
    OPENFILENAMEW ofn;
    WCHAR buf[MAX_PATH];
    static const WCHAR FLT[] =
        L"图片文件\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0所有文件\0*.*\0";

    buf[0] = 0;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = D.hwnd;
    ofn.lpstrFilter = FLT;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"选一张图片当作业说明";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return;
    {
        WCHAR name[64];
        if (!img_import(buf, name, 64)) {
            MessageBoxW(D.hwnd, L"图片复制失败（磁盘满或文件被占用）。",
                        L"DeskTodo", MB_ICONERROR | MB_OK);
            return;
        }
        free(D.img);
        D.img = wdup(name);
        reload_img();
    }
}

static void on_open(void)
{
    WCHAR p[MAX_PATH];
    if (!D.img || !D.img[0]) return;
    img_path_of(D.img, p, MAX_PATH);
    ShellExecuteW(D.hwnd, L"open", p, NULL, NULL, SW_SHOWNORMAL);
}

static void on_clear(void)
{
    if (!D.img) return;
    free(D.img);
    D.img = NULL;
    reload_img();
}

static void finish(void)
{
    if (D.app) {
        free(D.img);                 /* 取消时新导入的图会被 img_gc 清掉 */
        D.img = NULL;
        img_gc(D.app);
    }
    img_close(D.pic);
    D.pic = NULL;
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
            MessageBoxW(D.hwnd, L"这条待办已经被删掉或改名了，说明和图片没有保存。",
                        L"DeskTodo", MB_ICONWARNING | MB_OK);
            return;
        }
    }
    store_set_detail(a, ti, buf, D.img ? D.img : L"");
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
        /* 说明 */
        D.h_lbl1 = mk(L"STATIC", L"说明（写清楚作业要求、页码、提交方式…）", SS_LEFT,
                      0, 14, 10, 420, 18, -1, D.fontb);
        D.h_note = mk(L"EDIT", L"",
                      WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
                      WS_EX_CLIENTEDGE, 14, 30, 424, 118, ID_NOTE, D.font);
        SendMessageW(D.h_note, EM_SETLIMITTEXT, (WPARAM)(MAX_NOTE - 1), 0);
        /* 图片 */
        D.h_lbl2 = mk(L"STATIC", L"图片", SS_LEFT, 0, 14, 158, 40, 18, -1, D.fontb);
        D.h_imgn = mk(L"STATIC", L"（没有图片）", SS_LEFT | SS_ENDELLIPSIS,
                      0, 58, 158, 380, 18, -1, D.font);
        D.h_pick  = mk(L"BUTTON", L"选择图片…", WS_TABSTOP | BS_PUSHBUTTON,
                       0, 14, 180, 96, 26, ID_PICK, D.font);
        D.h_clear = mk(L"BUTTON", L"清除", WS_TABSTOP | BS_PUSHBUTTON,
                       0, 116, 180, 56, 26, ID_CLEAR, D.font);
        D.h_open  = mk(L"BUTTON", L"查看原图", WS_TABSTOP | BS_PUSHBUTTON,
                       0, 178, 180, 80, 26, ID_OPEN, D.font);
        D.pv.left = fzd(14); D.pv.top = fzd(214);
        D.pv.right = fzd(14 + 424); D.pv.bottom = fzd(214 + 140);
        /* 底部按钮 */
        D.h_save   = mk(L"BUTTON", L"保存", WS_TABSTOP | BS_DEFPUSHBUTTON,
                        0, 294, 368, 72, 28, ID_SAVE, D.font);
        D.h_cancel = mk(L"BUTTON", L"取消", WS_TABSTOP | BS_PUSHBUTTON,
                        0, 374, 368, 64, 28, ID_CANCEL, D.font);
        if (D.app && D.ti >= 0 && D.ti < D.app->todos.n) {
            Todo *t = &D.app->todos.v[D.ti];
            if (t->note) SetWindowTextW(D.h_note, t->note);
        }
        reload_img();
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
        RECT cr, pv = D.pv;
        HBRUSH b;
        GetClientRect(hwnd, &cr);
        if (D.br_page) FillRect(dc, &cr, D.br_page);
        b = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &pv, b);
        DeleteObject(b);
        b = CreateSolidBrush(RGB(0xD6, 0xD0, 0xC8));
        FrameRect(dc, &pv, b);
        DeleteObject(b);
        if (D.pic) {
            img_draw_fit(D.pic, dc, &pv, fzd(6));
        } else {
            WCHAR *msg = D.pic_fail ? L"这个图片打不开（只支持 png / jpg / bmp / gif）"
                                    : L"点上面的「选择图片…」放一张作业截图进来";
            RECT tr = pv;
            HFONT old;
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(0x9A, 0x94, 0x8C));
            old = (HFONT)SelectObject(dc, D.font);
            DrawTextW(dc, msg, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE |
                                    DT_END_ELLIPSIS);
            SelectObject(dc, old);
        }
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
        case ID_PICK:
            on_pick();
            return 0;
        case ID_CLEAR:
            on_clear();
            return 0;
        case ID_OPEN:
            on_open();
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
    D.img = wdup(a->todos.v[ti].img);
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
    ch = fzd(410);
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
                             DLG_CLASS, L"备注与图片", st,
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
